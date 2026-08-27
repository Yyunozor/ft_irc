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

---

# Partie 4 — Conformité à la grille d'évaluation (42evalhub)

Grille lue sur `42evalhub.com/common/ftirc`. Chaque point a été rejoué.

## Basic checks — éliminatoires (0 immédiat si un seul échoue)

| Point de la grille | État | Preuve |
|---|---|---|
| Makefile, compile avec les options requises, C++, exécutable attendu | ✅ | `make re` silencieux, `-Wall -Wextra -Werror -std=c++98 -pedantic-errors` |
| **Un seul `poll()`** | ✅ | un seul appel réel (`src/Server.cpp:86`) ; les autres occurrences sont des commentaires |
| `poll()` appelé avant **chaque** accept / recv / send, et pas d'`errno` ensuite | ✅ | `accept` sur POLLIN de la socket d'écoute, `recv` sur POLLIN, `send` sur POLLOUT. Aucune I/O ailleurs. `errno` n'apparaît que dans des commentaires. |
| `fcntl()` uniquement en `fcntl(fd, F_SETFL, O_NONBLOCK)` | ✅ | deux appels, tous deux sous cette forme exacte |

> C'est précisément ce point qui avait invalidé mon `send()` « best-effort » avant `close()` : il écrivait sur un fd sans POLLOUT. Il a été remplacé par un drain via `poll()`.

## Networking

| Point | État |
|---|---|
| Écoute sur toutes les interfaces, port pris sur la ligne de commande | ✅ `INADDR_ANY` |
| Connexion via `nc`, envoi de commandes, réponses du serveur | ✅ |
| Connexion via le client IRC de référence | ⚠️ **jamais testé — irssi n'est installé sur aucune machine de l'équipe** |
| Connexions multiples simultanées sans blocage | ✅ 8 clients en parallèle |
| JOIN, et messages relayés à tous les membres du channel | ✅ |

## Networking specials — les tests les plus durs

| Point | État | Mesure |
|---|---|---|
| Commandes partielles via `nc`, autres connexions intactes | ✅ | commande coupée en plein mot réassemblée |
| Client tué brutalement, serveur toujours opérationnel | ✅ | |
| `nc` tué avec **une demi-commande** en vol | ✅ | nouveau client servi normalement juste après |
| **Client figé (`^Z`) + flood du channel** : pas de blocage, puis traitement de tout l'arriéré au réveil, sans fuite | ✅ | 3000 messages accumulés, RSS 19,9 Mo, serveur resté réactif à un nouveau client, **3006 lignes délivrées** après `SIGCONT` |

## Fuites mémoire — exigence explicite de la grille

> *« Any memory allocated on the heap must be properly freed before the end of execution. »*

**Défaut trouvé et corrigé.** La boucle était `while (true)` et aucun signal n'était intercepté : le `Ctrl+C` de l'évaluateur tuait le processus, `~Server()` n'était jamais atteint, et **tous les `Client` et `Channel` du tas étaient perdus**.

`SIGINT` et `SIGTERM` posent désormais un drapeau (`volatile sig_atomic_t`, seule opération sûre dans un handler), la boucle sort, les destructeurs s'exécutent et les descripteurs des clients sont fermés. Vérifié : `leaks` rapporte 0 fuite, et l'arrêt sous ASan/UBSan est propre.

## Client Commands channel operator — noté de 0 à 5

Les cinq modes `i t k o l` fonctionnent (voir F1/F2), `KICK`, `INVITE`, `TOPIC`, `PART` aussi, et les changements de mode sont diffusés.

---

# Partie 5 — Branche `origin/ILIAS` : ce qu'on a repris, ce qu'on a écarté

La branche `origin/ILIAS` (`e2f4ab1`) part d'un état antérieur (`ce5685c`) et
contient du travail non fusionné. Chaque correctif a été analysé puis
**contre-vérifié de façon indépendante et adversariale** avant décision.

⚠️ **Ne jamais fusionner cette branche en bloc** : elle supprime
`inc/Replies.hpp`, dont `src/Server.cpp` dépend en 23 endroits.

## ✅ Repris — `Channel::removeInvite()`

Sur `main`, `_invited` n'était **jamais vidé**. Deux conséquences :

1. Une invitation était éternelle : un membre invité sur un salon `+i` pouvait
   partir et revenir indéfiniment sans nouvelle invitation.
2. Plus grave : `removeFromAllChannels()` n'appelait que `removeMember()`, qui
   ne touche que `_members` et `_operators`. Un invité qui se déconnectait
   **sans jamais entrer** laissait son adresse dans `_invited`, et
   `new Client(fd)` réutilise très souvent le bloc que `delete` vient de
   libérer — le client suivant héritait de l'invitation et entrait dans un
   salon `+i` sans y être convié.

**Mesuré : 11 contournements sur 12 avant correction, 0 sur 12 après.**
Ce n'est pas un crash (`_invited` n'est jamais déréférencé, seulement
`find`/`insert`/`erase`, donc ASan reste muet) mais c'est un contournement de
contrôle d'accès devant un correcteur.

*Adaptation par rapport à sa version* : l'appel est **inconditionnel**. Sa garde
`if (isInviteOnly() && isInvited())` était inutile (`erase` sur un élément
absent est un no-op) et nuisible : sur un salon `-i`, l'invitation restait en
réserve, prête à être encaissée le jour où un opérateur remettait `+i`.
Et l'appel est placé **après** `addMember()`, jamais avant : un `JOIN` refusé
plus haut par `+l` ou `+k` doit laisser l'invitation intacte.

