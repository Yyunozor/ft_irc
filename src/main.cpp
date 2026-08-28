#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <exception>
#include <string>

/*
** atoi() can't report failure: "abc" silently gives 0, "99999" silently
** wraps through htons(). strtol()'s end pointer tells us whether the whole
** argument was consumed, so garbage is rejected instead of turned into a
** wrong port.
*/
static bool	parsePort(const std::string &arg, int &port)
{
	if (arg.empty())
		return (false);

	char	*end = NULL;
	long	value = std::strtol(arg.c_str(), &end, 10);

	if (*end != '\0' || value < 1 || value > 65535)
		return (false);
	port = static_cast<int>(value);
	return (true);
}

int	main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
		return (1);
	}

	int	port;
	if (!parsePort(argv[1], port))
	{
		std::cerr << "Error: port must be a number between 1 and 65535" << std::endl;
		return (1);
	}

	std::string	password = argv[2];
	if (password.empty())
	{
		std::cerr << "Error: password must not be empty" << std::endl;
		return (1);
	}

	try
	{
		Server	server(port, password);
		server.start();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
