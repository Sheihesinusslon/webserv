#include "net/Connection.hpp"

#include <unistd.h>

Connection::Connection(int fd, const Listener &listener)
	: _fd(fd),
	  _listener(listener),
	  _in(""),
	  _out(""),
	  _close(false),
	  _lastActivity(std::time(NULL))
{
}

Connection::~Connection()
{
	if (_fd >= 0)
		::close(_fd);
}

int	Connection::fd() const
{
	return (_fd);
}

const Listener	&Connection::listener() const
{
	return (_listener);
}

std::string	&Connection::inBuffer()
{
	return (_in);
}

std::string	&Connection::outBuffer()
{
	return (_out);
}

bool	Connection::wantsWrite() const
{
	return (!_out.empty());
}

void	Connection::markClose()
{
	_close = true;
}

bool	Connection::shouldClose() const
{
	return (_close);
}

void	Connection::touch()
{
	_lastActivity = std::time(NULL);
}

bool	Connection::idleLongerThan(int seconds) const
{
	return (std::time(NULL) - _lastActivity > seconds);
}
