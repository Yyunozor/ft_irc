#include "Parser.hpp"
#include <cctype>

namespace irc
{

/*
** RFC 2812 separates parameters with one *or more* spaces. Consuming a single
** space per parameter would push an empty string on "PRIVMSG  bob :hi" and
** shift every following argument by one — the target would become "".
*/
static void	skipSpaces(const std::string &s, std::string::size_type &i)
{
	while (i < s.size() && s[i] == ' ')
		++i;
}

// Reads up to the next space (or end of line) and advances past the token.
static std::string	readToken(const std::string &s, std::string::size_type &i)
{
	std::string::size_type	start = i;

	while (i < s.size() && s[i] != ' ')
		++i;
	return (s.substr(start, i - start));
}

/*
** Command names are case-insensitive, so the router only ever compares one
** canonical form. std::toupper takes an int that must be representable as an
** unsigned char: handing it a negative signed char (an accent, a UTF-8 byte)
** is undefined behaviour, hence the double cast.
*/
static std::string	toUpper(const std::string &s)
{
	std::string	out = s;

	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
	return (out);
}

bool	parse(const std::string &line, Message &out)
{
	std::string::size_type	i = 0;

	out.prefix.clear();
	out.rawCommand.clear();
	out.command.clear();
	out.params.clear();

	skipSpaces(line, i);
	if (i >= line.size())
		return (false);

	// Clients never send a prefix — only servers do, when relaying. Parsed
	// anyway so a stray one is skipped instead of being mistaken for the
	// command.
	if (line[i] == ':')
	{
		++i;
		out.prefix = readToken(line, i);
		skipSpaces(line, i);
	}

	out.rawCommand = readToken(line, i);
	if (out.rawCommand.empty())
		return (false);
	out.command = toUpper(out.rawCommand);

	while (true)
	{
		skipSpaces(line, i);
		if (i >= line.size())
			break;

		// A ':' opens the trailing parameter: everything left, spaces
		// included, is one single parameter. Stopping here is the whole
		// point — splitting further would destroy those spaces.
		if (line[i] == ':')
		{
			out.params.push_back(line.substr(i + 1));
			break;
		}
		// Same swallowing behaviour once this is the last slot available,
		// with or without a ':'.
		if (out.params.size() + 1 == MAX_PARAMS)
		{
			out.params.push_back(line.substr(i));
			break;
		}
		out.params.push_back(readToken(line, i));
	}
	return (true);
}

} // namespace irc
