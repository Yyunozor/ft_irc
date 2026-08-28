/* ************************************************************************** */
/*                                                                            */
/*   CommandsChannel.cpp - partie C, les channels                           */
/*                                                                            */
/*   Auteur : Ilias                                                         */
/*   Contenu : JOIN, PART, KICK, INVITE, TOPIC et MODE (i, t, k, o, l)        */
/*                                                                            */
/*   Ce fichier fait partie de src/Server.cpp, decoupe en trois unites pour   */
/*   que chacun travaille dans la sienne : a 901 lignes, un seul fichier      */
/*   partage a trois produisait un conflit a chaque fusion.                   */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "Replies.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include <sstream>
#include <set>
#include <string>
#include <map>
#include <vector>

void Server::handleJoin(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "JOIN"));
        return;
    }

	if(!client.isRegistered())
	{
		client.appendToWrite(irc::errNotRegistered(client.getNick()));
		return ;
	}

	// A channel name always starts with '#' or '&'; anything else can never
	// have been created by getOrCreateChannel(), so treat it the same as an
	// unknown channel instead of creating a channel nobody could ever join.
	if (params[0][0] != '#' && params[0][0] != '&')
	{
		client.appendToWrite(irc::errNoSuchChannel(client.getNick(), params[0]));
		return;
	}

    Channel *channel = getOrCreateChannel(&client, params[0]);

    if (channel->isInviteOnly() && !channel->isInvited(&client))
    {
        client.appendToWrite(irc::errInviteOnlyChan(client.getNick(), params[0]));
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
		client.appendToWrite(irc::errBadChannelKey(client.getNick(), params[0]));
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

void Server::handleInvite(Client &client, const std::vector<std::string> &params)
{
    if (params.size() < 2)
    {
        client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "INVITE"));
        return;
    }

    const std::string &targetNick = params[0];
    const std::string &channelName = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite(irc::errNoSuchChannel(client.getNick(), channelName));
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        client.appendToWrite(irc::errChanOpPrivsNeeded(client.getNick(), channelName));
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target)
    {
        client.appendToWrite(irc::errNoSuchNick(client.getNick(), targetNick));
        return;
    }

    if (channel->isMember(target))
    {
        client.appendToWrite(irc::errUserOnChannel(client.getNick(), targetNick, channelName));
        return;
    }

    if (channel->isInvited(target))
    {
        client.appendToWrite(irc::errUserOnChannel(client.getNick(), targetNick, channelName));
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
        client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "PART"));
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite(irc::errNoSuchChannel(client.getNick(), channelName));
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        client.appendToWrite(irc::errNotOnChannel(client.getNick(), channelName));
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
        client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "KICK"));
        return;
    }

    const std::string &channelName = params[0];
    const std::string &targetNick = params[1];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite(irc::errNoSuchChannel(client.getNick(), channelName));
        return;
    }

    Channel *channel = it->second;

    if (!channel->isOperator(&client))
    {
        client.appendToWrite(irc::errChanOpPrivsNeeded(client.getNick(), channelName));
        return;
    }

    Client *target = findClientByNick(targetNick);
    if (!target || !channel->isMember(target))
    {
        client.appendToWrite(irc::errUserNotInChannel(client.getNick(), targetNick, channelName));
        return;
    }

    // RFC 2812 3.2.8: an optional trailing <comment>; real clients default it
    // to the kicker's own nick when omitted.
    const std::string &reason = params.size() > 2 ? params[2] : client.getNick();

    channel->removeMember(target);
    channel->broadcast(irc::fromUser(client.prefix(), "KICK " + channel->getName()
        + " " + target->getNick() + " :" + reason));
}

void Server::handleTopic(Client &client, const std::vector<std::string> &params)
{
    if (params.empty())
    {
        client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "TOPIC"));
        return;
    }

    const std::string &channelName = params[0];

    std::map<std::string, Channel *>::iterator it = _channels.find(channelName);
    if (it == _channels.end())
    {
        client.appendToWrite(irc::errNoSuchChannel(client.getNick(), channelName));
        return;
    }

    Channel *channel = it->second;

    if (!channel->isMember(&client))
    {
        client.appendToWrite(irc::errNotOnChannel(client.getNick(), channelName));
        return;
    }

    if (channel->isTopicRestricted() && !channel->isOperator(&client))
    {
        client.appendToWrite(irc::errChanOpPrivsNeeded(client.getNick(), channelName));
        return;
    }

    if (params.size() == 1)
    {
        if (channel->getTopic().empty())
            client.appendToWrite(irc::rplNoTopic(client.getNick(), channelName));
        else
            client.appendToWrite(irc::rplTopic(client.getNick(), channelName,
                channel->getTopic()));
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
		client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "MODE"));
		return ;
	}

	const std::string &channelName = params[0];

	std::map<std::string, Channel *>::iterator it = _channels.find(channelName);

	if(!client.isRegistered())
	{
		client.appendToWrite(irc::errNotRegistered(client.getNick()));
		return ;
	}
	if(it == _channels.end())
	{
		client.appendToWrite(irc::errNoSuchChannel(client.getNick(), channelName));
		return ;
	}
	Channel *channel = it->second;
	if(!channel->isMember(&client))
	{
		client.appendToWrite(irc::errNotOnChannel(client.getNick(), channelName));
		return ;
	}
	if(!channel->isOperator(&client))
	{
		client.appendToWrite(irc::errChanOpPrivsNeeded(client.getNick(), channelName));
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

