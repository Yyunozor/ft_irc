# ft_irc — Point d'entrée pour la partie C (Channels & operators)

## Le projet en une phrase

On construit un serveur IRC en C++98 (mono-thread, un seul `poll()`, pas
de fork). Le but : qu'un vrai client IRC (irssi, HexChat...) puisse s'y
connecter et discuter normalement — authentification, channels, messages,
commandes d'opérateur.

## Découpage de l'équipe

- **A — réseau** : socket, `poll()`, buffers de lecture/écriture
- **B — protocole** : parsing des commandes, `PASS`/`NICK`/`USER`,
  `PRIVMSG`, réponses conformes au protocole IRC (RFC 1459/2812)
- **C — channels & operators** (toi) : `Channel`, `JOIN`/`PART`,
  `KICK`/`INVITE`/`TOPIC`, `MODE` (`i t k o l`)

## Ce qui tourne déjà

- **A est fonctionnel** : le serveur accepte plusieurs clients en même
  temps, lit/écrit sans jamais bloquer, gère les déconnexions proprement.
- **Un routeur minimal existe** (`Server::dispatchLine`) : il découpe
  chaque ligne reçue en commande + paramètres et la redirige vers un
  handler.
- **`JOIN` est câblé de bout en bout, à titre d'exemple à suivre** :
  `Server::handleJoin()` (dans `src/Server.cpp`) récupère ou crée le
  `Channel` demandé, y ajoute le client, et diffuse (`broadcast`) la
  notification à tous les membres. Testé avec plusieurs clients réels
  connectés en parallèle.
- **`Channel` a le strict minimum** : `addMember`, `removeMember`,
  `isMember`, `broadcast`. Tout le reste (opérateurs, invitations,
  modes) est encore à faire.

## Ce que tu dois faire

Dans `inc/Channel.hpp` / `src/Channel.cpp` :
- `addOperator` / `removeOperator` / `isOperator`
- une liste d'invités (`_invited`) + `invite()` / `isInvited()` — nécessaire
  pour que le mode `i` (invite-only) ait un sens
- `setTopic`, `setKey`, `setInviteOnly`, `setTopicRestricted`, `setUserLimit`
- `empty()` — pour que le serveur sache quand supprimer un channel devenu vide

Dans `src/Server.cpp`, remplace les blocs `TODO (C)` de `dispatchLine()`
par de vrais handlers pour `PART`, `KICK`, `INVITE`, `TOPIC`, `MODE` — en
suivant **`handleJoin()` comme modèle** : récupérer le channel (et/ou le
client visé via `findClientByNick()`), vérifier les permissions si
besoin (`isOperator`), modifier l'état, puis `broadcast()`.

Point important à discuter avec l'équipe : qui devient opérateur d'un
channel à la création ? (convention IRC classique : le premier membre à
rejoindre un channel vide en devient automatiquement opérateur.)

## Comment lancer et tester

**Compiler**
```sh
make re
Lancer le serveur (reste ouvert, terminal 1)


./ircserv 6667 secret
Se connecter avec un client texte simple (terminal 2)


nc 127.0.0.1 6667
Tape par exemple :


JOIN #test
Ouvrir un second client (terminal 3), même commande nc, puis
JOIN #test aussi. Tu dois voir :

le terminal 3 reçoit son propre JOIN
le terminal 2 (déjà dans le channel) reçoit une notification de l'arrivée du second client — c'est le broadcast qui fonctionne
Utilise ce montage à 2-3 clients pour tester tes futures commandes
(PART, KICK, etc.) au fur et à mesure.

Avec un vrai client IRC (optionnel) :


irssi
/connect 127.0.0.1 6667 secret
Attends-toi à des erreurs d'affichage pour l'instant — PASS/NICK/USER
ne sont pas encore vraiment implémentés (partie B), donc irssi ne verra
pas encore les réponses attendues. Ça n'empêche pas de tester JOIN à la
main avec nc.