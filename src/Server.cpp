#include "Server.hpp"
#include "Replies.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstring>
#include <cctype>
#include <stdexcept>
#include <iostream>

Server::Server(int port, const std::string &password)
	: _listenFd(-1), _port(port), _password(password)
{
}

Server::~Server()
{
	for (std::map<int, Client *>::iterator it = _clients.begin();
		it != _clients.end(); ++it)
		delete it->second;
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
		delete it->second;
	if (_listenFd != -1)
		close(_listenFd);
}

int	Server::getPort() const
{
	return (_port);
}

const std::string	&Server::getPassword() const
{
	return (_password);
}

void	Server::setupListenSocket()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd < 0)
		throw std::runtime_error("socket() failed");

	int opt = 1;
	setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(_port);

	if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
		throw std::runtime_error("bind() failed");
	if (listen(_listenFd, SOMAXCONN) < 0)
		throw std::runtime_error("listen() failed");
	if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl() failed");

	struct pollfd listenPfd;
	listenPfd.fd = _listenFd;
	listenPfd.events = POLLIN;
	listenPfd.revents = 0;
	_pollFds.push_back(listenPfd);
}

void	Server::start()
{
	// send() on a socket the peer already closed would otherwise raise
	// SIGPIPE and kill the whole server; the return value alone is enough.
	signal(SIGPIPE, SIG_IGN);

	setupListenSocket();
	std::cout << "ircserv listening on port " << _port << std::endl;

	while (true)
	{
		int ready = poll(&_pollFds[0], _pollFds.size(), -1);
		if (ready < 0)
			continue; // interrupted by a signal: retry, don't inspect errno

		if (_pollFds[0].revents & POLLIN)
			acceptClient();

		std::vector<int> toRemove;
		for (std::size_t i = 1; i < _pollFds.size(); ++i)
		{
			int		fd = _pollFds[i].fd;
			short	revents = _pollFds[i].revents;

			if (revents & POLLNVAL)
			{
				toRemove.push_back(fd);
				continue;
			}
			// POLLHUP/POLLERR are folded into the same recv() attempt as
			// POLLIN: recv()'s return value decides whether to drop the
			// client, so a peer that sends a final line and immediately
			// closes still gets that line read (and its reply flushed
			// below) instead of being dropped unread.
			if ((revents & (POLLIN | POLLHUP | POLLERR)) && !readFromClient(fd))
			{
				toRemove.push_back(fd);
				continue;
			}
			if (revents & POLLOUT)
				writeToClient(fd);
		}

		for (std::size_t i = 0; i < toRemove.size(); ++i)
			removeClient(toRemove[i]);

		refreshPollEvents();
	}
}

void	Server::acceptClient()
{
	int fd = accept(_listenFd, NULL, NULL);
	if (fd < 0)
		return; // no pending connection after all, or a transient failure

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	{
		close(fd);
		return;
	}

	_clients[fd] = new Client(fd);

	struct pollfd clientPfd;
	clientPfd.fd = fd;
	clientPfd.events = POLLIN;
	clientPfd.revents = 0;
	_pollFds.push_back(clientPfd);

	std::cout << "client fd " << fd << " connected" << std::endl;
}

bool	Server::readFromClient(int fd)
{
	char	buf[4096];
	ssize_t	n = recv(fd, buf, sizeof(buf), 0);

	if (n <= 0)
		return (false); // 0 = orderly shutdown, <0 = error: either way, drop it

	Client &client = *_clients[fd];
	client.appendToRead(buf, static_cast<std::size_t>(n));

	std::string line;
	while (client.extractLine(line))
	{
		dispatchLine(client, line);
		// QUIT, or a rejected password: stop consuming this client's pending
		// lines and let the caller drop it. Anything still queued after a
		// QUIT is by definition sent by a client that no longer exists.
		if (client.isQuitting())
			return (false);
	}
	return (true);
}

void	Server::writeToClient(int fd)
{
	Client &client = *_clients[fd];
	const std::string &out = client.writeBuffer();

	if (out.empty())
		return;

	ssize_t n = send(fd, out.data(), out.size(), 0);
	if (n > 0)
		client.consumeWrite(static_cast<std::size_t>(n));
	// n <= 0 with POLLOUT already signalled: leave it for the next round
	// rather than guessing from errno.
}

