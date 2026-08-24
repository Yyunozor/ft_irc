# TODO

État après intégration du travail d'Ilias (partie C — channels & opérateurs) et
audit par l'agent évaluateur : le build est propre (`-Wall -Wextra -Werror`,
zéro warning), mais le serveur **crashe** sur une séquence banale (voir
"Bug transversal" ci-dessous). À corriger avant toute soutenance.

## Bug transversal — priorité absolue (crash confirmé)

- **Use-after-free confirmé (AddressSanitizer + test live).**
  `Server::removeClient()` (`src/Server.cpp:182`) supprime le `Client*` et
  l'enlève de `_clients`/`_pollFds`, mais ne le retire d'aucun `Channel` où
  il était membre. Séquence testée : deux clients rejoignent le même
  channel, l'un ferme sa connexion TCP sans `QUIT`/`PART`, l'autre envoie un
  `PRIVMSG` sur ce channel → `Channel::broadcast` (`src/Channel.cpp`) itère
  sur un pointeur déjà libéré → crash. Arrive sur **toute déconnexion qui
  n'est pas un `PART` propre**, donc quasi systématiquement en usage réel.
  Correction : avant le `delete` dans `removeClient`, parcourir `_channels`
  et appeler `channel->removeMember(client)` (`Channel::removeMember`,
  `src/Channel.cpp:58`) sur chacun — ni purement partie A (réseau) ni
  purement partie C (channels), à se répartir en équipe.

## Partie C — Channels & opérateurs (Ilias)

- **Pas de promotion d'opérateur.** Si le seul opérateur d'un channel fait
  `PART`, le channel se retrouve sans aucun opérateur ; plus personne ne
  peut alors `MODE`/`KICK`/`INVITE` dedans (`Channel::removeMember`,
  `src/Channel.cpp:58`). Décider d'une règle (ex : le membre restant le
  plus ancien devient op) et l'implémenter.
- **Pas de validation du nom de channel.** N'importe quelle chaîne est
  acceptée comme nom de channel dans `Server::handleJoin`
  (`src/Server.cpp:290`) ; devrait exiger un préfixe `#` (voire `&`).
- **`KICK` sans raison optionnelle.** `Server::handleKick` ignore un
  éventuel dernier paramètre `:raison`, pourtant permis par le protocole.
- **`TOPIC` sur un channel sans topic** renvoie une chaîne vide au lieu
  d'un message explicite ("pas de topic défini") — cosmétique.
- **Pas de `RPL_NAMREPLY`/`RPL_TOPIC` après un `JOIN` réussi.** Un vrai
  client (irssi, weechat) attend la liste des membres et le topic courant
  juste après avoir rejoint ; `handleJoin` ne broadcast que le
  `:nick JOIN #chan`, donc le client ne saura pas qui est dans le salon
  tant que quelqu'un ne parle pas.

## Partie B — Protocole

- **`Server::error()` ne répond jamais au client** (`src/Server.cpp:641`) —
  ne fait que `std::cerr <<`, pas de `Client&`, rien n'est écrit sur le
  socket. Testé en live : mauvais `PASS`, pseudo dupliqué, `PART` sur un
  channel jamais rejoint → le client ne reçoit rien et reste bloqué en
  silence. Seul `handleJoin` répond correctement (il n'utilise pas
  `error()`). À corriger : faire prendre un `Client&` à `error()` (ou
  passer par `client.appendToWrite(...)` comme le fait déjà `handleJoin`)
  et envoyer une vraie réponse numérique IRC.
- **Aucun contrôle d'enregistrement.** `Server::dispatchLine`
  (`src/Server.cpp:253`) route vers tous les handlers sans jamais vérifier
  `client.isRegistered()` (sauf dans `handleMode`). Testé : un client peut
  envoyer `JOIN #chan` en toute première ligne, sans `PASS`/`NICK`/`USER`.
  Ajouter un contrôle (451 ERR_NOTREGISTERED) en tête de `dispatchLine`,
  en exemptant `PASS`/`NICK`/`USER`/`PING`/`QUIT`.
- **`QUIT` et `NOTICE` non implémentés.** `dispatchLine` retombe sur
  l'écho brut de la ligne pour toute commande inconnue. Testé : `QUIT :bye`
  est renvoyé tel quel et **le serveur ne ferme pas le socket** côté
  serveur — le client doit fermer lui-même. `QUIT` est explicitement dans
  le scope du README ; à implémenter (fermeture propre + notification aux
  channels communs, qui recoupe le bug transversal ci-dessus).
- **`handleUSER` capture probablement le mauvais champ.**
  (`src/Server.cpp:672`) Avec un vrai `USER user hostname servername
  :realname` (4 champs), `parseLine` produit 4 params ; `handleUSER` ne
  vérifie que `params.size() >= 3` et lit `params[2]` (le `servername`
  littéral) au lieu de `params[3]` (le vrai realname) pour
  `client.setUser(params[0], params[2])`. Le realname est donc perdu et
  `"*"` stocké à la place. À vérifier/corriger avec un vrai client IRC.
- **Pas de validation port/mot de passe** dans `main.cpp`. `atoi()` sur un
  port invalide (`"abc"`, `0`, `99999`, `-1`) ne plante pas mais fait
  silencieusement n'importe quoi (port OS assigné ou tronqué par
  `htons()`). À rejeter proprement au démarrage.
- Réponses d'erreur envoyées comme chaînes ad-hoc (`"ERROR 461: ..."`)
  plutôt qu'au format numérique RFC (`:servername 461 nick :message\r\n`),
  et `RPL_WELCOME` (001) jamais envoyé après un enregistrement réussi
  (juste un texte libre "Welcome to..."). Un client strict peut ne pas
  s'en accommoder — à tester avec irssi/weechat avant la soutenance.

## Parsing des lignes (Client::extractLine)

- **Délimiteur laxiste.** La RFC 1459/2812 impose `\r\n`, mais beaucoup de
  clients réels — et surtout `nc`/netcat en test — envoient juste `\n`.
  `Client::extractLine()` (`src/Client.cpp:77`) cherche actuellement
  `"\r\n"` strict : un `\n` seul ne termine jamais une ligne, elle reste
  bloquée dans `_readBuf`. À gérer : accepter les deux (chercher `\n`,
  retirer un `\r` précédent s'il est présent).
- **Plafond de 512 octets.** RFC : une ligne complète ne dépasse pas 512
  octets (`\r\n` inclus). Actuellement `_readBuf` peut grossir sans limite
  si un client n'envoie jamais de fin de ligne (déjà noté dans
  `docs/partie-A-reseau.md` §6). Décider d'une politique — tronquer ou
  déconnecter le client — et l'implémenter dans `extractLine()`.
