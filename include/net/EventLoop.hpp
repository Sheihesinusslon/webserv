#ifndef EVENTLOOP_HPP
# define EVENTLOOP_HPP

# include <cstddef>
# include <map>
# include <poll.h>
# include <string>
# include <vector>

# include "config/Config.hpp"
# include "net/Connection.hpp"
# include "net/Socket.hpp"

class EventLoop
{
public:
	explicit EventLoop(const Config &config);
	~EventLoop();

	bool	open();
	int		run();
	bool	runOnce(int timeoutMs);

	static void	installSignals();
	static void	requestStop();
	static bool	stopRequested();

	void		setIdleTimeout(int seconds);
	std::size_t	listenerCount() const;
	std::size_t	connectionCount() const;
	const std::string	&error() const;

private:
	EventLoop(const EventLoop &other);
	EventLoop	&operator=(const EventLoop &other);

	void	buildPollSet(std::vector<struct pollfd> &fds) const;
	void	dispatch(const std::vector<struct pollfd> &fds);
	void	acceptFrom(const Socket &socket);
	void	onReadable(Connection &connection);
	void	onWritable(Connection &connection);
	void	closeConnection(int fd);
	void	closeIdle();
	void	closeAll();
	const Socket	*listeningSocket(int fd) const;

	const Config					&_config;
	std::vector<Socket *>			_sockets;
	std::map<int, Connection *>		_connections;
	int								_idleTimeout;
	std::string						_error;
};

#endif