void	Server::removeClient(int fd)
{
	std::map<int, Client *>::iterator it = _clients.find(fd);
	if (it != _clients.end())
	{
		// A client dropped for a protocol reason (rejected PASS, QUIT) still
		// has its last numeric sitting in the write buffer, and the poll()
		// loop skips POLLOUT for a client it is about to remove. One
		// best-effort send() before close() is what makes that reply
		// actually arrive; the return value is deliberately ignored, since
		// errno must not drive the logic here.
		const std::string &pending = it->second->writeBuffer();
		if (!pending.empty())
			(void)send(fd, pending.data(), pending.size(), 0);

		// Channels hold raw Client*: unless the client is evicted from every
		// one of them first, the next broadcast() walks freed memory.
		removeFromAllChannels(it->second);
		delete it->second;
		_clients.erase(it);
	}

	for (std::vector<struct pollfd>::iterator pit = _pollFds.begin();
		pit != _pollFds.end(); ++pit)
	{
		if (pit->fd == fd)
		{
			_pollFds.erase(pit);
			break;
		}
	}

	close(fd);
	std::cout << "client fd " << fd << " disconnected" << std::endl;
}

void	Server::refreshPollEvents()
{
	for (std::size_t i = 1; i < _pollFds.size(); ++i)
	{
		Client &client = *_clients[_pollFds[i].fd];
		_pollFds[i].events = POLLIN;
		if (client.hasPendingWrite())
			_pollFds[i].events |= POLLOUT;
	}
}

// Splits a raw line into an uppercase-insensitive command word and its
// params. A leading ":" on a param means "rest of the line, spaces
// included" (the IRC "trailing parameter" rule) — e.g. `PRIVMSG bob :hi
// there` yields params = {"bob", "hi there"}.
// TODO (B): this is deliberately minimal (no ":prefix" support, since
// clients never send one); promote/replace it as the real parser grows.
static void	parseLine(const std::string &line, std::string &command,
	std::vector<std::string> &params)
{
	std::string::size_type pos = line.find(' ');

	if (pos == std::string::npos)
	{
		command = line;
		return;
	}
	command = line.substr(0, pos);

	std::string rest = line.substr(pos + 1);
	while (!rest.empty())
	{
		if (rest[0] == ':')
		{
			params.push_back(rest.substr(1));
			break;
		}
		pos = rest.find(' ');
		if (pos == std::string::npos)
		{
			params.push_back(rest);
			break;
		}
		params.push_back(rest.substr(0, pos));
		rest = rest.substr(pos + 1);
	}
}

// IRC command names are case-insensitive ("nick" == "NICK"), so the router
// compares against a single canonical form.
static std::string	toUpper(const std::string &s)
{
	std::string	out = s;

	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
	return (out);
}

void	Server::dispatchLine(Client &client, const std::string &line)
{
	std::string					rawCommand;
	std::vector<std::string>	params;

	parseLine(line, rawCommand, params);
	if (rawCommand.empty())
		return;

	std::string	command = toUpper(rawCommand);

	// Commands accepted before registration completes.
	if (command == "PASS")
		return (handlePass(client, params));
	if (command == "NICK")
		return (handleNick(client, params));
	if (command == "USER")
		return (handleUser(client, params));
	if (command == "QUIT")
		return (handleQuit(client, params));
	if (command == "PING")
		return (handlePing(client, params));
	// A PONG is the client answering us; nothing to reply to it.
	if (command == "PONG")
		return;
	// irssi opens with "CAP LS 302" to negotiate capabilities. We support
	// none, and answering 421 makes some clients stall waiting for CAP END,
	// so it is silently ignored.
	if (command == "CAP")
		return;

	if (!client.isRegistered())
	{
		client.appendToWrite(irc::errNotRegistered(client.getNick()));
		return;
	}

	if (command == "PRIVMSG")
		handlePrivmsg(client, params, false);
	else if (command == "NOTICE")
		handlePrivmsg(client, params, true);
	else if (command == "JOIN")
		handleJoin(client, params);
	else if (command == "PART" || command == "KICK" || command == "INVITE"
		|| command == "TOPIC" || command == "MODE")
	{
		// TODO (C): implement, following handleJoin() as a model — look up
		// the channel via findChannel(), mutate it, then broadcast() the
		// result. The numerics you need (331/332/341/353/366/441/442/443/
		// 471/472/473/475/482) are ready to use in inc/Replies.hpp.
	}
	else
		client.appendToWrite(irc::errUnknownCommand(client.getNick(), rawCommand));
}

