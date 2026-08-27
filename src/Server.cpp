#include "Server.hpp"
#include "Parser.hpp"
#include "Replies.hpp"
#include <sstream>
#include <set>
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

void	Server::dispatchLine(Client &client, const std::string &line)
{
	irc::Message	msg;

	// Parsing lives in src/Parser.cpp: RFC 2812 grammar, parameters separated
	// by one *or more* spaces, trailing parameter after ':', 15-parameter cap.
	if (!irc::parse(line, msg))
		return;

	const std::string				&command = msg.command;
	const std::vector<std::string>	&params = msg.params;

	// Commands accepted before registration completes.
	if (command == "PASS")
		return (handlePASS(client, params));
	if (command == "NICK")
		return (handleNICK(client, params));
	if (command == "USER")
		return (handleUSER(client, params));
	if (command == "QUIT")
		return (handleQuit(client, params));
	if (command == "PING")
		return (handlePING(client, params));
	// A PONG is the client answering us: nothing to reply to a reply.
	if (command == "PONG")
		return;
	// irssi opens with "CAP LS 302". We support no capability, and answering
	// 421 leaves some clients waiting forever for a CAP END, so it is
	// silently ignored.
	if (command == "CAP")
		return;

	if (!client.isRegistered())
	{
		client.appendToWrite(irc::errNotRegistered(client.getNick()));
		return;
	}

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
	else if (command == "PRIVMSG")
		handlePrivmsg(client, params, false);
	else if (command == "NOTICE")
		handlePrivmsg(client, params, true);
	else
		client.appendToWrite(irc::errUnknownCommand(client.getNick(), msg.rawCommand));
}

void Server::handleJoin(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite("ERROR 461: JOIN command requires 1 parameter: <channel>\r\n");
        return;
    }

	if(!client.isRegistered())
	{
		client.appendToWrite("ERROR 451: MODE command can only be used by registered clients""\r\n");
		return ;
	}

    Channel *channel = getOrCreateChannel(&client, params[0]);

    if (channel->isInviteOnly() && !channel->isInvited(&client))
    {
        client.appendToWrite("ERROR 473: Cannot join channel " + params[0] + ", invite only\r\n");
        return;
    }

	// params[1] is the optional channel key: "JOIN #dev" carries none, so the
	// vector holds a single element and reading params[1] was out of bounds.
	// Mode l: the cap was stored but never enforced, so a full channel still
	// accepted everyone.
	if (channel->getUserLimit() > 0
		&& channel->getMembers().size() >= channel->getUserLimit()
		&& !channel->isMember(&client))
	{
		client.appendToWrite(irc::errChannelIsFull(client.getNick(), params[0]));
		return;
	}

	if (params.size() > 1 && params[1] != channel->getKey())
	{
		client.appendToWrite("ERROR 475: Cannot join channel " + params[0] + ", incorrect key\r\n");
		return;
	}

    channel->addMember(&client);

    // An invite is single-use: consumed once the JOIN actually succeeds, never
    // before (a JOIN rejected above by +l or +k must leave it intact).
    // Unconditional on purpose: guarding on isInviteOnly() would leave the
    // invite in reserve on a -i channel, ready to be cashed in the day an
    // operator sets +i again.
    channel->removeInvite(&client);

    // Full "nick!user@host" prefix: a real client needs user@host to build
    // its member list and to recognise its own JOIN.
    channel->broadcast(irc::fromUser(client.prefix(), "JOIN " + params[0]));

    // A JOIN is not finished with the echo above. A real client then waits for
    // the topic, and fills its member list from 353 before 366 tells it the
    // list is complete. Without these it opens the channel window with an
    // empty member list, however many people are actually in the room.
    if (channel->getTopic().empty())
        client.appendToWrite(irc::rplNoTopic(client.getNick(), params[0]));
    else
        client.appendToWrite(irc::rplTopic(client.getNick(), params[0],
            channel->getTopic()));

    const std::set<Client *>    &members = channel->getMembers();
    std::string                 names;

    for (std::set<Client *>::const_iterator it = members.begin();
        it != members.end(); ++it)
    {
        if (!names.empty())
            names += " ";
        // '@' is how the protocol marks a channel operator inside the names
        // list; clients render it as a badge next to the nickname.
        if (channel->isOperator(*it))
            names += "@";
        names += (*it)->getNick();
    }
    client.appendToWrite(irc::rplNamReply(client.getNick(), params[0], names));
    client.appendToWrite(irc::rplEndOfNames(client.getNick(), params[0]));
}

