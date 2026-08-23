#!/bin/bash

# ============================================
# Script de test pour ft_irc
# Usage : ./test_ircserv.sh <port> <password>
# ============================================

PORT=${1:-6667}
PASS=${2:-pass}
HOST=localhost

# Couleurs
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
RESET='\033[0m'

# Compteurs
TOTAL=0
PASSED=0
FAILED=0

# Fonction pour envoyer des commandes et capturer la réponse
send_cmds() {
    local cmds="$1"
    local timeout=${2:-1}
    echo -e "$cmds" | nc -C -w $timeout $HOST $PORT 2>/dev/null
}

# Fonction de test : envoie des commandes et vérifie qu'une string attendue est dans la réponse
test_response() {
    local name="$1"
    local cmds="$2"
    local expected="$3"
    
    TOTAL=$((TOTAL + 1))
    local response=$(send_cmds "$cmds")
    
    if echo "$response" | grep -q "$expected"; then
        echo -e "${GREEN}✓${RESET} TEST $TOTAL : $name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${RESET} TEST $TOTAL : $name"
        echo -e "${YELLOW}  Expected:${RESET} $expected"
        echo -e "${YELLOW}  Got:${RESET}"
        echo "$response" | head -5 | sed 's/^/    /'
        FAILED=$((FAILED + 1))
    fi
}

# Test que le serveur ne crashe pas après une commande
test_no_crash() {
    local name="$1"
    local cmds="$2"
    
    TOTAL=$((TOTAL + 1))
    send_cmds "$cmds" 1 > /dev/null
    
    sleep 0.3
    if nc -z $HOST $PORT 2>/dev/null; then
        echo -e "${GREEN}✓${RESET} TEST $TOTAL : $name (serveur toujours en vie)"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${RESET} TEST $TOTAL : $name (SERVEUR CRASHÉ !)"
        FAILED=$((FAILED + 1))
    fi
}

echo -e "${CYAN}========================================${RESET}"
echo -e "${CYAN}  Tests automatiques pour ircserv${RESET}"
echo -e "${CYAN}  Host: $HOST | Port: $PORT${RESET}"
echo -e "${CYAN}========================================${RESET}"

# Vérifie que le serveur tourne
if ! nc -z $HOST $PORT 2>/dev/null; then
    echo -e "${RED}ERREUR : impossible de se connecter à $HOST:$PORT${RESET}"
    echo -e "${YELLOW}Lance le serveur d'abord : ./ircserv $PORT $PASS${RESET}"
    exit 1
fi

echo ""
echo -e "${CYAN}--- BASICS ---${RESET}"

test_response "Welcome 001" \
    "PASS $PASS\r\nNICK testuser1\r\nUSER testuser1 0 * :Test\r\n" \
    "001"

test_response "Mauvais password rejeté" \
    "PASS wrongpass\r\nNICK testuser2\r\nUSER testuser2 0 * :Test\r\n" \
    "464"

test_response "NICK manquant -> 431" \
    "PASS $PASS\r\nNICK\r\n" \
    "431"

echo ""
echo -e "${CYAN}--- JOIN / PRIVMSG ---${RESET}"

test_response "JOIN broadcast (353 NAMES)" \
    "PASS $PASS\r\nNICK joiner1\r\nUSER joiner1 0 * :J\r\nJOIN #test1\r\n" \
    "353"

test_response "JOIN sans param -> 461" \
    "PASS $PASS\r\nNICK joiner2\r\nUSER joiner2 0 * :J\r\nJOIN\r\n" \
    "461"

echo ""
echo -e "${CYAN}--- FRAGMENTATION (sujet) ---${RESET}"

# Le test critique du sujet : envoyer en plusieurs morceaux
test_fragmentation() {
    TOTAL=$((TOTAL + 1))
    local response=$(
        (
            printf "PASS "
            sleep 0.1
            printf "$PASS\r\n"
            sleep 0.1
            printf "NICK fr"
            sleep 0.1
            printf "ag\r\n"
            sleep 0.1
            printf "USER frag 0 * :Frag\r\n"
            sleep 0.3
        ) | nc -C -w 2 $HOST $PORT 2>/dev/null
    )
    if echo "$response" | grep -q "001"; then
        echo -e "${GREEN}✓${RESET} TEST $TOTAL : Fragmentation TCP (test du sujet)"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${RESET} TEST $TOTAL : Fragmentation TCP"
        FAILED=$((FAILED + 1))
    fi
}
test_fragmentation

echo ""
echo -e "${CYAN}--- ROBUSTESSE (no crash) ---${RESET}"

test_no_crash "PRIVMSG vide" \
    "PASS $PASS\r\nNICK r1\r\nUSER r1 0 * :R\r\nPRIVMSG\r\n"

test_no_crash "PRIVMSG sans message" \
    "PASS $PASS\r\nNICK r2\r\nUSER r2 0 * :R\r\nPRIVMSG alice\r\n"

