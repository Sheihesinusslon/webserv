#ifndef CONFIGTOKENIZER_HPP
# define CONFIGTOKENIZER_HPP

# include <cstddef>
# include <string>
# include <vector>

struct Token
{
	enum Type
	{
		Word,
		BlockStart,
		BlockEnd,
		Semicolon
	};

	Token();
	Token(Type type, const std::string &text, std::size_t line);

	Type		type;
	std::string	text;
	std::size_t	line;
};

class ConfigTokenizer
{
public:
	ConfigTokenizer();

	bool	tokenize(const std::string &path);

	const std::vector<Token>	&tokens() const;
	const std::string			&error() const;

private:
	bool	readFile(const std::string &path, std::string &content);
	void	scan(const std::string &content);

	std::vector<Token>	_tokens;
	std::string			_error;
};

#endif
