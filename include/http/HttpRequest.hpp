#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <cstddef>
# include <map>
# include <string>

/*
** Incremental HTTP/1.x request parser. Pure: bytes in, state out, no syscalls.
** It works whatever way the request is split across feed() calls.
**
** Typical use by the caller (the event loop / request handler):
**
**   State s = req.feed(data, n);
**   if (s == HttpRequest::HeadersDone)
**   {
**       // headers are parsed, no body byte has been read yet:
**       // match the server and the location, then tell the parser the limit
**       req.setMaxBodySize(location->clientMaxBodySize);
**       s = req.feed(NULL, 0);          // resume: read the body
**   }
**   if (s == HttpRequest::Complete) ...  // handle the request
**   if (s == HttpRequest::Error)    ...  // answer req.errorStatus()
*/
class HttpRequest
{
public:
	/*
	** HeadersDone is the state that is not in the first sketch of the
	** contract (docs/ARCHITECTURE.md section 4), which had only
	** Incomplete / Complete / Error. It exists because client_max_body_size
	** is a per-location setting: only the headers (Host + path) say which
	** location applies, and the parser knows nothing about Config. So the
	** parser stops once the headers are parsed, lets the caller look up the
	** limit and call setMaxBodySize(), and only then reads the body. That
	** way an oversized Content-Length is refused with 413 straight away,
	** before the body is buffered. Every request reports HeadersDone once,
	** even one without a body (GET): the caller always takes the same path,
	** and the next feed() then returns Complete.
	*/
	enum State
	{
		Incomplete,		// need more bytes
		HeadersDone,	// headers parsed, body not read: set the body limit now
		Complete,		// whole request received (headers and body)
		Error			// malformed or refused, see errorStatus()
	};

	HttpRequest();
	~HttpRequest();

	State				feed(const char *data, size_t n);
	void				setMaxBodySize(size_t limit);

	// valid once the state is HeadersDone or later
	const std::string	&method() const;
	const std::string	&target() const;	// raw, e.g. "/a%20b?x=1"
	const std::string	&version() const;	// "HTTP/1.0" or "HTTP/1.1"
	bool				hasHeader(const std::string &name) const;
	const std::string	&header(const std::string &name) const;	// case-insensitive

	// valid once the state is Complete
	const std::string	&body() const;

	// 400, 413, 414, 431, 501 or 505 after Error, 0 otherwise
	int					errorStatus() const;

	// bytes received after the end of this request (pipelining)
	std::string			takeLeftover();

	// forget the current request, ready for the next one on the connection
	void				reset();

private:
	HttpRequest(const HttpRequest &other);
	HttpRequest	&operator=(const HttpRequest &other);

	// Parsing helpers: return true if they made progress, false if they need
	// more bytes or failed (fail() records the status and sets Error).
	bool	fail(int status);
	bool	parseRequestLine();
	bool	parseHeaderLine();
	bool	storeHeader(const std::string &line);
	bool	finishHeaders();
	bool	checkBodyHeaders();		// Content-Length / Transfer-Encoding rules
	bool	startBody();			// resumes after HeadersDone, applies the body limit
	bool	moveBody();
	bool	readFixedBody();
	bool	readChunkSize();
	bool	readChunkData();
	bool	readChunkDataEnd();
	bool	skipTrailers();
	bool	complete();

	enum Step
	{
		RequestLine,
		Headers,
		WaitLimit,		// paused after the headers (see HeadersDone)
		FixedBody,		// Content-Length body
		ChunkSize,
		ChunkData,
		ChunkDataEnd,
		Trailers,
		Done
	};

	std::string							_buf;		// bytes not consumed yet
	State								_state;
	Step								_step;
	int									_error;
	size_t								_maxBodySize;
	size_t								_headerBytes;	// header lines read so far (431 cap)
	bool								_chunked;		// Transfer-Encoding: chunked

	std::string							_method;
	std::string							_target;
	std::string							_version;
	std::map<std::string, std::string>	_headers;	// names lowercased
	std::string							_body;
	size_t								_bodyLeft;	// Content-Length / current chunk
};

#endif
