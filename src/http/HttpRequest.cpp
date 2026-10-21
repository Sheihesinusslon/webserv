#include "http/HttpRequest.hpp"
#include "webserv.hpp"

#include <algorithm>
#include <cctype>

static const char *const	g_methods[] = {"GET", "HEAD", "POST", "PUT", "DELETE",
	"OPTIONS", "PATCH", "CONNECT", "TRACE", NULL};

static bool	isKnownMethod(const std::string &method)
{
	for (size_t i = 0; g_methods[i] != NULL; i++)
		if (method == g_methods[i])
			return (true);
	return (false);
}

// RFC 9110 "token": letters, digits and a few symbols. Spaces and ':' are not allowed.
static bool	isToken(const std::string &s)
{
	if (s.empty())
		return (false);
	for (size_t i = 0; i < s.size(); i++)
	{
		unsigned char	c = s[i];
		if (!std::isalnum(c) && std::string("!#$%&'*+-.^_`|~").find(c) == std::string::npos)
			return (false);
	}
	return (true);
}

static bool	hasBadByte(const std::string &s, bool allowTab)
{
	for (size_t i = 0; i < s.size(); i++)
	{
		unsigned char	c = s[i];
		if (c == '\t' && allowTab)
			continue ;
		if (c < 0x20 || c == 0x7f)
			return (true);
	}
	return (false);
}

static std::string	toLower(std::string s)
{
	for (size_t i = 0; i < s.size(); i++)
		s[i] = std::tolower(static_cast<unsigned char>(s[i]));
	return (s);
}

static std::string	trim(const std::string &s)
{
	size_t	begin = s.find_first_not_of(" \t");
	if (begin == std::string::npos)
		return ("");
	return (s.substr(begin, s.find_last_not_of(" \t") - begin + 1));
}

// "HTTP/" digit "." digit
static bool	looksLikeVersion(const std::string &v)
{
	return (v.size() == 8 && v.compare(0, 5, "HTTP/") == 0
		&& std::isdigit(static_cast<unsigned char>(v[5])) && v[6] == '.'
		&& std::isdigit(static_cast<unsigned char>(v[7])));
}

HttpRequest::HttpRequest()
	: _state(Incomplete), _step(RequestLine), _error(0),
	_maxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE), _headerBytes(0),
	_chunked(false), _bodyLeft(0)
{
}

HttpRequest::~HttpRequest()
{
}

bool	HttpRequest::fail(int status)
{
	_error = status;
	_state = Error;
	return (false);
}

HttpRequest::State	HttpRequest::feed(const char *data, size_t n)
{
	if (_state == Error || _state == Complete)
		return (_state);
	if (n > 0)
		_buf.append(data, n);
	if (_state == HeadersDone)
		_state = Incomplete;	// the caller set the body limit and resumed
	bool	progressed = true;
	while (progressed && _state == Incomplete)
	{
		// No default: on purpose. Every Step is listed, so adding a new one
		// without handling it here is a compile error (-Wall -Werror).
		switch (_step)
		{
			case RequestLine:	progressed = parseRequestLine();	break ;
			case Headers:		progressed = parseHeaderLine();		break ;
			case WaitLimit:		progressed = startBody();			break ;
			case FixedBody:		progressed = readFixedBody();		break ;
			case ChunkSize:		progressed = readChunkSize();		break ;
			case ChunkData:		progressed = readChunkData();		break ;
			case ChunkDataEnd:	progressed = readChunkDataEnd();	break ;
			case Trailers:		progressed = skipTrailers();		break ;
			case Done:			progressed = false;					break ;
		}
	}
	return (_state);
}