/*
** PASS <password>
** Must arrive before registration completes. Getting it wrong is fatal for
** the connection: the client is told 464 and dropped, rather than being left
** to guess forever.
*/
void	Server::handlePass(Client &client, const std::vector<std::string> &params)
{
	if (client.isRegistered())
	{
		client.appendToWrite(irc::errAlreadyRegistered(client.getNick()));
		return;
	}
	if (params.empty())
	{
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "PASS"));
		return;
	}
	if (params[0] != _password)
	{
		client.appendToWrite(irc::errPasswdMismatch(client.getNick()));
		disconnect(client, "Bad password");
		return;
	}
	client.setPassValidated(true);
}

/*
** RFC 2812 grammar: a nickname starts with a letter or one of []\`_^{|},
** continues with those plus digits and '-', and is at most 9 characters.
*/
static bool	isValidNick(const std::string &nick)
{
	static const std::string	special = "[]\\`_^{|}";

	if (nick.empty() || nick.size() > 9)
		return (false);
	if (!std::isalpha(static_cast<unsigned char>(nick[0]))
		&& special.find(nick[0]) == std::string::npos)
		return (false);
	for (std::string::size_type i = 1; i < nick.size(); ++i)
	{
		if (!std::isalnum(static_cast<unsigned char>(nick[i]))
			&& special.find(nick[i]) == std::string::npos
			&& nick[i] != '-')
			return (false);
	}
	return (true);
}

/*
** NICK <nickname>
** Also handles a rename after registration, which must be announced to every
** channel the client shares — otherwise other clients keep showing the old
** nick and later messages look like they come from a stranger.
*/
void	Server::handleNick(Client &client, const std::vector<std::string> &params)
{
	if (params.empty() || params[0].empty())
	{
		client.appendToWrite(irc::errNoNicknameGiven(client.getNick()));
		return;
	}

	const std::string	&wanted = params[0];

	if (!isValidNick(wanted))
	{
		client.appendToWrite(irc::errErroneusNickname(client.getNick(), wanted));
		return;
	}

	Client	*holder = findClientByNick(wanted);
	if (holder != NULL && holder != &client)
	{
		client.appendToWrite(irc::errNicknameInUse(client.getNick(), wanted));
		return;
	}
	if (holder == &client)
		return; // same nick, nothing to announce

	if (client.isRegistered())
	{
		std::string	announce = irc::fromUser(client.prefix(), "NICK :" + wanted);

		client.appendToWrite(announce);
		for (std::map<std::string, Channel *>::iterator it = _channels.begin();
			it != _channels.end(); ++it)
		{
			if (it->second->isMember(&client))
				it->second->broadcast(announce, &client);
		}
		client.setNick(wanted);
		return;
	}

	client.setNick(wanted);
	completeRegistration(client);
}

/*
** USER <username> <mode> <unused> :<realname>
** The middle two parameters are vestigial in RFC 2812; only the first and
** the trailing realname carry information for us.
*/
void	Server::handleUser(Client &client, const std::vector<std::string> &params)
{
	if (client.isRegistered())
	{
		client.appendToWrite(irc::errAlreadyRegistered(client.getNick()));
		return;
	}
	if (params.size() < 4)
	{
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "USER"));
		return;
	}

	client.setUser(params[0]);
	client.setRealname(params[3]);
	client.setUserReceived(true);
	completeRegistration(client);
}

/*
** Registration needs all three of PASS, NICK and USER. Whichever arrives
** last triggers the welcome burst, so the order the client picks does not
** matter — irssi sends all three in one packet.
*/
void	Server::completeRegistration(Client &client)
{
	if (client.isRegistered())
		return;
	if (client.getNick().empty() || !client.hasUserInfo())
		return;

	// NICK/USER complete but no valid PASS: refuse rather than let the client
	// think it is connected to an open server.
	if (!client.isPassValidated())
	{
		client.appendToWrite(irc::errPasswdMismatch(client.getNick()));
		disconnect(client, "Bad password");
		return;
	}

	client.setRegistered(true);

	const std::string	&nick = client.getNick();
	client.appendToWrite(irc::welcome(nick, client.prefix()));
	client.appendToWrite(irc::yourHost(nick));
	client.appendToWrite(irc::created(nick));
	client.appendToWrite(irc::myInfo(nick));
}

