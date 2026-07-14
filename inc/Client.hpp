#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <cstddef>

/*
** One connected client = one fd + its state.
**   - fd and buffers are owned by A (network core).
**   - registration state (pass/nick/user) is owned by B (protocol).
** This is a starting skeleton: adapt the members and signatures, you must be
** able to justify them at the defense.
*/
class Client
{
	private:
		int			_fd;
		std::string	_readBuf;	// A: raw bytes received, until a full "\r\n" line
		std::string	_writeBuf;	// A: bytes still waiting to be sent (partial send)

		bool		_passValidated;	// B: PASS accepted
		std::string	_nick;			// B
		std::string	_user;			// B
		bool		_registered;	// B: PASS + NICK + USER all done

		Client(const Client &other);
		Client &operator=(const Client &other);

	public:
		Client(int fd);
		~Client();

		int					getFd() const;

		// --- registration state (B) ---
		bool				isPassValidated() const;
		void				setPassValidated(bool v);
		const std::string	&getNick() const;
		void				setNick(const std::string &nick);
		const std::string	&getUser() const;
		void				setUser(const std::string &user);
		bool				isRegistered() const;
		void				setRegistered(bool v);

		// --- buffers (A) ---
		const std::string	&readBuffer() const;
		void				appendToRead(const char *data, std::size_t n);
		const std::string	&writeBuffer() const;
		void				appendToWrite(const std::string &data);
		// TODO (A): extract a complete "\r\n"-terminated command from _readBuf,
		// TODO (A): consume bytes from _writeBuf once sent.
};

#endif
