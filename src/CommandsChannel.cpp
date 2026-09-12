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

	// A channel key is required whenever one is set, invite or not -- being
	// invited only waives the invite-only gate above, never the key. Without
	// this, a JOIN that simply omitted params[1] (as an invited client often
	// does, trusting the invite alone) skipped the check entirely instead of
	// being rejected.
	if (!channel->getKey().empty()
		&& (params.size() <= 1 || params[1] != channel->getKey()))
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

    // RFC 2812 3.2.2: the PART is sent to every member of the channel, the
    // one leaving included -- a real client waits for that echo to close the
    // channel window -- so broadcast before removeMember() drops them.
    channel->broadcast(irc::fromUser(client.prefix(), "PART " + channel->getName()));
    channel->removeMember(&client);
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

    // The KICK line goes to every member INCLUDING the target -- that is how
    // the kicked client learns it was removed -- so it must be broadcast
    // before removeMember() takes the target out of the channel.
    channel->broadcast(irc::fromUser(client.prefix(), "KICK " + channel->getName()
        + " " + target->getNick() + " :" + reason));
    channel->removeMember(target);
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
        // Le mode t restreint la modification du sujet, pas sa consultation
        // (RFC 2812 3.2.4) : placé plus haut, il renvoyait 482 à un membre
        // qui demandait simplement "TOPIC #chan" au lieu de 331/332.
        if (channel->isTopicRestricted() && !channel->isOperator(&client))
        {
            client.appendToWrite(irc::errChanOpPrivsNeeded(client.getNick(), channelName));
            return;
        }
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

	// Une cible qui n'est pas un salon est un mode utilisateur, pas un mode de
	// salon : irssi envoie "MODE <pseudo> +i" juste apres l'enregistrement
	// (reglage usermode). Sans cette sortie, la recherche ci-dessous ne trouve
	// evidemment aucun salon de ce nom et repond 403 avec le pseudo du client
	// dans le champ salon -- une erreur affichee des la connexion.
	if (channelName[0] != '#' && channelName[0] != '&')
	{
		if (channelName == client.getNick())
			client.appendToWrite(irc::rplUmodeIs(client.getNick()));
		else
			client.appendToWrite(irc::errUsersDontMatch(client.getNick()));
		return ;
	}

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

	const std::string	&modeStr = params[1];
	// A mode is only announced once it has actually changed something. Before
	// this, an unrecognised modeStr (a typo, or a client gluing a stray word
	// onto what looked like a valid mode -- "+tsalon" instead of "+t salon")
	// matched none of the branches below yet still fell through to an
	// unconditional broadcast, announcing a change that never happened.
	bool				applied = false;
	// Only for a mode that both takes an argument and was actually applied:
	// i, t, -k and -l never take one, so garbage in params[2] must never be
	// glued onto their announcement either.
	std::string			arg;
	bool				hasArg = false;

	if (modeStr == "+o" || modeStr == "-o")
	{
		// findClientByNick() returns NULL for an unknown nickname -- storing
		// that NULL in the operator set made the next broadcast() on this
		// channel dereference it.
		if (params.size() <= 2)
		{
			client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "MODE"));
			return ;
		}
		Client *target = findClientByNick(params[2]);
		if (target == NULL)
		{
			client.appendToWrite(irc::errNoSuchNick(client.getNick(), params[2]));
			return ;
		}
		if (modeStr == "+o")
			channel->addOperator(target);
		else
			channel->removeOperator(target);
		arg = params[2];
		hasArg = true;
		applied = true;
	}
	else if (modeStr == "+i")
	{
		channel->setInviteOnly();
		applied = true;
	}
	else if (modeStr == "-i")
	{
		channel->removeInviteOnly();
		applied = true;
	}
	// Mode t restricts TOPIC to operators; it does not touch the topic text.
	// The previous code called setTopic()/removeTopic(), which changed or
	// erased the subject instead.
	else if (modeStr == "+t")
	{
		channel->setTopicRestricted();
		applied = true;
	}
	else if (modeStr == "-t")
	{
		channel->removeTopicRestricted();
		applied = true;
	}
	else if (modeStr == "+k")
	{
		if (params.size() <= 2)
		{
			client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "MODE"));
			return ;
		}
		channel->setKey(params[2]);
		arg = params[2];
		hasArg = true;
		applied = true;
	}
	else if (modeStr == "-k")
	{
		channel->removeKey();
		applied = true;
	}
	// Mode l: "+l <n>" caps the membership, "-l" lifts the cap. A limit that
	// is not a positive number is ignored rather than parsed as 0, which
	// would silently mean "no limit".
	else if (modeStr == "+l")
	{
		if (params.size() <= 2)
		{
			client.appendToWrite(irc::errNeedMoreParams(client.getNick(), "MODE"));
			return ;
		}

		std::istringstream	iss(params[2]);
		long				limit = 0;

		if ((iss >> limit) && iss.eof() && limit > 0)
		{
			channel->setUserLimit(static_cast<std::size_t>(limit));
			arg = params[2];
			hasArg = true;
			applied = true;
		}
		// Malformed limit ("+l abc", "+l -3"): silently rejected, nothing to
		// announce since nothing changed.
	}
	else if (modeStr == "-l")
	{
		channel->setUserLimit(0);
		applied = true;
	}
	else
	{
		// Anything not matched above -- an unknown letter, or a mangled
		// string like "+tsalon" -- is rejected outright instead of being
		// parroted back to the whole channel as if it had taken effect.
		client.appendToWrite(irc::errUnknownMode(client.getNick(), modeStr));
		return ;
	}

	if (!applied)
		return ;

	// Every member has to learn about the change, otherwise their client keeps
	// showing stale channel modes.
	std::string	announce = modeStr;
	if (hasArg)
		announce += " " + arg;
	channel->broadcast(irc::fromUser(client.prefix(),
		"MODE " + channel->getName() + " " + announce));
}

