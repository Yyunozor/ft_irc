# ft_irc — audit de conformité au sujet

Établi contre `en.subject-15.pdf`, sur l'état du dépôt au commit `0206b2f`.
Chaque constat est soit **vérifié à l'exécution** (la preuve est citée), soit
**relevé à la lecture** (c'est indiqué explicitement).

**Aucune correction n'a été appliquée.** Ce document liste ce qu'il faut faire
et qui doit le faire.

---

## Résumé

| Sévérité | Nombre | Effet si non corrigé |
|---|---|---|
| 🔴 Rédhibitoire | 3 | Note 0 (chapitre II ou IV.1) |
| 🟠 Bloquant pour l'évaluation | 4 | Le client de référence ne fonctionne pas |
| 🟡 Micro-correction | 5 | Aucun effet sur la note, propreté du code |
| ✅ Conforme | 12 | À laisser tel quel |

---

## 🔴 Rédhibitoire — note 0

### R1. Crash sur `JOIN #dev` — lecture hors bornes

**Fichier** : `src/Server.cpp:307` (`handleJoin`)

```cpp
if (params[1] != channel->getKey())
```

`params[1]` est lu sans vérifier la taille du vecteur. Sur `JOIN #dev` — sans
clé, donc le cas normal — `params.size()` vaut 1 : c'est une lecture hors
bornes, comportement indéfini.

**Vérifié à l'exécution**, build `make DEBUG=1` :

```
JOIN #dev
==96489==ERROR: AddressSanitizer: heap-buffer-overflow
SUMMARY: heap-buffer-overflow in std::basic_string::__is_long()
*** processus mort ***
```

**Règle violée** — chapitre II : *« Your program should not crash in any
circumstances […] your grade will be 0. »*

**Correction** (une ligne) :

```cpp
if (params.size() > 1 && params[1] != channel->getKey())
```

À noter aussi : en l'état, un channel sans clé refuse toute tentative de
`JOIN` fournissant une clé, et le test s'exécute **avant** `addMember()`, donc
la vérification d'invitation `+i` passe avant celle de la clé — l'ordre est
correct, seule la borne est fautive.

---

### R2. Use-after-free à la déconnexion

**Fichier** : `src/Server.cpp:182` (`removeClient`)

```cpp
delete it->second;      // le client reste référencé dans les channels
```

`Channel` stocke des `Client *` bruts. Un client détruit sans avoir été retiré
de ses channels y laisse un pointeur pendouillant ; le prochain `broadcast()`
déréférence de la mémoire libérée.

**Vérifié à l'exécution** — scénario : ana et ben dans `#x`, ben ferme
brutalement, cat écrit dans le channel.

```
==28686==ERROR: AddressSanitizer: heap-buffer-overflow
*** processus mort ***
```

**Règle violée** — chapitre II, même clause que R1.

**Correction** : retirer le client de tous les channels **avant** le `delete`.

```cpp
for (std::map<std::string, Channel *>::iterator c = _channels.begin();
     c != _channels.end(); ++c)
    c->second->removeMember(it->second);
delete it->second;
```

---

### R3. Le client de référence doit se connecter sans erreur

**Règle** — chapitre IV.1 : *« Your reference client must be able to connect to
your server without encountering any error. »* Et chapitre VII : *« Your
reference client will be used during the evaluation process. »*

Aujourd'hui, le serveur répond en texte libre au lieu des réponses numériques
du protocole. **Vérifié à l'exécution** :

| Émis par le serveur | Attendu par un client IRC |
|---|---|
| `Welcome to the IRC server, ana!` | `:ircserv 001 ana :Welcome to the Internet Relay Network ana!ana@localhost` |
| `ERROR 461: PASS command requires…` | `:ircserv 461 ana PASS :Not enough parameters` |
| `PONG tok` | `:ircserv PONG ircserv :tok` |
| `:ana JOIN #dev` | `:ana!ana@localhost JOIN #dev` |

Sans les numériques `001`-`004`, un client IRC réel ne se considère jamais
enregistré et reste bloqué à la connexion. Sans le préfixe complet
`nick!user@host`, il ne peut pas attribuer les messages ni peupler sa liste de
membres.

**C'est la partie B**, donc à corriger par le propriétaire de B. Les 30
fonctions nécessaires existent déjà dans `inc/Replies.hpp`.

---

## 🟠 Bloquant pour l'évaluation

### B1. `handleUSER` impose `username == nickname`

**Fichier** : `src/Server.cpp:630`

```cpp
if (client.getNick() != params[0])
    client.appendToWrite("ERROR 464: USER command can only be used with the same nickname\r\n");
```

Aucun client IRC ne respecte cette contrainte : irssi envoie le login système
comme username, distinct du pseudo. L'enregistrement échoue systématiquement.

La RFC 2812 définit `USER <user> <mode> <unused> :<realname>` — le premier
paramètre est le *username*, sans rapport avec le pseudo.

**Correction** : supprimer ce test.

### B2. `handleUSER` attend 2 paramètres au lieu de 4

**Fichier** : `src/Server.cpp:635` — `if (params.size() < 2)`. La forme réelle
en compte 4. irssi envoie `USER yyuno 0 * :Yyuno`, donc `params[1]` vaut `"0"`
et est stocké comme realname.

**Correction** : exiger `params.size() >= 4`, prendre `params[0]` comme
username et `params[3]` comme realname.

### B3. Commandes manquantes

`QUIT`, `NOTICE` et `PONG` ne sont pas traitées. Une commande inconnue est
**renvoyée en écho** au client (`src/Server.cpp:281`) au lieu de produire un
`421 ERR_UNKNOWNCOMMAND`. irssi envoie `CAP LS 302` à la connexion : l'écho le
laisse attendre indéfiniment un `CAP END`.

### B4. Pas de garde-fou avant enregistrement

Seuls `handleJoin` et `handleMode` vérifient `isRegistered()`. Les autres
commandes sont exécutables par un client non enregistré, là où le protocole
impose `451 ERR_NOTREGISTERED`.

---

## 🟡 Micro-corrections — mentionnées, non appliquées

| # | Fichier | Constat |
|---|---|---|
| M1 | `src/Server.cpp:319-323` (`std::exit` en 322) | Bloc de code mort commenté contenant `std::exit()`. Inoffensif (commenté), mais `exit` ne figure pas dans la liste autorisée : à supprimer pour éviter la question en soutenance. |
| M2 | `src/Server.cpp:583-586` | Second bloc de code mort commenté, même forme. |
| M3 | `src/Server.cpp` | Les channels vides ne sont jamais détruits. Ils s'accumulent pendant l'exécution (libérés seulement dans le destructeur de `Server`). Fuite lente, sans crash. |
| M4 | `src/main.cpp:14` | Le port n'est pas validé : `std::atoi("abc")` vaut 0, `atoi("99999")` déborde la plage TCP. Le mot de passe vide est accepté. Ne crashe pas, mais `bind()` échoue avec un message peu clair. |
| M5 | `src/Client.cpp:62` | `_readBuf` n'a pas de plafond. Un client qui envoie des octets sans jamais de `\r\n` fait grossir la mémoire indéfiniment. Non exploité par un correcteur normal, mais c'est une vraie faiblesse. |

---

## ✅ Conforme — à laisser tel quel

Vérifié, rien à changer :

1. **Un seul `poll()`** dans tout le programme (`src/Server.cpp:83`) ✅
2. **Tous les fd non-bloquants** — socket d'écoute (`:62`) et sockets acceptées (`:128`) ✅
3. **`fcntl` sous la forme exacte autorisée** `fcntl(fd, F_SETFL, O_NONBLOCK)` — pas de `F_GETFL`, conforme au chapitre IV.2 ✅
4. **Aucun `errno` lu après `recv`/`send`** — une seule occurrence, dans un commentaire ✅
5. **`SIGPIPE` ignoré au démarrage** (`src/Server.cpp:76`) — sans quoi un `send()` vers un pair fermé tuerait le processus ✅
6. **Aucun `fork`, aucun thread** ✅
7. **Aucune fonction hors liste autorisée** ✅
8. **C++98 strict** — compile avec `-std=c++98 -pedantic-errors`, aucun `auto`, `nullptr`, lambda, range-for ✅
9. **Aucune bibliothèque externe, pas de Boost** ✅
10. **Makefile** — `$(NAME)`, `all`, `clean`, `fclean`, `re` présents, pas de relink inutile ✅
11. **`POLLOUT` armé seulement si le client a des données en attente** (`src/Server.cpp:202`) — évite la boucle à 100% CPU ✅
12. **Suppression différée des clients** — marqués pendant l'itération, détruits après ✅

Côté fonctionnel, **vérifié à l'exécution et fonctionnel** :
`JOIN` (hors bug de bornes), `TOPIC` avec diffusion, `PRIVMSG` vers un client
et vers un channel, `INVITE` répondant correctement sur un pseudo inexistant,
création de channel avec attribution du statut opérateur au créateur
(`getOrCreateChannel`), et la classe `Channel` complète (opérateurs, invités,
clé, limite, topic restreint).

---

## ⚠️ Correction d'une recommandation antérieure

J'avais proposé, et appliqué dans une version précédente, un `send()`
« best-effort » juste avant `close()` dans `removeClient()`, pour qu'un client
rejeté sur `PASS` reçoive bien son `464`.

**Cette approche est risquée.** Le chapitre IV.1 est explicite :

> *« if you attempt to read/recv or write/send in any file descriptor without
> using poll() (or equivalent), your grade will be 0. »*

Un `send()` déclenché par la fermeture, et non par un `POLLOUT` remonté par
`poll()`, entre exactement dans cette description. Un correcteur strict peut
le sanctionner.

**Alternative conforme** : ne pas supprimer le client immédiatement. Le marquer
`quitting`, cesser de lire ses commandes, mais **le garder dans la boucle
`poll()`** jusqu'à ce que son buffer d'écriture soit vide — c'est `POLLOUT` qui
émet la réponse, comme pour tout le monde — puis le retirer. Un tour de boucle
de plus, et aucune I/O hors `poll()`.

---

## Répartition proposée des corrections

| # | Qui | Pourquoi |
|---|---|---|
| R1, R2 | **à décider en équipe** | Trois lignes au total, dans du code partagé. Rédhibitoire : quelqu'un doit le faire aujourd'hui. |
| R3, B1-B4 | **partie B** | C'est le protocole. `Replies.hpp` est prêt. |
| M1-M5 | **propriétaire de chaque fichier** | Aucun effet sur la note. |

**Deux préalables avant que B soit corrigé :**

1. Trancher quelle version de B fait foi. Deux implémentations existent : celle
   du dépôt et celle de l'historique (`4e3e385`, avec numériques complets,
   testée 16/16). Personne ne devrait écrire de code avant cet arbitrage.
2. Le parser autonome (`inc/Parser.hpp`, `src/Parser.cpp`, commit `0206b2f`)
   est écrit, testé 16/16 et compilé, mais **pas encore branché** :
   `dispatchLine()` appelle toujours l'ancien `parseLine()`, qui pousse un
   paramètre vide sur un double espace (`PRIVMSG  bob :hi` → cible vide → 401).
   Le brancher touche `src/Server.cpp`.
