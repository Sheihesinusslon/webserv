#ifndef LISTENER_HPP
# define LISTENER_HPP

# include <string>

struct sockaddr;

struct Listener
{
	Listener();
	Listener(const std::string &host, int port);

	std::string	host;
	int			port;

	std::string	key() const;
	bool		operator==(const Listener &other) const;

	bool		resolve(std::string &error);
	bool		isIpv6() const;
	bool		isWildcard() const;
	Listener	wildcard() const;

	static bool	fromAddress(const struct sockaddr *address, Listener &out);
	static bool	fromSocket(int fd, Listener &out);
};

#endif
