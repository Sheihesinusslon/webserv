#include "http/RequestHandler.hpp"
#include "webserv.hpp"

#include <iostream>
#include <string>

static int	g_passed = 0;
static int	g_failed = 0;

static void	check(const std::string &name, bool condition)
{
	if (condition)
	{
		std::cout << "OK   " << name << std::endl;
		g_passed++;
	}
	else
	{
		std::cout << "KO   " << name << std::endl;
		g_failed++;
	}
}

static bool	startsWith(const std::string &s, const std::string &prefix)
{
	return (s.compare(0, prefix.size(), prefix) == 0);
}

static bool	contains(const std::string &s, const std::string &needle)
{
	return (s.find(needle) != std::string::npos);
}

// The client reached the specific address; handler.conf only listens on the
// wildcard of that port, so matchServer has to fall back to it.
static Client	clientOn(int port)
{
	Client	client;

	client.local = Listener("127.0.0.1", port);
	client.peer = Listener("127.0.0.1", 54321);
	return (client);
}

// Feed the whole request the way the event loop does: ask the handler for the
// body limit at HeadersDone, then resume.
static HttpRequest::State	drive(HttpRequest &request,
				const RequestHandler &handler, const Client &client,
				const std::string &raw)
{
	HttpRequest::State	state = request.feed(raw.data(), raw.size());

	if (state == HttpRequest::HeadersDone)
	{
		request.setMaxBodySize(handler.bodyLimit(request, client));
		state = request.feed(NULL, 0);
	}
	return (state);
}

static size_t	limitFor(const RequestHandler &handler, const Client &client,
				const std::string &head)
{
	HttpRequest	request;
	std::string	raw = head + "\r\n";

	if (request.feed(raw.data(), raw.size()) != HttpRequest::HeadersDone)
		return (0);
	return (handler.bodyLimit(request, client));
}

static HandlerResult	answer(const RequestHandler &handler, const std::string &raw)
{
	HttpRequest	request;
	Client		client = clientOn(8900);

	drive(request, handler, client, raw);
	return (handler.handle(request, client));
}

static void	testDefaults(void)
{
	HandlerResult	result;

	std::cout << "-- result defaults" << std::endl;
	check("a fresh result is a response", result.kind == HandlerResult::Respond);
	check("and closes by default", result.close);
	check("no CGI pid", result.cgi.pid == -1);
	check("no CGI fds", result.cgi.input == -1 && result.cgi.output == -1);
}

static void	testBodyLimit(const RequestHandler &handler)
{
	Client	client = clientOn(8900);

	std::cout << "-- bodyLimit picks server and location" << std::endl;
	check("named server, catch-all location",
		limitFor(handler, client, "POST /x HTTP/1.1\r\nHost: big.test\r\n") == 5000);
	check("named server, own location limit",
		limitFor(handler, client, "POST /upload/a HTTP/1.1\r\nHost: big.test\r\n") == 9000);
	check("query string ignored when matching",
		limitFor(handler, client, "POST /upload?x=/y HTTP/1.1\r\nHost: big.test\r\n") == 9000);
	check("location prefix ends on a boundary",
		limitFor(handler, client, "POST /uploadx HTTP/1.1\r\nHost: big.test\r\n") == 5000);
	check("Host is case-insensitive and its port dropped",
		limitFor(handler, client, "POST /upload HTTP/1.1\r\nHost: BIG.test:8900\r\n") == 9000);
	check("unknown Host goes to the nameless server",
		limitFor(handler, client, "POST /api HTTP/1.1\r\nHost: nope.test\r\n") == 100);
	check("no matching location: server limit",
		limitFor(handler, client, "POST /x HTTP/1.1\r\nHost: nope.test\r\n") == 100);
	check("HTTP/1.0 without Host: nameless server",
		limitFor(handler, client, "POST /x HTTP/1.0\r\n") == 100);
	check("nobody listens there: built-in default",
		limitFor(handler, clientOn(9999), "POST /x HTTP/1.1\r\nHost: big.test\r\n")
			== DEFAULT_CLIENT_MAX_BODY_SIZE);
}

