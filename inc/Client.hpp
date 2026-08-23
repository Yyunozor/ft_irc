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
		std::string	_realName;
		std::string _username;
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

		// Pops one complete "\r\n"-terminated command out of the read buffer
		// (without the trailing CRLF) and erases it from the buffer.
		// Returns false if no full line is available yet.
		bool				extractLine(std::string &line);

		// Erases the first n bytes of the write buffer (bytes actually sent).
		void				consumeWrite(std::size_t n);

		// Whether poll() should be asked for POLLOUT on this client's fd.
		bool				hasPendingWrite() const;
		void				setUser(const std::string &realname, const std::string &username);
};

#endif
