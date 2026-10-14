#include "config/Listener.hpp"
#include "webserv.hpp"

#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>

#define IPV6_WILDCARD "[::]"

static std::string	nodeName(const std::string &host)
{
	if (host.size() >= 2 && host[0] == '[' && host[host.size() - 1] == ']')
		return (host.substr(1, host.size() - 2));
	return (host);
}

static std::string	formatIpv4(const unsigned char *bytes)
{
	std::ostringstream	out;
	int					i;

	for (i = 0; i < 4; i++)
	{
		if (i > 0)
			out << '.';
		out << static_cast<int>(bytes[i]);
	}
	return (out.str());
}

static bool	isMappedIpv4(const unsigned char *bytes)
{
	int	i;

	for (i = 0; i < 10; i++)
	{
		if (bytes[i] != 0)
			return (false);
	}
	return (bytes[10] == 0xff && bytes[11] == 0xff);
}

static std::string	formatIpv6(const unsigned char *bytes, unsigned int scope)
{
	std::ostringstream	out;
	int					groups[8];
	int					bestStart;
	int					bestLength;
	int					runStart;
	int					i;

	if (isMappedIpv4(bytes))
		return ("[::ffff:" + formatIpv4(bytes + 12) + "]");
	for (i = 0; i < 8; i++)
		groups[i] = (bytes[2 * i] << 8) | bytes[2 * i + 1];
	bestStart = -1;
	bestLength = 1;
	runStart = -1;
	for (i = 0; i <= 8; i++)
	{
		if (i < 8 && groups[i] == 0)
		{
			if (runStart < 0)
				runStart = i;
			continue;
		}
		if (runStart >= 0 && i - runStart > bestLength)
		{
			bestStart = runStart;
			bestLength = i - runStart;
		}
		runStart = -1;
	}
	out << '[' << std::hex;
	for (i = 0; i < 8; i++)
	{
		if (i == bestStart)
		{
			out << "::";
			i += bestLength - 1;
			continue;
		}
		if (i > 0 && i != bestStart + bestLength)
			out << ':';
		out << groups[i];
	}
	if (scope != 0)
		out << '%' << std::dec << scope;
	out << ']';
	return (out.str());
}

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

static int	lookup(const std::string &node, int port, int family,
				struct addrinfo **results)
{
	struct addrinfo		hints;
	std::ostringstream	service;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	service << port;
	return (getaddrinfo(node.c_str(), service.str().c_str(), &hints, results));
}

bool	Listener::resolve(std::string &error)
{
	struct addrinfo		*results;
	Listener			resolved;
	int					status;

	status = EAI_NONAME;
	if (!isIpv6())
		status = lookup(host, port, AF_INET, &results);
	if (status != 0)
		status = lookup(nodeName(host), port, AF_INET6, &results);
	if (status != 0)
	{
		error = "host not found in listen '" + key() + "': "
			+ gai_strerror(status);
		return (false);
	}
	if (!fromAddress(results->ai_addr, resolved))
	{
		freeaddrinfo(results);
		error = "unsupported address family in listen '" + key() + "'";
		return (false);
	}
	freeaddrinfo(results);
	host = resolved.host;
	return (true);
}

bool	Listener::isIpv6() const
{
	return (!host.empty() && host[0] == '[');
}

bool	Listener::isWildcard() const
{
	return (host == DEFAULT_HOST || host == IPV6_WILDCARD);
}

Listener	Listener::wildcard() const
{
	if (isIpv6())
		return (Listener(IPV6_WILDCARD, port));
	return (Listener(DEFAULT_HOST, port));
}

bool	Listener::fromAddress(const struct sockaddr *address, Listener &out)
{
	const struct sockaddr_in	*ipv4;
	const struct sockaddr_in6	*ipv6;

	if (address->sa_family == AF_INET)
	{
		ipv4 = reinterpret_cast<const struct sockaddr_in *>(address);
		out.host = formatIpv4(
				reinterpret_cast<const unsigned char *>(&ipv4->sin_addr));
		out.port = ntohs(ipv4->sin_port);
		return (true);
	}
	if (address->sa_family == AF_INET6)
	{
		ipv6 = reinterpret_cast<const struct sockaddr_in6 *>(address);
		out.host = formatIpv6(ipv6->sin6_addr.s6_addr, ipv6->sin6_scope_id);
		out.port = ntohs(ipv6->sin6_port);
		return (true);
	}
	return (false);
}

bool	Listener::fromSocket(int fd, Listener &out)
{
	struct sockaddr_storage	address;
	socklen_t				length;

	length = sizeof(address);
	if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&address),
			&length) < 0)
		return (false);
	return (fromAddress(reinterpret_cast<struct sockaddr *>(&address), out));
}