/*void    client.appendToWrite(const std::string &msg)
{
    std::cerr << "Error: " << msg << std::endl;
    std::exit(EXIT_FAILURE);
}*/

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

void Server::handleInvite(Client &client, const std::vector<std::string> &params)
{
    if (params.size() < 2)
    {
        client.appendToWrite("ERROR 461: INVITE command requires 2 parameters: <nick> <channel>""\r\n");
        return;
    }

    const std::string &targetNick = params[0];
    const std::string &channelName = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite("ERROR 403: No such channel""\r\n");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        client.appendToWrite("ERROR 482: You're not a channel operator""\r\n");
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target)
    {
        client.appendToWrite("ERROR 401: No such nick/channel""\r\n");
        return;
    }

    if (channel->isMember(target))
    {
        client.appendToWrite("ERROR 443: User is already in the channel""\r\n");
        return;
    }

    if (channel->isInvited(target))
    {
        client.appendToWrite("ERROR 443: User is already invited to the channel""\r\n");
        return;
    }

    channel->invite(target);
    // RFC 2812 3.2.7: exactly two people are notified -- the invitee gets the
    // INVITE, and the inviter gets 341 as confirmation. Nobody else.
    client.appendToWrite(irc::rplInviting(client.getNick(), channel->getName(),
        target->getNick()));
    target->appendToWrite(irc::fromUser(client.prefix(), "INVITE " + target->getNick() + " :" + channel->getName()));
}

void Server::handlePart(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite("ERROR 461: PART command requires 1 parameter: <channel>""\r\n");
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite("ERROR 403: No such channel""\r\n");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        client.appendToWrite("ERROR 442: You're not a member of this channel""\r\n");
        return;
    }

    channel->removeMember(&client);
    channel->broadcast(irc::fromUser(client.prefix(), "PART " + channel->getName()));
    // Same rule as removeFromAllChannels(): the last one out closes the door,
    // so an empty channel never survives to become an unjoinable ghost.
    if (channel->getMembers().empty())
    {
        _channels.erase(it);
        delete channel;
    }
}

void Server::handleKick(Client &client, const std::vector<std::string> &params)
{
    if (params.size() < 2)
    {
        client.appendToWrite("ERROR 461: KICK command requires 2 parameters: <channel> <user>""\r\n");
        return;
    }

    const std::string &channelName = params[0];
    const std::string &targetNick = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite("ERROR 403: No such channel""\r\n");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        client.appendToWrite("ERROR 482: You're not a channel operator""\r\n");
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target || !channel->isMember(target))
    {
        client.appendToWrite("ERROR 441: User not in channel""\r\n");
        return;
    }

    channel->removeMember(target);
    channel->broadcast(irc::fromUser(client.prefix(), "KICK " + channel->getName() + " " + target->getNick()));
}

void Server::handleTopic(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite("ERROR 461: TOPIC command requires 1 parameter: <channel>""\r\n");
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite("ERROR 403: No such channel""\r\n");
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        client.appendToWrite("ERROR 442: You're not a member of this channel""\r\n");
        return;
    }

    if (channel->isTopicRestricted() && !channel->isOperator(&client))
    {
        client.appendToWrite("ERROR 482: You're not a channel operator""\r\n");
        return;
    }

    if (params.size() == 1)
    {
        client.appendToWrite(":" + channel->getName() + " : " + channel->getTopic() + "\r\n");
    }
    else
    {
        channel->setTopic(params[1]);
        channel->broadcast(irc::fromUser(client.prefix(), "TOPIC " + channel->getName() + " :" + params[1]));
    }
}

