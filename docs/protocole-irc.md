# ft_irc — Le protocole IRC, guide de maîtrise (partie B)

Ce document complète [`partie-A-reseau.md`](partie-A-reseau.md) et
[`syscalls-reseau.md`](syscalls-reseau.md). Ces deux-là expliquent comment des
octets circulent entre l'OS et le programme. Celui-ci explique **ce que ces
octets veulent dire** : la grammaire IRC, les réponses numériques, et la
machine à états qu'un client traverse avant de pouvoir faire quoi que ce soit
d'utile. C'est la partie B — actuellement un TODO dans
[`src/Server.cpp`](../src/Server.cpp).

## 1. Le rôle de B

A garantit qu'une ligne complète (`\r\n`-terminée) arrive intacte jusqu'à
`dispatchLine()`. B doit :
1. La découper en commande + paramètres (déjà fait par `parseLine()`)
2. Vérifier que le client a le droit d'exécuter cette commande *maintenant*
   (un client non enregistré ne peut pas faire `JOIN`)
3. Exécuter l'effet (changer un état, envoyer un message à d'autres clients)
4. Répondre — soit par une confirmation implicite (le message est relayé),
   soit par un **code numérique** en cas d'erreur ou d'info

B ne touche jamais `poll`/`recv`/`send`/fd directement — il lit des lignes
déjà extraites et remplit le write buffer via `client.appendToWrite(...)`.

## 2. La grammaire d'un message IRC

Un message, une fois le `\r\n` retiré, a cette forme (RFC 2812 §2.3) :

```
[':' préfixe SPACE] commande [SPACE param]* [SPACE ':' trailing]
```

Exemples réels :
```
PASS secret
NICK bob
USER bob 0 * :Bob Smith
JOIN #general
PRIVMSG #general :hello everyone
PRIVMSG bob :salut, ça va ?
:bob!~bob@localhost PRIVMSG #general :hello everyone
```

- **Préfixe** (`:bob!~bob@localhost`) : optionnel, indique l'émetteur. **Un
  client ne l'envoie jamais** — c'est le serveur qui le rajoute quand il
  relaie un message à d'autres clients, pour que le destinataire sache qui
  parle. C'est pour ça que `parseLine()` dans le code actuel ne le gère pas
  côté réception : il n'y en a pas à parser depuis un client.
- **Commande** : un mot (`JOIN`, `PRIVMSG`...) ou un code numérique à 3
  chiffres (utilisé uniquement par le serveur en réponse, jamais envoyé par
  un client).
- **Paramètres** : séparés par des espaces. Le dernier peut commencer par
  `:` — dans ce cas il contient tout le reste de la ligne, espaces compris.
  C'est la seule façon d'envoyer un message contenant des espaces
  (`PRIVMSG #general :hello everyone` → deux params : `#general` et
  `hello everyone`, pas quatre).

C'est exactement ce que fait `parseLine()` (`src/Server.cpp:212`) : couper au
premier espace pour la commande, puis boucler sur le reste en s'arrêtant dès
qu'un token commence par `:`.

**Limite RFC** : une ligne complète ne dépasse pas 512 octets, `\r\n` inclus.
Un serveur strict rejette/tronque au-delà — lié au TODO de A sur le
plafonnement de `_readBuf`.

## 3. Les réponses numériques (numeric replies)

Le serveur ne répond jamais en texte libre à une commande — toujours un code
à 3 chiffres, formaté comme un message dont le préfixe est le nom du serveur
et le premier paramètre le nick du client visé :

```
:irc.server.local 001 bob :Welcome to the Internet Relay Network bob
:irc.server.local 433 * bob :Nickname is already in use
```

Deux familles :
- **1xx-3xx** : succès / info (`RPL_*`)
- **4xx-5xx** : erreur (`ERR_*`)

Celles qui comptent vraiment pour ce projet (RFC 2812 §5) :

