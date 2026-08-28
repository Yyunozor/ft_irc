# TODO

État au 2026-08-28 : build propre (`-Wall -Wextra -Werror -std=c++98
-pedantic-errors`, zéro warning), aucune erreur ASan/UBSan sous les scénarios
testés, serveur fonctionnel de bout en bout, toutes les erreurs protocole au
format numérique RFC. Voir [`docs/audit.md`](docs/audit.md) pour l'historique
des corrections et [`docs/avancement.md`](docs/avancement.md) pour l'état
complet par partie.

**Il ne reste que deux points, tous les deux hors du code :**

## Avant la soutenance

- **Logins 42 dans le `README.md`** encore en placeholder
  (`<login1>, <login2>, <login3>`).
- **Tester avec un vrai client IRC (irssi/weechat).** Jamais fait faute
  d'installation sur les machines de l'équipe — point explicite de la
  grille d'évaluation. Testé côté serveur avec `nc`/scripts Python
  (enregistrement, JOIN, toutes les erreurs numériques, buffer overflow,
  déconnexions brutales, ligne vide, délimiteur `\n` seul), et vérifié sous
  ASan/UBSan, mais rien ne remplace un vrai client.

## Corrigé le 2026-08-28

- ✅ Validation de grammaire du pseudo (`isValidNick()`, `432
  ERR_ERRONEUSNICKNAME`)
- ✅ Validation du nom de channel (`#`/`&` exigé, `403` sinon)
- ✅ Toutes les erreurs texte libre (`PASS`, `NICK`, `USER`, `PART`, `KICK`,
  `INVITE`, `TOPIC`, `JOIN`, `MODE`) converties au format numérique RFC
- ✅ Promotion d'opérateur : le membre présent le plus longtemps devient op
  si le dernier opérateur part/est kické/se déconnecte
  (`Channel::removeMember`, suivi via `_joinOrder`)
- ✅ Plafond de 512 octets sur `_readBuf` sans terminateur → déconnexion
  (`Client::pendingLineTooLong()`)
- ✅ `KICK` accepte désormais la raison optionnelle (`:comment`), avec le
  pseudo du kickeur comme valeur par défaut
- ✅ `Channel::removeInviteOnly(Channel *)` (dead code, jamais définie ni
  appelée) supprimée
- ✅ Validation du port (1-65535) et du mot de passe (non vide) dans
  `main.cpp`, via `strtol` (`atoi` ne peut pas signaler une erreur de
  parsing)
- ✅ `Client::extractLine()` accepte un `\n` seul en plus de `\r\n` (le `\r`
  précédent, s'il existe, est retiré proprement — vérifié en bytes bruts,
  aucune contamination des messages sortants)