/*
** PRIVMSG/NOTICE <target> :<text>
** `isNotice` suppresses every error reply: RFC 2812 forbids answering a
** NOTICE with an error, so that automated senders cannot ping-pong failures.
*/
void	Server::handlePrivmsg(Client &client, const std::vector<std::string> &params,
	bool isNotice)
{
	const std::string	verb = isNotice ? "NOTICE" : "PRIVMSG";

	if (params.empty())
	{
		if (!isNotice)
			client.appendToWrite(irc::errNoRecipient(client.getNick(), verb));
		return;
	}
	if (params.size() < 2 || params[1].empty())
	{
		if (!isNotice)
			client.appendToWrite(irc::errNoTextToSend(client.getNick()));
		return;
	}

	const std::string	&target = params[0];
	std::string			body = verb + " " + target + " :" + params[1];

	if (target[0] == '#' || target[0] == '&')
	{
		Channel	*channel = findChannel(target);

		if (channel == NULL)
		{
			if (!isNotice)
				client.appendToWrite(irc::errNoSuchChannel(client.getNick(), target));
			return;
		}
		// Refusing non-members keeps a client from shouting into a room it
		// never joined, which is what 404 exists for.
		if (!channel->isMember(&client))
		{
			if (!isNotice)
				client.appendToWrite(irc::errCannotSendToChan(client.getNick(), target));
			return;
		}
		channel->broadcast(irc::fromUser(client.prefix(), body), &client);
		return;
	}

	Client	*receiver = findClientByNick(target);
	if (receiver == NULL)
	{
		if (!isNotice)
			client.appendToWrite(irc::errNoSuchNick(client.getNick(), target));
		return;
	}
	receiver->appendToWrite(irc::fromUser(client.prefix(), body));
}

/*
** PING <token>
** Answered with the same token so the client can match request to reply.
*/
void	Server::handlePing(Client &client, const std::vector<std::string> &params)
{
	std::string	token = params.empty() ? std::string(SERVER_NAME) : params[0];

	client.appendToWrite(":" SERVER_NAME " PONG " SERVER_NAME " :" + token + "\r\n");
}

void	Server::handleQuit(Client &client, const std::vector<std::string> &params)
{
	disconnect(client, params.empty() ? "Client quit" : params[0]);
}

void	Server::disconnect(Client &client, const std::string &reason)
{
	if (client.isQuitting())
		return;

	std::string	announce = irc::fromUser(client.prefix(), "QUIT :" + reason);

	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
	{
		if (it->second->isMember(&client))
			it->second->broadcast(announce, &client);
	}
	client.setQuitting(true);
}

void	Server::handleJoin(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "JOIN"));
		return;
	}

	Channel *channel = getOrCreateChannel(params[0]);
	channel->addMember(&client);

	// Full "nick!user@host" prefix, not just the nick: a real client matches
	// the JOIN against its own prefix to know the join is its own, and needs
	// user@host to populate the member list. Broadcast to everyone including
	// the joiner — the joiner's own JOIN is its confirmation.
	channel->broadcast(irc::fromUser(client.prefix(), "JOIN " + params[0]));
}

Channel	*Server::getOrCreateChannel(const std::string &name)
{
	std::map<std::string, Channel *>::iterator it = _channels.find(name);

	if (it != _channels.end())
		return (it->second);

	Channel *channel = new Channel(name);
	_channels[name] = channel;
	return (channel);
}

Channel	*Server::findChannel(const std::string &name)
{
	std::map<std::string, Channel *>::iterator it = _channels.find(name);

	if (it == _channels.end())
		return (NULL);
	return (it->second);
}

Client	*Server::findClientByNick(const std::string &nick)
{
	for (std::map<int, Client *>::iterator it = _clients.begin();
		it != _clients.end(); ++it)
	{
		if (it->second->getNick() == nick)
			return (it->second);
	}
	return (NULL);
}

/*
** Empty channels are deliberately left in place here: deciding when a
** channel dies (and who inherits operator status) is C's call, and dropping
** them from under C's feet would change JOIN semantics mid-flight.
*/
void	Server::removeFromAllChannels(Client *client)
{
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
		it->second->removeMember(client);
}
