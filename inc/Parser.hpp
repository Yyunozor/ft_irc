#ifndef PARSER_HPP
# define PARSER_HPP

# include <string>
# include <vector>

/*
** IRC message parser (RFC 2812, section 2.3.1).
**
**     [":" prefix SPACE] command *( SPACE middle ) [ SPACE ":" trailing ]
**
** Lives in its own translation unit rather than inside Server.cpp: the
** grammar is self-contained, it is the piece most likely to be questioned at
** the defense, and keeping it here means three people stop editing the same
** file to touch unrelated things.
**
** Owned by B.
*/

namespace irc
{

/*
** At most 15 parameters (RFC 2812 section 2.3). The 15th swallows the rest
** of the line even without a leading ':', which is why the limit belongs in
** the parser and not in the callers.
*/
const std::size_t	MAX_PARAMS = 15;

struct Message
{
	std::string					prefix;		// without its ':', empty when absent
	std::string					rawCommand;	// exactly as received, for ERR_UNKNOWNCOMMAND
	std::string					command;	// uppercased, what the router compares
	std::vector<std::string>	params;
};

/*
** Fills `out` from one line (already stripped of its trailing CRLF).
** Returns false when the line carries no command at all — empty, or only
** spaces — so the caller can drop it without answering.
*/
bool	parse(const std::string &line, Message &out);

} // namespace irc

#endif
