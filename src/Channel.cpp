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
	_members.erase(client);
	_operators.erase(client);
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
