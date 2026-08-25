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

void Channel::removeInvite(Client *client)
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

void	Channel::removeInviteOnly()
{
	_inviteOnly = false;
}

/*void Channel::mode(Client *client, const std::string &mode)
{
	if (mode == "+o")
		addOperator(client);
	else if (mode == "-o")
		_operators.erase(client);
	else if (mode == "+i")
		_inviteOnly = true;
	else if (mode == "-i")
		_inviteOnly = false;
	else if (mode == "+t")
		_topicRestricted = true;
	else if (mode == "-t")
		_topicRestricted = false;
	else if (mode[0] == '+')
		setKey(mode.substr(1));
	else if (mode[0] == '-')
		setKey("");
}*/