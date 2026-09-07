#include "config/ConfigParser.hpp"
#include "webserv.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>

static std::string	toString(std::size_t value)
{
	std::ostringstream	out;

	out << value;
	return (out.str());
}

static bool	isNumber(const std::string &text)
{
	std::size_t	i;

	if (text.empty())
		return (false);
	for (i = 0; i < text.size(); i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(text[i])))
			return (false);
	}
	return (true);
}

static bool	parseSize(const std::string &text, std::size_t &out)
{
	std::size_t	i;
	std::size_t	value;
	std::size_t	digit;

	if (!isNumber(text))
		return (false);
	value = 0;
	for (i = 0; i < text.size(); i++)
	{
		digit = static_cast<std::size_t>(text[i] - '0');
		if (value > (static_cast<std::size_t>(-1) - digit) / 10)
			return (false);
		value = value * 10 + digit;
	}
	out = value;
	return (true);
}

static bool	parsePort(const std::string &text, int &out)
{
	std::size_t	value;

	if (!parseSize(text, value))
		return (false);
	if (value < MIN_PORT || value > MAX_PORT)
		return (false);
	out = static_cast<int>(value);
	return (true);
}

static bool	parseStatusCode(const std::string &text, int &out)
{
	std::size_t	value;

	if (!parseSize(text, value))
		return (false);
	if (value < MIN_STATUS_CODE || value > MAX_STATUS_CODE)
		return (false);
	out = static_cast<int>(value);
	return (true);
}

static bool	parseBool(const std::string &text, bool &out)
{
	if (text == "on")
	{
		out = true;
		return (true);
	}
	if (text == "off")
	{
		out = false;
		return (true);
	}
	return (false);
}

static bool	isValidHost(const std::string &host)
{
	std::size_t	zone;
	std::size_t	i;

	if (host.empty())
		return (false);
	if (host[0] == '[')
	{
		if (host.size() < 3 || host[host.size() - 1] != ']')
			return (false);
		zone = host.find('%');
		if (zone == std::string::npos)
			zone = host.size() - 1;
		for (i = 1; i < zone; i++)
		{
			if (!std::isxdigit(static_cast<unsigned char>(host[i]))
				&& host[i] != ':' && host[i] != '.')
				return (false);
		}
		for (i = zone + 1; i + 1 < host.size(); i++)
		{
			if (!std::isalnum(static_cast<unsigned char>(host[i]))
				&& host[i] != '-' && host[i] != '_')
				return (false);
		}
		return (zone > 1);
	}
	for (i = 0; i < host.size(); i++)
	{
		if (!std::isalnum(static_cast<unsigned char>(host[i]))
			&& host[i] != '.' && host[i] != '-'
			&& host[i] != '_' && host[i] != '~')
			return (false);
	}
	return (true);
}

static bool	parseListenValue(const std::string &text, Listener &out)
{
	std::size_t	colon;

	if (!text.empty() && text[0] == '[')
	{
		colon = text.find(']');
		if (colon == std::string::npos)
			return (false);
		colon++;
		out.host = text.substr(0, colon);
		if (!isValidHost(out.host))
			return (false);
		if (colon == text.size())
		{
			out.port = DEFAULT_PORT;
			return (true);
		}
		if (text[colon] != ':')
			return (false);
		return (parsePort(text.substr(colon + 1), out.port));
	}
	colon = text.rfind(':');
	if (colon == std::string::npos)
	{
		if (isNumber(text))
		{
			out.host = DEFAULT_HOST;
			return (parsePort(text, out.port));
		}
		out.host = text;
		out.port = DEFAULT_PORT;
		return (isValidHost(out.host));
	}
	out.host = text.substr(0, colon);
	if (out.host.empty())
		out.host = DEFAULT_HOST;
	if (!isValidHost(out.host))
		return (false);
	return (parsePort(text.substr(colon + 1), out.port));
}

static bool	isKnownMethod(const std::string &method)
{
	return (method == METHOD_GET || method == METHOD_POST
		|| method == METHOD_DELETE);
}

ConfigParser::ConfigParser() : _tokens(NULL), _pos(0)
{
}

