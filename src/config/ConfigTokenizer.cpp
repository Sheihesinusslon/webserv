#include "config/ConfigTokenizer.hpp"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

static bool	isDelimiter(char c)
{
	return (std::isspace(static_cast<unsigned char>(c))
		|| c == '{' || c == '}' || c == ';' || c == '#');
}

Token::Token() : type(Word), text(""), line(0)
{
}

Token::Token(Type type, const std::string &text, std::size_t line)
	: type(type), text(text), line(line)
{
}

ConfigTokenizer::ConfigTokenizer()
{
}

const std::vector<Token>	&ConfigTokenizer::tokens() const
{
	return (_tokens);
}

const std::string	&ConfigTokenizer::error() const
{
	return (_error);
}

bool	ConfigTokenizer::tokenize(const std::string &path)
{
	std::string	content;

	_tokens.clear();
	_error.clear();
	if (!readFile(path, content))
		return (false);
	scan(content);
	return (true);
}

bool	ConfigTokenizer::readFile(const std::string &path, std::string &content)
{
	struct stat			info;
	std::ifstream		in;
	std::ostringstream	buffer;

	if (stat(path.c_str(), &info) != 0)
	{
		_error = "cannot open '" + path + "': " + std::strerror(errno);
		return (false);
	}
	if (!S_ISREG(info.st_mode))
	{
		_error = "'" + path + "' is not a regular file";
		return (false);
	}
	in.open(path.c_str(), std::ifstream::in);
	if (!in.is_open())
	{
		_error = "cannot open '" + path + "'";
		return (false);
	}
	buffer << in.rdbuf();
	content = buffer.str();
	in.close();
	return (true);
}

void	ConfigTokenizer::scan(const std::string &content)
{
	std::size_t	i;
	std::size_t	line;
	std::size_t	start;

	i = 0;
	line = 1;
	while (i < content.size())
	{
		if (content[i] == '\n')
		{
			line++;
			i++;
		}
		else if (std::isspace(static_cast<unsigned char>(content[i])))
			i++;
		else if (content[i] == '#')
		{
			while (i < content.size() && content[i] != '\n')
				i++;
		}
		else if (content[i] == '{')
		{
			_tokens.push_back(Token(Token::BlockStart, "{", line));
			i++;
		}
		else if (content[i] == '}')
		{
			_tokens.push_back(Token(Token::BlockEnd, "}", line));
			i++;
		}
		else if (content[i] == ';')
		{
			_tokens.push_back(Token(Token::Semicolon, ";", line));
			i++;
		}
		else
		{
			start = i;
			while (i < content.size() && !isDelimiter(content[i]))
				i++;
			_tokens.push_back(Token(Token::Word,
					content.substr(start, i - start), line));
		}
	}
}
