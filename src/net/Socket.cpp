#include "net/Socket.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static std::string	nodeName(const std::string &host)
{
	if (host.size() >= 2 && host[0] == '[' && host[host.size() - 1] == ']')
		return (host.substr(1, host.size() - 2));
	return (host);
}

Socket::Socket(const Listener &listener) : _listener(listener), _fd(-1)
{
}

Socket::~Socket()
{
	close();
}

int	Socket::fd() const
{
	return (_fd);
}

const Listener	&Socket::listener() const
{
	return (_listener);
}

const std::string	&Socket::error() const
{
	return (_error);
}

bool	Socket::fail(const std::string &what)
{
	_error = _listener.key() + ": " + what;
	if (errno != 0)
		_error += ": " + std::string(std::strerror(errno));
	close();
	return (false);
}

void	Socket::close()
{
	if (_fd >= 0)
		::close(_fd);
	_fd = -1;
}

bool	Socket::open()
{
	struct addrinfo		hints;
	struct addrinfo		*results;
	struct addrinfo		*it;
	std::ostringstream	port;
	std::string			node;
	int					status;
	int					yes;

	close();
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	port << _listener.port;
	node = nodeName(_listener.host);
	status = getaddrinfo(node.c_str(), port.str().c_str(), &hints, &results);
	if (status != 0)
	{
		_error = _listener.key() + ": " + gai_strerror(status);
		return (false);
	}
	yes = 1;
	errno = 0;
	for (it = results; it != NULL; it = it->ai_next)
	{
		_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (_fd < 0)
			continue;
		setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		if (it->ai_family == AF_INET6)
			setsockopt(_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
		if (bind(_fd, it->ai_addr, it->ai_addrlen) == 0)
			break;
		::close(_fd);
		_fd = -1;
	}
	freeaddrinfo(results);
	if (_fd < 0)
		return (fail("cannot bind"));
	if (fcntl(_fd, F_SETFL, O_NONBLOCK) < 0)
		return (fail("cannot set non-blocking"));
	if (listen(_fd, SOMAXCONN) < 0)
		return (fail("cannot listen"));
	return (true);
}
