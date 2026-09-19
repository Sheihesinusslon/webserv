#include "webserv.hpp"
#include "config/Config.hpp"
#include "net/EventLoop.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
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

static void	checkCount(const std::string &name, std::size_t got, std::size_t want)
{
	if (got == want)
		check(name, true);
	else
	{
		std::cout << "KO   " << name << " (want " << want << ", got " << got
			<< ")" << std::endl;
		g_failed++;
	}
}

static int	connectTo(int port)
{
	struct sockaddr_in	addr;
	struct timeval		timeout;
	int					fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return (-1);
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		close(fd);
		return (-1);
	}
	return (fd);
}

static void	spin(EventLoop &loop, int times)
{
	while (times-- > 0)
		loop.runOnce(50);
}

static std::string	readAll(int fd)
{
	std::string	out;
	char		buf[4096];
	ssize_t		n;

	while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
		out.append(buf, static_cast<std::size_t>(n));
	return (out);
}

static bool	sendAll(int fd, const std::string &data)
{
	return (send(fd, data.data(), data.size(), 0)
		== static_cast<ssize_t>(data.size()));
}

static void	testOpen(const Config &config)
{
	EventLoop	loop(config);
	EventLoop	second(config);

	std::cout << "-- open" << std::endl;
	check("open() succeeds", loop.open());
	checkCount("one socket per listener", loop.listenerCount(), 2);
	checkCount("no connections yet", loop.connectionCount(), 0);
	check("idle runOnce returns true", loop.runOnce(10));
	check("second loop on the same ports fails", !second.open());
	check("failure names the reason",
		second.error().find("Address already in use") != std::string::npos);
	checkCount("failed open leaves no sockets", second.listenerCount(), 0);
}

static void	testAcceptAndClose(const Config &config)
{
	EventLoop	loop(config);
	int			client;

	std::cout << "-- accept and disconnect" << std::endl;
	loop.open();
	client = connectTo(8800);
	check("client connected", client >= 0);
	spin(loop, 2);
	checkCount("connection accepted", loop.connectionCount(), 1);
	close(client);
	spin(loop, 2);
	checkCount("hang-up removes the connection", loop.connectionCount(), 0);
	check("loop still alive after disconnect", loop.runOnce(10));
}

static void	testRequestResponse(const Config &config)
{
	EventLoop	loop(config);
	std::string	reply;
	int			client;

	std::cout << "-- request / response" << std::endl;
	loop.open();
	client = connectTo(8801);
	spin(loop, 2);
	check("client on the loopback-only listener accepted",
		loop.connectionCount() == 1);
	check("request sent", sendAll(client, "GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
	spin(loop, 4);
	reply = readAll(client);
	check("status line", reply.find("HTTP/1.1 200 OK\r\n") == 0);
	check("body delivered",
		reply.find("\r\n\r\nHello, world!\n") != std::string::npos);
	check("Content-Length matches body",
		reply.find("Content-Length: 14\r\n") != std::string::npos);
	checkCount("server closed after Connection: close", loop.connectionCount(), 0);
	close(client);
}

static void	testPartialRequest(const Config &config)
{
	EventLoop	loop(config);
	std::string	reply;
	char		probe;
	int			client;

	std::cout << "-- request arriving in pieces" << std::endl;
	loop.open();
	client = connectTo(8800);
	spin(loop, 2);
	sendAll(client, "GET / HTT");
	spin(loop, 3);
	checkCount("half a request line: connection kept", loop.connectionCount(), 1);
	check("half a request line: nothing sent back",
		recv(client, &probe, 1, MSG_DONTWAIT) < 0);
	sendAll(client, "P/1.1\r\nHost: x\r\n");
	spin(loop, 3);
	checkCount("headers not finished: still waiting", loop.connectionCount(), 1);
	sendAll(client, "\r\n");
	spin(loop, 4);
	reply = readAll(client);
	check("final CRLF completes the request",
		reply.find("HTTP/1.1 200 OK") == 0);
	close(client);
}

static void	testManyClients(const Config &config)
{
	EventLoop	loop(config);
	int			clients[6];
	int			served;
	int			i;

	std::cout << "-- several clients at once" << std::endl;
	loop.open();
	for (i = 0; i < 6; i++)
		clients[i] = connectTo(i % 2 ? 8800 : 8801);
	spin(loop, 4);
	checkCount("all six accepted", loop.connectionCount(), 6);
	for (i = 0; i < 6; i++)
		sendAll(clients[i], "GET / HTTP/1.1\r\n\r\n");
	spin(loop, 8);
	served = 0;
	for (i = 0; i < 6; i++)
	{
		if (readAll(clients[i]).find("HTTP/1.1 200 OK") == 0)
			served++;
		close(clients[i]);
	}
	checkCount("all six served", served, 6);
	spin(loop, 2);
	checkCount("all six closed", loop.connectionCount(), 0);
}

static void	testIdleTimeout(const Config &config)
{
	EventLoop	loop(config);
	int			client;

	std::cout << "-- idle timeout" << std::endl;
	loop.open();
	client = connectTo(8800);
	spin(loop, 2);
	checkCount("silent client accepted", loop.connectionCount(), 1);
	loop.setIdleTimeout(0);
	usleep(1100000);
	spin(loop, 1);
	checkCount("silent client dropped after the timeout", loop.connectionCount(), 0);
	close(client);
}

static void	testStop(const Config &config)
{
	EventLoop	loop(config);

	std::cout << "-- stop" << std::endl;
	loop.open();
	check("not stopped initially", !EventLoop::stopRequested());
	EventLoop::requestStop();
	check("stop flag visible", EventLoop::stopRequested());
	check("run() returns 0 once stop is requested", loop.run() == 0);
	checkCount("run() released the sockets", loop.listenerCount(), 0);
}

int	main(void)
{
	Config	config;

	if (!config.load("tests/configs/valid/loop.conf"))
	{
		std::cout << "KO   load loop.conf: " << config.error() << std::endl;
		return (1);
	}
	testOpen(config);
	testAcceptAndClose(config);
	testRequestResponse(config);
	testPartialRequest(config);
	testManyClients(config);
	testIdleTimeout(config);
	testStop(config);
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
