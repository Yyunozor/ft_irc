# ft_irc — présentation du projet

## En une phrase

On construit un serveur IRC (Internet Relay Chat) en C++98, capable
d'accueillir un vrai client IRC existant (irssi, HexChat...) qui s'y
connecte, s'authentifie, rejoint des channels et discute — exactement
comme il le ferait avec un vrai serveur IRC public.

## C'est quoi IRC ?

IRC est un protocole de chat textuel créé en 1988, un des tout premiers
systèmes de "chat" au sens moderne (ancêtre de MSN, Slack, Discord).
Toujours utilisé aujourd'hui par des communautés open-source. Le
principe : un serveur central relie plusieurs clients, qui échangent des
messages privés (`PRIVMSG`) ou des messages de groupe via des **channels**
(salons, préfixés `#`).

Chaque client a un pseudo (`NICK`) et doit s'enregistrer avant de faire
quoi que ce soit. Dans un channel, certains membres sont **opérateurs**
et peuvent modérer (expulser, inviter, changer les règles du salon).

## Ce qu'on construit exactement

**Le serveur, pas le client.** On ne fait pas l'interface que
l'utilisateur voit — on fait le programme auquel un vrai client IRC se
connecte. C'est pour ça que le respect strict du protocole (RFC 1459 /
RFC 2812) compte : si le serveur ne répond pas comme un serveur IRC est
censé répondre, un client réel ne comprend pas et se comporte mal.

## Contraintes imposées par le sujet

- **C++98** strict, compilation avec `-Wall -Wextra -Werror`
- **Un seul process, un seul thread** — pas de fork, pas de threads
- **Toutes les I/O passent par un unique `poll()`** (ou équivalent
  `select`/`epoll`/`kqueue`, mais un seul appel pour tout le programme)
- **Aucun descripteur bloquant** — un client lent ou hostile ne doit
  jamais bloquer les autres
- **Jamais lire `errno` après `recv`/`send`** pour décider quoi faire —
  seule la valeur de retour compte
- **Ne jamais crasher**, quoi qu'envoie un client (c'est le premier
  critère éliminatoire en soutenance)
- Les données réseau arrivent en morceaux imprévisibles : il faut
  bufferiser et ne traiter une commande qu'une fois complètement reçue
  (terminée par `\r\n`)

## Fonctionnalités attendues

- Connexion protégée par mot de passe (`PASS`)
- Enregistrement (`NICK`, `USER`)
- Messages privés et de channel (`PRIVMSG`, `NOTICE`)
- Gestion de channels : `JOIN`, `PART`, `TOPIC`, `INVITE`, `KICK`
- Modes d'opérateur de channel :
  - `i` — sur invitation seulement
  - `t` — sujet modifiable par les opérateurs seulement
  - `k` — protégé par mot de passe
  - `o` — privilège d'opérateur
  - `l` — nombre maximum de membres
- Maintien de connexion (`PING`/`PONG`) et déconnexion propre (`QUIT`)

## Architecture générale (sans rentrer dans le code)

Trois classes principales :

- **`Server`** — possède la boucle réseau, la liste des clients connectés
  et la liste des channels existants. C'est le chef d'orchestre : il
  reçoit les données brutes, les transforme en commandes, et route
  chaque commande vers le bon comportement.
- **`Client`** — représente une connexion : son identité (pseudo,
  username), son état (enregistré ou non), et ses buffers de données en
  attente d'envoi/réception.
- **`Channel`** — représente un salon : ses membres, ses opérateurs, son
  sujet, ses règles (modes actifs).

## Répartition de l'équipe en 3 parties

- **A — Réseau** : la "tuyauterie" bas niveau — accepter des connexions,
  lire/écrire sans bloquer, gérer plusieurs clients à la fois
- **B — Protocole** : comprendre ce qu'un client demande (parsing des
  commandes), gérer l'enregistrement, répondre selon les règles du
  protocole IRC
- **C — Channels & opérateurs** : la logique de groupe — qui est dans
  quel salon, qui a le droit de faire quoi

Ces trois parties s'empilent : A permet à B d'exister (transporte les
octets), B permet à C d'exister (comprend et route les commandes de
channel).

## Modalités d'évaluation

Projet 42 : soutenance orale en binôme/trinôme avec un pair-évaluateur.
Points clés généralement vérifiés :
- Le serveur ne doit **jamais crasher**, même sous comportement hostile
  (flood, déconnexion brutale, commandes malformées)
- Compatibilité avec un vrai client IRC
- Compréhension du code — chaque membre doit pouvoir justifier ses choix,
  y compris ceux faits par les coéquipiers
- Respect strict des contraintes du sujet (C++98, un seul `poll()`, etc.)

## Ressources de référence

- RFC 1459 / RFC 2812 — spécification du protocole IRC
- modern.ircdocs.horse — documentation IRC plus lisible que les RFC
- Beej's Guide to Network Programming — bases des sockets
- `man 2 poll`, `man 2 recv`, `man 2 send`, `man 2 fcntl`

---

Pour un guide détaillé sur un point précis (le fonctionnement de
`poll()`, le format des messages IRC, etc.), demande-le séparément.