**Changement de comportement à assumer** : une invitation devient à usage
unique. C'est la sémantique des vrais serveurs IRC.

## ✅ Repris — destruction des channels devenus vides

Son `handlePart` supprimait le channel vidé. Repris, **et étendu au chemin de
déconnexion**, sinon le sort d'un salon dépendrait de la *manière* dont le
dernier membre est parti : `PART` le détruisait, une socket coupée non.

Le survivant est un fantôme : `getOrCreateChannel()` rend un salon existant
**sans accorder le statut d'opérateur**, donc le prochain arrivant hérite d'un
salon qu'il ne peut pas administrer, et qui a gardé ses `+i`/`+k`/`+l`. Un
fantôme `+i` est injoignable à jamais.

*Note C++98* : `std::map::erase(iterator)` renvoie `void`, donc la forme
`it = erase(it)` n'existe pas ; `erase(it++)` est l'idiome portable, le
post-incrément étant séquencé avant l'invalidation.

## ❌ Écarté — diffusion de l'`INVITE` à tout le channel

RFC 2812 §3.2.7 et RFC 1459 : seuls **l'émetteur** (via `341 RPL_INVITING`) et
**l'invité** sont notifiés. Diffuser à tout le salon est une fuite
d'information — tout le monde apprend qui a été invité — et n'est le
comportement d'aucun serveur IRC. La seule variante qui notifie des tiers est
la capability IRCv3 `invite-notify`, opt-in via `CAP` et réservée aux
opérateurs.

## ❌ Écarté — refus de se kicker soi-même

Trois raisons :

1. **Hors protocole.** La RFC 2812 §3.2.8 énumère six réponses d'erreur pour
   `KICK` (461, 403, 476, 482, 441, 442) ; aucune ne signifie « tu ne peux pas
   te kicker ». Aucun serveur de production ne le refuse — c'est même un
   idiome pour quitter un salon.
2. **Collision de code.** Il réutilise `482` (`ERR_CHANOPRIVSNEEDED`) avec un
   autre sens, alors que le vrai `482` est émis six lignes plus bas dans la
   même fonction. Un client reçoit deux textes différents sous un même numéro.
3. **Ordre des vérifications faux.** Le bloc est placé avant le test `403`
   (salon inexistant) et avant le `482` : `KICK #nexistepas monpseudo` répond
   « You cannot kick yourself » au lieu de `403`. La précédence des erreurs est
   exactement ce qu'un correcteur teste.

## ⚠️ À adapter — `test_ircserv.sh`

Son script (241 lignes) a une vraie valeur, mais il est **inutilisable tel
quel** : `send_cmds` fait `echo | nc -C -w 1`, or `nc` ferme dès l'EOF de stdin,
**avant** l'arrivée de la réponse — 9 assertions comparent une chaîne vide.
Mesuré contre le `main` actuel : 16/25, dont 9 échecs qui sont purement des
artefacts du harnais. Avec une seule ligne corrigée (garder stdin ouvert) :
24/25. En assertions strictes : 21/25.

Et un vrai bug qu'il révèle : **`353 RPL_NAMREPLY` est absent du `JOIN`**.

---

# Partie 6 — Ce qui reste

| # | Sujet | Pourquoi ça compte |
|---|---|---|
| 1 | **Tester avec irssi** | Point explicite de la grille, jamais fait faute d'installation |
| ~~2~~ | ~~`353`/`366` absents du `JOIN`~~ | ✅ **corrigé** — voir ci-dessous |
| ~~3~~ | ~~`341` absent de l'`INVITE`~~ | ✅ **corrigé** — voir ci-dessous |
| 4 | 40 messages d'erreur en texte libre | Le serveur est **hybride** : vrais numériques pour 001-004, 421, 451, 461, 471, 473, 475, 482, texte libre ailleurs |
| 5 | `inc/Channel.hpp` : `removeInviteOnly(Channel *)` déclarée, jamais définie | Erreur de link le jour où quelqu'un l'appelle |
| 6 | `handlePart` diffuse **après** `removeMember` | Le partant ne reçoit pas son propre `PART` |
| 7 | Logins 42 dans le README | Exigence du chapitre V |


---

# Partie 7 — Séquence de JOIN complète

Un `JOIN` ne se termine pas avec l'écho de la commande. Un client IRC réel
enchaîne sur le sujet du salon, puis remplit sa liste de membres à partir du
`353` et cesse d'attendre au `366`. Sans ces réponses, il ouvre la fenêtre du
salon avec **une liste de membres vide**, quel que soit le nombre de personnes
présentes.

**Avant** (5 lignes) :

```
:ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@localhost
:ircserv 002 alice :Your host is ircserv, running version ft_irc-1.0
:ircserv 003 alice :This server was created at startup
:ircserv 004 alice ircserv ft_irc-1.0 o itkol
:alice!alice@localhost JOIN #test
```

**Après** (8 lignes) :

```
... les quatre numériques d'accueil, identiques ...
:alice!alice@localhost JOIN #test
:ircserv 332 alice #test :notre salon de dev
:ircserv 353 alice = #test :@deja bob alice
:ircserv 366 alice #test :End of /NAMES list
```

- `332 RPL_TOPIC` quand un sujet est posé, `331 RPL_NOTOPIC` sinon
- `353 RPL_NAMREPLY` liste les membres, les opérateurs préfixés d'un `@`
- `366 RPL_ENDOFNAMES` clôt la liste

`INVITE` notifie désormais exactement deux personnes, comme l'impose la
RFC 2812 §3.2.7 — et personne d'autre :

```
émetteur : :ircserv 341 op #inv cible
invité   : :op!op@localhost INVITE cible :#inv
```
