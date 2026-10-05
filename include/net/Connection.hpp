#ifndef CONNECTION_HPP
# define CONNECTION_HPP

# include <ctime>
# include <string>

# include "config/Listener.hpp"

class Connection
{
public:
	Connection(int fd, const Listener &listener);
	~Connection();

	int				fd() const;
	const Listener	&listener() const;

	std::string		&inBuffer();
	std::string		&outBuffer();
	bool			wantsWrite() const;

	void			stopReading();
	bool			wantsRead() const;

	void			markClose();
	bool			shouldClose() const;

	void			touch();
	bool			idleLongerThan(int seconds) const;

private:
	Connection(const Connection &other);
	Connection	&operator=(const Connection &other);

	int			_fd;
	Listener	_listener;
	std::string	_in;
	std::string	_out;
	bool		_close;
	bool		_readClosed;
	std::time_t	_lastActivity;
};

#endif