| Code | Nom | Quand |
|---|---|---|
| 001 | RPL_WELCOME | juste après un enregistrement réussi (PASS+NICK+USER) |
| 331/332 | RPL_TOPIC / RPL_NOTOPIC | réponse à `TOPIC` sans argument |
| 353/366 | RPL_NAMREPLY / RPL_ENDOFNAMES | liste des membres après `JOIN` |
| 401 | ERR_NOSUCHNICK | `PRIVMSG` vers un nick inconnu |
| 403 | ERR_NOSUCHCHANNEL | commande sur un salon inexistant |
| 411/412 | ERR_NORECIPIENT / ERR_NOTEXTTOSEND | `PRIVMSG` sans destinataire/texte |
| 421 | ERR_UNKNOWNCOMMAND | commande non reconnue |
| 431/432 | ERR_NONICKNAMEGIVEN / ERR_ERRONEUSNICKNAME | `NICK` invalide |
| 433 | ERR_NICKNAMEINUSE | nick déjà pris |
| 441 | ERR_USERNOTINCHANNEL | `KICK` sur quelqu'un pas dans le salon |
| 442 | ERR_NOTONCHANNEL | commande de salon alors qu'on n'y est pas |
| 443 | ERR_USERONCHANNEL | `INVITE` de quelqu'un déjà présent |
| 461 | ERR_NEEDMOREPARAMS | trop peu de paramètres (déjà en TODO dans `handleJoin`) |
| 462 | ERR_ALREADYREGISTRED | `PASS`/`USER` renvoyé après enregistrement |
| 464 | ERR_PASSWDMISMATCH | mauvais mot de passe |
| 471-475 | divers | `JOIN` refusé (salon plein, `+i`, `+k`, banni...) |
| 481 | ERR_NOPRIVILEGES | commande d'op tentée par un non-op |

La liste complète est dans la RFC ; celle-ci couvre ce que le scope du sujet
utilise réellement.

## 4. La machine à états d'un client

C'est le vrai cœur de la partie B : **la même commande n'a pas le même effet
selon l'état du client**. Trois états s'accumulent, dans n'importe quel
ordre, avant l'enregistrement complet :

```
                    ┌─────────────┐
   connecté ───────►│ non enregistré │
                    └──────┬──────┘
                           │ PASS ok, NICK ok, USER ok (les 3, ordre libre)
                           ▼
                    ┌─────────────┐
                    │  enregistré  │──── peut faire JOIN/PRIVMSG/...
                    └──────┬──────┘
                           │ QUIT / recv()==0 / erreur
                           ▼
                      déconnecté
```

Concrètement, `Client` (`inc/Client.hpp`) a déjà les trois bascules :
`_passValidated`, `_nick`/`_user` non vides, `_registered`. La règle à coder
dans `dispatchLine()` :

- Avant `_registered == true` : seules `PASS`, `NICK`, `USER`, `QUIT`,
  `CAP` (si un client moderne la tente) sont acceptées. Toute autre commande
  → `451 ERR_NOTREGISTERED`.
- `_registered` devient `true` seulement quand les trois briques (mot de
  passe validé, nick posé, user posé) sont réunies — c'est à ce moment-là,
  et une seule fois, qu'on envoie `001 RPL_WELCOME` (et généralement 002-004,
  simplifiables pour ce projet).
- Après `_registered == true`, retenter `PASS`/`USER` → `462
  ERR_ALREADYREGISTRED`. `NICK`, lui, reste utilisable à tout moment (changer
  de pseudo en cours de session est légal).

C'est un piège classique : oublier de vérifier l'état avant d'exécuter une
commande permet à un client de faire `JOIN` sans jamais s'être identifié.

## 5. Le cas `NICK` : pourquoi c'est plus qu'un `setNick()`

`NICK` illustre bien la différence entre "parser une commande" et "respecter
le protocole" :
1. Paramètre manquant → `431 ERR_NONICKNAMEGIVEN`
2. Caractères invalides (RFC : lettre/chiffre/`[]{}\|^_-`, pas de commencer
   par un chiffre) → `432 ERR_ERRONEUSNICKNAME`
3. Déjà pris par un autre client connecté → `433 ERR_NICKNAMEINUSE`
4. Sinon : mise à jour, et si le client était déjà enregistré et dans des
   salons, **il faut relayer le changement** à tous ses interlocuteurs
   (`:ancien_nick!user@host NICK :nouveau_nick`) — sinon leurs clients IRC
   affichent encore l'ancien pseudo.

Le point (4) est facile à oublier en testant seul (un seul client ne voit pas
le problème) mais saute immédiatement aux yeux avec deux clients connectés en
même temps — bon réflexe de test pour la soutenance.

