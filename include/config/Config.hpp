#ifndef CONFIG_HPP
# define CONFIG_HPP

# include <string>
# include <vector>

# include "config/Listener.hpp"
# include "config/ServerConfig.hpp"

class Config
{
public:
	Config();

	bool	load(const std::string &path);

	const std::vector<ServerConfig>	&servers() const;
	const std::vector<Listener>		&listeners() const;
	const std::string				&error() const;

	const ServerConfig	*matchServer(const Listener &local,
							const std::string &hostHeader) const;

private:
	bool	resolveListens(const std::string &path);
	bool	validate(const std::string &path);
	bool	checkServerCollisions(const std::string &path);
	void	normalizeNames();
	void	inherit();
	void	collectListeners();
	bool	hasServerOn(const Listener &listener) const;

	std::vector<ServerConfig>	_servers;
	std::vector<Listener>		_listeners;
	std::string					_error;
};

#endif
