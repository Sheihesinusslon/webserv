#ifndef SOCKET_HPP
# define SOCKET_HPP

# include <string>

# include "config/Listener.hpp"

class Socket
{
public:
	explicit Socket(const Listener &listener);
	~Socket();

	bool	open();
	void	close();

	int					fd() const;
	const Listener		&listener() const;
	const std::string	&error() const;

private:
	Socket(const Socket &other);
	Socket	&operator=(const Socket &other);

	bool	fail(const std::string &what);

	Listener	_listener;
	int			_fd;
	std::string	_error;
};

#endif
