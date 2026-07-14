#include "Channel.hpp"

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
