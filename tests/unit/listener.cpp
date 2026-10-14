#include "webserv.hpp"
#include "config/Listener.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static int	g_passed = 0;
static int	g_failed = 0;

static void	check(const std::string &name, bool condition)
{
	std::cout << (condition ? "OK   " : "KO   ") << name << std::endl;
	if (condition)
		g_passed++;
	else
		g_failed++;
}

static void	checkEqual(const std::string &name, const std::string &got,
				const std::string &want)
{
	if (got == want)
		check(name, true);
	else
	{
		std::cout << "KO   " << name << " (want '" << want << "', got '"
			<< got << "')" << std::endl;
		g_failed++;
	}
}

static std::string	ipv4Key(unsigned int address, int port)
{
	struct sockaddr_in	in;
	Listener			out;

	std::memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(port);
	in.sin_addr.s_addr = htonl(address);
	if (!Listener::fromAddress(reinterpret_cast<struct sockaddr *>(&in), out))
		return ("<failed>");
	return (out.key());
}

static std::string	ipv6Key(const unsigned short *groups, unsigned int scope)
{
	struct sockaddr_in6	in;
	Listener			out;
	int					i;

	std::memset(&in, 0, sizeof(in));
	in.sin6_family = AF_INET6;
	in.sin6_port = htons(80);
	in.sin6_scope_id = scope;
	for (i = 0; i < 8; i++)
	{
		in.sin6_addr.s6_addr[2 * i] = static_cast<unsigned char>(groups[i] >> 8);
		in.sin6_addr.s6_addr[2 * i + 1] = static_cast<unsigned char>(groups[i]);
	}
	if (!Listener::fromAddress(reinterpret_cast<struct sockaddr *>(&in), out))
		return ("<failed>");
	return (out.key());
}

static std::string	resolved(const std::string &host, int port)
{
	Listener	listener(host, port);
	std::string	error;

	if (!listener.resolve(error))
		return ("<" + error + ">");
	return (listener.key());
}

static void	testFormatting(void)
{
	const unsigned short	any[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	const unsigned short	loopback[8] = {0, 0, 0, 0, 0, 0, 0, 1};
	const unsigned short	doc[8] = {0x2001, 0xdb8, 0, 0, 1, 0, 0, 1};
	const unsigned short	single[8] = {0x2001, 0xdb8, 0, 1, 1, 1, 1, 1};
	const unsigned short	tail[8] = {1, 0, 0, 2, 0, 0, 0, 3};
	const unsigned short	full[8] = {1, 2, 3, 4, 5, 6, 7, 0xabcd};
	const unsigned short	mapped[8] = {0, 0, 0, 0, 0, 0xffff, 0xc000, 0x0201};
	const unsigned short	linkLocal[8] = {0xfe80, 0, 0, 0, 0, 0, 0, 1};

	std::cout << "-- fromAddress: text form" << std::endl;
	checkEqual("ipv4", ipv4Key(0x7f000001, 8080), "127.0.0.1:8080");
	checkEqual("ipv4 any", ipv4Key(0, 80), "0.0.0.0:80");
	checkEqual("ipv6 any", ipv6Key(any, 0), "[::]:80");
	checkEqual("ipv6 loopback", ipv6Key(loopback, 0), "[::1]:80");
	checkEqual("equal runs: leftmost collapsed", ipv6Key(doc, 0),
		"[2001:db8::1:0:0:1]:80");
	checkEqual("single zero group not collapsed", ipv6Key(single, 0),
		"[2001:db8:0:1:1:1:1:1]:80");
	checkEqual("longest run collapsed", ipv6Key(tail, 0), "[1:0:0:2::3]:80");
	checkEqual("no zeros, lowercase hex", ipv6Key(full, 0),
		"[1:2:3:4:5:6:7:abcd]:80");
	checkEqual("ipv4-mapped keeps dotted tail", ipv6Key(mapped, 0),
		"[::ffff:192.0.2.1]:80");
	checkEqual("scope id appended", ipv6Key(linkLocal, 3), "[fe80::1%3]:80");
}

static void	testResolve(void)
{
	std::cout << "-- resolve: config spelling to address" << std::endl;
	checkEqual("ipv4 literal unchanged", resolved("127.0.0.1", 80),
		"127.0.0.1:80");
	checkEqual("wildcard unchanged", resolved(DEFAULT_HOST, 80), "0.0.0.0:80");
	checkEqual("localhost prefers ipv4", resolved("localhost", 80),
		"127.0.0.1:80");
	checkEqual("ipv6 spelling normalised", resolved("[0:0::0001]", 80),
		"[::1]:80");
	checkEqual("ipv6 uppercase lowered", resolved("[2001:DB8::A]", 80),
		"[2001:db8::a]:80");
	checkEqual("ipv4-mapped stays ipv6", resolved("[::ffff:192.0.2.1]", 80),
		"[::ffff:192.0.2.1]:80");
	check("unknown host fails",
		resolved("no-such-host.invalid", 80).find("host not found") == 1);
}

static void	testWildcard(void)
{
	std::cout << "-- wildcard" << std::endl;
	check("0.0.0.0 is wildcard", Listener(DEFAULT_HOST, 80).isWildcard());
	check("[::] is wildcard", Listener("[::]", 80).isWildcard());
	check("127.0.0.1 is not", !Listener("127.0.0.1", 80).isWildcard());
	check("ipv4 wildcard of 127.0.0.1",
		Listener("127.0.0.1", 81).wildcard() == Listener(DEFAULT_HOST, 81));
	check("ipv6 wildcard of [::1]",
		Listener("[::1]", 81).wildcard() == Listener("[::]", 81));
}

static void	testFromSocket(void)
{
	struct sockaddr_in	address;
	socklen_t			length;
	Listener			bound;
	Listener			local;
	int					server;
	int					client;
	int					accepted;

	std::cout << "-- fromSocket: real address behind a wildcard socket"
		<< std::endl;
	server = socket(AF_INET, SOCK_STREAM, 0);
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	length = sizeof(address);
	if (server < 0
		|| bind(server, reinterpret_cast<struct sockaddr *>(&address),
			sizeof(address)) < 0
		|| listen(server, 1) < 0
		|| !Listener::fromSocket(server, bound))
	{
		check("wildcard socket opened", false);
		return ;
	}
	checkEqual("listening socket reports the wildcard", bound.host,
		DEFAULT_HOST);
	client = socket(AF_INET, SOCK_STREAM, 0);
	address.sin_port = htons(bound.port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	connect(client, reinterpret_cast<struct sockaddr *>(&address), length);
	accepted = accept(server, NULL, NULL);
	check("accepted", accepted >= 0);
	check("accepted socket reports the address the client reached",
		Listener::fromSocket(accepted, local)
		&& local == Listener("127.0.0.1", bound.port));
	check("closed fd fails", !Listener::fromSocket(-1, local));
	close(accepted);
	close(client);
	close(server);
}

int	main(void)
{
	testFormatting();
	testResolve();
	testWildcard();
	testFromSocket();
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