bool	HttpRequest::parseRequestLine()
{
	// RFC 9112 section 2.2: a server expecting a request line SHOULD ignore
	// at least one empty line (CRLF) before it. Some clients send an extra
	// CRLF after a previous POST body, so without this the next request on
	// the same connection would be refused with 400.
	while (_buf.compare(0, 2, "\r\n") == 0)
		_buf.erase(0, 2);

	size_t	end = _buf.find("\r\n");
	if (end == std::string::npos)
	{
		if (_buf.size() > MAX_REQUEST_LINE)
			return (fail(414));
		return (false);			// wait for more bytes
	}
	if (end > MAX_REQUEST_LINE)
		return (fail(414));
	std::string	line = _buf.substr(0, end);
	_buf.erase(0, end + 2);

	// exactly: METHOD SP TARGET SP VERSION
	size_t	sp1 = line.find(' ');
	size_t	sp2 = (sp1 == std::string::npos) ? sp1 : line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos || line.find(' ', sp2 + 1) != std::string::npos)
		return (fail(400));
	_method = line.substr(0, sp1);
	_target = line.substr(sp1 + 1, sp2 - sp1 - 1);
	_version = line.substr(sp2 + 1);

	if (!isToken(_method) || _target.empty() || _target[0] != '/'
		|| hasBadByte(_target, false) || !looksLikeVersion(_version))
		return (fail(400));
	if (!isKnownMethod(_method))
		return (fail(501));
	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
		return (fail(505));
	_step = Headers;
	return (true);
}

bool	HttpRequest::parseHeaderLine()
{
	size_t	end = _buf.find("\r\n");
	if (end == std::string::npos)
	{
		if (_headerBytes + _buf.size() > MAX_HEADER_SIZE)
			return (fail(431));
		return (false);
	}
	_headerBytes += end + 2;
	if (_headerBytes > MAX_HEADER_SIZE)
		return (fail(431));
	std::string	line = _buf.substr(0, end);
	_buf.erase(0, end + 2);
	if (line.empty())
		return (finishHeaders());	// blank line: end of the headers
	return (storeHeader(line));
}

bool	HttpRequest::storeHeader(const std::string &line)
{
	if (line[0] == ' ' || line[0] == '\t')
		return (fail(400));			// obsolete line folding
	size_t	colon = line.find(':');
	if (colon == std::string::npos)
		return (fail(400));
	std::string	name = line.substr(0, colon);
	std::string	value = trim(line.substr(colon + 1));
	if (!isToken(name) || hasBadByte(value, true))	// "Name :" fails isToken
		return (fail(400));
	name = toLower(name);

	std::map<std::string, std::string>::iterator	it = _headers.find(name);
	if (it == _headers.end())
		_headers[name] = value;
	else if (name == "host" || name == "content-length")
		return (fail(400));			// duplicates are ambiguous
	else
		it->second += ", " + value;
	return (true);
}

bool	HttpRequest::finishHeaders()
{
	if (_version == "HTTP/1.1" && !hasHeader("host"))
		return (fail(400));
	if (!checkBodyHeaders())
		return (false);
	_step = WaitLimit;
	_state = HeadersDone;
	return (true);
}

static int	hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

// Called from finishHeaders(), before the HeadersDone pause.
bool	HttpRequest::checkBodyHeaders()
{
	bool	hasLength = hasHeader("content-length");
	bool	hasEncoding = hasHeader("transfer-encoding");

	// RFC 9112 section 6.3: both together is a request-smuggling vector, refuse it.
	if (hasLength && hasEncoding)
		return (fail(400));
	if (hasEncoding)
	{
		if (toLower(header("transfer-encoding")) != "chunked")
			return (fail(501));
		_chunked = true;
	}
	if (hasLength)
	{
		const std::string	&raw = header("content-length");
		if (raw.empty() || raw.find_first_not_of("0123456789") != std::string::npos)
			return (fail(400));
		size_t		first = raw.find_first_not_of('0');
		std::string	digits = (first == std::string::npos) ? "0" : raw.substr(first);
		if (digits.size() > 9)		// >= 1 GB: also keeps size_t from overflowing on 32-bit
			return (fail(413));
		for (size_t i = 0; i < digits.size(); i++)
			_bodyLeft = _bodyLeft * 10 + (digits[i] - '0');
	}
	return (true);
}

// First step after the caller resumed: now the body limit is known.
bool	HttpRequest::startBody()
{
	if (_chunked)
	{
		_step = ChunkSize;
		return (true);
	}
	if (_bodyLeft > _maxBodySize)		// refused before reading a single body byte
		return (fail(413));
	if (_bodyLeft == 0)
		return (complete());			// no body (typical GET)
	_step = FixedBody;
	return (true);
}

