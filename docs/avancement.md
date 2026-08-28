# ft_irc — état d'avancement

Dernière mise à jour : 2026-08-28, après homogénéisation des réponses
numériques RFC, validation pseudo/channel, promotion d'opérateur et plafond
du buffer de lecture. Voir aussi [`docs/audit.md`](audit.md) pour le détail
des corrections de robustesse (crashs, fuites) et [`TODO.md`](../TODO.md)
pour la liste de ce qui reste.

## Vue d'ensemble

| Partie | État | Détail |
|---|---|---|
| Build system | ✅ Fait | `make re` silencieux, `-Wall -Wextra -Werror -std=c++98 -pedantic-errors`, `make DEBUG=1` avec ASan/UBSan |
| A — Réseau | ✅ Fonctionnel et robuste | Boucle `poll()` complète, arrêt propre sur SIGINT/SIGTERM, plafond de buffer, testée avec 8 clients + déconnexions brutales + flood |
| B — Protocole | ✅ Fonctionnel | Enregistrement (avec validation de pseudo), PRIVMSG/NOTICE, PING/QUIT, toutes les erreurs au format numérique RFC |
| C — Channels | ✅ Fonctionnel | JOIN/PART/KICK/INVITE/TOPIC/MODE, séquence JOIN complète (topic + NAMES), validation du nom de channel, promotion d'opérateur automatique |

**Verdict global : le serveur est utilisable de bout en bout avec un vrai
client IRC** (connexion, enregistrement, JOIN, chat, déconnexion), ne
plante pas sous les scénarios testés (déconnexions brutales, flood, buffer
surdimensionné, commandes malformées), compile proprement, et répond
uniquement au format numérique RFC — plus aucune erreur en texte libre.
Ce qui reste dans `TODO.md` est mineur (raison optionnelle de `KICK`,
validation du port/mot de passe, test avec un vrai client irssi/weechat).

## Détail par partie

### A — Réseau (fonctionnel et robuste)

Fait :
- Socket d'écoute : `socket`/`bind`/`listen`, non-bloquant
- Boucle `poll()` unique : accepte les connexions, lit, écrit, gère les
  déconnexions (suppression différée, jamais pendant l'itération)
- Buffers par client : découpage des lignes reçues sur `\r\n`,
  gestion de l'envoi partiel (`send()` qui n'envoie pas tout d'un coup)
- `POLLOUT` activé seulement quand un client a réellement des données
  en attente (évite de saturer le CPU)
- Arrêt propre sur `SIGINT`/`SIGTERM` (flag `g_running`), destructeurs
  exécutés, fd fermés — plus de fuite mémoire/descripteurs au `Ctrl+C` de
  l'évaluateur
- **Plafond de 512 octets sur `_readBuf`** (`Client::pendingLineTooLong()`) :
  un client qui n'envoie jamais `\r\n` est déconnecté proprement au lieu de
  faire grossir son buffer indéfiniment. Vérifié : 600 octets sans
  terminateur → connexion coupée, serveur toujours réactif pour les autres.
- Testé : 8 clients simultanés, déconnexions brutales, paquets fragmentés,
  client figé (`^Z`) + 3000 messages de flood absorbés sans fuite ni blocage

- `extractLine()` accepte maintenant un `\n` seul en plus de `\r\n` (le `\r`
  précédent est retiré proprement) — confort pour les tests manuels
  (`nc`/`printf`), sans rien perdre côté clients qui envoient du `\r\n`
  conforme

Reste à faire : rien côté A.

### B — Protocole (fonctionnel)

Fait :
- Parser dédié (`src/Parser.cpp`) : casse insensible, espaces multiples,
  paramètre trailing après `:`, limite de 15 paramètres
- Enregistrement complet : `PASS`/`NICK`/`USER` dans n'importe quel ordre,
  `001`-`004` envoyés dès que les trois sont validés
- **Validation de grammaire du pseudo** (`isValidNick()`) : 1er caractère
  lettre ou spécial RFC, 9 caractères max, sinon `432
  ERR_ERRONEUSNICKNAME`
- Garde d'enregistrement : toute commande hors `PASS/NICK/USER/QUIT/PING/CAP`
  répond `451 ERR_NOTREGISTERED` avant enregistrement
- `PRIVMSG`/`NOTICE` (vers un client ou un channel, `NOTICE` sans jamais
  produire d'erreur — RFC 2812 §3.3.2), `PING`/`PONG`, `QUIT` (diffusé aux
  channels partagés, déconnexion différée via `poll()`)
- `CAP` ignoré silencieusement (sinon irssi reste bloqué à la connexion)
- **Toutes les erreurs au format numérique RFC** — `PASS` (`461`/`464`),
  `NICK` (`431`/`432`/`433`), `USER` (`462`) : plus aucun texte libre
- Testé : mauvais mot de passe (`464`), pseudo invalide (`432`), pseudo pris
  (`433`), enregistrement complet avec un client-like irssi (`USER` à
  4 champs, `CAP LS`)

- Port (1-65535) et mot de passe (non vide) validés dans `main.cpp` via
  `strtol` (`atoi` ne pouvait pas distinguer une erreur de parsing d'un
  port `0` légitime)

Reste à faire : rien côté B.

### C — Channels & opérateurs (fonctionnel)

Fait :
- `Channel` gère membres, opérateurs, invités, clé, limite, sujet,
  diffusion (avec exclusion optionnelle de l'émetteur)
- `JOIN` complet : **validation du nom de channel** (`#`/`&` exigé, sinon
  `403`), création à la volée, refus propre sur `+i`/`+k`/`+l`, invitation
  à usage unique consommée à l'entrée, diffusion `JOIN` avec préfixe
  complet, puis `331`/`332` (topic) et `353`/`366` (liste des membres,
  opérateurs préfixés `@`) — un vrai client peuple correctement sa fenêtre
  de salon
- `PART`, `KICK`, `INVITE` (avec `341` à l'émetteur, notifie exactement
  l'invité, pas tout le salon — RFC 2812 §3.2.7), `TOPIC` (respecte `+t`)
- Modes `i`, `t`, `k`, `o`, `l` tous fonctionnels et diffusés aux membres
- Channel détruit quand il devient vide, y compris sur déconnexion brutale
  (pas seulement `PART`) — évite les salons fantômes qui gardent leurs modes
- **Promotion d'opérateur automatique** : si le dernier opérateur part,
  est kické, ou se déconnecte, le membre présent depuis le plus longtemps
  (suivi via `Channel::_joinOrder`) devient opérateur, et le changement est
  annoncé (`:ircserv MODE #chan +o nick`) — un salon n'est jamais orphelin
- **Toutes les erreurs au format numérique RFC** — `PART`/`KICK`/`INVITE`/
  `TOPIC`/`JOIN`/`MODE` : plus aucun texte libre

- `KICK` accepte désormais la raison optionnelle (`:comment`), avec le
  pseudo du kickeur comme valeur par défaut
- `removeInviteOnly(Channel *)` (dead code, jamais définie ni appelée)
  supprimée

Reste à faire : rien côté C.

## Ce qui reste, tout court

Plus rien dans le code. Il ne reste que deux points, tous les deux en
dehors du code :

1. **Tester avec un vrai client IRC installé (irssi/weechat)** — jamais
   fait faute d'installation sur les machines de l'équipe, c'est un point
   explicite de la grille d'évaluation. Le serveur a été testé de façon
   poussée avec `nc` et des scripts Python (tous les scénarios listés dans
   `docs/audit.md` et ci-dessus), mais rien ne remplace un vrai client.
2. **Logins 42 dans le `README.md`**, encore en placeholder.
