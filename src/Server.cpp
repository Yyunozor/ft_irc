#include "Server.hpp"
#include <unistd.h>

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

void	Server::start()
{
	// TODO (A): create the listening socket (socket, setsockopt, bind, listen),
	// TODO (A): set it non-blocking with fcntl(fd, F_SETFL, O_NONBLOCK),
	// TODO (A): then run the single poll() loop:
	// TODO (A):   - accept new clients,
	// TODO (A):   - on POLLIN: recv, append to the client read buffer, extract
	// TODO (A):     complete "\r\n" lines and hand each to the dispatcher (B),
	// TODO (A):   - on POLLOUT: send from the client write buffer,
	// TODO (A):   - handle disconnections (recv == 0) and errors (recv < 0).
	// Reminder: never inspect errno to decide what to do after recv/send.
}