test_no_crash "PRIVMSG channel inexistant" \
    "PASS $PASS\r\nNICK r3\r\nUSER r3 0 * :R\r\nPRIVMSG #nopasla :hello\r\n"

test_no_crash "KICK sans params" \
    "PASS $PASS\r\nNICK r4\r\nUSER r4 0 * :R\r\nKICK\r\n"

test_no_crash "MODE sans params" \
    "PASS $PASS\r\nNICK r5\r\nUSER r5 0 * :R\r\nMODE\r\n"

test_no_crash "MODE channel inexistant" \
    "PASS $PASS\r\nNICK r6\r\nUSER r6 0 * :R\r\nMODE #fakechan +t\r\n"

test_no_crash "MODE +k sans clé" \
    "PASS $PASS\r\nNICK r7\r\nUSER r7 0 * :R\r\nJOIN #m1\r\nMODE #m1 +k\r\n"

test_no_crash "JOIN sur channel +k sans clé" \
    "PASS $PASS\r\nNICK r8\r\nUSER r8 0 * :R\r\nJOIN #m2\r\nMODE #m2 +k secret\r\nQUIT\r\n"

test_no_crash "MODE +l arg invalide" \
    "PASS $PASS\r\nNICK r9\r\nUSER r9 0 * :R\r\nJOIN #m3\r\nMODE #m3 +l abc\r\n"

test_no_crash "Commande inconnue" \
    "PASS $PASS\r\nNICK r10\r\nUSER r10 0 * :R\r\nFOOBAR blabla\r\n"

test_no_crash "TOPIC sans params" \
    "PASS $PASS\r\nNICK r11\r\nUSER r11 0 * :R\r\nTOPIC\r\n"

test_no_crash "INVITE sans params" \
    "PASS $PASS\r\nNICK r12\r\nUSER r12 0 * :R\r\nINVITE\r\n"

echo ""
echo -e "${CYAN}--- MODES ---${RESET}"

# Pour les modes, on a besoin de simuler 2 clients. Plus complexe.
# On utilise un truc : envoyer plusieurs commandes du même client.

test_response "MODE +t broadcasté" \
    "PASS $PASS\r\nNICK opuser1\r\nUSER opuser1 0 * :O\r\nJOIN #modtest1\r\nMODE #modtest1 +t\r\n" \
    "MODE #modtest1 +t"

test_response "MODE +i broadcasté" \
    "PASS $PASS\r\nNICK opuser2\r\nUSER opuser2 0 * :O\r\nJOIN #modtest2\r\nMODE #modtest2 +i\r\n" \
    "MODE #modtest2 +i"

test_response "MODE +k broadcasté" \
    "PASS $PASS\r\nNICK opuser3\r\nUSER opuser3 0 * :O\r\nJOIN #modtest3\r\nMODE #modtest3 +k secret\r\n" \
    "MODE #modtest3 +k"

test_response "MODE +l broadcasté" \
    "PASS $PASS\r\nNICK opuser4\r\nUSER opuser4 0 * :O\r\nJOIN #modtest4\r\nMODE #modtest4 +l 5\r\n" \
    "MODE #modtest4 +l"

echo ""
echo -e "${CYAN}--- DÉCONNEXION ---${RESET}"

test_no_crash "QUIT explicite" \
    "PASS $PASS\r\nNICK q1\r\nUSER q1 0 * :Q\r\nJOIN #qtest\r\nQUIT :bye\r\n"

test_no_crash "Déconnexion brutale (Ctrl+C simulé)" \
    "PASS $PASS\r\nNICK q2\r\nUSER q2 0 * :Q\r\nJOIN #qtest\r\n"

echo ""
echo -e "${CYAN}--- CONNEXIONS MULTIPLES ---${RESET}"

# 10 connexions parallèles
TOTAL=$((TOTAL + 1))
for i in {1..10}; do
    (echo -e "PASS $PASS\r\nNICK multi$i\r\nUSER multi$i 0 * :M\r\nJOIN #multi\r\n"; sleep 1) | nc -C -w 2 $HOST $PORT > /dev/null 2>&1 &
done
wait

sleep 0.5
if nc -z $HOST $PORT 2>/dev/null; then
    echo -e "${GREEN}✓${RESET} TEST $TOTAL : 10 connexions parallèles (serveur toujours en vie)"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${RESET} TEST $TOTAL : 10 connexions parallèles (serveur crashé)"
    FAILED=$((FAILED + 1))
fi

echo ""
echo -e "${CYAN}========================================${RESET}"
echo -e "${CYAN}  RÉSULTATS${RESET}"
echo -e "${CYAN}========================================${RESET}"
echo -e "Total  : $TOTAL"
echo -e "${GREEN}Passed : $PASSED${RESET}"
echo -e "${RED}Failed : $FAILED${RESET}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}🎉 Tous les tests passent !${RESET}"
    exit 0
else
    echo -e "${RED}⚠️  $FAILED tests ont échoué${RESET}"
    exit 1
fi