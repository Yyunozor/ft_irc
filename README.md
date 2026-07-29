*This project has been created as part of the 42 curriculum by <login1>, <login2>, <login3>.*

# ft_irc

## Description

`ft_irc` is an IRC server written in C++98.

The goal is to implement a server that a real, unmodified IRC client can connect
to and use: authenticating with a password, registering a nickname and a
username, joining channels, exchanging private and channel messages, and running
channel operator commands.

The server handles every client in a **single process and a single thread**, with
no forking. All file descriptors are non-blocking and multiplexed through one
`poll()` call, so a slow or hostile client can never block the others. Data
arriving fragmented across several TCP packets is buffered per client and
reassembled into complete `\r\n`-terminated commands before anything is executed.

Scope:

- Password-protected connection (`PASS`), registration with `NICK` and `USER`
- Private and channel messaging (`PRIVMSG`, `NOTICE`)
- Channels: `JOIN`, `PART`, `TOPIC`, `INVITE`, `KICK`
- Channel operator modes: `i` (invite-only), `t` (topic restriction), `k` (key),
  `o` (operator privilege), `l` (user limit)
- Keep-alive (`PING` / `PONG`) and clean disconnection (`QUIT`)

## Instructions

### Requirements

- A C++98-capable compiler — `clang++` on macOS, `g++` on Linux
- GNU Make
- No external library is used, and no Boost library is used

### Build

```sh
make          # build ./ircserv
make clean    # remove object files
make fclean   # remove object files and the binary
make re       # fclean, then build
```

An optional debug build adds AddressSanitizer and UndefinedBehaviorSanitizer.
It is **not** the submitted build:

```sh
make DEBUG=1
```

### Run

```sh
./ircserv <port> <password>
```

- `port` — the TCP port the server listens on, for example `6667`
- `password` — the connection password every client must supply with `PASS`

### Connect

Using an IRC client:

```sh
irssi
/connect 127.0.0.1 6667 <password>
/nick <nickname>
/join #channel
```

To check that fragmented packets are correctly reassembled, as described in the
subject, send a single command in several pieces using ctrl+D between fragments:

```sh
nc -C 127.0.0.1 6667
```

## Technical choices

- **One `poll()` loop, no threads.** Every socket — the listening socket included
  — is registered in a single `poll()` call. `POLLOUT` is only requested for a
  client that actually has pending bytes, otherwise the loop would spin at 100%
  CPU.
- **Per-client read and write buffers.** `recv()` returns arbitrary chunks, not
  messages. Bytes are accumulated until a complete `\r\n` line is available, and
  outgoing data is queued rather than sent immediately, so a partial `send()`
  never loses data.
- **Deferred client deletion.** A client that disconnects or errors out is
  flagged, not destroyed, while the loop is still iterating. Actual destruction
  happens at the end of the iteration, once no iterator or `pollfd` still refers
  to it.
- **No `errno` inspection after `recv()` / `send()`.** As required by the
  subject, the return value alone decides what happens next.

## Work split

Provisional — see the team for the current assignment.

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

**Rule:** only A touches `poll` / `recv` / `send` / fds. Never crash, never read
`errno` after `recv` / `send`, always compile with
`-Wall -Wextra -Werror -std=c++98`.

## Resources

### IRC protocol

- RFC 1459 — Internet Relay Chat Protocol: https://datatracker.ietf.org/doc/html/rfc1459
- RFC 2812 — IRC Client Protocol, message grammar and numeric replies: https://datatracker.ietf.org/doc/html/rfc2812
- Modern IRC client protocol documentation: https://modern.ircdocs.horse/

### Network programming

- Beej's Guide to Network Programming: https://beej.us/guide/bgnet/
- `man 2 poll`, `man 2 recv`, `man 2 send`, `man 2 fcntl`, `man 7 socket`

### Use of AI

This section records how AI assistance was used, for which tasks and on which
parts of the project. It is updated as the project progresses.

| Task | Part of the project | What AI was used for |
|---|---|---|
| Build system | `Makefile` | Replacing manual dependency handling with compiler-generated depfiles (`-MMD -MP`), adding `uname`-based platform detection and an opt-in sanitizer build. Verified by us: editing a header recompiles exactly the objects that include it, and `make` with no change performs no relinking. |
| Project analysis | repository-wide | Cross-checking the subject requirements against the state of the repository, and identifying gaps in the initial class design. |
| Documentation | `README.md` | Drafting this file. |

Every AI-assisted contribution listed above has been reviewed, tested and is
understood by the authors, who take responsibility for it.
