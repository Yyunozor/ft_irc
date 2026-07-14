#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <exception>

int	main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
		return (1);
	}

	// TODO: validate port (e.g. 1024-65535) and non-empty password before use.
	int			port = std::atoi(argv[1]);
	std::string	password = argv[2];

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
