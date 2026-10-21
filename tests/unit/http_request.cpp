#include "http/HttpRequest.hpp"

#include <algorithm>
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

// Feed `raw` in pieces of `step` bytes. When the parser stops after the headers,
// apply `limit` and resume - exactly what the event loop will do.
static HttpRequest::State	drive(HttpRequest &req, const std::string &raw,
				size_t limit, size_t step)
{
	HttpRequest::State	state = HttpRequest::Incomplete;

	for (size_t i = 0; i < raw.size(); i += step)
	{
		state = req.feed(raw.data() + i, std::min(step, raw.size() - i));
		if (state == HttpRequest::HeadersDone)
		{
			req.setMaxBodySize(limit);
			state = req.feed(NULL, 0);
		}
		if (state == HttpRequest::Complete || state == HttpRequest::Error)
			break ;
	}
	return (state);
}

// 0 when the request is accepted, otherwise the error status.
static int	statusOf(const std::string &raw, size_t limit)
{
	HttpRequest	req;

	if (drive(req, raw, limit, raw.size()) != HttpRequest::Error)
		return (0);
	return (req.errorStatus());
}

static const std::string	POST = "POST /up HTTP/1.1\r\nHost: a\r\n";

static void	testRequestLineAndHeaders(void)
{
	HttpRequest		req;
	std::string		raw = "GET /a%20b?x=1 HTTP/1.1\r\nHost: Example.com\r\n"
		"X-A: 1\r\nx-a: 2\r\n\r\n";

	check("headers parsed, then paused",
		req.feed(raw.data(), raw.size()) == HttpRequest::HeadersDone);
	check("method", req.method() == "GET");
	check("target kept raw", req.target() == "/a%20b?x=1");
	check("version", req.version() == "HTTP/1.1");
	check("header names are case-insensitive", req.header("HOST") == "Example.com");
	check("repeated header is joined", req.header("x-a") == "1, 2");
	check("missing header", !req.hasHeader("x-none") && req.header("x-none").empty());
	check("GET completes after resume", req.feed(NULL, 0) == HttpRequest::Complete);
}

static void	testPauseBeforeBody(void)
{
	HttpRequest	req;
	std::string	raw = POST + "Content-Length: 2000\r\n\r\nabc";

	check("stops after the headers",
		req.feed(raw.data(), raw.size()) == HttpRequest::HeadersDone);
	req.setMaxBodySize(1000);
	check("over the limit: 413 on resume",
		req.feed(NULL, 0) == HttpRequest::Error && req.errorStatus() == 413);
	check("no body byte was read", req.body().empty());
	check("error is sticky", req.feed("x", 1) == HttpRequest::Error);
}

static void	testSplitAnywhere(void)
{
	const size_t		steps[] = {1, 3, 7, 64};
	const std::string	fixed = POST + "Content-Length: 5\r\n\r\nhello";
	const std::string	chunked = POST + "Transfer-Encoding: chunked\r\n\r\n"
		"5\r\nhello\r\n6;ext=1\r\n world\r\n0\r\nX-T: 1\r\n\r\n";

	for (size_t i = 0; i < 4; i++)
	{
		HttpRequest	a;
		HttpRequest	b;

		check("content-length body, any split",
			drive(a, fixed, 1000, steps[i]) == HttpRequest::Complete
			&& a.body() == "hello");
		check("chunked body + trailers, any split",
			drive(b, chunked, 1000, steps[i]) == HttpRequest::Complete
			&& b.body() == "hello world");
	}
}

static void	testPipelining(void)
{
	HttpRequest	req;
	std::string	both = POST + "Content-Length: 2\r\n\r\nhi"
		"GET /next HTTP/1.1\r\nHost: a\r\n\r\n";

	check("first of two pipelined requests",
		drive(req, both, 1000, both.size()) == HttpRequest::Complete
		&& req.body() == "hi");
	std::string	rest = req.takeLeftover();
	check("leftover is exactly the second request",
		rest == "GET /next HTTP/1.1\r\nHost: a\r\n\r\n");
	req.reset();
	check("second request parses after reset",
		drive(req, rest, 1000, 1) == HttpRequest::Complete
		&& req.target() == "/next");

	HttpRequest	chunked;
	check("chunked leftover kept", drive(chunked, POST
		+ "Transfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\nNEXT", 1000, 1000)
		== HttpRequest::Complete && chunked.takeLeftover() == "NEXT");

	HttpRequest	blank;
	check("blank line before the request line is ignored (RFC 9112 2.2)",
		drive(blank, "\r\nGET / HTTP/1.0\r\n\r\n", 10, 1) == HttpRequest::Complete);
	check("HTTP/1.0 needs no Host", blank.version() == "HTTP/1.0");
}

