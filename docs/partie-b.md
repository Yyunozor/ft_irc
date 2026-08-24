# Partie B — Protocole & parsing

Fiche de révision pour la soutenance. Tout ce qui suit renvoie à du code réel
du dépôt, avec les numéros de ligne.

---

## 1. Le rôle de B en une phrase

**A transporte des octets, B leur donne un sens, C applique la logique de groupe.**

B reçoit une ligne de texte complète (A a déjà fait le découpage sur `\r\n`),
la transforme en `commande + paramètres`, vérifie que le client a le droit de
la lancer, et produit une réponse conforme au protocole IRC.

---

## 2. Le format d'un message IRC

C'est la base de tout le parsing. Un message IRC tient sur une ligne terminée
par `\r\n`, et suit cette grammaire :

```
[:préfixe] COMMANDE [param1] [param2] ... [:paramètre final avec des espaces]
```

Trois règles à connaître par cœur :

1. **Le préfixe** (`:nick!user@host`) indique *qui parle*. Un **client n'en
   envoie jamais** — c'est le serveur qui en ajoute un quand il relaie un
   message. C'est pourquoi notre parser ne le gère pas en lecture.
2. **Les paramètres sont séparés par des espaces**, et il y en a au maximum 15.
3. **Le dernier paramètre peut commencer par `:`** — dans ce cas il vaut *tout
   le reste de la ligne, espaces inclus*. C'est le « trailing parameter », et
   c'est la seule façon d'envoyer un message contenant des espaces.

Exemple concret :

```
PRIVMSG #dev :salut tout le monde
└──┬──┘ └─┬─┘ └────────┬─────────┘
commande  param0    param1 (trailing, espaces conservés)
```

Sans la règle du `:`, `salut tout le monde` deviendrait quatre paramètres
distincts.

---

## 3. Le parser — `parseLine()`, [src/Server.cpp:234](../src/Server.cpp)

Algorithme, à savoir redérouler au tableau :

```
1. couper à la première espace  -> commande = avant, rest = après
2. tant que rest n'est pas vide :
     si rest commence par ':'  -> pousser tout le reste (sans le ':'), STOP
     sinon couper à la prochaine espace -> pousser le morceau, continuer
```

**Question piège :** pourquoi la boucle s'arrête net sur le `:` ?
→ Parce que le trailing est *par définition* le dernier paramètre. Continuer à
découper détruirait les espaces qu'on cherche justement à préserver.

### `toUpper()` — [src/Server.cpp:267](../src/Server.cpp)

Les noms de commandes IRC sont **insensibles à la casse** : `nick`, `Nick` et
`NICK` sont la même commande. On normalise en majuscules avant de router.

Détail à justifier : `std::toupper` prend un `int` et exige une valeur
représentable en `unsigned char`. Passer un `char` signé négatif (accent, octet
UTF-8) est un comportement indéfini — d'où le double cast
`static_cast<char>(std::toupper(static_cast<unsigned char>(c)))`.

---

## 4. La machine à états de l'enregistrement

C'est le cœur de B, et la question la plus probable en soutenance.

```
        connexion
            │
            ▼
   ┌──────────────────┐
   │  NON ENREGISTRÉ  │   commandes autorisées :
   │                  │   PASS, NICK, USER, QUIT, PING, CAP
   │  _passValidated  │   tout le reste -> 451 ERR_NOTREGISTERED
   │  _nick           │
   │  _userReceived   │
   └────────┬─────────┘
            │  les 3 sont satisfaits
            ▼
   completeRegistration()      [src/Server.cpp:460]
            │
            ├── PASS invalide -> 464 + déconnexion
            │
            ▼
   ┌──────────────────┐
   │    ENREGISTRÉ    │   envoi de 001, 002, 003, 004
   │                  │   toutes les commandes deviennent accessibles
   └──────────────────┘
```

### Les trois conditions

| Champ | Posé par | Fichier |
|---|---|---|
| `_passValidated` | `handlePass()` | [src/Server.cpp:337](../src/Server.cpp) |
| `_nick` | `handleNick()` | [src/Server.cpp:387](../src/Server.cpp) |
| `_userReceived` | `handleUser()` | [src/Server.cpp:436](../src/Server.cpp) |

**Pourquoi `_userReceived` en plus de `_user` ?**
Parce qu'un client peut envoyer `USER "" 0 * :realname`. Tester `_user.empty()`
confondrait « USER jamais reçu » et « USER reçu avec un username vide ». Un
booléen distinct sépare les deux — c'est une question classique.

**Pourquoi `completeRegistration()` est appelée depuis NICK *et* USER ?**
Parce que l'ordre d'arrivée n'est pas garanti. Celle des deux qui arrive en
dernier déclenche l'accueil. irssi envoie les trois d'un coup, un autre client
peut les espacer.

### La validation du pseudo — `isValidNick()`, [src/Server.cpp:362](../src/Server.cpp)

