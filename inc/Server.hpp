#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <map>
# include "Client.hpp"
# include "Channel.hpp"

/*
** The server: owns the listening socket, the single poll() loop, and the
** collections of clients and channels (owned by A).
** Starting skeleton — adapt as needed and be ready to justify it.
*/
class Server
{
	private:
		int								_listenFd;
		int								_port;
		std::string						_password;
		std::map<int, Client *>			_clients;	// keyed by fd
		std::map<std::string, Channel *>	_channels;	// keyed by name

		Server(const Server &other);
		Server &operator=(const Server &other);

	public:
		Server(int port, const std::string &password);
		~Server();

		int					getPort() const;
		const std::string	&getPassword() const;

		void				start();	// TODO (A): socket + bind + listen +
										// non-blocking setup, then the single
										// poll() loop (accept / read / write /
										// disconnect).
		// TODO (A): acceptClient(), removeClient(int fd),
		// TODO (A): handleReadable(Client&), handleWritable(Client&).
};

#endif
