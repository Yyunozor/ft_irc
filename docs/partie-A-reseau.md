# ft_irc — Partie A : le réseau, guide de maîtrise

Ce document explique ce que fait la partie A, pourquoi elle est écrite
comme ça, et comment elle fonctionne en détail. Objectif : pouvoir
défendre chaque ligne à l'oral.

Fichiers concernés : [`inc/Server.hpp`](../inc/Server.hpp),
[`src/Server.cpp`](../src/Server.cpp), et deux méthodes de
[`inc/Client.hpp`](../inc/Client.hpp) / [`src/Client.cpp`](../src/Client.cpp).

> **Mise à jour** : `src/Server.cpp` a été découpé en trois unités pour éviter
> les conflits de fusion à trois (901 lignes partagées à l'origine). Les
> handlers B (`PASS`/`NICK`/`USER`/`PRIVMSG`/`QUIT`/...) sont partis dans
> [`src/CommandsProtocol.cpp`](../src/CommandsProtocol.cpp), ceux de C
> (`JOIN`/`PART`/`KICK`/`INVITE`/`TOPIC`/`MODE`) dans
> [`src/CommandsChannel.cpp`](../src/CommandsChannel.cpp), et le parsing dans
> [`src/Parser.cpp`](../src/Parser.cpp). **Rien de tout ça ne touche A** :
> `Server.cpp` ne contient plus que la boucle `poll()`, le socket d'écoute,
> l'accept/lecture/écriture bufferisées, le cycle de vie des clients, et les
> recherches partagées (`getOrCreateChannel`, `findClientByNick`). Tout ce
> document reste valable tel quel.

## 1. Le rôle de A

A ne comprend rien au protocole IRC. Il transporte des octets entre le
système d'exploitation et le reste du programme, pour plusieurs clients
à la fois, sans qu'aucun ne bloque les autres. C'est tout — et c'est
déjà beaucoup.

## 2. Concepts à maîtriser avant de défendre cette partie

### Un socket, c'est quoi

Un socket est un descripteur de fichier (`int`, comme ceux retournés par
`open()`) qui représente une extrémité de connexion réseau. On le crée
avec `socket()`, on peut lire dessus (`recv`) et écrire dessus (`send`)
comme sur un fichier, et le fermer (`close`).

### TCP transporte des octets, pas des messages

C'est le point le plus mal compris par les débutants. Quand un client
envoie `"NICK bob\r\n"`, rien ne garantit qu'un seul `recv()` côté
serveur va récupérer exactement cette chaîne. Le réseau peut la couper
en plusieurs morceaux (`"NICK b"` puis `"ob\r\n"`), ou au contraire
regrouper plusieurs commandes envoyées séparément par le client en un
seul `recv()` (`"NICK bob\r\nUSER bob 0 * :Bob\r\n"`).

**Conséquence directe sur le code** : on ne peut jamais traiter les
octets reçus tels quels. Il faut les accumuler dans un buffer, et
n'extraire une commande que lorsqu'un `\r\n` complet est présent.

### Bloquant vs non-bloquant

Par défaut, `recv()` sur un socket **bloquant** suspend le programme
jusqu'à ce que des données arrivent. Avec un seul thread et plusieurs
clients, c'est fatal : le programme resterait figé sur le premier
client silencieux, sans jamais s'occuper des autres.

`fcntl(fd, F_SETFL, O_NONBLOCK)` rend un socket **non-bloquant** :
`recv()`/`accept()`/`send()` retournent immédiatement même s'il n'y a
rien à faire, avec un code de retour particulier. On configure ça sur
le socket d'écoute et sur chaque socket client.

### `poll()` — le multiplexage d'I/O

Puisqu'on ne peut plus attendre bêtement sur un seul socket, il faut un
mécanisme qui surveille **tous les sockets à la fois** et ne réveille le
programme que quand l'un d'eux a quelque chose à signaler (données
prêtes à lire, place pour écrire, erreur). C'est le rôle de `poll()` :
un seul appel bloquant qui surveille un tableau de `struct pollfd`
(chacun avec un fd et les événements qui l'intéressent) et retourne dès
qu'au moins un est prêt.

C'est le design imposé par le sujet : *un seul* `poll()` pour tout le
programme (le socket d'écoute et tous les clients dans le même tableau).

### Le pattern Reactor

Le nom académique de cette architecture (un thread, une boucle
d'événements, un multiplexeur qui dit quoi traiter). C'est une
technique ancienne — `select()` date de 1983, `poll()` du milieu des
années 80 — pensée précisément pour gérer beaucoup de connexions sans
thread ni fork. `webserv` (l'autre projet 42) utilise exactement le même
principe pour la même raison.

## 3. Ce qu'on a construit, fonction par fonction

### `Client::extractLine()` / `consumeWrite()` / `hasPendingWrite()`

Avant de parler du réseau, deux méthodes côté `Client` qui rendent tout
le reste possible :

```cpp
bool Client::extractLine(std::string &line)
{
    std::string::size_type pos = _readBuf.find("\r\n");
    if (pos == std::string::npos)
        return (false);
    line = _readBuf.substr(0, pos);
    _readBuf.erase(0, pos + 2);
    return (true);
}
```
Cherche un `\r\n` dans le buffer de réception. S'il n'y en a pas encore,
on ne fait rien (la commande n'est pas complète). S'il y en a un, on
extrait tout ce qui précède et on le retire du buffer.

```cpp
void Client::consumeWrite(std::size_t n)
{
    _writeBuf.erase(0, n);
}
```
Retire du buffer d'écriture les `n` octets qui ont réellement été
envoyés par `send()` — utile parce que `send()` peut n'envoyer qu'une
partie des données demandées (partial send).

### `Server::setupListenSocket()`

Exécutée une seule fois, au démarrage :

```cpp
_listenFd = socket(AF_INET, SOCK_STREAM, 0);
setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
bind(_listenFd, ...);
listen(_listenFd, SOMAXCONN);
fcntl(_listenFd, F_SETFL, O_NONBLOCK);
```

- `socket(AF_INET, SOCK_STREAM, 0)` : crée un socket TCP/IPv4
- `SO_REUSEADDR` : permet de relancer le serveur juste après l'avoir
  arrêté sans attendre que l'OS libère le port (sinon `bind()` échoue
  avec "address already in use" pendant ~1 minute)
- `bind()` : attache le socket au port demandé
- `listen()` : passe le socket en mode "j'accepte des connexions
  entrantes", avec une file d'attente de connexions en attente
  (`SOMAXCONN`)
- `fcntl(O_NONBLOCK)` : le rend non-bloquant

Le socket d'écoute est ensuite ajouté en première position (index 0) du
tableau `_pollFds` — **une convention que tout le reste du code
suppose** : l'index 0, c'est toujours le socket d'écoute, jamais un
client.

### `Server::start()` — la boucle principale

```cpp
while (true)
{
    int ready = poll(&_pollFds[0], _pollFds.size(), -1);
    if (ready < 0)
        continue;

    if (_pollFds[0].revents & POLLIN)
        acceptClient();

    std::vector<int> toRemove;
    for (std::size_t i = 1; i < _pollFds.size(); ++i)
    {
        // ... lire, écrire, ou marquer pour suppression
    }
    for (std::size_t i = 0; i < toRemove.size(); ++i)
        removeClient(toRemove[i]);

    refreshPollEvents();
}
```

Le déroulé à chaque tour :
1. `poll(-1)` attend indéfiniment (`-1` = pas de timeout) qu'un des fds
   surveillés ait un événement
2. Si le socket d'écoute a `POLLIN`, une connexion attend →
   `acceptClient()`
3. On parcourt chaque client, on lit/écrit selon ce qu'il a signalé
4. Les clients à supprimer sont collectés dans `toRemove`, **jamais
   supprimés pendant la boucle `for`** : modifier `_pollFds` (donc ses
   indices) pendant qu'on le parcourt casserait l'itération
5. `refreshPollEvents()` recalcule, pour chaque client, s'il faut encore
   surveiller `POLLOUT`

### Pourquoi `POLLHUP`/`POLLERR` sont traités comme `POLLIN`

```cpp
if ((revents & (POLLIN | POLLHUP | POLLERR)) && !readFromClient(fd))
{
    toRemove.push_back(fd);
    continue;
}
```

Première version (buggée) : `POLLHUP` était vérifié **avant** `POLLIN`,
et faisait supprimer le client immédiatement. Problème réel, découvert
en testant : un client qui envoie une dernière commande puis ferme sa
connexion déclenche `POLLIN` **et** `POLLHUP` dans le même `revents`. Le
supprimer sans lire d'abord aurait perdu sa dernière commande et
empêché toute réponse de lui être renvoyée. La version actuelle tente
toujours `recv()` d'abord ; c'est son retour (0 ou négatif) qui décide
de la déconnexion, jamais l'indicateur `POLLHUP` seul.

### `Server::acceptClient()`

```cpp
int fd = accept(_listenFd, NULL, NULL);
if (fd < 0)
    return;
fcntl(fd, F_SETFL, O_NONBLOCK);
_clients[fd] = new Client(fd);
// ajouté à _pollFds avec POLLIN
```

`accept()` récupère la première connexion en attente et retourne un
**nouveau** fd dédié à ce client (le socket d'écoute continue, lui, à
n'accepter que des connexions). Ce nouveau fd est rendu non-bloquant lui
aussi — c'est facile à oublier, mais indispensable, sinon ce client
précis pourrait bloquer toute la boucle plus tard.

Un seul `accept()` par tour de boucle, pas une boucle `while`. Ce n'est
pas un oubli : si plusieurs connexions arrivent en même temps, `poll()`
va re-signaler `POLLIN` sur le socket d'écoute au tour suivant tant
qu'il en reste une en attente.

### `Server::readFromClient()`

```cpp
char buf[4096];
ssize_t n = recv(fd, buf, sizeof(buf), 0);
if (n <= 0)
    return (false);
client.appendToRead(buf, n);
std::string line;
while (client.extractLine(line))
    dispatchLine(client, line);
return (true);
```

`recv()` retourne :
- `> 0` : le nombre d'octets effectivement lus
- `0` : le client a fermé proprement sa connexion (FIN)
- `< 0` : erreur — **on ne regarde jamais pourquoi** (interdiction du
  sujet de lire `errno`), on traite ça comme une déconnexion

Une fois les octets ajoutés au buffer, on boucle sur `extractLine()`
tant qu'il y a des lignes complètes — un seul `recv()` peut contenir
plusieurs commandes d'un coup, il ne faut pas en perdre.

### `Server::writeToClient()`

```cpp
ssize_t n = send(fd, out.data(), out.size(), 0);
if (n > 0)
    client.consumeWrite(n);
```

Envoie ce qui est en attente. Si `send()` n'a pu envoyer qu'une partie
des données (`n < out.size()`), le reste reste dans le buffer pour le
prochain tour — c'est le *partial send* mentionné dans le README.

### `Server::removeClient()`

Supprime le `Client` (avec `delete`), le retire de `_clients` et de
`_pollFds`, puis `close(fd)`. Appelé uniquement après la boucle `for`
principale (suppression différée, cf. plus haut).

### `Server::refreshPollEvents()`

```cpp
_pollFds[i].events = POLLIN;
if (client.hasPendingWrite())
    _pollFds[i].events |= POLLOUT;
```

`POLLOUT` n'est demandé que si le client a réellement quelque chose à
envoyer. Sinon `poll()` réveillerait le programme en boucle pour dire
"tu peux écrire" alors qu'il n'y a rien à écrire — ça ferait tourner le
CPU à 100% pour rien.

## 4. Le flux complet, schématisé

```
                     ┌─────────────────────┐
                     │   poll(_pollFds)     │  ← bloque jusqu'à un événement
                     └──────────┬───────────┘
                                │
              ┌─────────────────┴──────────────────┐
              │                                      │
     socket d'écoute prêt                   un client a un événement
              │                                      │
       acceptClient()                    POLLIN/HUP/ERR → readFromClient()
     (nouveau Client + pollfd)              recv() → buffer → extractLine()
                                              → dispatchLine() (remplit le
                                                write buffer du/des clients
                                                concernés)

                                            POLLOUT → writeToClient()
                                              send() depuis le write buffer

                                            recv()==0/erreur → à supprimer
                                              (différé, après la boucle)
```

## 5. Pièges déjà rencontrés (bons exemples pour la soutenance)

1. **Ordre `POLLHUP` vs `POLLIN`** (détaillé plus haut) — trouvé en
   testant avec un vrai client qui envoie puis ferme immédiatement.
2. **`std::vector::data()` est C++11, pas C++98.** Ça compilait quand
   même avec `clang++` sur macOS (extension de la bibliothèque), mais ce
   n'est pas garanti avec `g++` en mode strict sur la machine de
   l'école, et un évaluateur peut le relever comme non-conforme au
   sujet. Remplacé par `&_pollFds[0]`, un idiome garanti par le
   standard C++98/C++03 (le stockage d'un `std::vector` est contigu).

## 6. Ce qu'il reste à faire pour A

- ~~Plafonner la taille de `_readBuf`~~ — **fait** :
  `Client::pendingLineTooLong()` (`src/Client.cpp`) renvoie `true` au-delà de
  512 octets accumulés sans `\r\n` (limite RFC), et `Server::readFromClient()`
  déconnecte le client dans ce cas, juste après avoir traité toutes les
  lignes complètes déjà reçues. Vérifié : un client qui envoie 600 octets sans
  jamais de retour à la ligne est bien coupé, le serveur reste opérationnel
  pour les autres.
- ~~`extractLine()` n'accepte que `\r\n` strict~~ — **fait** : elle cherche
  maintenant un `\n` et retire un `\r` précédent s'il est présent, donc les
  deux formes sont acceptées. Vérifié en bytes bruts (Python) : un vrai
  client qui envoie du `\r\n` conforme ne voit jamais de `\r` résiduel dans
  les réponses, et une ligne vide (`\n` seul en tout début de buffer) ne
  plante pas le serveur.
- ~~Décider si `SIGINT` doit fermer proprement les connexions~~ — **fait** :
  `SIGINT`/`SIGTERM` posent un flag (`g_running`, `sig_atomic_t`), la boucle
  `poll()` sort proprement et les destructeurs tournent (`close(fd)` sur
  chaque client restant a aussi été ajouté dans `~Server()`). Corrige une
  fuite mémoire/fd rédhibitoire à l'évaluation (Ctrl+C tuait le process avant
  que `~Server()` ne s'exécute).
- **Tester avec un plus grand nombre de connexions simultanées** — fait
  partiellement : 8 clients en parallèle avec déconnexions brutales, et un
  test de flood (3000 messages accumulés pendant qu'un client est figé en
  `^Z`) sans fuite ni blocage. Pas testé au-delà.

## 7. Questions probables en soutenance, et pistes de réponse

**"Pourquoi un seul `poll()` et pas un thread par client ?"**
→ Le sujet l'impose, mais aussi : un thread par client ne passe pas à
l'échelle (coût mémoire, changement de contexte), et introduit des
problèmes de synchronisation (accès concurrent à `_clients`/`_channels`)
qu'on évite complètement avec un seul thread.

**"Pourquoi rendre les sockets non-bloquants si tu utilises déjà
`poll()` pour savoir quand ils sont prêts ?"**
→ Filet de sécurité : `poll()` dit qu'un fd est *probablement* prêt,
mais entre le retour de `poll()` et l'appel à `recv()`/`send()`, l'état
peut changer (par exemple un autre thread/process, ou une race
inhérente à l'API). Un socket bloquant pourrait alors quand même
suspendre le programme. Le non-bloquant garantit qu'on ne bloque
*jamais*, même dans ce cas limite.

**"Que se passe-t-il si `send()` échoue avec `POLLOUT` déjà signalé ?"**
→ On ne déconnecte pas le client sur la foi d'un seul échec : on laisse
les données dans le buffer et on retente au tour suivant. On ne lit pas
`errno`, donc on ne peut pas distinguer une vraie erreur d'un simple
"réessaie plus tard" — le comportement choisi est donc de rester
tolérant côté écriture (au pire, si le client est vraiment mort, `recv()`
finira par le signaler via un retour `<= 0`).

**"Pourquoi la suppression des clients est-elle différée ?"**
→ Parce qu'on est en train d'itérer sur `_pollFds` avec un indice `i`
pendant la boucle `for`. Supprimer un élément du vecteur pendant
l'itération décale tous les indices suivants, ce qui ferait sauter des
clients ou accéder à un élément invalide.

**"Pourquoi le buffer de lecture fait 4096 octets, pas plus, pas
moins ?"**
→ Taille arbitraire raisonnable pour un `recv()` — largement suffisante
pour une commande IRC (limite RFC de 512 octets par ligne), et un choix
qui n'a pas besoin d'être extrêmement précis puisqu'on boucle de toute
façon (les données sont accumulées dans `_readBuf`, pas perdues si une
commande dépasse un seul `recv()`).
