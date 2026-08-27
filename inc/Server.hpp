#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <map>
# include <vector>
# include <poll.h>
# include "Client.hpp"
# include "Channel.hpp"

/*
** The server: owns the listening socket, the single poll() loop, and the
** collections of clients and channels (owned by A).
** Starting skeleton â adapt as needed and be ready to justify it.
*/
class Server
{
	private:
		int								_listenFd;
		int								_port;
		std::string						_password;
		std::map<int, Client *>			_clients;	// keyed by fd
		std::map<std::string, Channel *>	_channels;	// keyed by name

		// A: everything below is the single poll() loop and its plumbing.
		std::vector<struct pollfd>		_pollFds;

		void	setupListenSocket();
		void	acceptClient();
		bool	readFromClient(int fd);		// false => client must be dropped
		void	writeToClient(int fd);
		void	removeClient(int fd);
		void	refreshPollEvents();		// arm/disarm POLLOUT per client

		void						handlePart(Client &client, const std::vector<std::string> &params);
		void						handleKick(Client &client, const std::vector<std::string> &params);
		void						handleInvite(Client &client, const std::vector<std::string> &params);
		void						handleTopic(Client &client, const std::vector<std::string> &params);
		void						handleMode(Client &client, const std::vector<std::string> &params);
		// PRIVMSG and NOTICE share one implementation; NOTICE must never
		// produce an error reply (RFC 2812 3.3.2).
		void 						handlePrivmsg(Client &client, const std::vector<std::string> &params,
										bool isNotice);
		void						handlePASS(Client &client, const std::vector<std::string> &params);
		void						handleNICK(Client &client, const std::vector<std::string> &params);
		void 						handleUSER(Client &client, const std::vector<std::string> &params);
		void                        handlePING(Client &client, const std::vector<std::string> &params);
		void						handleQuit(Client &client, const std::vector<std::string> &params);
		// Sends 001-004 the moment PASS + NICK + USER are all satisfied.
		void						completeRegistration(Client &client);

		// B: parses a line into command + params and routes it.
		// Only JOIN is wired to a real handler so far, as a working example
		// for C to extend; PASS/NICK/USER/PRIVMSG/... still echo (TODO B),
		// PART/KICK/INVITE/TOPIC/MODE are unimplemented (TODO C).
		void	dispatchLine(Client &client, const std::string &line);
		void	handleJoin(Client &client, const std::vector<std::string> &params);

		// Shared lookups B/C's handlers need; A/B/C should not reach into
		// _clients/_channels directly from outside Server.
		Channel	*getOrCreateChannel(Client *client, const std::string &name);
		Client	*findClientByNick(const std::string &nick);
		// Drops a client from every channel it joined. Must run before the
		// Client is deleted, otherwise Channel keeps a dangling pointer.
		void	removeFromAllChannels(Client *client);

		Server(const Server &other);
		Server &operator=(const Server &other);

	public:
		Server(int port, const std::string &password);
		~Server();

		int					getPort() const;
		const std::string	&getPassword() const;

		void						error(const std::string &msg);

		void				start();
};

#endif
