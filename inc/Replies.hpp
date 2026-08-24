#ifndef REPLIES_HPP
# define REPLIES_HPP

# include <string>
# include <sstream>
# include <cstddef>

/*
** IRC numeric replies (RFC 2812, section 5).
**
** Every numeric has the same shape:
**     :<server> <code> <target> <params> :<human readable text>
** where <target> is the recipient's nickname, or "*" while it is still
** unknown (before NICK has been accepted).
**
** Header-only on purpose: these are pure string builders, so adding one
** costs nothing and does not touch the Makefile — which is a shared file
** and therefore a merge-conflict magnet.
**
** Owned by B, but the channel numerics at the bottom are here for C to
** use as-is.
*/

# define SERVER_NAME	"ircserv"
# define SERVER_VERSION	"ft_irc-1.0"

namespace irc
{

inline std::string	toString(std::size_t n)
{
	std::ostringstream	oss;

	oss << n;
	return (oss.str());
}

/*
** `rest` is everything between the target and the trailing text, already
** formatted (including its leading ":" when there is trailing text).
*/
inline std::string	numeric(const std::string &code, const std::string &nick,
	const std::string &rest)
{
	std::string	target = nick.empty() ? "*" : nick;

	return (":" SERVER_NAME " " + code + " " + target + " " + rest + "\r\n");
}

/*
** A message sent *as* a user rather than as the server, e.g.
**     :nick!user@host PRIVMSG #chan :hello
*/
inline std::string	fromUser(const std::string &prefix, const std::string &body)
{
	return (":" + prefix + " " + body + "\r\n");
}

// --- registration (001-004) ------------------------------------------------

inline std::string	welcome(const std::string &nick, const std::string &prefix)
{
	return (numeric("001", nick,
		":Welcome to the Internet Relay Network " + prefix));
}

inline std::string	yourHost(const std::string &nick)
{
	return (numeric("002", nick,
		":Your host is " SERVER_NAME ", running version " SERVER_VERSION));
}

inline std::string	created(const std::string &nick)
{
	return (numeric("003", nick, ":This server was created at startup"));
}

inline std::string	myInfo(const std::string &nick)
{
	// <servername> <version> <user modes> <channel modes>
	return (numeric("004", nick, SERVER_NAME " " SERVER_VERSION " o itkol"));
}

// --- errors used by B ------------------------------------------------------

inline std::string	errNoSuchNick(const std::string &nick, const std::string &target)
{
	return (numeric("401", nick, target + " :No such nick/channel"));
}

inline std::string	errNoSuchChannel(const std::string &nick, const std::string &chan)
{
	return (numeric("403", nick, chan + " :No such channel"));
}

inline std::string	errCannotSendToChan(const std::string &nick, const std::string &chan)
{
	return (numeric("404", nick, chan + " :Cannot send to channel"));
}

inline std::string	errNoRecipient(const std::string &nick, const std::string &command)
{
	return (numeric("411", nick, ":No recipient given (" + command + ")"));
}

inline std::string	errNoTextToSend(const std::string &nick)
{
	return (numeric("412", nick, ":No text to send"));
}

inline std::string	errUnknownCommand(const std::string &nick, const std::string &command)
{
	return (numeric("421", nick, command + " :Unknown command"));
}

inline std::string	errNoNicknameGiven(const std::string &nick)
{
	return (numeric("431", nick, ":No nickname given"));
}

inline std::string	errErroneusNickname(const std::string &nick, const std::string &bad)
{
	return (numeric("432", nick, bad + " :Erroneous nickname"));
}

inline std::string	errNicknameInUse(const std::string &nick, const std::string &taken)
{
	return (numeric("433", nick, taken + " :Nickname is already in use"));
}

inline std::string	errNotRegistered(const std::string &nick)
{
	return (numeric("451", nick, ":You have not registered"));
}

inline std::string	errNeedMoreParams(const std::string &nick, const std::string &command)
{
	return (numeric("461", nick, command + " :Not enough parameters"));
}

/*
** 462 is spelled ERR_ALREADYREGISTRED in RFC 2812 — the missing second "E"
** is a typo in the RFC itself, kept because implementations had already
** aligned on it. The wire format only carries the number, so this only
** matters if you go looking for the name in the RFC.
*/
inline std::string	errAlreadyRegistered(const std::string &nick)
{
	return (numeric("462", nick, ":Unauthorized command (already registered)"));
}

inline std::string	errPasswdMismatch(const std::string &nick)
{
	return (numeric("464", nick, ":Password incorrect"));
}

// --- channel numerics, for C ----------------------------------------------

inline std::string	rplChannelModeIs(const std::string &nick, const std::string &chan,
	const std::string &modes)
{
	return (numeric("324", nick, chan + " " + modes));
}

inline std::string	rplNoTopic(const std::string &nick, const std::string &chan)
{
	return (numeric("331", nick, chan + " :No topic is set"));
}

inline std::string	rplTopic(const std::string &nick, const std::string &chan,
	const std::string &topic)
{
	return (numeric("332", nick, chan + " :" + topic));
}

inline std::string	rplInviting(const std::string &nick, const std::string &chan,
	const std::string &target)
{
	return (numeric("341", nick, chan + " " + target));
}

inline std::string	rplNamReply(const std::string &nick, const std::string &chan,
	const std::string &names)
{
	return (numeric("353", nick, "= " + chan + " :" + names));
}

inline std::string	rplEndOfNames(const std::string &nick, const std::string &chan)
{
	return (numeric("366", nick, chan + " :End of /NAMES list"));
}

inline std::string	errUserNotInChannel(const std::string &nick, const std::string &target,
	const std::string &chan)
{
	return (numeric("441", nick, target + " " + chan + " :They aren't on that channel"));
}

inline std::string	errNotOnChannel(const std::string &nick, const std::string &chan)
{
	return (numeric("442", nick, chan + " :You're not on that channel"));
}

inline std::string	errUserOnChannel(const std::string &nick, const std::string &target,
	const std::string &chan)
{
	return (numeric("443", nick, target + " " + chan + " :is already on channel"));
}

inline std::string	errChannelIsFull(const std::string &nick, const std::string &chan)
{
	return (numeric("471", nick, chan + " :Cannot join channel (+l)"));
}

inline std::string	errUnknownMode(const std::string &nick, const std::string &mode)
{
	return (numeric("472", nick, mode + " :is unknown mode char to me"));
}

inline std::string	errInviteOnlyChan(const std::string &nick, const std::string &chan)
{
	return (numeric("473", nick, chan + " :Cannot join channel (+i)"));
}

inline std::string	errBadChannelKey(const std::string &nick, const std::string &chan)
{
	return (numeric("475", nick, chan + " :Cannot join channel (+k)"));
}

inline std::string	errChanOPrivsNeeded(const std::string &nick, const std::string &chan)
{
	return (numeric("482", nick, chan + " :You're not channel operator"));
}

} // namespace irc

#endif