Grammaire RFC 2812 :
- **1er caractère** : une lettre, ou un des spéciaux `` []\`_^{|} ``
- **suivants** : lettres, chiffres, spéciaux, ou `-`
- **longueur** : 9 caractères maximum

Donc `1bad` est rejeté (commence par un chiffre) → `432 ERR_ERRONEUSNICKNAME`.

### Le renommage après enregistrement

`handleNick()` a deux comportements selon `isRegistered()` :
- **avant** : on pose le pseudo, on tente `completeRegistration()`
- **après** : on **diffuse** `:ancien_prefixe NICK :nouveau` à tous les channels
  partagés, *puis* on change le pseudo

**L'ordre compte** : le préfixe doit contenir l'*ancien* pseudo, sinon les
autres clients ne peuvent pas faire le lien avec la personne qu'ils
connaissaient.

---

## 5. Les réponses numériques

### Le format

```
:<serveur> <code> <destinataire> <params> :<texte lisible>
```

Construit par `irc::numeric()` — [inc/Replies.hpp:42](../inc/Replies.hpp).

**Pourquoi le destinataire est répété dans chaque réponse ?**
Héritage du relais entre serveurs : un numérique pouvait transiter par
plusieurs serveurs, chacun devant savoir à qui le livrer. Quand le pseudo n'est
pas encore connu, on met `*`.

### Les messages relayés

```
:nick!user@host PRIVMSG #dev :salut
```

Construit par `irc::fromUser()` — [inc/Replies.hpp:54](../inc/Replies.hpp),
à partir de `Client::prefix()` — [src/Client.cpp:83](../src/Client.cpp).

### À retenir absolument

| Code | Nom | Quand |
|---|---|---|
| `001`-`004` | WELCOME / YOURHOST / CREATED / MYINFO | enregistrement réussi |
| `401` | ERR_NOSUCHNICK | destinataire inconnu |
| `403` | ERR_NOSUCHCHANNEL | channel inexistant |
| `404` | ERR_CANNOTSENDTOCHAN | on écrit dans un channel non rejoint |
| `421` | ERR_UNKNOWNCOMMAND | commande non reconnue |
| `431`/`432`/`433` | pas de pseudo / pseudo invalide / pseudo pris | NICK |
| `451` | ERR_NOTREGISTERED | commande avant enregistrement |
| `461` | ERR_NEEDMOREPARAMS | paramètres manquants |
| `462` | ERR_ALREADYREGISTRED | PASS ou USER après enregistrement |
| `464` | ERR_PASSWDMISMATCH | mauvais mot de passe |

Anecdote à ressortir : **462 s'écrit `ALREADYREGISTRED`**, sans le second « E ».
C'est une faute de frappe dans la RFC 2812 elle-même, conservée parce que les
implémentations s'étaient déjà alignées dessus.

---

## 6. PRIVMSG et NOTICE — [src/Server.cpp:490](../src/Server.cpp)

Une seule fonction, un booléen `isNotice` de différence.

```
target commence par '#' ou '&' ?
   ├── oui : findChannel()            (PAS getOrCreateChannel !)
   │         channel inexistant       -> 403
   │         pas membre               -> 404
   │         sinon broadcast(..., &client)   <- except = l'émetteur
   └── non : findClientByNick()
             inconnu                  -> 401
             sinon appendToWrite() sur le destinataire