struct ErrorCase
{
	const char	*name;
	const char	*raw;
	size_t		limit;
	int			status;
};

static const ErrorCase	g_errors[] = {
	{"400 no Host on HTTP/1.1", "GET / HTTP/1.1\r\n\r\n", 1000, 400},
	{"400 request line too short", "GET /\r\n\r\n", 1000, 400},
	{"400 target not starting with /",
		"GET x HTTP/1.1\r\nHost: a\r\n\r\n", 1000, 400},
	{"400 malformed version", "GET / HTTP/x\r\nHost: a\r\n\r\n", 1000, 400},
	{"400 duplicate Host",
		"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 1000, 400},
	{"400 space before colon", "GET / HTTP/1.1\r\nHost : a\r\n\r\n", 1000, 400},
	{"400 folded header",
		"GET / HTTP/1.1\r\nHost: a\r\n b\r\n\r\n", 1000, 400},
	{"400 Content-Length + Transfer-Encoding",
		"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\n"
		"Transfer-Encoding: chunked\r\n\r\n", 1000, 400},
	{"400 duplicate Content-Length",
		"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\n"
		"Content-Length: 3\r\n\r\n", 1000, 400},
	{"400 non-numeric Content-Length",
		"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3x\r\n\r\n", 1000, 400},
	{"400 negative Content-Length",
		"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -3\r\n\r\n", 1000, 400},
	{"400 bad chunk size",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
		"zz\r\n", 1000, 400},
	{"400 missing CRLF after chunk",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
		"3\r\nabcXY0\r\n\r\n", 1000, 400},
	{"413 Content-Length over limit",
		"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 2000\r\n\r\n", 1000, 413},
	{"413 chunk over limit",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
		"8\r\nabcdefgh\r\n0\r\n\r\n", 4, 413},
	{"413 chunks add up past limit",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
		"3\r\nabc\r\n3\r\ndef\r\n0\r\n\r\n", 5, 413},
	{"413 huge Content-Length",
		"POST / HTTP/1.1\r\nHost: a\r\n"
		"Content-Length: 99999999999999999999\r\n\r\n", 1000, 413},
	{"413 huge chunk size",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
		"FFFFFFFFFFFFFFFFFF\r\n", 1000, 413},
	{"501 unknown method", "BREW / HTTP/1.1\r\nHost: a\r\n\r\n", 1000, 501},
	{"501 Transfer-Encoding gzip",
		"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: gzip\r\n\r\n", 1000, 501},
	{"505 HTTP/2.0", "GET / HTTP/2.0\r\nHost: a\r\n\r\n", 1000, 505},
	{NULL, NULL, 0, 0}
};

static void	testErrors(void)
{
	for (size_t i = 0; g_errors[i].name != NULL; i++)
		check(g_errors[i].name,
			statusOf(g_errors[i].raw, g_errors[i].limit) == g_errors[i].status);
	check("414 request line too long",
		statusOf("GET /" + std::string(9000, 'a')
			+ " HTTP/1.1\r\nHost: a\r\n\r\n", 1000) == 414);
	check("414 before any line ending arrives",
		statusOf(std::string(9000, 'a'), 1000) == 414);
	check("431 headers too large",
		statusOf("GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(9000, 'a')
			+ "\r\n\r\n", 1000) == 431);
	check("431 trailers too large",
		statusOf(POST + "Transfer-Encoding: chunked\r\n\r\n0\r\nX: "
			+ std::string(9000, 'a') + "\r\n\r\n", 1000) == 431);
	check("accepted at exactly the limit",
		statusOf(POST + "Content-Length: 4\r\n\r\nabcd", 4) == 0);
	check("leading zeros in Content-Length",
		statusOf(POST + "Content-Length: 0000000003\r\n\r\nabc", 1000) == 0);
}

int	main(void)
{
	testRequestLineAndHeaders();
	testPauseBeforeBody();
	testSplitAnywhere();
	testPipelining();
	testErrors();
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
