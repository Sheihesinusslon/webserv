#ifndef CONFIGPARSER_HPP
# define CONFIGPARSER_HPP

# include <cstddef>
# include <string>
# include <vector>

# include "config/ConfigTokenizer.hpp"
# include "config/LocationConfig.hpp"
# include "config/ServerConfig.hpp"

class ConfigParser
{
public:
	ConfigParser();

	bool	parse(const std::vector<Token> &tokens,
				std::vector<ServerConfig> &servers);

	const std::string	&error() const;

private:
	bool		atEnd() const;
	const Token	&peek() const;

	bool	fail(const std::string &message);
	bool	failAt(std::size_t line, const std::string &message);

	bool	expectWord(std::string &out, const std::string &what);
	bool	expectType(Token::Type type, const std::string &what);
	bool	collectArgs(std::vector<std::string> &args);
	bool	needArgs(const std::string &name,
				const std::vector<std::string> &args,
				std::size_t min, std::size_t max, std::size_t line);

	bool	parseServer(ServerConfig &server);
	bool	parseLocation(LocationConfig &location);
	bool	applyServerDirective(ServerConfig &server, const std::string &name,
				const std::vector<std::string> &args, std::size_t line);
	bool	applyLocationDirective(LocationConfig &location,
				const std::string &name, const std::vector<std::string> &args,
				std::size_t line);

	const std::vector<Token>	*_tokens;
	std::size_t					_pos;
	std::string					_error;
};

#endif
