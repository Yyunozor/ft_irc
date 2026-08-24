# Workflow réseau — les syscalls en pratique

Ce guide détaille, dans l'ordre exact où ils sont utilisés, tous les
appels système (syscalls) qui font tourner le serveur. Pour le
"pourquoi" architectural (poll vs epoll, bloquant vs non-bloquant),
voir [partie-A-reseau.md](partie-A-reseau.md) — ici on se concentre sur
**la mécanique précise, syscall par syscall, dans l'ordre où le
programme les appelle réellement**.

## Vue d'ensemble : deux temporalités bien distinctes

1. **Le setup** — une poignée de syscalls exécutés **une seule fois**,
   au tout début du programme, pour préparer le socket d'écoute
2. **La boucle** — un petit groupe de syscalls **répétés indéfiniment**,
   une fois par client actif, tant que le serveur tourne

Confondre les deux est une source fréquente d'erreurs (par exemple,
rappeler `bind()` dans la boucle n'aurait aucun sens — un socket ne se
bind qu'une fois).

## Phase 1 — Le setup, une seule fois (`setupListenSocket()`)

Dans l'ordre strict, chacun dépendant du précédent :

### 1. `socket(AF_INET, SOCK_STREAM, 0)`
```cpp
_listenFd = socket(AF_INET, SOCK_STREAM, 0);
```
Crée un fd "vierge" représentant un socket. `AF_INET` = IPv4,
`SOCK_STREAM` = TCP (flux fiable et ordonné, par opposition à
`SOCK_DGRAM` = UDP, non fiable). Retourne `-1` en cas d'échec. À ce
stade, le socket n'est ni attaché à un port, ni en écoute — c'est juste
un fd qui existe.

### 2. `setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, ...)`
```cpp
int opt = 1;
setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```
Configure une option sur le socket. `SO_REUSEADDR` dit à l'OS
"autorise-moi à réutiliser ce port immédiatement, même s'il est encore
marqué occupé par une connexion précédente qui vient de se fermer" (état
`TIME_WAIT` du protocole TCP, qui dure ~1 minute par défaut). Sans ça,
relancer le serveur juste après l'avoir arrêté échouerait souvent avec
"Address already in use".

### 3. `bind(_listenFd, &addr, sizeof(addr))`
```cpp
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;   // écoute sur toutes les interfaces
addr.sin_port = htons(_port);         // htons: ordre des octets réseau
bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
```
Attache le socket à une adresse et un port précis. `INADDR_ANY` = accepter
des connexions arrivant sur n'importe quelle interface réseau de la
machine (pas juste `127.0.0.1`). `htons()` (*host to network short*)
convertit l'entier du port dans l'ordre d'octets attendu par le réseau
(big-endian) — nécessaire parce que l'ordre mémoire natif d'un CPU x86
est l'inverse (little-endian).

### 4. `listen(_listenFd, SOMAXCONN)`
```cpp
listen(_listenFd, SOMAXCONN);
```
Fait passer le socket en mode écoute : à partir de maintenant, l'OS
accepte des tentatives de connexion entrantes et les met en attente dans
une **file d'attente** avant que le programme les traite avec `accept()`.
`SOMAXCONN` est la taille maximale de cette file — au-delà, les
connexions supplémentaires sont refusées le temps que le programme
"consomme" les précédentes.

### 5. `fcntl(_listenFd, F_SETFL, O_NONBLOCK)`
```cpp
fcntl(_listenFd, F_SETFL, O_NONBLOCK);
```
Rend le socket non-bloquant. À partir de là, `accept()` sur ce socket ne
suspend jamais le programme — s'il n'y a personne dans la file
d'attente, il retourne immédiatement une erreur au lieu d'attendre.

