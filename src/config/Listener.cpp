#include "config/Listener.hpp"
#include "webserv.hpp"

#include <sstream>

Listener::Listener() : host(DEFAULT_HOST), port(DEFAULT_PORT)
{
}

Listener::Listener(const std::string &host, int port) : host(host), port(port)
{
}

std::string	Listener::key() const
{
	std::ostringstream	out;

	out << host << ":" << port;
	return (out.str());
}

bool	Listener::operator==(const Listener &other) const
{
	return (host == other.host && port == other.port);
}
