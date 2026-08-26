#include "webserv.hpp"
#include "config/ConfigTokenizer.hpp"

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

static const char	*typeName(Token::Type type)
{
	if (type == Token::BlockStart)
		return ("block-start");
	if (type == Token::BlockEnd)
		return ("block-end");
	if (type == Token::Semicolon)
		return ("semicolon");
	return ("word");
}

static void	dumpTokens(const std::vector<Token> &tokens)
{
	std::size_t	i;

	std::cout << "tokens: " << tokens.size() << std::endl;
	for (i = 0; i < tokens.size(); i++)
	{
		std::cout << "  line " << tokens[i].line
			<< "  " << typeName(tokens[i].type)
			<< "  '" << tokens[i].text << "'" << std::endl;
	}
}

int	main(int argc, char **argv)
{
	std::string		configPath;
	ConfigTokenizer	tokenizer;

	if (argc > 2)
	{
		std::cerr << "usage: ./" << WEBSERV_NAME
			<< " [configuration file]" << std::endl;
		return (1);
	}
	configPath = (argc == 2) ? argv[1] : DEFAULT_CONFIG;
	if (!tokenizer.tokenize(configPath))
	{
		std::cerr << WEBSERV_NAME << ": " << tokenizer.error() << std::endl;
		return (1);
	}
	std::cout << versionName() << std::endl;
	std::cout << "config: " << configPath << std::endl;
	dumpTokens(tokenizer.tokens());
	return (0);
}
