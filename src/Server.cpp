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
	else if (command == "PART")
		handlePart(client, params);
	else if (command == "INVITE")
		handleInvite(client, params);
	else if (command == "KICK")
		handleKick(client, params);
	else if (command == "TOPIC")
		handleTopic(client, params);
	else if (command == "MODE")
		handleMode(client, params);
	else if (command == "PASS")
		handlePASS(client, params);
	else if (command == "NICK")
		handleNICK(client, params);
	else if (command == "USER")
		handleUSER(client, params);
	else if (command == "PING")
		handlePING(client, params);
	else if (command == "PRIVMSG")
		handlePrivmsg(client, params);
	/*else if (command == "PASS" || command == "NICK" || command == "USER"
		|| command == "PRIVMSG" || command == "NOTICE" || command == "PING"
		|| command == "QUIT")
	
	|| command == "KICK" || command == "INVITE"
		|| command == "TOPIC" || command == "MODE")
	{
		// TODO (C): implement, following handleJoin() as a model — look up
		// the channel via getOrCreateChannel()/_channels, mutate it, then
		// channel->broadcast(...) the result.
	}*/
	else
	{
		// TODO (B): PASS/NICK/USER/PRIVMSG/NOTICE/PING/QUIT + numeric
		// replies. Echo kept only so the poll loop stays testable meanwhile.
		client.appendToWrite(line + "\r\n");
	}
}

void Server::handleJoin(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite("ERROR 461: JOIN command requires 1 parameter: <channel>\r\n");
        return;
    }

    Channel *channel = getOrCreateChannel(&client, params[0]);

    if (channel->isInviteOnly() && !channel->isInvited(&client))
    {
        client.appendToWrite("ERROR 473: Cannot join channel " + params[0] + ", invite only\r\n");
        return;
    }

	if (params[1] != channel->getKey())
	{
		client.appendToWrite("ERROR 475: Cannot join channel " + params[0] + ", incorrect key\r\n");
		return;
	}

    channel->addMember(&client);

    std::string nick = client.getNick().empty() ? "*" : client.getNick();
    channel->broadcast(":" + nick + " JOIN " + params[0] + "\r\n");
}

void    error(const std::string &msg)
{
    std::cerr << "Error: " << msg << std::endl;
    std::exit(EXIT_FAILURE);
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

void Server::handleInvite(Client &client, const std::vector<std::string> &params)
{
    if (params.size() < 2)
    {
        error("ERROR 461: INVITE command requires 2 parameters: <nick> <channel>");
        return;
    }

    const std::string &targetNick = params[0];
    const std::string &channelName = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        error("ERROR 403: No such channel");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        error("ERROR 482: You're not a channel operator");
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target)
    {
        error("ERROR 401: No such nick/channel");
        return;
    }

    if (channel->isMember(target))
    {
        error("ERROR 443: User is already in the channel");
        return;
    }

    if (channel->isInvited(target))
    {
        error("ERROR 443: User is already invited to the channel");
        return;
    }

    channel->invite(target);
    target->appendToWrite(":" + client.getNick() + " INVITE " + target->getNick() + " :" + channel->getName() + "\r\n");
}

void Server::handlePart(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        error("ERROR 461: PART command requires 1 parameter: <channel>");
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        error("ERROR 403: No such channel");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        error("ERROR 442: You're not a member of this channel");
        return;
    }

    channel->removeMember(&client);
    channel->broadcast(":" + client.getNick() + " PART " + channel->getName() + "\r\n");
}

void Server::handleKick(Client &client, const std::vector<std::string> &params)
{
    if (params.size() < 2)
    {
        error("ERROR 461: KICK command requires 2 parameters: <channel> <user>");
        return;
    }

    const std::string &channelName = params[0];
    const std::string &targetNick = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        error("ERROR 403: No such channel");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        error("ERROR 482: You're not a channel operator");
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target || !channel->isMember(target))
    {
        error("ERROR 441: User not in channel");
        return;
    }

    channel->removeMember(target);
    channel->broadcast(":" + client.getNick() + " KICK " + channel->getName() + " " + target->getNick() + "\r\n");
}

void Server::handleTopic(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        error("ERROR 461: TOPIC command requires 1 parameter: <channel>");
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        error("ERROR 403: No such channel");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        error("ERROR 442: You're not a member of this channel");
        return;
    }

    if (channel->isTopicRestricted() && !channel->isOperator(&client))
    {
        error("ERROR 482: You're not a channel operator");
        return;
    }

    if (params.size() == 1)
    {
        client.appendToWrite(":" + channel->getName() + " : " + channel->getTopic() + "\r\n");
    }
    else
    {
        channel->setTopic(params[1]);
        channel->broadcast(":" + client.getNick() + " TOPIC " + channel->getName() + " :" + params[1] + "\r\n");
    }
}

