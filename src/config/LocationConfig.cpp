#include "config/LocationConfig.hpp"
#include "webserv.hpp"

#include <algorithm>

LocationConfig::LocationConfig()
	: path(""),
	  root(""),
	  index(""),
	  autoindex(false),
	  clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE),
	  uploadStore(""),
	  redirectCode(0),
	  redirectTarget(""),
	  hasRoot(false),
	  hasIndex(false),
	  hasAutoindex(false),
	  hasClientMaxBodySize(false)
{
}

bool	LocationConfig::isMethodAllowed(const std::string &method) const
{
	return (std::find(allowMethods.begin(), allowMethods.end(), method)
		!= allowMethods.end());
}

bool	LocationConfig::hasRedirect() const
{
	return (redirectCode != 0);
}

bool	LocationConfig::uploadsAllowed() const
{
	return (!uploadStore.empty());
}

std::string	LocationConfig::cgiInterpreter(const std::string &extension) const
{
	std::map<std::string, std::string>::const_iterator	it;

	it = cgiExt.find(extension);
	if (it == cgiExt.end())
		return ("");
	return (it->second);
}
