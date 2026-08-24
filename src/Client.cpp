#include "Client.hpp"

Client::Client(int fd)
	: _fd(fd), _passValidated(false), _host("localhost"),
	  _userReceived(false), _registered(false), _quitting(false)
{
}

Client::~Client()
{
}

int	Client::getFd() const
{
	return (_fd);
}

bool	Client::isPassValidated() const
{
	return (_passValidated);
}

void	Client::setPassValidated(bool v)
{
	_passValidated = v;
}

const std::string	&Client::getNick() const
{
	return (_nick);
}

void	Client::setNick(const std::string &nick)
{
	_nick = nick;
}

const std::string	&Client::getUser() const
{
	return (_user);
}

void	Client::setUser(const std::string &user)
{
	_user = user;
}

const std::string	&Client::getRealname() const
{
	return (_realname);
}

void	Client::setRealname(const std::string &realname)
{
	_realname = realname;
}

const std::string	&Client::getHost() const
{
	return (_host);
}

void	Client::setHost(const std::string &host)
{
	_host = host;
}

bool	Client::hasUserInfo() const
{
	return (_userReceived);
}

void	Client::setUserReceived(bool v)
{
	_userReceived = v;
}

/*
** Falls back to "*" for a missing nick or user so the prefix is never
** malformed: a client can legitimately send PRIVMSG the instant after NICK
** but before USER, and a real client parsing ":!@host" would choke.
*/
std::string	Client::prefix() const
{
	std::string	nick = _nick.empty() ? "*" : _nick;
	std::string	user = _user.empty() ? "*" : _user;

	return (nick + "!" + user + "@" + _host);
}

bool	Client::isQuitting() const
{
	return (_quitting);
}

void	Client::setQuitting(bool v)
{
	_quitting = v;
}

bool	Client::isRegistered() const
{
	return (_registered);
}

void	Client::setRegistered(bool v)
{
	_registered = v;
}

const std::string	&Client::readBuffer() const
{
	return (_readBuf);
}

void	Client::appendToRead(const char *data, std::size_t n)
{
	_readBuf.append(data, n);
}

const std::string	&Client::writeBuffer() const
{
	return (_writeBuf);
}

void	Client::appendToWrite(const std::string &data)
{
	_writeBuf += data;
}

bool	Client::extractLine(std::string &line)
{
	std::string::size_type pos = _readBuf.find("\r\n");

	if (pos == std::string::npos)
		return (false);
	line = _readBuf.substr(0, pos);
	_readBuf.erase(0, pos + 2);
	return (true);
}

void	Client::consumeWrite(std::size_t n)
{
	_writeBuf.erase(0, n);
}

bool	Client::hasPendingWrite() const
{
	return (!_writeBuf.empty());
}
