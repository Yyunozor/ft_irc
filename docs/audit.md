# ft_irc — audit de conformité et corrections

Établi contre `en.subject-15.pdf`. Base de travail : **l'état du dépôt GitHub**
(`ce5685c`, « version illias intacte »), qui fait foi par décision d'équipe.

Chaque constat est soit **vérifié à l'exécution** (la preuve est citée), soit
**relevé à la lecture** (c'est indiqué). Les corrections appliquées se limitent
au rédhibitoire ; tout ce qui fonctionne a été laissé tel quel.

---

## Résumé

| | Nombre | État |
|---|---|---|
| 🔴 Crashs rédhibitoires | 3 | ✅ **corrigés et vérifiés** |
| 🟠 Bloquants pour le client de référence | 5 | ✅ **corrigés et vérifiés** |
| 🔵 Modes obligatoires non fonctionnels | 2 | ✅ **corrigés et vérifiés** |
| 🟡 Micro-corrections | 6 | ⚠️ mentionnées, non appliquées |
| ✅ Déjà conforme | 12 | laissé tel quel |

**Résultat mesuré** : 18/18 sur la suite d'intégration, 0 erreur ASan/UBSan,
survit à 24 commandes malformées et 8 clients simultanés avec déconnexions
brutales, compile sans warning en `-Wall -Wextra -Werror -std=c++98
-pedantic-errors`.

---

# Partie 1 — Corrections appliquées

## 🔴 C1. Crash sur `JOIN #dev` — lecture hors bornes

`src/Server.cpp`, `handleJoin` — `params[1]` (la clé de channel, optionnelle)
était lu sans vérifier la taille. Sur `JOIN #dev` sans clé, `params.size()`
vaut 1.

**Preuve avant correction** :
```
JOIN #dev
==96489==ERROR: AddressSanitizer: heap-buffer-overflow
*** processus mort ***
```

**Correction** : `if (params.size() > 1 && params[1] != channel->getKey())`

## 🔴 C2. Use-after-free à la déconnexion

`src/Server.cpp`, `removeClient` — `delete` du client sans le retirer des
channels. `Channel` stocke des `Client *` bruts : le prochain `broadcast()`
déréférençait de la mémoire libérée.

**Preuve avant correction** : ana et ben dans `#x`, ben ferme brutalement, cat
écrit → `heap-buffer-overflow`, processus mort.

**Correction** : ajout de `removeFromAllChannels()`, appelé avant le `delete`.

## 🔴 C3. Quatre lectures hors bornes dans `handleMode`

Le plus grave du lot, révélé seulement sous sanitizers :

| Ligne | Problème | Déclencheur |
|---|---|---|
| `params[0]` lu **avant** le garde de taille | hors bornes | `MODE` seul |
| `params[1]` sans garde | hors bornes | `MODE #a` |
| `params[2]` sans garde (×4) | hors bornes | `MODE #a +o` |
| `addOperator(target)` sans test NULL | pointeur nul stocké | `MODE #a +o inconnu` |

Le dernier est sournois : `findClientByNick()` renvoie `NULL` pour un pseudo
inconnu, ce `NULL` entrait dans le `std::set` des opérateurs, et le
`broadcast()` suivant le déréférençait.

**Correction** : gardes de taille sur chaque accès, test `target != NULL`.
**Aucune logique modifiée.**

## 🟠 C4. Enregistrement impossible avec un vrai client

`handleUSER` exigeait `params[0] == pseudo` et seulement 2 paramètres. La forme
RFC est `USER <user> <mode> <unused> :<realname>` : le username est le login
système, **sans rapport avec le pseudo**. irssi échouait systématiquement.

**Correction** : test sur le pseudo supprimé, 4 paramètres exigés, `params[0]`
pris comme username et `params[3]` comme realname.

## 🟠 C5. Aucune réponse numérique à l'enregistrement

Le serveur répondait `Welcome to the IRC server, ana!` au lieu de
`:ircserv 001 ana :Welcome…`. Sans les `001`-`004`, un client IRC ne se
considère **jamais** enregistré et reste bloqué à la connexion.

Le sujet, chapitre IV.1 : *« Your reference client must be able to connect to
your server without encountering any error. »*

**Correction** : `completeRegistration()` émet `001`, `002`, `003`, `004` dès
que PASS + NICK + USER sont satisfaits, quel que soit leur ordre d'arrivée.

## 🟠 C6. Préfixes tronqués

Les diffusions portaient `:ana JOIN #dev` au lieu de
`:ana!ana@localhost JOIN #dev`. Un client réel a besoin du `user@host` pour
peupler sa liste de membres et reconnaître ses propres actions.

**Correction** : ajout de `Client::prefix()`, utilisé dans les diffusions de
`JOIN`, `PART`, `KICK`, `TOPIC`, `INVITE`, `PRIVMSG`, `QUIT` et `NICK`.
La logique des handlers n'a pas été touchée.

## 🟠 C7. Commandes manquantes et écho brut

`QUIT`, `NOTICE` et `PONG` n'étaient pas traités, et toute commande inconnue
était **renvoyée en écho** au client. irssi ouvre la connexion avec
`CAP LS 302` : l'écho le laissait attendre indéfiniment un `CAP END`.

**Correction** : `QUIT` (diffusé aux channels partagés), `NOTICE` (partage
l'implémentation de `PRIVMSG`, sans jamais produire d'erreur — RFC 2812 §3.3.2),
`PONG` ignoré, `CAP` ignoré silencieusement, `421 ERR_UNKNOWNCOMMAND` à la
place de l'écho, et garde-fou `451 ERR_NOTREGISTERED` avant enregistrement.

## 🟠 C8. Parser branché

`inc/Parser.hpp` / `src/Parser.cpp` remplacent l'ancien `parseLine()`, qui
poussait un **paramètre vide** quand deux espaces se suivaient : `PRIVMSG  bob
:hi` donnait une cible vide et répondait 401 au lieu de livrer le message.

Le nouveau parser gère aussi la casse, un préfixe entrant, et la limite de 15
paramètres (le 15ᵉ avale le reste de la ligne). Testé isolément : 16/16.

---

# Partie 2 — À faire, non appliqué

## ✅ F1. Mode `t` — CORRIGÉ

`_topicRestricted` n'était écrit qu'à un seul endroit : dans `Channel::mode()`,
**entièrement commentée**. Le membre restait donc `false` à vie, et dans
`handleMode`, `+t` appelait `setTopic(params[2])` — qui change le *texte* du
sujet — tandis que `-t` appelait `removeTopic()`, qui l'efface.

**Vérifié avant correction** : ana pose `+t`, ben (membre non-opérateur) change
le sujet sans être refusé.

**Correction** : ajout de `Channel::setTopicRestricted()` /
`removeTopicRestricted()`, appelés depuis `handleMode`.
**Vérifié après** : ben refusé avec `482`, `-t` lève bien la restriction.

## ✅ F2. Mode `l` — CORRIGÉ

Ni `+l` ni `-l` n'étaient traités. `setUserLimit()` était déclarée mais **jamais
définie**, et `handleJoin` ne consultait jamais `getUserLimit()`.

**Vérifié avant correction** : `MODE #c +l 1` puis un second client rejoint
quand même.

**Correction** : définition de `setUserLimit()`, traitement de `+l <n>` / `-l`
dans `handleMode` (une valeur non numérique ou nulle est ignorée plutôt que
lue comme « pas de limite »), et refus du `JOIN` avec `471 ERR_CHANNELISFULL`
quand la limite est atteinte.
**Vérifié après** : second client refusé avec `471`.

## ✅ F3. Les changements de mode sont désormais diffusés

Le `TODO(C)` en fin de `handleMode` signalait que rien n'était annoncé aux
membres. Ils recevaient donc des modes périmés. Un
`:nick!user@host MODE #chan +t` est maintenant diffusé.

## 🟡 Micro-corrections

| # | Fichier | Constat |
|---|---|---|
| M1 | `src/Server.cpp` | Deux blocs de code mort commentés, dont un contenant `std::exit()` (fonction hors liste autorisée, mais commentée donc inoffensive). À supprimer pour éviter la question en soutenance. |
| M2 | `src/Server.cpp` | 40 messages d'erreur en texte libre (`"ERROR 403: No such channel"`) au lieu des numériques. irssi les affiche en texte brut : laid, mais la session n'est pas cassée. Les 30 fonctions prêtes sont dans `inc/Replies.hpp`. |
| M3 | `src/Server.cpp` | Les channels vides ne sont jamais détruits ; ils s'accumulent pendant l'exécution. **La branche `ILIAS` contient déjà le correctif** (`handlePart` supprime le channel devenu vide) : à récupérer plutôt qu'à réécrire. |
| M4 | `src/main.cpp` | Le port n'est pas validé : `atoi("abc")` vaut 0, `atoi("99999")` déborde la plage TCP. Mot de passe vide accepté. Pas de crash, mais un message d'erreur peu clair. |
| M5 | `src/Client.cpp` | `_readBuf` sans plafond : un client qui envoie des octets sans jamais de `\r\n` fait grossir la mémoire indéfiniment. |
| M6 | `inc/Channel.hpp` | `getinviteOnly()` (minuscule), une surcharge `removeInviteOnly(Channel *)` et `mode()` sont déclarés mais semblent inutilisés. À nettoyer. |

---

# Partie 3 — Déjà conforme, laissé tel quel

1. Un seul `poll()` dans tout le programme
2. Tous les fd non-bloquants — socket d'écoute et sockets acceptées
3. `fcntl` sous la forme exacte autorisée `fcntl(fd, F_SETFL, O_NONBLOCK)` (chapitre IV.2)
4. Aucun `errno` lu après `recv` / `send`
5. `SIGPIPE` ignoré au démarrage
6. Aucun `fork`, aucun thread
7. Aucune fonction hors de la liste autorisée
8. C++98 strict — compile avec `-pedantic-errors`
9. Aucune bibliothèque externe, pas de Boost
10. Makefile : `$(NAME)`, `all`, `clean`, `fclean`, `re`, pas de relink inutile
11. `POLLOUT` armé seulement si le client a des données en attente
12. Suppression différée des clients

Côté fonctionnel, vérifié et conservé : `JOIN`, `PART`, `KICK`, `INVITE`,
`TOPIC`, `PRIVMSG`, l'attribution du statut opérateur au créateur du channel,
les modes `i`, `k`, `o`, et la classe `Channel` (membres, opérateurs, invités,
clé, diffusion).

---

# Annexe — correction d'une recommandation antérieure

J'avais d'abord proposé un `send()` « best-effort » juste avant `close()`, pour
qu'un client déconnecté reçoive sa dernière réponse. **C'est risqué.** Le
chapitre IV.1 est explicite :

> *« if you attempt to read/recv or write/send in any file descriptor without
> using poll() (or equivalent), your grade will be 0. »*

Une écriture déclenchée par la fermeture, et non par un `POLLOUT` remonté par
`poll()`, entre exactement dans cette description.

**Solution retenue** : un client qui envoie `QUIT` est seulement *marqué*. Il
reste dans la boucle `poll()` un tour de plus — on cesse de le lire
(`events = 0`), mais `POLLOUT` reste armé tant que son buffer n'est pas vide —
puis il est retiré une fois drainé. Aucune I/O hors `poll()`.

---

# Comment reproduire les tests

```sh
make re                    # build de rendu, doit être silencieux
make DEBUG=1               # ASan + UBSan, pour les tests de robustesse
./ircserv 6667 secret
```

Scénarios couverts par la suite : enregistrement nominal, format irssi
(username ≠ pseudo, `CAP LS`), fragmentation TCP en plein mot, double espace,
`451` / `421` / `461`, `PONG`, `JOIN` / `PRIVMSG` / `TOPIC` / `QUIT` à deux
clients, `NOTICE` sans erreur, 24 commandes malformées, 8 clients simultanés
avec déconnexions brutales.


---

# Annexe — portabilité Linux

Le projet sera rendu et évalué sur Linux, mais développé sur macOS. Vérifié
depuis le Mac avec **GCC 15 et libstdc++** (la bibliothèque standard de Linux,
et non la libc++ d'Apple) :

```
g++-15 -Wall -Wextra -Werror -std=c++98 -Iinc -c src/*.cpp
  -> 0 warning, 0 erreur, sur les 5 unités de compilation
```

Le binaire produit par GCC passe **18/18** sur la même suite d'intégration que
celui produit par clang, avec des réponses identiques octet pour octet.

C'est le meilleur signal atteignable sans machine Linux, parce que la cause
n°1 de casse macOS → Linux est l'include manquant : la libc++ d'Apple tire
transitivement des en-têtes que la libstdc++ ne tire pas. Compiler avec GCC et
libstdc++ élimine cette classe entière de problèmes.

**Ce que ce test ne couvre pas** : le noyau Linux reste différent (comportement
de `poll()` sous charge, sémantique exacte de `SOMAXCONN`), et GNU Make 4.x
remplace le 3.81 de macOS — ce dernier point est plutôt un progrès, la version
d'Apple comparant les dates à la seconde près. Il reste à faire un `make re`
puis un test avec un vrai client sur la machine cible.
