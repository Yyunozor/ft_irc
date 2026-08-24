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

		// A: everything below is the single poll() loop and its plumbing.
		std::vector<struct pollfd>		_pollFds;

		void	setupListenSocket();
		void	acceptClient();
		bool	readFromClient(int fd);		// false => client must be dropped
		void	writeToClient(int fd);
		void	removeClient(int fd);
		void	refreshPollEvents();		// arm/disarm POLLOUT per client

		// B: parses a line into command + params and routes it.
		void	dispatchLine(Client &client, const std::string &line);
		void	handleJoin(Client &client, const std::vector<std::string> &params);

		// --- B: registration ---------------------------------------------
		void	handlePass(Client &client, const std::vector<std::string> &params);
		void	handleNick(Client &client, const std::vector<std::string> &params);
		void	handleUser(Client &client, const std::vector<std::string> &params);
		// Sends 001-004 the moment PASS + NICK + USER are all satisfied.
		void	completeRegistration(Client &client);

		// --- B: messaging and session ------------------------------------
		// PRIVMSG and NOTICE share one implementation: the only difference is
		// that NOTICE must never generate an error reply (RFC 2812 3.3.2),
		// otherwise two servers bouncing errors at each other would loop.
		void	handlePrivmsg(Client &client, const std::vector<std::string> &params,
					bool isNotice);
		void	handlePing(Client &client, const std::vector<std::string> &params);
		void	handleQuit(Client &client, const std::vector<std::string> &params);

		// Announces a QUIT to every channel the client shares with others,
		// then flags it for A to drop at the end of the poll() iteration.
		void	disconnect(Client &client, const std::string &reason);

		// --- C: channels & operators ---------------------------------------
		void	handlePart(Client &client, const std::vector<std::string> &params);
		void	handleKick(Client &client, const std::vector<std::string> &params);
		void	handleInvite(Client &client, const std::vector<std::string> &params);
		void	handleTopic(Client &client, const std::vector<std::string> &params);
		void	handleMode(Client &client, const std::vector<std::string> &params);

		// Drops a channel from _channels (and frees it) once it has no
		// members left, so PART/KICK don't leak empty channels forever.
		void	dropChannelIfEmpty(Channel *channel);

		// Shared lookups B/C's handlers need; A/B/C should not reach into
		// _clients/_channels directly from outside Server.
		Channel	*getOrCreateChannel(const std::string &name);
		// Unlike getOrCreateChannel(), returns NULL instead of creating:
		// PRIVMSG to an unknown channel must answer 403, not conjure it.
		Channel	*findChannel(const std::string &name);
		Client	*findClientByNick(const std::string &nick);
		// Drops a client from every channel it joined. Must run before the
		// Client is deleted, otherwise Channel::_members keeps a dangling
		// pointer and the next broadcast() dereferences freed memory.
		void	removeFromAllChannels(Client *client);

		Server(const Server &other);
		Server &operator=(const Server &other);

	public:
		Server(int port, const std::string &password);
		~Server();

		int					getPort() const;
		const std::string	&getPassword() const;

		void				start();
};

#endif
