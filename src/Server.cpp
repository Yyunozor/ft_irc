#include "Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstring>
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
		dispatchLine(client, line);
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

void	Server::dispatchLine(Client &client, const std::string &line)
{
	std::string					command;
	std::vector<std::string>	params;

	parseLine(line, command, params);

	if (command == "JOIN")
		handleJoin(client, params);
	else if (command == "PART" || command == "KICK" || command == "INVITE"
		|| command == "TOPIC" || command == "MODE")
	{
		// TODO (C): implement, following handleJoin() as a model — look up
		// the channel via getOrCreateChannel()/_channels, mutate it, then
		// channel->broadcast(...) the result.
	}
	else
	{
		// TODO (B): PASS/NICK/USER/PRIVMSG/NOTICE/PING/QUIT + numeric
		// replies. Echo kept only so the poll loop stays testable meanwhile.
		client.appendToWrite(line + "\r\n");
	}
}

void	Server::handleJoin(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
		return ; // TODO (B): numeric reply 461 ERR_NEEDMOREPARAMS

	Channel *channel = getOrCreateChannel(params[0]);
	channel->addMember(&client);

	std::string nick = client.getNick().empty() ? "*" : client.getNick();
	channel->broadcast(":" + nick + " JOIN " + params[0] + "\r\n");
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