const std::string	&ConfigParser::error() const
{
	return (_error);
}

bool	ConfigParser::atEnd() const
{
	return (_pos >= _tokens->size());
}

const Token	&ConfigParser::peek() const
{
	return ((*_tokens)[_pos]);
}

bool	ConfigParser::fail(const std::string &message)
{
	_error = message;
	return (false);
}

bool	ConfigParser::failAt(std::size_t line, const std::string &message)
{
	_error = "line " + toString(line) + ": " + message;
	return (false);
}

bool	ConfigParser::expectType(Token::Type type, const std::string &what)
{
	if (atEnd())
		return (fail("unexpected end of file, expected " + what));
	if (peek().type != type)
	{
		return (failAt(peek().line, "expected " + what
				+ " but found '" + peek().text + "'"));
	}
	_pos++;
	return (true);
}

bool	ConfigParser::expectWord(std::string &out, const std::string &what)
{
	if (atEnd())
		return (fail("unexpected end of file, expected " + what));
	if (peek().type != Token::Word)
	{
		return (failAt(peek().line, "expected " + what
				+ " but found '" + peek().text + "'"));
	}
	out = peek().text;
	_pos++;
	return (true);
}

bool	ConfigParser::collectArgs(std::vector<std::string> &args)
{
	while (!atEnd() && peek().type == Token::Word)
	{
		args.push_back(peek().text);
		_pos++;
	}
	return (expectType(Token::Semicolon, "';'"));
}

bool	ConfigParser::needArgs(const std::string &name,
			const std::vector<std::string> &args,
			std::size_t min, std::size_t max, std::size_t line)
{
	std::string	expected;

	if (args.size() >= min && args.size() <= max)
		return (true);
	if (min == max)
		expected = toString(min);
	else
		expected = toString(min) + " to " + toString(max);
	return (failAt(line, "'" + name + "' expects " + expected
			+ " argument(s), got " + toString(args.size())));
}

bool	ConfigParser::parse(const std::vector<Token> &tokens,
			std::vector<ServerConfig> &servers)
{
	_tokens = &tokens;
	_pos = 0;
	_error.clear();
	servers.clear();
	while (!atEnd())
	{
		if (peek().type != Token::Word || peek().text != "server")
		{
			return (failAt(peek().line, "expected a 'server' block but found '"
					+ peek().text + "'"));
		}
		_pos++;
		servers.push_back(ServerConfig());
		if (!parseServer(servers.back()))
			return (false);
	}
	if (servers.empty())
		return (fail("no server block defined"));
	return (true);
}

bool	ConfigParser::parseServer(ServerConfig &server)
{
	std::string					name;
	std::vector<std::string>	args;
	std::size_t					line;

	if (!expectType(Token::BlockStart, "'{' after 'server'"))
		return (false);
	while (!atEnd() && peek().type != Token::BlockEnd)
	{
		line = peek().line;
		if (!expectWord(name, "a directive name"))
			return (false);
		if (name == "location")
		{
			server.locations.push_back(LocationConfig());
			if (!parseLocation(server.locations.back()))
				return (false);
			continue;
		}
		args.clear();
		if (!collectArgs(args))
			return (false);
		if (!applyServerDirective(server, name, args, line))
			return (false);
	}
	return (expectType(Token::BlockEnd, "'}' to close 'server'"));
}

bool	ConfigParser::parseLocation(LocationConfig &location)
{
	std::string					name;
	std::vector<std::string>	args;
	std::size_t					line;

	line = atEnd() ? 0 : peek().line;
	if (!expectWord(location.path, "a location path"))
		return (false);
	if (location.path[0] != '/')
		return (failAt(line, "location path must start with '/'"));
	while (location.path.size() > 1
		&& location.path[location.path.size() - 1] == '/')
		location.path.erase(location.path.size() - 1);
	if (!expectType(Token::BlockStart, "'{' after location path"))
		return (false);
	while (!atEnd() && peek().type != Token::BlockEnd)
	{
		line = peek().line;
		if (!expectWord(name, "a directive name"))
			return (false);
		if (name == "location")
			return (failAt(line, "nested location blocks are not supported"));
		args.clear();
		if (!collectArgs(args))
			return (false);
		if (!applyLocationDirective(location, name, args, line))
			return (false);
	}
	return (expectType(Token::BlockEnd, "'}' to close 'location'"));
}

