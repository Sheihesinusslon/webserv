#include "webserv.hpp"
#include "config/Config.hpp"
#include "config/ConfigDump.hpp"
#include "net/EventLoop.hpp"

#include <exception>

#ifdef BONUS
# include "webserv_bonus.hpp"
#endif

enum RunMode
{
	RunServer,
	CheckConfig,
	DumpConfig
};

static int	usage(void)
{
	std::cerr << "usage: ./" << WEBSERV_NAME << " [-t | -T] [configuration file]"
		<< std::endl;
	return (1);
}

static bool	parseArgs(int argc, char **argv, RunMode &mode, std::string &path)
{
	std::string	arg;
	bool		pathSet;
	int			i;

	mode = RunServer;
	path = DEFAULT_CONFIG;
	pathSet = false;
	for (i = 1; i < argc; i++)
	{
		arg = argv[i];
		if (arg == "-t" && mode == RunServer)
			mode = CheckConfig;
		else if (arg == "-T" && mode == RunServer)
			mode = DumpConfig;
		else if (!arg.empty() && arg[0] != '-' && !pathSet)
		{
			path = arg;
			pathSet = true;
		}
		else
			return (false);
	}
	return (true);
}

static int	checkConfig(const Config &config, const std::string &path, bool dump)
{
	if (dump)
		dumpConfig(config);
	std::cerr << WEBSERV_NAME << ": configuration file " << path
		<< " test is successful" << std::endl;
	return (0);
}

static int	runServer(const Config &config)
{
	EventLoop	loop(config);
	std::size_t	i;

	if (!loop.open())
	{
		std::cerr << WEBSERV_NAME << ": " << loop.error() << std::endl;
		return (1);
	}
	for (i = 0; i < config.listeners().size(); i++)
		std::cout << "listening on " << config.listeners()[i].key() << std::endl;
	EventLoop::installSignals();
	return (loop.run());
}

static int	webserv(int argc, char **argv)
{
	std::string	configPath;
	RunMode		mode;
	Config		config;

	if (!parseArgs(argc, argv, mode, configPath))
		return (usage());
	if (!config.load(configPath))
	{
		std::cerr << WEBSERV_NAME << ": " << config.error() << std::endl;
		return (1);
	}
	if (mode != RunServer)
		return (checkConfig(config, configPath, mode == DumpConfig));
	return (runServer(config));
}

int	main(int argc, char **argv)
{
	try
	{
		return (webserv(argc, argv));
	}
	catch (const std::exception &e)
	{
		std::cerr << WEBSERV_NAME << ": fatal: " << e.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << WEBSERV_NAME << ": fatal: unknown exception" << std::endl;
	}
	return (1);
}
