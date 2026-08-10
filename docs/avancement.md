# ft_irc — état d'avancement

Dernière mise à jour : après les commits `network: implement single
poll() event loop` et `network: add minimal command router and wire
JOIN end-to-end`.

## Vue d'ensemble

| Partie | État | Détail |
|---|---|---|
| Build system | ✅ Fait | Makefile complet, dépendances auto, build debug avec sanitizers |
| A — Réseau | ✅ Base fonctionnelle | Boucle `poll()` complète, testée |
| B — Protocole | 🟡 Amorcé | Routeur minimal en place, mais aucune vraie commande de B |
| C — Channels | 🟡 Amorcé | `JOIN` fonctionne de bout en bout, le reste manque |

## Détail par partie

### A — Réseau (fonctionnel)

Fait :
- Socket d'écoute : `socket`/`bind`/`listen`, non-bloquant
- Boucle `poll()` unique : accepte les connexions, lit, écrit, gère les
  déconnexions (suppression différée, jamais pendant l'itération)
- Buffers par client : découpage des lignes reçues sur `\r\n`,
  gestion de l'envoi partiel (`send()` qui n'envoie pas tout d'un coup)
- `POLLOUT` activé seulement quand un client a réellement des données
  en attente (évite de saturer le CPU)
- Testé avec plusieurs clients simultanés, déconnexions propres, paquets
  fragmentés

Reste à faire (robustesse, avant la soutenance) :
- Limiter la taille du buffer de lecture par client (protection contre
  un client qui envoie des données sans jamais de `\r\n` — risque
  d'épuisement mémoire)
- Test de charge avec un nombre plus élevé de connexions simultanées
- Décider si un arrêt propre sur `Ctrl+C` (`SIGINT`) est nécessaire

### B — Protocole (amorcé, presque tout reste à faire)

Fait :
- Un routeur minimal : chaque ligne reçue est découpée en commande +
  paramètres, puis redirigée vers un handler selon le nom de la commande
  (avec la règle IRC du paramètre "trailing" après `:`)

Reste à faire (l'essentiel de B) :
- `PASS` — vérification du mot de passe de connexion
- `NICK` / `USER` — enregistrement du client, avec la machine à états
  correcte (pas d'accès aux autres commandes tant que non enregistré)
- Les réponses numériques du protocole IRC (accueil, erreurs, pseudo
  déjà pris, paramètres manquants...)
- `PRIVMSG` / `NOTICE` — messages privés et vers un channel
- `PING` / `PONG` — maintien de connexion
- `QUIT` — déconnexion propre avec message

Tant que ça n'est pas fait, toute commande non reconnue (dont
`PASS`/`NICK`/`USER`/`PRIVMSG`) continue à être renvoyée en écho brut —
un client IRC réel (irssi...) ne fonctionnera pas correctement en
attendant.

### C — Channels & opérateurs (amorcé, `JOIN` sert de modèle)

Fait :
- `Channel` sait gérer des membres : ajouter, retirer, vérifier
  l'appartenance, diffuser un message à tous les membres
- `JOIN` est câblé de bout en bout : rejoindre un channel (qui est créé
  à la volée s'il n'existe pas encore) et être notifié, comme les autres
  membres déjà présents — testé avec plusieurs clients réels connectés
  en parallèle

Reste à faire :
- Sur `Channel` : gestion des opérateurs (ajouter/retirer/vérifier),
  une liste d'invités (nécessaire pour le mode `i`), les réglages de
  topic/clé/limite de membres, et une méthode pour savoir si un channel
  est vide (pour le supprimer proprement)
- Les commandes `PART`, `KICK`, `INVITE`, `TOPIC`, `MODE` — chacune peut
  suivre le même schéma que `JOIN` : retrouver le channel/client visé,
  vérifier les droits si nécessaire, modifier l'état, diffuser le
  résultat
- Décision d'équipe à prendre : qui devient opérateur d'un channel à sa
  création (convention IRC habituelle : le premier membre à le rejoindre)

## Priorités suggérées, dans l'ordre

1. **A** — plafonner la taille du buffer de lecture (rapide, protège
   contre un vrai motif d'échec en soutenance)
2. **B** — `PASS`/`NICK`/`USER` en priorité : sans enregistrement
   fonctionnel, un vrai client IRC ne peut rien faire d'autre
3. **C** — continuer en parallèle sur `Channel` (opérateurs, invités,
   modes), puis brancher les commandes restantes une fois que
   l'enregistrement de B est stable