bool	ConfigParser::applyServerDirective(ServerConfig &server,
			const std::string &name, const std::vector<std::string> &args,
			std::size_t line)
{
	Listener	listener;
	int			code;
	std::size_t	i;

	if (name == "listen")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		if (!parseListenValue(args[0], listener))
			return (failAt(line, "invalid listen value '" + args[0] + "'"));
		if (!server.listensOn(listener))
			server.listens.push_back(listener);
		return (true);
	}
	if (name == "server_name")
	{
		if (!needArgs(name, args, 1, MAX_SERVER_NAMES, line))
			return (false);
		for (i = 0; i < args.size(); i++)
			server.serverNames.push_back(args[i]);
		return (true);
	}
	if (name == "root")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		server.root = args[0];
		return (true);
	}
	if (name == "index")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		server.index = args[0];
		return (true);
	}
	if (name == "autoindex")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		if (!parseBool(args[0], server.autoindex))
			return (failAt(line, "'autoindex' expects 'on' or 'off', got '"
					+ args[0] + "'"));
		return (true);
	}
	if (name == "client_max_body_size")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		if (!parseSize(args[0], server.clientMaxBodySize))
			return (failAt(line, "invalid size '" + args[0] + "'"));
		return (true);
	}
	if (name == "error_page")
	{
		if (!needArgs(name, args, 2, 2, line))
			return (false);
		if (!parseStatusCode(args[0], code))
			return (failAt(line, "invalid status code '" + args[0] + "'"));
		server.errorPages[code] = args[1];
		return (true);
	}
	return (failAt(line, "unknown directive '" + name + "' in server block"));
}

bool	ConfigParser::applyLocationDirective(LocationConfig &location,
			const std::string &name, const std::vector<std::string> &args,
			std::size_t line)
{
	std::size_t	i;

	if (name == "allow_methods")
	{
		if (!needArgs(name, args, 1, MAX_ALLOW_METHODS, line))
			return (false);
		for (i = 0; i < args.size(); i++)
		{
			if (!isKnownMethod(args[i]))
				return (failAt(line, "unsupported method '" + args[i] + "'"));
			if (!location.isMethodAllowed(args[i]))
				location.allowMethods.push_back(args[i]);
		}
		return (true);
	}
	if (name == "root")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		location.root = args[0];
		location.hasRoot = true;
		return (true);
	}
	if (name == "index")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		location.index = args[0];
		location.hasIndex = true;
		return (true);
	}
	if (name == "autoindex")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		if (!parseBool(args[0], location.autoindex))
			return (failAt(line, "'autoindex' expects 'on' or 'off', got '"
					+ args[0] + "'"));
		location.hasAutoindex = true;
		return (true);
	}
	if (name == "client_max_body_size")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		if (!parseSize(args[0], location.clientMaxBodySize))
			return (failAt(line, "invalid size '" + args[0] + "'"));
		location.hasClientMaxBodySize = true;
		return (true);
	}
	if (name == "upload_store")
	{
		if (!needArgs(name, args, 1, 1, line))
			return (false);
		location.uploadStore = args[0];
		return (true);
	}
	if (name == "cgi_ext")
	{
		if (!needArgs(name, args, 2, 2, line))
			return (false);
		if (args[0].empty() || args[0][0] != '.')
			return (failAt(line, "cgi extension must start with '.', got '"
					+ args[0] + "'"));
		location.cgiExt[args[0]] = args[1];
		return (true);
	}
	if (name == "return")
	{
		if (!needArgs(name, args, 2, 2, line))
			return (false);
		if (!parseStatusCode(args[0], location.redirectCode))
			return (failAt(line, "invalid redirect code '" + args[0] + "'"));
		location.redirectTarget = args[1];
		return (true);
	}
	return (failAt(line, "unknown directive '" + name + "' in location block"));
}