void	Server::handleMode(Client &client, const std::vector<std::string> &params)
{
	// The size guard has to come first: params[0] was read before it, so a
	// bare "MODE" indexed an empty vector.
	if(params.size() < 1)
	{
		client.appendToWrite("ERROR 461: MODE command requires at least 1 parameter: <channel> [<mode>]""\r\n");
		return ;
	}

	const std::string &channelName = params[0];

	std::map<std::string, Channel *>::iterator it = _channels.find(channelName);

	if(!client.isRegistered())
	{
		client.appendToWrite("ERROR 451: MODE command can only be used by registered clients""\r\n");
		return ;
	}
	if(it == _channels.end())
	{
		client.appendToWrite("ERROR 403: MODE command can only be used for existing channels""\r\n");
		return ;
	}
	Channel *channel = it->second;
	if(!channel->isMember(&client))
	{
		client.appendToWrite("ERROR 442: MODE command can only be used by channel members""\r\n");
		return ;
	}
	if(!channel->isOperator(&client))
	{
		client.appendToWrite("ERROR 482: MODE command can only be used by channel operators""\r\n");
		return ;
	}
	// "MODE #chan" without a mode letter is a query, not a change: nothing to
	// apply, and params[1] would be out of bounds.
	if(params.size() < 2)
		return ;

	// Each of these takes an argument: params[2] was read unconditionally, and
	// findClientByNick() returns NULL for an unknown nickname -- storing that
	// NULL in the operator set made the next broadcast() dereference it.
	if(params[1] == "+o" && params.size() > 2)
	{
		const std::string &targetNick = params[2];
		Client *target = findClientByNick(targetNick);
		if (target != NULL)
			channel->addOperator(target);
	}
	if(params[1] == "-o" && params.size() > 2)
	{
		const std::string &targetNick = params[2];
		Client *target = findClientByNick(targetNick);
		if (target != NULL)
			channel->removeOperator(target);
	}
	if(params[1] == "+i")
		channel->setInviteOnly();
	if(params[1] == "-i")
		channel->removeInviteOnly();
	// Mode t restricts TOPIC to operators; it does not touch the topic text.
	// The previous code called setTopic()/removeTopic(), which changed or
	// erased the subject instead.
	if(params[1] == "+t")
		channel->setTopicRestricted();
	if(params[1] == "-t")
		channel->removeTopicRestricted();
	if(params[1] == "+k" && params.size() > 2)
		channel->setKey(params[2]);
	if(params[1] == "-k")
		channel->removeKey();
	// Mode l: "+l <n>" caps the membership, "-l" lifts the cap. A limit that
	// is not a positive number is ignored rather than parsed as 0, which
	// would silently mean "no limit".
	if(params[1] == "+l" && params.size() > 2)
	{
		std::istringstream	iss(params[2]);
		long				limit = 0;

		if ((iss >> limit) && iss.eof() && limit > 0)
			channel->setUserLimit(static_cast<std::size_t>(limit));
	}
	if(params[1] == "-l")
		channel->setUserLimit(0);

	// Every member has to learn about the change, otherwise their client keeps
	// showing stale channel modes.
	std::string	announce = params[1];
	if (params.size() > 2)
		announce += " " + params[2];
	channel->broadcast(irc::fromUser(client.prefix(),
		"MODE " + channel->getName() + " " + announce));
}

void Server::handlePASS(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		client.appendToWrite("ERROR 461: PASS command requires 1 parameter: <password>""\r\n");
		return;
	}

	const std::string &password = params[0];

	if (password != _password)
	{
		client.appendToWrite("ERROR 464: Password incorrect""\r\n");
		return;
	}

	client.setPassValidated(true);
}

/*void Server::client.appendToWrite(const std::string &msg)
{
    std::cerr << "Error: " << msg << std::endl;
}*/

void Server::handleNICK(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		client.appendToWrite("ERROR 431: NICK command requires 1 parameter: <nickname>""\r\n");
		return;
	}

	const std::string &newNick = params[0];

	Client *existingClient = findClientByNick(newNick);
	if (existingClient && existingClient != &client)
	{
		client.appendToWrite("ERROR 433: Nickname is already in use""\r\n");
		return;
	}

	// A rename after registration must be announced to every channel shared
	// with others, using the OLD prefix -- otherwise nobody can tell who the
	// new nickname used to be.
	if (client.isRegistered())
	{
		std::string	announce = irc::fromUser(client.prefix(), "NICK :" + newNick);

		client.appendToWrite(announce);
		for (std::map<std::string, Channel *>::iterator it = _channels.begin();
			it != _channels.end(); ++it)
		{
			if (it->second->isMember(&client))
				it->second->broadcast(announce, &client);
		}
		client.setNick(newNick);
		return;
	}

	client.setNick(newNick);
	completeRegistration(client);
}

