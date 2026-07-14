*This project has been created as part of the 42 curriculum by <login1>, <login2>, <login3>.*

# ft_irc

IRC server in C++98 — single non-blocking `poll()` loop, no fork, no threads.

## Build

```sh
make
./ircserv <port> <password>
```

## Tasks

**A — Network core**
- [ ] socket / bind / listen + non-blocking (`fcntl`, `O_NONBLOCK`)
- [ ] single `poll()` loop: accept, read, write, disconnect
- [ ] per-client read buffer (split on `\r\n`) + write buffer (partial send)

**B — Protocol & messaging**
- [ ] command parser + dispatcher
- [ ] `PASS` / `NICK` / `USER` + numeric replies
- [ ] `PRIVMSG` / `NOTICE`, `PING` / `PONG`, `QUIT`

**C — Channels & operators**
- [ ] `Channel`, `JOIN` / `PART`
- [ ] `KICK` / `INVITE` / `TOPIC`
- [ ] `MODE` `i` `t` `k` `o` `l`

**Rule:** only A touches `poll`/`recv`/`send`/fds. Never crash, never read `errno` after recv/send, compile `-Wall -Wextra -Werror -std=c++98`.