## 6. `PRIVMSG`/`NOTICE` : à qui, et comment retrouver le destinataire

```
PRIVMSG <cible> :<texte>
```
`<cible>` est soit un nick (message privé), soit un nom de salon commençant
par `#` (message de salon, à relayer à tous les membres **sauf**
l'émetteur). Deux recherches différentes :
- nick → parcourir `_clients` par nick (implique de maintenir un index
  nick→Client, ou de le parcourir linéairement — acceptable vu l'échelle du
  projet)
- `#salon` → `_channels`, déjà utilisé par `getOrCreateChannel()`

Différence `PRIVMSG`/`NOTICE` selon la RFC : `NOTICE` ne doit **jamais**
provoquer de réponse automatique (pas de numeric reply en cas d'erreur), pour
éviter les boucles infinies entre deux bots qui se répondraient l'un à
l'autre indéfiniment.

## 7. `PING`/`PONG` : pourquoi c'est obligatoire, pas juste "nice to have"

Un vrai client IRC (irssi, HexChat...) envoie périodiquement `PING
:quelquechose` et attend `PONG :quelquechose` en retour ; s'il ne le reçoit
pas dans un délai, il considère la connexion morte et la ferme lui-même. Sans
réponse à `PING`, un client réel finit par se déconnecter tout seul même si
le serveur va bien — un bug qui n'apparaît qu'avec un vrai client, jamais
avec `nc`. Implémentation : trivial, renvoyer `PONG :<même paramètre>`.

## 8. Comment ça s'articule avec le code existant

`dispatchLine()` (`src/Server.cpp:243`) route déjà `JOIN` vers un vrai
handler et laisse un `TODO (B)` pour le reste. Le pattern à suivre est celui
de `handleJoin()` :
1. Vérifier les params (→ 461 si incomplet)
2. Vérifier l'état/permissions (→ code d'erreur adapté sinon)
3. Muter l'état (`Client`/`Channel`)
4. Émettre la réponse : soit un relais (`channel->broadcast(...)`), soit un
   numeric reply direct sur `client.appendToWrite(...)`

Écrire les numeric replies proprement mérite un petit helper (ex.
`Server::reply(Client &, int code, const std::string &msg)` qui préfixe avec
le nom du serveur et le nick du client) plutôt que de construire la chaîne à
la main à chaque endroit — évite les fautes de format répétées partout.

## 9. Questions probables en soutenance

**"Pourquoi un client ne peut pas envoyer de préfixe `:nick!user@host` dans
ses propres commandes ?"**
→ Le préfixe identifie l'émetteur pour le *destinataire* d'un message relayé.
Un client parle en son nom propre par construction (le serveur sait déjà qui
il est via le fd) ; c'est le serveur qui ajoute le préfixe en relayant à
d'autres. L'accepter depuis un client ouvrirait une usurpation d'identité
triviale.

**"Que se passe-t-il si un client fait `PRIVMSG` avant `USER`/`NICK` ?"**
→ Doit être refusé avec `451 ERR_NOTREGISTERED` — la machine à états de la
section 4 doit être vérifiée avant *toute* commande hors PASS/NICK/USER/QUIT.

**"Pourquoi distinguer `PRIVMSG` et `NOTICE` si le comportement est presque
identique ?"**
→ `NOTICE` garantit l'absence de réponse automatique, ce qui évite les
boucles infinies entre bots/scripts. C'est une convention du protocole, pas
un détail cosmétique.

**"Comment le client sait-il que l'enregistrement a réussi ?"**
→ Il n'y a pas de commande dédiée : c'est la réception de `001 RPL_WELCOME`
qui signale au client "tu es maintenant enregistré, tu peux utiliser toutes
les commandes."

## 10. Ce qu'il reste à faire pour B

- `PASS`/`NICK`/`USER` avec la machine à états complète (section 4) et les
  numeric replies associés
- `PRIVMSG`/`NOTICE` avec résolution nick ou `#salon`
- `PING`/`PONG` (section 7)
- `QUIT` avec message de départ relayé aux salons communs
- Un helper centralisé pour formater les numeric replies (section 8), pour
  ne pas dupliquer `":" + serverName + " " + code + " " + nick + " ..."`
  partout dans le code
