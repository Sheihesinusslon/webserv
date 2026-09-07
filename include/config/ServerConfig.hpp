#ifndef SERVERCONFIG_HPP
# define SERVERCONFIG_HPP

# include <cstddef>
# include <map>
# include <string>
# include <vector>

# include "config/Listener.hpp"
# include "config/LocationConfig.hpp"

struct ServerConfig
{
	ServerConfig();

	std::vector<Listener>		listens;
	std::vector<std::string>	serverNames;
	std::string					root;
	std::string					index;
	bool						autoindex;
	std::size_t					clientMaxBodySize;
	std::map<int, std::string>	errorPages;
	std::vector<LocationConfig>	locations;

	const LocationConfig	*matchLocation(const std::string &path) const;
	std::string				errorPage(int code) const;
	bool					hasServerName(const std::string &name) const;
	bool					listensOn(const Listener &listener) const;
};

#endif
