#ifndef REQUESTHANDLER_HPP
# define REQUESTHANDLER_HPP

# include <cstddef>
# include <string>
# include <sys/types.h>

# include "config/Config.hpp"
# include "config/Listener.hpp"
# include "http/HttpRequest.hpp"

/*
** The contract between the event loop (net, B) and the HTTP layer (http, C).
** Full description with the loop's side of it: docs/ARCHITECTURE.md section 4.
**
** The loop owns every fd and never reads a header; the handler reads headers
** and never calls recv / send / poll. One RequestHandler serves the whole
** process: it keeps no per-request state, everything it needs is passed in.
**
**   State s = request.feed(data, n);
**   if (s == HttpRequest::HeadersDone)
**   {
**       request.setMaxBodySize(handler.bodyLimit(request, client));
**       s = request.feed(NULL, 0);
**   }
**   if (s == HttpRequest::Complete || s == HttpRequest::Error)
**       result = handler.handle(request, client);
**   // result.kind == Respond    -> queue result.response
**   // result.kind == CgiPending -> poll result.cgi, then finishCgi()
*/

// Who is talking to us. Both ends come from the accepted socket.
struct Client
{
	Listener	local;	// address the client reached (getsockname): matchServer, SERVER_*
	Listener	peer;	// address the client came from (accept): REMOTE_ADDR
};

/*
** A CGI script the handler has started. From the moment handle() returns it,
** the loop owns all three: it polls the fds, closes them, and reaps the pid.
** Both fds are already O_NONBLOCK and FD_CLOEXEC.
*/
struct CgiProcess
{
	CgiProcess();

	pid_t	pid;
	int		input;	// write end of the script's stdin; -1 when there is no body
	int		output;	// read end of the script's stdout
};

struct HandlerResult
{
	enum Kind
	{
		Respond,	// response holds a complete HTTP response: send it
		CgiPending	// a script is running: poll cgi, then call finishCgi()
	};

	HandlerResult();

	Kind		kind;
	std::string	response;	// Respond: serialized status line, headers and body
	bool		close;		// Respond: close the connection once response is sent
	CgiProcess	cgi;		// CgiPending
};

class RequestHandler
{
public:
	explicit RequestHandler(const Config &config);

	// At HeadersDone: the client_max_body_size of the location the request goes to.
	size_t			bodyLimit(const HttpRequest &request, const Client &client) const;

	// At Complete or Error. An Error request is answered with its errorStatus().
	HandlerResult	handle(const HttpRequest &request, const Client &client) const;

	// The script of a CgiPending result is over: its stdout reached EOF, or the
	// loop killed it after CGI_TIMEOUT_SEC. Always returns Respond.
	HandlerResult	finishCgi(const HttpRequest &request, const Client &client,
						const std::string &output, bool timedOut) const;

private:
	const Config	&_config;
};

#endif