// Move up to _bodyLeft bytes from the input buffer to the body.
bool	HttpRequest::moveBody()
{
	size_t	n = std::min(_buf.size(), _bodyLeft);
	if (n == 0)
		return (false);
	_body.append(_buf, 0, n);
	_buf.erase(0, n);
	_bodyLeft -= n;
	return (true);
}

bool	HttpRequest::readFixedBody()
{
	moveBody();
	if (_bodyLeft == 0)
		return (complete());
	return (false);					// buffer is empty: wait for more bytes
}

bool	HttpRequest::readChunkSize()
{
	size_t	end = _buf.find("\r\n");
	if (end == std::string::npos)
	{
		if (_buf.size() > MAX_CHUNK_LINE)
			return (fail(400));
		return (false);
	}
	std::string	line = _buf.substr(0, end);
	_buf.erase(0, end + 2);
	if (line.find(';') != std::string::npos)
		line.erase(line.find(';'));		// chunk extensions are ignored
	if (line.empty())
		return (fail(400));
	size_t	size = 0;
	for (size_t i = 0; i < line.size(); i++)
	{
		int	digit = hexValue(line[i]);
		if (digit < 0)
			return (fail(400));
		if (size > (static_cast<size_t>(-1) >> 4))		// next *16 would overflow
			return (fail(413));
		size = size * 16 + digit;
	}
	if (size == 0)
	{
		_headerBytes = 0;					// trailers get their own size budget
		_step = Trailers;
		return (true);
	}
	// 413 as soon as the decoded body would pass the limit, not after buffering it
	if (size > _maxBodySize - _body.size())
		return (fail(413));
	_bodyLeft = size;
	_step = ChunkData;
	return (true);
}

bool	HttpRequest::readChunkData()
{
	moveBody();
	if (_bodyLeft > 0)
		return (false);
	_step = ChunkDataEnd;
	return (true);
}

bool	HttpRequest::readChunkDataEnd()
{
	if (_buf.size() < 2)
		return (false);
	if (_buf.compare(0, 2, "\r\n") != 0)
		return (fail(400));
	_buf.erase(0, 2);
	_step = ChunkSize;
	return (true);
}

// After the last chunk: optional trailer lines, then a blank line. Trailers are discarded.
bool	HttpRequest::skipTrailers()
{
	size_t	end = _buf.find("\r\n");
	if (end == std::string::npos)
	{
		if (_headerBytes + _buf.size() > MAX_HEADER_SIZE)
			return (fail(431));
		return (false);
	}
	_headerBytes += end + 2;
	if (_headerBytes > MAX_HEADER_SIZE)
		return (fail(431));
	_buf.erase(0, end + 2);
	if (end == 0)
		return (complete());
	return (true);
}

bool	HttpRequest::complete()
{
	_step = Done;
	_state = Complete;
	return (true);
}

void	HttpRequest::setMaxBodySize(size_t limit)
{
	_maxBodySize = limit;
}

const std::string	&HttpRequest::method() const
{
	return (_method);
}

const std::string	&HttpRequest::target() const
{
	return (_target);
}

const std::string	&HttpRequest::version() const
{
	return (_version);
}

bool	HttpRequest::hasHeader(const std::string &name) const
{
	return (_headers.find(toLower(name)) != _headers.end());
}

const std::string	&HttpRequest::header(const std::string &name) const
{
	static const std::string	empty;
	std::map<std::string, std::string>::const_iterator	it = _headers.find(toLower(name));
	return (it == _headers.end() ? empty : it->second);
}

const std::string	&HttpRequest::body() const
{
	return (_body);
}

int	HttpRequest::errorStatus() const
{
	return (_error);
}

std::string	HttpRequest::takeLeftover()
{
	std::string	rest;
	rest.swap(_buf);
	return (rest);
}

void	HttpRequest::reset()
{
	_buf.clear();
	_state = Incomplete;
	_step = RequestLine;
	_error = 0;
	_maxBodySize = DEFAULT_CLIENT_MAX_BODY_SIZE;
	_headerBytes = 0;
	_chunked = false;
	_method.clear();
	_target.clear();
	_version.clear();
	_headers.clear();
	_body.clear();
	_bodyLeft = 0;
}