**Pourquoi cet ordre précis ?** Chaque étape dépend de la précédente :
pas de `bind()` sans un fd valide (`socket()` d'abord), pas de `listen()`
sur un socket non attaché à un port (`bind()` avant), pas de sens à
rendre non-bloquant un socket qu'on n'utilisera pas encore (`fcntl()` en
dernier, juste avant utilisation réelle).

## Phase 2 — La boucle infinie : `poll()`

```cpp
int ready = poll(&_pollFds[0], _pollFds.size(), -1);
```

Contrairement à la phase 1, ce syscall est appelé **en boucle, tant que
le serveur tourne**. Trois paramètres :
- `&_pollFds[0]` : pointeur vers le premier élément d'un tableau de
  `struct pollfd` (chaque élément = un fd à surveiller + les événements
  qui nous intéressent, généralement `POLLIN` = "préviens-moi quand il y
  a des données à lire")
- `_pollFds.size()` : la taille du tableau
- `-1` : le timeout en millisecondes — `-1` veut dire "bloque
  indéfiniment, jusqu'à ce qu'au moins un événement arrive" (pas de
  polling actif, pas de gaspillage CPU en attendant)

`poll()` retourne le **nombre** de fds ayant un événement (pas lesquels —
il faut ensuite parcourir le tableau et regarder le champ `revents` de
chacun). Les événements possibles qu'on regarde :
- `POLLIN` : des données sont prêtes à être lues (`recv()` ne bloquera
  pas)
- `POLLOUT` : le socket peut accepter de l'écriture (`send()` ne
  bloquera pas) — demandé seulement quand on a effectivement quelque
  chose à envoyer
- `POLLHUP` : l'autre extrémité a fermé la connexion
- `POLLERR` : une erreur est survenue sur ce fd
- `POLLNVAL` : le fd n'est pas valide (ne devrait normalement pas arriver
  si la gestion du tableau est correcte)

## Phase 3 — Réagir à un événement

Une fois que `poll()` a signalé un fd particulier, ce qu'on fait dépend
de **lequel** (le socket d'écoute ou un client) et de **quel événement**.

### Sur le socket d'écoute (`_pollFds[0]`) : `accept()`
```cpp
int fd = accept(_listenFd, NULL, NULL);
```
Retire **une** connexion de la file d'attente créée par `listen()`, et
retourne un **nouveau** fd, dédié à cette connexion précise (le socket
d'écoute, lui, continue d'exister et reste réservé à accepter d'autres
connexions futures — il ne sert jamais à transférer des données). Les
deux `NULL` : on ne récupère pas ici l'adresse IP du client (pas
nécessaire pour ce projet).

Immédiatement après, on répète l'étape 5 de la phase 1 sur ce **nouveau**
fd :
```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```
Chaque fd a son propre statut bloquant/non-bloquant — le rendre
non-bloquant sur le socket d'écoute ne rend pas automatiquement
non-bloquants les fds clients qui en découlent. Un oubli ici est un bug
classique : ce client précis pourrait bloquer toute la boucle plus tard.

### Sur un client, si `POLLIN`/`POLLHUP`/`POLLERR` : `recv()`
```cpp
char buf[4096];
ssize_t n = recv(fd, buf, sizeof(buf), 0);
```
Lit jusqu'à 4096 octets disponibles dans le buffer `buf`. Le dernier
paramètre (`0`) est un ensemble de flags optionnels, non utilisés ici.
Trois cas pour le retour `n` :
- `n > 0` : nombre d'octets effectivement lus (peut être inférieur à
  4096, TCP ne garantit rien sur la taille des paquets)
- `n == 0` : le client a fermé proprement sa connexion (FIN TCP reçu)
- `n < 0` : une erreur — on ne l'inspecte jamais plus finement (le sujet
  interdit de lire `errno`), traité comme une déconnexion

### Sur un client, si `POLLOUT` : `send()`
```cpp
ssize_t n = send(fd, out.data(), out.size(), 0);
```
Tente d'envoyer les octets en attente. Peut n'en envoyer qu'une partie
(`n < out.size()`) — c'est le *partial send*, normal en TCP, pas une
erreur. Le reste attend le prochain tour de boucle.

### Suppression d'un client : `close()`
```cpp
close(fd);
```
Ferme le fd, libère les ressources associées côté OS. Appelé une fois
qu'on a décidé de déconnecter un client (après un `recv()` retournant
`<= 0`, ou une erreur détectée).

## La vie complète d'un client, en une frise chronologique

