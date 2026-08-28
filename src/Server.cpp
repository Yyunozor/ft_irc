/* ************************************************************************** */
/*                                                                            */
/*   Server.cpp - coeur reseau                                              */
/*                                                                            */
/*   Auteur : Luca (boucle poll, sockets, buffers)                          */
/*   Contenu : socket d'ecoute, boucle poll() unique, acceptation, lecture,    */
/*             ecriture bufferisee, cycle de vie des clients, et les          */
/*             recherches partagees dont B et C ont besoin.                   */
/*                                                                            */
/*   Ce fichier fait partie de src/Server.cpp, decoupe en trois unites pour   */
/*   que chacun travaille dans la sienne : a 901 lignes, un seul fichier      */
/*   partage a trois produisait un conflit a chaque fusion.                   */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <string>
#include <map>
#include <vector>

/*
** The evaluation checks for memory leaks, and an evaluator stops the server
** with Ctrl+C. SIGINT's default action kills the process outright, so ~Server()
** would never run and every Client and Channel on the heap would be reported
** as lost. Catching it lets the loop exit and the destructors do their work.
**
** Only a flag is set: a handler must stay async-signal-safe, and poll() then
** returns -1 with the loop condition already false.
*/
static volatile sig_atomic_t	g_running = 1;

static void	stopOnSignal(int)
{
	g_running = 0;
}

Server::Server(int port, const std::string &password)
	: _listenFd(-1), _port(port), _password(password)
{
}

Server::~Server()
{
	for (std::map<int, Client *>::iterator it = _clients.begin();
		it != _clients.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
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
	signal(SIGINT, stopOnSignal);
	signal(SIGTERM, stopOnSignal);

	setupListenSocket();
	std::cout << "ircserv listening on port " << _port << std::endl;

	while (g_running)
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

			// A client that sent QUIT is kept one more iteration so POLLOUT
			// can flush its farewell, then dropped once drained. Sending it
			// directly from removeClient() would be I/O outside poll(), which
			// chapter IV.1 punishes with a grade of 0.
			std::map<int, Client *>::iterator cit = _clients.find(fd);
			if (cit != _clients.end() && cit->second->isQuitting()
				&& !cit->second->hasPendingWrite())
				toRemove.push_back(fd);
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
	// A client flooding bytes with no "\r\n" would otherwise grow _readBuf
	// without bound; dropping it here is the only place both A owns the
	// buffer and can act on poll()-driven I/O.
	if (client.pendingLineTooLong())
		return (false);
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
		// Channels hold raw Client*: unless the client is evicted from every
		// one of them first, the next broadcast() dereferences freed memory.
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
		// A quitting client is no longer read from: only its pending farewell
		// still has to go out.
		_pollFds[i].events = client.isQuitting() ? 0 : POLLIN;
		if (client.hasPendingWrite())
			_pollFds[i].events |= POLLOUT;
	}
}

Channel	*Server::getOrCreateChannel(Client *client, const std::string &name)
{
	std::map<std::string, Channel *>::iterator it = _channels.find(name);

	if (it != _channels.end())
		return (it->second);

	Channel *channel = new Channel(name);
	_channels[name] = channel;
	channel->addOperator(client);

	return (channel);
}

/*
** Losing its last member destroys the channel here too, exactly as handlePart()
** does. Otherwise a channel's fate would depend on HOW the last member left --
** PART would delete it, a dropped socket would not -- and the survivor is a
** ghost: getOrCreateChannel() hands an existing channel back without granting
** operator status, so the next arrival inherits a room they cannot administrate,
** still carrying its +i/+k/+l. A +i ghost is unjoinable forever.
**
** C++98 note: std::map::erase(iterator) returns void, so `it = erase(it)` is
** unavailable; `erase(it++)` is the portable idiom, the post-increment being
** sequenced before the erase invalidates the iterator.
*/
void	Server::removeFromAllChannels(Client *client)
{
	std::map<std::string, Channel *>::iterator it = _channels.begin();

	while (it != _channels.end())
	{
		Channel	*channel = it->second;

		channel->removeMember(client);
		// The invite list holds raw Client* too. A client invited to a +i
		// channel who disconnects WITHOUT ever joining leaves its address
		// behind, and new Client(fd) frequently reuses the block malloc just
		// freed -- the next arrival then inherits the invitation and walks
		// into a +i channel uninvited. Measured: 11 bypasses out of 12.
		channel->removeInvite(client);
		if (channel->getMembers().empty())
		{
			_channels.erase(it++);
			delete channel;
		}
		else
			++it;
	}
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

