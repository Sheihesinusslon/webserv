#include "webserv.hpp"

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

int	main(int argc, char **argv)
{
	std::string	configPath;

	if (argc > 2)
	{
		std::cerr << "usage: ./" << WEBSERV_NAME
			<< " [configuration file]" << std::endl;
		return (1);
	}
	configPath = (argc == 2) ? argv[1] : DEFAULT_CONFIG;
	std::cout << versionName() << std::endl;
	std::cout << "config: " << configPath << std::endl;
	return (0);
}
