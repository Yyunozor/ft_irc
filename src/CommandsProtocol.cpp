/* ************************************************************************** */
/*                                                                            */
/*   CommandsProtocol.cpp - partie B, le protocole                          */
/*                                                                            */
/*   Auteur : Yyuno                                                         */
/*   Contenu : routage des commandes, enregistrement (PASS/NICK/USER),        */
/*             messagerie (PRIVMSG/NOTICE), session (PING/PONG, QUIT)        */
/*                                                                            */
/*   Ce fichier fait partie de src/Server.cpp, decoupe en trois unites pour   */
/*   que chacun travaille dans la sienne : a 901 lignes, un seul fichier      */
/*   partage a trois produisait un conflit a chaque fusion.                   */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "Parser.hpp"
#include "Replies.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include <cctype>
#include <string>
#include <map>
#include <vector>

/*
** RFC 2812 grammar for a nickname:
**   first char  : a letter, or one of []\`_^{|}
**   later chars : letters, digits, one of []\`_^{|}, or '-'
**   length      : at most 9 characters
** static_cast<unsigned char> before toupper/isalpha/isalnum: passing a
** signed char with the high bit set (accented byte, raw UTF-8) is undefined
** behaviour otherwise, since these functions take an int representable as
** unsigned char.
*/
static bool	isNickSpecial(char c)
{
	return (c == '[' || c == ']' || c == '\\' || c == '`'
		|| c == '_' || c == '^' || c == '{' || c == '|');
}

static bool	isValidNick(const std::string &nick)
{
	if (nick.empty() || nick.size() > 9)
		return (false);

	unsigned char	first = static_cast<unsigned char>(nick[0]);
	if (!std::isalpha(first) && !isNickSpecial(nick[0]))
		return (false);

	for (std::string::size_type i = 1; i < nick.size(); ++i)
	{
		unsigned char	c = static_cast<unsigned char>(nick[i]);
		if (!std::isalnum(c) && !isNickSpecial(nick[i]) && nick[i] != '-')
			return (false);
	}
	return (true);
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

void Server::handlePASS(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "PASS"));
		return;
	}

	const std::string &password = params[0];

	if (password != _password)
	{
		client.appendToWrite(irc::errPasswdMismatch(client.getNick()));
		return;
	}

	client.setPassValidated(true);
}

void Server::handleNICK(Client &client, const std::vector<std::string> &params)
{
	if (params.empty())
	{
		client.appendToWrite(irc::errNoNicknameGiven(client.getNick()));
		return;
	}

	const std::string &newNick = params[0];

	if (!isValidNick(newNick))
	{
		client.appendToWrite(irc::errErroneusNickname(client.getNick(), newNick));
		return;
	}

	Client *existingClient = findClientByNick(newNick);
	if (existingClient && existingClient != &client)
	{
		client.appendToWrite(irc::errNicknameInUse(client.getNick(), newNick));
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
		client.appendToWrite(irc::errAlreadyRegistred(client.getNick()));
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

