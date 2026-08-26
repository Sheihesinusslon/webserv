#ifndef LISTENER_HPP
# define LISTENER_HPP

# include <string>

struct Listener
{
	Listener();
	Listener(const std::string &host, int port);

	std::string	host;
	int			port;

	std::string	key() const;
	bool		operator==(const Listener &other) const;
};

#endif