```

**Trois points à savoir défendre :**

1. **`findChannel()` et non `getOrCreateChannel()`** — écrire dans un channel
   inexistant doit répondre 403, pas le créer. Deux fonctions distinctes
   ([src/Server.cpp:593](../src/Server.cpp) et [605](../src/Server.cpp))
   précisément pour rendre l'intention explicite à l'appel.

2. **`except = &client`** dans le broadcast — l'émetteur ne reçoit pas son
   propre message. Son client l'affiche déjà localement au moment de la frappe ;
   le renvoyer ferait un doublon à l'écran.

3. **NOTICE ne génère JAMAIS d'erreur** (RFC 2812 §3.3.2). C'est une règle
   anti-boucle : si un NOTICE en erreur produisait un NOTICE d'erreur, deux
   automates pourraient se renvoyer des erreurs à l'infini. D'où tous les
   `if (!isNotice)` avant chaque réponse d'erreur.

---

## 7. PING / PONG et QUIT

- **`handlePing()`** [src/Server.cpp:547](../src/Server.cpp) — renvoie le
  **même token**, pour que le client apparie requête et réponse.
- **`PONG` reçu** → ignoré. C'est le client qui répond à *notre* ping ; il n'y a
  rien à répondre à une réponse.
- **`handleQuit()`** [src/Server.cpp:554](../src/Server.cpp) → délègue à
  `disconnect()` [559](../src/Server.cpp) : diffuse le QUIT à tous les channels
  partagés, puis lève le drapeau `_quitting`.

### CAP — le détail qui débloque irssi

irssi ouvre la connexion avec `CAP LS 302` pour négocier des capacités. On n'en
supporte aucune. Répondre `421` fait attendre certains clients indéfiniment un
`CAP END` — donc on **ignore silencieusement**. Sans ça, irssi se fige à la
connexion.

---

## 8. Les trois pièges que j'ai dû corriger

Ce sont d'excellentes réponses à « qu'est-ce qui a été difficile ? ».

### a) Use-after-free — `removeClient()` [src/Server.cpp:181](../src/Server.cpp)

`Channel` stocke des `Client *` bruts. Supprimer un client sans le retirer des
channels laissait un pointeur pendouillant : le prochain `broadcast()` lisait de
la mémoire libérée. → `removeFromAllChannels()` avant le `delete`.

> **Crash = note 0** selon le chapitre II du sujet. C'est LE scénario qu'un
> correcteur déclenche en fermant un client brutalement.

### b) La réponse perdue avant la déconnexion

Conflit d'exigences entre A et B :
- **A** veut la suppression différée : un client marqué « à supprimer » sort de
  la boucle sans passer par `POLLOUT`.
- **B** veut « répondre 464, *puis* déconnecter » — la réponse doit partir avant
  la fermeture.

Symptôme : le client recevait une connexion fermée sans explication.
Solution : un `send()` best-effort dans `removeClient()` juste avant `close()`.
Sa valeur de retour est délibérément ignorée — c'est du best-effort, et lire
`errno` est interdit par la règle d'équipe.

### c) Le préfixe tronqué sur JOIN

`handleJoin()` diffusait `:ben JOIN #dev` au lieu de
`:ben!ben@localhost JOIN #dev`. Un vrai client a besoin du `user@host` pour
peupler sa liste de membres et pour reconnaître que le JOIN est le sien.

---

## 9. Le piège TCP, transversal

**TCP est un flux d'octets, pas un flux de messages.** Deux conséquences
opposées, et il faut gérer les deux :

- une commande peut arriver **en plusieurs paquets** → il faut accumuler
- plusieurs commandes peuvent arriver **dans un seul `recv()`** → il faut
  extraire *toutes* les lignes complètes, dans une boucle

C'est exactement ce que fait `readFromClient()`
[src/Server.cpp:142](../src/Server.cpp) :

```cpp
while (client.extractLine(line))
    dispatchLine(client, line);
```

Un `if` au lieu du `while` et irssi reste bloqué à la connexion, parce qu'il
envoie `PASS`, `NICK` et `USER` dans un seul paquet.

---

## 10. Questions probables en soutenance

| Question | Réponse en une phrase |
|---|---|
| Pourquoi le parser s'arrête au `:` ? | C'est le trailing parameter : tout le reste, espaces compris, est un seul paramètre. |
| Pourquoi majusculer la commande ? | Les noms de commandes IRC sont insensibles à la casse. |
| Pourquoi `_userReceived` et pas `_user.empty()` ? | Pour distinguer « USER jamais reçu » de « USER reçu avec un username vide ». |
| Pourquoi `completeRegistration()` appelée à deux endroits ? | L'ordre d'arrivée de NICK et USER n'est pas garanti ; le dernier arrivé déclenche. |
| Pourquoi NOTICE ne répond jamais d'erreur ? | Règle anti-boucle de la RFC 2812 entre automates. |
| Pourquoi l'émetteur ne reçoit pas son PRIVMSG ? | Son client l'affiche déjà localement ; le renvoyer ferait un doublon. |
| Pourquoi `findChannel` et `getOrCreateChannel` séparés ? | PRIVMSG vers un channel inexistant doit répondre 403, pas le créer. |
| Pourquoi un `while` et pas un `if` sur `extractLine` ? | Plusieurs commandes peuvent arriver dans un seul `recv()`. |
| Pourquoi ignorer le retour du `send()` final ? | C'est du best-effort avant `close()`, et lire `errno` est interdit. |
| Que se passe-t-il si un client ferme pendant qu'on lui écrit ? | `SIGPIPE` tuerait le process — il est ignoré au démarrage (`signal(SIGPIPE, SIG_IGN)`). |

---

## 11. Modification en direct — s'y préparer

Le chapitre VII prévoit qu'on te demande une petite modification pendant
l'évaluation. Les plus probables sur B, et où les faire :

| Demande | Où |
|---|---|
| Ajouter une réponse numérique | une fonction `inline` dans [inc/Replies.hpp](../inc/Replies.hpp) |
| Changer la longueur max d'un pseudo | `isValidNick()`, [src/Server.cpp:362](../src/Server.cpp) |
| Ajouter une commande (ex. `AWAY`) | un `else if` dans `dispatchLine()`, [276](../src/Server.cpp) |
| Interdire une commande de plus avant enregistrement | la retirer de la liste en tête de `dispatchLine()` |
| Refuser les pseudos réservés | un test en tête de `handleNick()`, [387](../src/Server.cpp) |

Entraîne-toi à en faire une ou deux **sans relire la fiche** : c'est exactement
l'exercice de la soutenance.
