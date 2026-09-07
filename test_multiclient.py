#!/usr/bin/env python3
# =============================================================================
#  Tests multi-clients pour ft_irc
#  Usage : ./test_multiclient.py [port] [password]
#
#  Le script test_ircserv.sh envoie tout depuis une seule connexion. Or le
#  coeur d'un serveur IRC c'est "A parle -> B recoit". Ici on ouvre plusieurs
#  vraies sockets en parallele et on verifie ce que chaque client recoit
#  (ou ne recoit pas).
# =============================================================================

import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PASS = sys.argv[2] if len(sys.argv) > 2 else "pass"

GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[0;33m"
CYAN = "\033[0;36m"
RESET = "\033[0m"

total = 0
passed = 0
failed = 0


class Client:
    """Une connexion IRC, avec un buffer cumulatif de tout ce qu'on a recu."""

    def __init__(self, name):
        self.name = name
        self.sock = socket.create_connection((HOST, PORT), timeout=2)
        self.sock.settimeout(0.3)
        self.buf = ""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def pump(self, seconds=0.4):
        """Lit tout ce qui arrive pendant `seconds` et l'ajoute au buffer."""
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buf += data.decode(errors="replace")
            except socket.timeout:
                pass
        return self.buf

    def register(self):
        self.send("PASS " + PASS)
        self.send("NICK " + self.name)
        self.send("USER {0} 0 * :{0} realname".format(self.name))
        self.pump(0.5)

    def clear(self):
        self.buf = ""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def check(name, condition):
    global total, passed, failed
    total += 1
    if condition:
        print("{0}[OK]{1} TEST {2} : {3}".format(GREEN, RESET, total, name))
        passed += 1
    else:
        print("{0}[KO]{1} TEST {2} : {3}".format(RED, RESET, total, name))
        failed += 1


def got(client, needle):
    return needle in client.pump()


print("{0}========================================{1}".format(CYAN, RESET))
print("{0}  Tests multi-clients ircserv{1}".format(CYAN, RESET))
print("{0}  {1}:{2}  password={3}{4}".format(CYAN, HOST, PORT, PASS, RESET))
print("{0}========================================{1}".format(CYAN, RESET))

try:
    probe = socket.create_connection((HOST, PORT), timeout=2)
    probe.close()
except OSError:
    print("{0}ERREUR : impossible de se connecter a {1}:{2}{3}".format(
        RED, HOST, PORT, RESET))
    print("{0}Lance le serveur d'abord : ./ircserv {1} {2}{3}".format(
        YELLOW, PORT, PASS, RESET))
    sys.exit(1)

# --- Enregistrement -----------------------------------------------------------
alice = Client("alice")
bob = Client("bob")
carol = Client("carol")

alice.register()
bob.register()
carol.register()

check("alice recoit le welcome 001", "001" in alice.buf)
check("bob recoit le welcome 001", "001" in bob.buf)

# --- JOIN : broadcast aux autres membres -------------------------------------
alice.clear(); bob.clear()
alice.send("JOIN #room")
alice.pump(0.3)
bob.send("JOIN #room")
# alice, deja dans #room, doit voir le JOIN de bob
check("alice voit le JOIN de bob", got(alice, "JOIN") and "bob" in alice.buf)
# bob recoit sa liste de noms (353) avec alice dedans
check("bob recoit 353 NAMES avec alice", "353" in bob.pump() and "alice" in bob.buf)

# --- PRIVMSG vers un channel ------------------------------------------------
alice.clear(); bob.clear()
alice.send("PRIVMSG #room :hello room")
check("bob recoit le PRIVMSG channel de alice", got(bob, "PRIVMSG #room :hello room"))
check("alice ne recoit PAS son propre PRIVMSG channel (pas d'echo)",
      "hello room" not in alice.pump())

# --- PRIVMSG vers un user --------------------------------------------------
alice.clear(); bob.clear()
bob.send("PRIVMSG alice :coucou toi")
check("alice recoit le PRIVMSG prive de bob",
      got(alice, "PRIVMSG alice :coucou toi") and "bob" in alice.buf)

# --- MODE : broadcast du changement --------------------------------------
alice.clear(); bob.clear()
alice.send("MODE #room +t")
check("bob voit le MODE +t diffuse", got(bob, "MODE") and "+t" in bob.buf)

# --- MODE +i puis INVITE ------------------------------------------------
alice.clear(); bob.clear(); carol.clear()
alice.send("MODE #room +i")
alice.pump(0.3)
alice.clear()
alice.send("INVITE carol #room")
check("alice recoit la confirmation 341 RPL_INVITING", got(alice, "341"))
check("carol recoit l'INVITE", got(carol, "INVITE") and "#room" in carol.buf)
check("bob ne recoit PAS l'INVITE (fuite)", "INVITE" not in bob.pump())

# --- +i : la cible invitee peut JOIN, une autre non --------------------
carol.clear()
carol.send("JOIN #room")
check("carol (invitee) reussit le JOIN sur #room +i", got(carol, "JOIN"))

dan = Client("dan")
dan.register()
dan.clear()
dan.send("JOIN #room")
check("dan (non invite) est rejete par 473 sur #room +i", got(dan, "473"))

# --- KICK --------------------------------------------------------------
alice.clear(); bob.clear()
alice.send("KICK #room bob")
check("bob recoit le KICK", got(bob, "KICK") and "bob" in bob.buf)
bob.clear()
bob.send("PRIVMSG #room :encore la ?")
check("bob ne peut plus ecrire dans #room apres le KICK (404)", got(bob, "404"))

# --- TOPIC ------------------------------------------------------------
alice.clear(); carol.clear()
alice.send("TOPIC #room :sujet du jour")
check("carol voit le TOPIC diffuse", got(carol, "TOPIC") and "sujet du jour" in carol.buf)

# --- QUIT -----------------------------------------------------------
carol.clear()
alice.send("QUIT :salut")
check("carol voit le QUIT de alice", got(carol, "QUIT") and "alice" in carol.buf)

# --- Promotion auto d'operateur -----------------------------------
op1 = Client("op1"); op2 = Client("op2")
op1.register(); op2.register()
op1.send("JOIN #promo"); op1.pump(0.3)
op2.send("JOIN #promo"); op2.pump(0.3)
op2.clear()
op1.send("PART #promo")          # op1 etait le seul operateur
check("op2 est promu operateur quand op1 (seul op) part",
      got(op2, "MODE") and "+o" in op2.buf and "op2" in op2.buf)

for c in (alice, bob, carol, dan, op1, op2):
    c.close()

print("{0}========================================{1}".format(CYAN, RESET))
print("Total  : {0}".format(total))
print("{0}Passed : {1}{2}".format(GREEN, passed, RESET))
print("{0}Failed : {1}{2}".format(RED, failed, RESET))
print()
sys.exit(0 if failed == 0 else 1)
