#include "config/ServerConfig.hpp"
#include "webserv.hpp"

#include <algorithm>

ServerConfig::ServerConfig()
	: root(""),
	  index(""),
	  autoindex(false),
	  clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE)
{
}

const LocationConfig	*ServerConfig::matchLocation(const std::string &path) const
{
	const LocationConfig	*best;
	std::size_t				bestLength;
	std::size_t				i;
	std::size_t				length;

	best = NULL;
	bestLength = 0;
	for (i = 0; i < locations.size(); i++)
	{
		length = locations[i].path.size();
		if (path.compare(0, length, locations[i].path) != 0)
			continue;
		if (length > 1 && path.size() > length && path[length] != '/')
			continue;
		if (best == NULL || length > bestLength)
		{
			best = &locations[i];
			bestLength = length;
		}
	}
	return (best);
}

std::string	ServerConfig::errorPage(int code) const
{
	std::map<int, std::string>::const_iterator	it;

	it = errorPages.find(code);
	if (it == errorPages.end())
		return ("");
	return (it->second);
}

bool	ServerConfig::hasServerName(const std::string &name) const
{
	return (std::find(serverNames.begin(), serverNames.end(), name)
		!= serverNames.end());
}

bool	ServerConfig::listensOn(const Listener &listener) const
{
	return (std::find(listens.begin(), listens.end(), listener)
		!= listens.end());
}
