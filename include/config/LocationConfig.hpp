#ifndef LOCATIONCONFIG_HPP
# define LOCATIONCONFIG_HPP

# include <cstddef>
# include <map>
# include <string>
# include <vector>

struct LocationConfig
{
	LocationConfig();

	std::string							path;
	std::vector<std::string>			allowMethods;
	std::string							root;
	std::string							index;
	bool								autoindex;
	std::size_t							clientMaxBodySize;
	std::string							uploadStore;
	std::map<std::string, std::string>	cgiExt;
	int									redirectCode;
	std::string							redirectTarget;

	bool	hasRoot;
	bool	hasIndex;
	bool	hasAutoindex;
	bool	hasClientMaxBodySize;

	bool		isMethodAllowed(const std::string &method) const;
	bool		hasRedirect() const;
	bool		uploadsAllowed() const;
	std::string	cgiInterpreter(const std::string &extension) const;
	std::string	resolvePath(const std::string &uri) const;
};

#endif
