#include "config/Config.hpp"
#include "config/ConfigParser.hpp"
#include "config/ConfigTokenizer.hpp"
#include "webserv.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

static std::string	toLower(const std::string &text)
{
	std::string	out;
	std::size_t	i;

	out = text;
	for (i = 0; i < out.size(); i++)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return (out);
}

/*
** Drops the optional ":port" from a Host header value.  A host may be an IPv6
** literal in brackets ("[::1]", "[::1]:8080"), where only a colon that follows
** the closing bracket separates the port; the colons inside must be kept.
*/
static std::string	stripPort(const std::string &hostHeader)
{
	std::string	host;
	std::size_t	colon;

	host = hostHeader;
	if (!host.empty() && host[0] == '[')
	{
		colon = host.find(']');
		if (colon == std::string::npos)
			return (host);
		colon++;
		if (colon < host.size() && host[colon] == ':')
			host.erase(colon);
		return (host);
	}
	colon = host.rfind(':');
	if (colon != std::string::npos)
		host.erase(colon);
	return (host);
}

Config::Config()
{
}

const std::vector<ServerConfig>	&Config::servers() const
{
	return (_servers);
}

const std::vector<Listener>	&Config::listeners() const
{
	return (_listeners);
}

const std::string	&Config::error() const
{
	return (_error);
}

bool	Config::load(const std::string &path)
{
	ConfigTokenizer	tokenizer;
	ConfigParser	parser;

	_servers.clear();
	_listeners.clear();
	_error.clear();
	if (!tokenizer.tokenize(path))
	{
		_error = tokenizer.error();
		return (false);
	}
	if (!parser.parse(tokenizer.tokens(), _servers))
	{
		_error = path + ": " + parser.error();
		return (false);
	}
	if (!validate(path))
		return (false);
	normalizeNames();
	inherit();
	collectListeners();
	return (true);
}

bool	Config::validate(const std::string &path)
{
	std::size_t	i;
	std::size_t	j;
	std::size_t	k;

	for (i = 0; i < _servers.size(); i++)
	{
		if (_servers[i].listens.empty())
		{
			_error = path + ": server block has no 'listen' directive";
			return (false);
		}
		for (j = 0; j < _servers[i].locations.size(); j++)
		{
			for (k = j + 1; k < _servers[i].locations.size(); k++)
			{
				if (_servers[i].locations[j].path
					== _servers[i].locations[k].path)
				{
					_error = path + ": duplicate location '"
						+ _servers[i].locations[j].path + "'";
					return (false);
				}
			}
		}
	}
	return (true);
}

void	Config::normalizeNames()
{
	std::size_t	i;
	std::size_t	j;

	for (i = 0; i < _servers.size(); i++)
	{
		for (j = 0; j < _servers[i].serverNames.size(); j++)
			_servers[i].serverNames[j] = toLower(_servers[i].serverNames[j]);
	}
}

void	Config::inherit()
{
	std::size_t	i;
	std::size_t	j;

	for (i = 0; i < _servers.size(); i++)
	{
		if (_servers[i].index.empty())
			_servers[i].index = DEFAULT_INDEX;
		for (j = 0; j < _servers[i].locations.size(); j++)
		{
			LocationConfig	&location = _servers[i].locations[j];

			if (!location.hasRoot)
				location.root = _servers[i].root;
			if (!location.hasIndex)
				location.index = _servers[i].index;
			if (!location.hasAutoindex)
				location.autoindex = _servers[i].autoindex;
			if (!location.hasClientMaxBodySize)
				location.clientMaxBodySize = _servers[i].clientMaxBodySize;
		}
	}
}

void	Config::collectListeners()
{
	std::size_t	i;
	std::size_t	j;

	for (i = 0; i < _servers.size(); i++)
	{
		for (j = 0; j < _servers[i].listens.size(); j++)
		{
			if (std::find(_listeners.begin(), _listeners.end(),
					_servers[i].listens[j]) == _listeners.end())
				_listeners.push_back(_servers[i].listens[j]);
		}
	}
}

const ServerConfig	*Config::matchServer(const Listener &listener,
						const std::string &hostHeader) const
{
	const ServerConfig	*fallback;
	std::string			host;
	std::size_t			i;

	host = toLower(stripPort(hostHeader));
	fallback = NULL;
	for (i = 0; i < _servers.size(); i++)
	{
		if (!_servers[i].listensOn(listener))
			continue;
		if (fallback == NULL)
			fallback = &_servers[i];
		if (_servers[i].hasServerName(host))
			return (&_servers[i]);
	}
	return (fallback);
}