void	Server::handleMode(Client &client, const std::vector<std::string> &params)
{
	const std::string &channelName = params[0];

	std::map<std::string, Channel *>::iterator it = _channels.find(channelName);

	if(params.size() < 1)
	{
		error("ERROR 461: MODE command requires at least 1 parameter: <channel> [<mode>]");
		return ;
	}
	if(!client.isRegistered())
	{
		error("ERROR 451: MODE command can only be used by registered clients");
		return ;
	}
	if(it == _channels.end())
	{
		error("ERROR 403: MODE command can only be used for existing channels");
		return ;
	}
	Channel *channel = it->second;
	if(!channel->isMember(&client))
	{
		error("ERROR 442: MODE command can only be used by channel members");
		return ;
	}
	if(!channel->isOperator(&client))
	{
		error("ERROR 482: MODE command can only be used by channel operators");
		return ;
	}
	if(params[1] == "+o")
	{
		const std::string &targetNick = params[2];
		Client *target = findClientByNick(targetNick);
		channel->addOperator(target);
	}
	if(params[1] == "-o")
	{
		const std::string &targetNick = params[2];
		Client *target = findClientByNick(targetNick);
		channel->removeOperator(target);
	}
	if(params[1] == "+i")
		channel->setInviteOnly();
	if(params[1] == "-i")
		channel->removeInviteOnly();
	if(params[1] == "+t")
		channel->setTopic(params[2]);
	if(params[1] == "-t")
		channel->removeTopic();
	if(params[1] == "+k")
		channel->setKey(params[2]);
	if(params[1] == "-k")
		channel->removeKey();

	//TODO(C): implement mode changes and broadcast the result to channel members

}

void Server::handlePASS(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		error("ERROR 461: PASS command requires 1 parameter: <password>");
		return;
	}

	const std::string &password = params[0];

	if (password != _password)
	{
		error("ERROR 464: Password incorrect");
		return;
	}

	client.setPassValidated(true);
}

void Server::error(const std::string &msg)
{
    std::cerr << "Error: " << msg << std::endl;
}

void Server::handleNICK(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		error("ERROR 431: NICK command requires 1 parameter: <nickname>");
		return;
	}

	const std::string &newNick = params[0];

	Client *existingClient = findClientByNick(newNick);
	if (existingClient && existingClient != &client)
	{
		error("ERROR 433: Nickname is already in use");
		return;
	}

	client.setNick(newNick);

	if (client.isPassValidated() && !client.getUser().empty())
	{
		client.setRegistered(true);
		client.appendToWrite("Welcome to the IRC server, " + newNick + "!\r\n");
	}
}

void	Server::handleUSER(Client &client, const std::vector<std::string> &params)
{
	if (params.size() < 3)
	{
		error("ERROR 461: USER command requires 3 parameters: <username> <servername> <realname>");
		return;
	}

	client.setUser(params[0], params[2]);
	if (client.isPassValidated() && !client.getNick().empty())
	{
		client.setRegistered(true);
		client.appendToWrite("Welcome to the IRC server, " + client.getNick() + "!\r\n");
	}
}

void Server::handlePING(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		error("ERROR 409: PING command requires 1 parameter: <token>");
		return;
	}

	const std::string &token = params[0];
	client.appendToWrite("PONG " + token + "\r\n");
}

void Server::handlePrivmsg(Client &client, const std::vector<std::string> &params)
{
	if (params.size() < 2)
	{
		error("ERROR 461: PRIVMSG command requires 2 parameters: <target> <message>");
		return;
	}

	const std::string &target = params[0];
	const std::string &message = params[1];

	Client *targetClient = findClientByNick(target);
	if (targetClient)
	{
		targetClient->appendToWrite(":" + client.getNick() + " PRIVMSG " + target + " :" + message + "\r\n");
	}
	else
	{
		std::map<std::string, Channel *>::iterator it = _channels.find(target);
		if (it != _channels.end())
		{
			Channel *channel = it->second;
			if (!channel->isMember(&client))
			{
				error("ERROR 404: Cannot send to channel, you are not a member");
				return;
			}
			channel->broadcast(":" + client.getNick() + " PRIVMSG " + target + " :" + message + "\r\n", &client);
		}
		else
		{
			error("ERROR 401: No such nick/channel");
			return;
		}
	}
}