```
CÔTÉ SERVEUR (une seule boucle poll() pour tout le monde)
──────────────────────────────────────────────────────────────
setup (une fois) : socket → setsockopt → bind → listen → fcntl
──────────────────────────────────────────────────────────────
                    ┌── tour de boucle ──┐
poll() bloque ──────┤                    │
                    │ POLLIN sur écoute  │
                    └──────┬─────────────┘
                           │
                     accept() ──► nouveau fd ──► fcntl(NONBLOCK)
                           │
                    (le client est maintenant surveillé par poll()
                     au même titre que le socket d'écoute)
                           │
                    ┌── tours suivants ──┐
poll() bloque ──────┤                    │
                    │ POLLIN sur fd      │
                    └──────┬─────────────┘
                           │
                      recv() ──► ligne complète ──► traitement
                           │
                    ┌── éventuellement ──┐
poll() bloque ──────┤ POLLOUT sur fd     │
                    └──────┬─────────────┘
                           │
                      send() ──► réponse envoyée
                           │
                    ... (autant de cycles recv/send que nécessaire) ...
                           │
                    POLLHUP/recv()==0 ──► close(fd) + nettoyage
```

Le point essentiel : **tout ça se passe dans la même boucle `poll()`
unique**, entrelacé avec exactement le même traitement pour tous les
autres clients connectés en parallèle. Rien n'est dédié à un client en
particulier — chaque tour de boucle peut traiter le socket d'écoute et
N clients différents, selon ce que `poll()` a signalé.

## Tableau récapitulatif

| Syscall | Header | Appelé quand | Bloque ? | Dans notre code |
|---|---|---|---|---|
| `socket()` | `<sys/socket.h>` | une fois, au démarrage | non | `setupListenSocket()` |
| `setsockopt()` | `<sys/socket.h>` | une fois, juste après `socket()` | non | `setupListenSocket()` |
| `bind()` | `<sys/socket.h>` | une fois | non | `setupListenSocket()` |
| `listen()` | `<sys/socket.h>` | une fois | non | `setupListenSocket()` |
| `fcntl()` | `<fcntl.h>` | au démarrage (écoute) + à chaque connexion acceptée | non | `setupListenSocket()`, `acceptClient()` |
| `poll()` | `<poll.h>` | en boucle, tant que le serveur tourne | **oui**, jusqu'à un événement | `start()` |
| `accept()` | `<sys/socket.h>` | quand `poll()` signale le socket d'écoute | non (fd non-bloquant) | `acceptClient()` |
| `recv()` | `<sys/socket.h>` | quand `poll()` signale `POLLIN` sur un client | non (fd non-bloquant) | `readFromClient()` |
| `send()` | `<sys/socket.h>` | quand `poll()` signale `POLLOUT` sur un client | non (fd non-bloquant) | `writeToClient()` |
| `close()` | `<unistd.h>` | à la déconnexion d'un client, ou à l'arrêt du serveur | non | `removeClient()`, destructeur de `Server` |

**Le seul syscall bloquant volontaire de tout le programme, c'est
`poll()`** — et c'est exactement le but : au lieu de bloquer sur un
`recv()` ou un `accept()` individuel (ce qui figerait le serveur pour
tous les autres clients), on bloque une seule fois, en attendant
n'importe quel événement sur n'importe quel fd surveillé.

## Pourquoi cet ordre est obligatoire, pas juste une convention

- `socket()` avant tout : sans fd valide, tous les autres appels
  échoueraient (paramètre invalide)
- `bind()` avant `listen()` : on ne peut pas écouter sur un socket qui
  n'est attaché à aucun port
- `listen()` avant `accept()` : sans file d'attente créée par `listen()`,
  `accept()` n'a rien à retirer
- `fcntl(O_NONBLOCK)` **avant d'entrer dans la boucle `poll()`** : sinon,
  la toute première connexion pourrait bloquer le programme avant même
  que le mécanisme de multiplexage ne soit utile
- `accept()` avant tout `recv()`/`send()` sur un client : ces derniers
  ont besoin du fd que `accept()` vient de créer, il n'existe pas avant

## Pièges déjà rencontrés sur ce projet (voir aussi partie-A-reseau.md)

- Vérifier `POLLHUP` avant `POLLIN` aurait fait perdre la dernière
  commande d'un client qui se déconnecte juste après l'avoir envoyée —
  corrigé en tentant toujours `recv()` d'abord
- Oublier `fcntl(O_NONBLOCK)` sur le fd retourné par `accept()` (et pas
  seulement sur le socket d'écoute) est une erreur facile à faire —
  chaque fd a son propre statut bloquant, indépendant des autres