static void	testLimitEnforced(const RequestHandler &handler)
{
	HttpRequest	request;
	Client		client = clientOn(8900);

	std::cout << "-- the limit from bodyLimit reaches the parser" << std::endl;
	check("under the location limit is accepted",
		drive(request, handler, client, "POST /upload HTTP/1.1\r\nHost: big.test\r\n"
			"Content-Length: 6000\r\n\r\n" + std::string(6000, 'a'))
			== HttpRequest::Complete);
	request.reset();
	check("over the server limit is refused",
		drive(request, handler, client, "POST /x HTTP/1.1\r\nHost: big.test\r\n"
			"Content-Length: 6000\r\n\r\n") == HttpRequest::Error);
	check("with 413", request.errorStatus() == 413);
}

static void	testKeepAlive(const RequestHandler &handler)
{
	HandlerResult	result;

	std::cout << "-- close after send" << std::endl;
	result = answer(handler, "GET / HTTP/1.1\r\nHost: big.test\r\n\r\n");
	check("complete request gets a response", result.kind == HandlerResult::Respond);
	check("HTTP/1.1 stays open", !result.close);
	check("and says so", contains(result.response, "\r\nConnection: keep-alive\r\n"));
	result = answer(handler, "GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n");
	check("HTTP/1.1 with Connection: close closes", result.close);
	check("and says so", contains(result.response, "\r\nConnection: close\r\n"));
	result = answer(handler, "GET / HTTP/1.1\r\nHost: a\r\nConnection: Upgrade, CLOSE\r\n\r\n");
	check("close found in a token list, any case", result.close);
	result = answer(handler, "GET / HTTP/1.1\r\nHost: a\r\nConnection: closed\r\n\r\n");
	check("a token merely starting with close is not close", !result.close);
	result = answer(handler, "GET / HTTP/1.0\r\n\r\n");
	check("HTTP/1.0 closes", result.close);
	result = answer(handler, "GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n");
	check("HTTP/1.0 with keep-alive stays open", !result.close);
}

static void	testErrors(const RequestHandler &handler)
{
	HandlerResult	result;

	std::cout << "-- a request the parser refused" << std::endl;
	result = answer(handler, "GET / HTTP/1.1\r\n\r\n");
	check("is answered", result.kind == HandlerResult::Respond);
	check("with its error status", startsWith(result.response, "HTTP/1.1 400 "));
	check("and always closes", result.close);
	check("even when it asked for keep-alive",
		answer(handler, "GET / HTTP/1.0\r\nConnection: keep-alive\r\n"
			"Content-Length: x\r\n\r\n").close);
	result = answer(handler, "POST /x HTTP/1.1\r\nHost: big.test\r\n"
		"Content-Length: 6000\r\n\r\n");
	check("413 from the location limit", startsWith(result.response, "HTTP/1.1 413 "));
	result = answer(handler, "GET / HTTP/2.0\r\nHost: a\r\n\r\n");
	check("505 for an unsupported version", startsWith(result.response, "HTTP/1.1 505 "));
}

static void	testFinishCgi(const RequestHandler &handler)
{
	HttpRequest		request;
	Client			client = clientOn(8900);
	HandlerResult	result;

	std::cout << "-- finishCgi" << std::endl;
	drive(request, handler, client, "GET /cgi HTTP/1.1\r\nHost: big.test\r\n\r\n");
	result = handler.finishCgi(request, client, "", true);
	check("timed out script: a response", result.kind == HandlerResult::Respond);
	check("timed out script: 504", startsWith(result.response, "HTTP/1.1 504 "));
	result = handler.finishCgi(request, client, "", false);
	check("script with no output: 502", startsWith(result.response, "HTTP/1.1 502 "));
	check("keeps the request's keep-alive", !result.close);
}

int	main(void)
{
	Config	config;

	if (!config.load("tests/configs/valid/handler.conf"))
	{
		std::cout << "KO   load handler.conf: " << config.error() << std::endl;
		return (1);
	}
	RequestHandler	handler(config);

	testDefaults();
	testBodyLimit(handler);
	testLimitEnforced(handler);
	testKeepAlive(handler);
	testErrors(handler);
	testFinishCgi(handler);
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
