#include "Channel.hpp"
#include "Client.hpp"

Channel::Channel(const std::string &name)
	: _name(name), _inviteOnly(false), _topicRestricted(false), _userLimit(0)
{
}

Channel::~Channel()
{
}

const std::string	&Channel::getName() const
{
	return (_name);
}

const std::string	&Channel::getTopic() const
{
	return (_topic);
}

const std::string	&Channel::getKey() const
{
	return (_key);
}

bool	Channel::isInviteOnly() const
{
	return (_inviteOnly);
}

bool	Channel::isTopicRestricted() const
{
	return (_topicRestricted);
}

std::size_t	Channel::getUserLimit() const
{
	return (_userLimit);
}

const std::set<Client *>	&Channel::getMembers() const
{
	return (_members);
}

const std::set<Client *>	&Channel::getOperators() const
{
	return (_operators);
}

void	Channel::addMember(Client *client)
{
	_members.insert(client);
}

void	Channel::removeMember(Client *client)
{
	_operators.erase(client);
	_members.erase(client);
}

bool	Channel::isMember(Client *client) const
{
	return (_members.find(client) != _members.end());
}

void	Channel::broadcast(const std::string &msg, Client *except) const
{
	for (std::set<Client *>::const_iterator it = _members.begin();
		it != _members.end(); ++it)
	{
		if (*it != except)
			(*it)->appendToWrite(msg);
	}
}

void	Channel::invite(Client *client)
{
	_invited.insert(client);
}

void	Channel::removeInvite(Client *client)
{
	_invited.erase(client);
}

bool 	Channel::isInvited(Client *client) const
{
	std::set<Client *>::const_iterator it = _invited.find(client);
	if (it != _invited.end())
		return (true);
	else
		return (false);
}

/*
bool	Channel::isMember(Client *client) const
{
	std::set<Client *>::const_iterator it = _members.find(client);
	if (it != _members.end())
		return (true);
	else
		return (false);
}*/

void Channel::removeOperator(Client *client)
{
	_operators.erase(client);
}

bool 	Channel::isOperator(Client *client) const
{
	std::set <Client *>::iterator it = _operators.find(client);
	if(it != _operators.end())
		return true;
	return false;
}

void 	Channel::addOperator(Client *client)
{
	_operators.insert(client);
}

void Channel::setKey(const std::string &key)
{
    _key = key;
}

void Channel::removeKey()
{
	_key.clear();
}

void Channel::setTopic(const std::string &topic)
{
    _topic = topic;
}

void Channel::removeTopic()
{
	_topic.clear();
}

void	Channel::setInviteOnly()
{
	_inviteOnly = true;
}

void	Channel::setTopicRestricted()
{
	_topicRestricted = true;
}

void	Channel::removeTopicRestricted()
{
	_topicRestricted = false;
}

/*
** A limit of 0 means "no limit", which is also what -l restores.
*/
void	Channel::setUserLimit(std::size_t limit)
{
	_userLimit = limit;
}

void	Channel::removeInviteOnly()
{
	_inviteOnly = false;
}