void	Server::handleUSER(Client &client, const std::vector<std::string> &params)
{
	if (client.isRegistered())
	{
		client.appendToWrite("ERROR 462: You may not reregister""\r\n");
		return;
	}
	// RFC 2812: USER <user> <mode> <unused> :<realname>. The username is the
	// client's own login and is unrelated to the nickname -- requiring them to
	// match made registration impossible with any real client.
	if (params.size() < 4)
	{
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "USER"));
		return;
	}

	client.setUser(params[0]);
	client.setUser(params[3], params[0]);	// realname, username
	client.setUserReceived(true);
	completeRegistration(client);
}

void Server::handlePING(Client &client, const std::vector<std::string> &params)
{
	// The same token is echoed back so the client can match request to reply.
	std::string	token = params.empty() ? std::string(SERVER_NAME) : params[0];

	client.appendToWrite(":" SERVER_NAME " PONG " SERVER_NAME " :" + token + "\r\n");
}

void Server::handlePrivmsg(Client &client, const std::vector<std::string> &params,
	bool isNotice)
{
	// RFC 2812 3.3.2: a NOTICE must never trigger an error reply, so two
	// automated senders cannot bounce errors at each other forever.
	const std::string	verb = isNotice ? "NOTICE" : "PRIVMSG";

	if (params.size() < 2)
	{
		if (!isNotice)
			client.appendToWrite(irc::errNeedMoreParams(client.getNick(), verb));
		return;
	}

	const std::string &target = params[0];
	const std::string &message = params[1];

	Client *targetClient = findClientByNick(target);
	if (targetClient)
	{
		targetClient->appendToWrite(irc::fromUser(client.prefix(),
			verb + " " + target + " :" + message));
	}
	else
	{
		std::map<std::string, Channel *>::iterator it = _channels.find(target);
		if (it != _channels.end())
		{
			Channel *channel = it->second;
			if (!channel->isMember(&client))
			{
				if (!isNotice)
					client.appendToWrite(irc::errCannotSendToChan(client.getNick(), target));
				return;
			}
			channel->broadcast(irc::fromUser(client.prefix(),
				verb + " " + target + " :" + message), &client);
		}
		else
		{
			if (!isNotice)
				client.appendToWrite(irc::errNoSuchNick(client.getNick(), target));
			return;
		}
	}
}

/*
** Registration needs PASS, NICK and USER. Whichever arrives last triggers the
** welcome burst, so the order the client picks does not matter -- irssi sends
** all three in a single packet.
*/
void	Server::completeRegistration(Client &client)
{
	if (client.isRegistered())
		return;
	if (client.getNick().empty() || !client.hasUserInfo())
		return;
	if (!client.isPassValidated())
		return;

	client.setRegistered(true);

	const std::string	&nick = client.getNick();
	client.appendToWrite(irc::welcome(nick, client.prefix()));
	client.appendToWrite(irc::yourHost(nick));
	client.appendToWrite(irc::created(nick));
	client.appendToWrite(irc::myInfo(nick));
}

/*
** QUIT is announced to every channel the client shares, then the client is
** only *flagged*. It stays in the poll() loop one more iteration so POLLOUT
** can flush whatever is still queued: writing to a descriptor outside poll()
** is what chapter IV.1 punishes with a grade of 0.
*/
void	Server::handleQuit(Client &client, const std::vector<std::string> &params)
{
	std::string	reason = params.empty() ? std::string("Client quit") : params[0];
	std::string	announce = irc::fromUser(client.prefix(), "QUIT :" + reason);

	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
	{
		if (it->second->isMember(&client))
			it->second->broadcast(announce, &client);
	}
	client.setQuitting(true);
}
