#include "net/EventLoop.hpp"
#include "webserv.hpp"

#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t	g_stop = 0;

static const char	*g_helloResponse =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 14\r\n"
	"Connection: close\r\n"
	"\r\n"
	"Hello, world!\n";

static void	onSignal(int signum)
{
	(void)signum;
	g_stop = 1;
}

EventLoop::EventLoop(const Config &config)
	: _config(config), _idleTimeout(CLIENT_TIMEOUT_SEC)
{
}

EventLoop::~EventLoop()
{
	closeAll();
}

void	EventLoop::installSignals()
{
	signal(SIGINT, onSignal);
	signal(SIGTERM, onSignal);
	signal(SIGPIPE, SIG_IGN);
}

void	EventLoop::requestStop()
{
	g_stop = 1;
}

bool	EventLoop::stopRequested()
{
	return (g_stop != 0);
}

void	EventLoop::setIdleTimeout(int seconds)
{
	_idleTimeout = seconds;
}

std::size_t	EventLoop::listenerCount() const
{
	return (_sockets.size());
}

std::size_t	EventLoop::connectionCount() const
{
	return (_connections.size());
}

const std::string	&EventLoop::error() const
{
	return (_error);
}

bool	EventLoop::open()
{
	std::size_t	i;

	closeAll();
	for (i = 0; i < _config.listeners().size(); i++)
	{
		_sockets.push_back(new Socket(_config.listeners()[i]));
		if (!_sockets.back()->open())
		{
			_error = _sockets.back()->error();
			closeAll();
			return (false);
		}
	}
	return (true);
}

int	EventLoop::run()
{
	while (!stopRequested())
	{
		if (!runOnce(POLL_INTERVAL_MS))
			break ;
	}
	closeAll();
	return (0);
}

bool	EventLoop::runOnce(int timeoutMs)
{
	std::vector<struct pollfd>	fds;
	int							ready;

	buildPollSet(fds);
	ready = poll(&fds[0], fds.size(), timeoutMs);
	if (ready < 0)
		return (stopRequested() ? false : true);
	if (ready > 0)
		dispatch(fds);
	closeIdle();
	return (true);
}

void	EventLoop::buildPollSet(std::vector<struct pollfd> &fds) const
{
	std::map<int, Connection *>::const_iterator	it;
	struct pollfd								entry;
	std::size_t									i;

	fds.clear();
	for (i = 0; i < _sockets.size(); i++)
	{
		entry.fd = _sockets[i]->fd();
		entry.events = POLLIN;
		entry.revents = 0;
		fds.push_back(entry);
	}
	for (it = _connections.begin(); it != _connections.end(); ++it)
	{
		entry.fd = it->first;
		entry.events = POLLIN;
		if (it->second->wantsWrite())
			entry.events |= POLLOUT;
		entry.revents = 0;
		fds.push_back(entry);
	}
}

void	EventLoop::dispatch(const std::vector<struct pollfd> &fds)
{
	std::map<int, Connection *>::iterator	found;
	const Socket							*socket;
	std::size_t								i;

	for (i = 0; i < fds.size(); i++)
	{
		if (fds[i].revents == 0)
			continue;
		socket = listeningSocket(fds[i].fd);
		if (socket != NULL)
		{
			if (fds[i].revents & POLLIN)
				acceptFrom(*socket);
			continue;
		}
		found = _connections.find(fds[i].fd);
		if (found == _connections.end())
			continue;
		if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			closeConnection(fds[i].fd);
			continue;
		}
		if (fds[i].revents & POLLIN)
			onReadable(*found->second);
		found = _connections.find(fds[i].fd);
		if (found != _connections.end() && (fds[i].revents & POLLOUT))
			onWritable(*found->second);
	}
}

const Socket	*EventLoop::listeningSocket(int fd) const
{
	std::size_t	i;

	for (i = 0; i < _sockets.size(); i++)
	{
		if (_sockets[i]->fd() == fd)
			return (_sockets[i]);
	}
	return (NULL);
}

void	EventLoop::acceptFrom(const Socket &socket)
{
	int	fd;

	fd = accept(socket.fd(), NULL, NULL);
	if (fd < 0)
		return ;
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	{
		::close(fd);
		return ;
	}
	_connections[fd] = new Connection(fd, socket.listener());
}

void	EventLoop::onReadable(Connection &connection)
{
	char	chunk[RECV_CHUNK_SIZE];
	ssize_t	received;

	received = recv(connection.fd(), chunk, sizeof(chunk), 0);
	if (received <= 0)
	{
		closeConnection(connection.fd());
		return ;
	}
	connection.touch();
	connection.inBuffer().append(chunk, static_cast<std::size_t>(received));
	if (connection.inBuffer().find("\r\n\r\n") != std::string::npos)
	{
		connection.outBuffer() = g_helloResponse;
		connection.inBuffer().clear();
		connection.markClose();
	}
}

void	EventLoop::onWritable(Connection &connection)
{
	std::string	&out = connection.outBuffer();
	ssize_t		sent;

	if (out.empty())
		return ;
	sent = send(connection.fd(), out.data(), out.size(), 0);
	if (sent <= 0)
	{
		closeConnection(connection.fd());
		return ;
	}
	connection.touch();
	out.erase(0, static_cast<std::size_t>(sent));
	if (out.empty() && connection.shouldClose())
		closeConnection(connection.fd());
}

void	EventLoop::closeConnection(int fd)
{
	std::map<int, Connection *>::iterator	it;

	it = _connections.find(fd);
	if (it == _connections.end())
		return ;
	delete it->second;
	_connections.erase(it);
}

void	EventLoop::closeIdle()
{
	std::map<int, Connection *>::iterator	it;
	std::map<int, Connection *>::iterator	next;

	it = _connections.begin();
	while (it != _connections.end())
	{
		next = it;
		++next;
		if (it->second->idleLongerThan(_idleTimeout))
			closeConnection(it->first);
		it = next;
	}
}

void	EventLoop::closeAll()
{
	std::map<int, Connection *>::iterator	it;
	std::size_t								i;

	for (it = _connections.begin(); it != _connections.end(); ++it)
		delete it->second;
	_connections.clear();
	for (i = 0; i < _sockets.size(); i++)
		delete _sockets[i];
	_sockets.clear();
}
