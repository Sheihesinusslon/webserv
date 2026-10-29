#include "http/RequestHandler.hpp"
#include "webserv.hpp"

#include <cctype>
#include <sstream>

static std::string	toLower(std::string s)
{
	for (size_t i = 0; i < s.size(); i++)
		s[i] = std::tolower(static_cast<unsigned char>(s[i]));
	return (s);
}

static std::string	trim(const std::string &s)
{
	size_t	begin = s.find_first_not_of(" \t");
	if (begin == std::string::npos)
		return ("");
	return (s.substr(begin, s.find_last_not_of(" \t") - begin + 1));
}

// Connection is a comma-separated list of tokens: "keep-alive, Upgrade"
static bool	hasToken(const std::string &list, const std::string &token)
{
	std::string	lower = toLower(list);
	size_t		start = 0;

	while (start <= lower.size())
	{
		size_t	comma = lower.find(',', start);
		if (comma == std::string::npos)
			comma = lower.size();
		if (trim(lower.substr(start, comma - start)) == token)
			return (true);
		start = comma + 1;
	}
	return (false);
}

// RFC 9112 section 9.3: HTTP/1.1 stays open unless asked to close, HTTP/1.0
// closes unless asked to stay open. After a parse error the stream position
// is unknown, so the connection always closes.
static bool	closeAfter(const HttpRequest &request)
{
	const std::string	&connection = request.header("connection");

	if (request.errorStatus() != 0 || hasToken(connection, "close"))
		return (true);
	if (request.version() == "HTTP/1.1")
		return (false);
	return (!hasToken(connection, "keep-alive"));
}

static std::string	reasonPhrase(int code)
{
	switch (code)
	{
		case 200:	return ("OK");
		case 400:	return ("Bad Request");
		case 413:	return ("Content Too Large");
		case 414:	return ("URI Too Long");
		case 431:	return ("Request Header Fields Too Large");
		case 501:	return ("Not Implemented");
		case 502:	return ("Bad Gateway");
		case 504:	return ("Gateway Timeout");
		case 505:	return ("HTTP Version Not Supported");
	}
	return ("Error");
}

static HandlerResult	respond(int code, bool close)
{
	HandlerResult		result;
	std::ostringstream	out;
	std::string			body;

	if (code == 200)
		body = "Hello, world!\n";
	else
	{
		std::ostringstream	text;
		text << code << " " << reasonPhrase(code) << "\n";
		body = text.str();
	}
	out << "HTTP/1.1 " << code << " " << reasonPhrase(code) << "\r\n"
		<< "Content-Type: text/plain\r\n"
		<< "Content-Length: " << body.size() << "\r\n"
		<< "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
		<< "\r\n" << body;
	result.kind = HandlerResult::Respond;
	result.response = out.str();
	result.close = close;
	return (result);
}

CgiProcess::CgiProcess() : pid(-1), input(-1), output(-1)
{
}

HandlerResult::HandlerResult() : kind(Respond), close(true)
{
}

RequestHandler::RequestHandler(const Config &config) : _config(config)
{
}

size_t	RequestHandler::bodyLimit(const HttpRequest &request,
			const Client &client) const
{
	const ServerConfig		*server;
	const LocationConfig	*location;

	server = _config.matchServer(client.local, request.header("host"));
	if (server == NULL)
		return (DEFAULT_CLIENT_MAX_BODY_SIZE);
	location = server->matchLocation(
			request.target().substr(0, request.target().find('?')));
	if (location == NULL)
		return (server->clientMaxBodySize);
	return (location->clientMaxBodySize);
}

// Stub
HandlerResult	RequestHandler::handle(const HttpRequest &request,
					const Client &client) const
{
	(void)client;
	if (request.errorStatus() != 0)
		return (respond(request.errorStatus(), true));
	return (respond(200, closeAfter(request)));
}

// Stub
HandlerResult	RequestHandler::finishCgi(const HttpRequest &request,
					const Client &client, const std::string &output,
					bool timedOut) const
{
	(void)client;
	(void)output;
	if (timedOut)
		return (respond(504, closeAfter(request)));
	return (respond(502, closeAfter(request)));
}
