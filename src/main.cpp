#include "webserv.hpp"
#include "config/Config.hpp"

#ifdef BONUS
# include "webserv_bonus.hpp"
#endif

static std::string	versionName(void)
{
#ifdef BONUS
	return (bonusVersion());
#else
	return (WEBSERV_NAME);
#endif
}

static std::string	prefix(std::size_t index)
{
	std::ostringstream	out;

	out << "server[" << index << "] ";
	return (out.str());
}

static void	dumpLocation(const std::string &head, const LocationConfig &location)
{
	std::map<std::string, std::string>::const_iterator	it;
	std::string											tag;
	std::size_t											i;

	tag = head + "location[" + location.path + "] ";
	for (i = 0; i < location.allowMethods.size(); i++)
		std::cout << tag << "allow_methods " << location.allowMethods[i] << std::endl;
	std::cout << tag << "root " << location.root << std::endl;
	std::cout << tag << "index " << location.index << std::endl;
	std::cout << tag << "autoindex " << (location.autoindex ? "on" : "off") << std::endl;
	std::cout << tag << "client_max_body_size " << location.clientMaxBodySize << std::endl;
	if (location.uploadsAllowed())
		std::cout << tag << "upload_store " << location.uploadStore << std::endl;
	for (it = location.cgiExt.begin(); it != location.cgiExt.end(); ++it)
		std::cout << tag << "cgi_ext " << it->first << " " << it->second << std::endl;
	if (location.hasRedirect())
	{
		std::cout << tag << "return " << location.redirectCode
			<< " " << location.redirectTarget << std::endl;
	}
}

static void	dumpServer(std::size_t index, const ServerConfig &server)
{
	std::map<int, std::string>::const_iterator	it;
	std::string									head;
	std::size_t									i;

	head = prefix(index);
	for (i = 0; i < server.listens.size(); i++)
		std::cout << head << "listen " << server.listens[i].key() << std::endl;
	for (i = 0; i < server.serverNames.size(); i++)
		std::cout << head << "server_name " << server.serverNames[i] << std::endl;
	std::cout << head << "root " << server.root << std::endl;
	std::cout << head << "index " << server.index << std::endl;
	std::cout << head << "autoindex " << (server.autoindex ? "on" : "off") << std::endl;
	std::cout << head << "client_max_body_size " << server.clientMaxBodySize << std::endl;
	for (it = server.errorPages.begin(); it != server.errorPages.end(); ++it)
		std::cout << head << "error_page " << it->first << " " << it->second << std::endl;
	for (i = 0; i < server.locations.size(); i++)
		dumpLocation(head, server.locations[i]);
}

static void	dumpConfig(const Config &config)
{
	std::size_t	i;

	for (i = 0; i < config.servers().size(); i++)
		dumpServer(i, config.servers()[i]);
	for (i = 0; i < config.listeners().size(); i++)
		std::cout << "listener " << config.listeners()[i].key() << std::endl;
}

int	main(int argc, char **argv)
{
	std::string	configPath;
	Config		config;

	if (argc > 2)
	{
		std::cerr << "usage: ./" << WEBSERV_NAME
			<< " [configuration file]" << std::endl;
		return (1);
	}
	configPath = (argc == 2) ? argv[1] : DEFAULT_CONFIG;
	if (!config.load(configPath))
	{
		std::cerr << WEBSERV_NAME << ": " << config.error() << std::endl;
		return (1);
	}
	std::cout << versionName() << std::endl;
	std::cout << "config: " << configPath << std::endl;
	dumpConfig(config);
	return (0);
}
