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

static std::string	normalizePath(const std::string &raw)
{
	std::vector<std::string>	parts;
	std::string					segment;
	std::string					out;
	std::size_t					i;
	std::size_t					start;
	bool						absolute;

	absolute = (!raw.empty() && raw[0] == '/');
	start = 0;
	for (i = 0; i <= raw.size(); i++)
	{
		if (i != raw.size() && raw[i] != '/')
			continue;
		segment = raw.substr(start, i - start);
		if (segment == "..")
		{
			if (!parts.empty())
				parts.pop_back();
		}
		else if (!segment.empty() && segment != ".")
			parts.push_back(segment);
		start = i + 1;
	}
	for (i = 0; i < parts.size(); i++)
	{
		out += "/";
		out += parts[i];
	}
	if (absolute && out.empty())
		return ("/");
	if (!absolute && !out.empty())
		out.erase(0, 1);
	return (out);
}

static bool	isInside(const std::string &full, const std::string &base)
{
	if (base == "/")
		return (!full.empty() && full[0] == '/');
	if (full.size() < base.size())
		return (false);
	if (full.compare(0, base.size(), base) != 0)
		return (false);
	if (full.size() > base.size() && full[base.size()] != '/')
		return (false);
	return (true);
}

std::string	LocationConfig::resolvePath(const std::string &uri) const
{
	std::string	base;
	std::string	rest;
	std::string	full;

	if (root.empty() || path.empty())
		return ("");
	if (uri.size() < path.size() || uri.compare(0, path.size(), path) != 0)
		return ("");
	if (path.size() > 1 && uri.size() > path.size() && uri[path.size()] != '/')
		return ("");
	base = normalizePath(root);
	if (base.empty())
		return ("");
	rest = uri.substr(path.size());
	if (rest.empty())
		full = base;
	else if (rest[0] == '/')
		full = normalizePath(base + rest);
	else
		full = normalizePath(base + "/" + rest);
	if (!isInside(full, base))
		return ("");
	return (full);
}
