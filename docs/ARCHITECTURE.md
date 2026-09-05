# webserv — architecture & work split

Reference document for the team. Describes the module boundaries, who owns what,
the contracts between modules, and how each subject requirement maps to an owner.

**Status**

| part | state |
|---|---|
| A - config & routing | **done** - parse, validate, inherit, matchServer, matchLocation, resolvePath; 139 tests |
| B - transport & event loop | not started - no `include/net/`, no `src/net/` |
| C - http & handlers | not started - no `include/http/`, no `src/http/` |
| CGI | not started |
| `www/` content + Python test suite | not started |

Still thin in A: nothing rejects two servers claiming the same `host:port` *and* the
same `server_name`; roots and CGI interpreters are not checked to exist on disk.

---

## 1. The rule that shapes everything

From the subject (§IV.1):

> It must be non-blocking and use only **1 poll()** for all the I/O operations between
> the clients and the server (listen included). You must never do a read or a write
> operation without going through poll(). Checking the value of errno to adjust the
> server behaviour is strictly forbidden after performing a read or write operation.

Breaking this is a **grade of 0**, not a lost point. The design goal follows directly:

> **Exactly one module is allowed to touch a socket or pipe file descriptor.**
> Every other module is pure: bytes in, bytes out, no syscalls.

This is also what makes a three-way split safe. Two of the three people can work,
build and test with no networking involved at all.

A second grade-0 rule (§II): the program must never crash, even on out-of-memory.
Every module owns its failure paths; the event loop carries a top-level catch-all.

---

## 2. Module map

```
                       config/default.conf
                                |
                                v
                    +-----------------------+
                    |   A. CONFIG & ROUTING |
                    |                       |
                    |  parse, validate,     |
                    |  match server,        |
                    |  match location       |
                    +-----------------------+
                       |                 ^
        listeners()    |                 |  matchServer() / matchLocation()
        (host:port)    |                 |
                       v                 |
   network       +-----------------------+-----+
   <----------->  |  B. TRANSPORT & EVENT LOOP |
   sockets        |                            |
                  |  sockets, single poll(),   |
                  |  connection state machine, |
                  |  buffers, timeouts         |
                  +----------------------------+
                       |                 ^
        raw request    |                 |  response bytes
        bytes          v                 |
                    +-----------------------+
                    |   C. HTTP & HANDLERS  |
                    |                       |
                    |  parse request,       |
                    |  build response,      |
                    |  GET / POST / DELETE  |
                    +-----------------------+
                                |
                                v
                       disk files, CGI
```

A never sees a socket. C never sees a socket. B never interprets HTTP.

---

## 3. Responsibilities

### Part A — Configuration & Routing

Turns the config file into an immutable in-memory model and answers routing questions.

| In scope | Out of scope |
|---|---|
| Tokenizer and recursive block parser | Anything touching a file descriptor |
| `Config` / `ServerConfig` / `LocationConfig` model | Opening files to serve them |
| Validation, clear error message, clean exit | HTTP parsing or response building |
| Directive inheritance (location inherits from server) | |
| Deduplicating `listen` into unique `host:port` listeners | |
| `matchServer()` and `matchLocation()` resolution | |
| `resolvePath()` - URL to filesystem path, plus containment check | |

- **Input:** a file path.
- **Output:** an object graph that B and C query, read-only, for the process lifetime.
- **Done when:** every directive in `config/default.conf` round-trips into the model,
  a malformed config exits cleanly with a useful message, and the match functions are
  unit-tested without a running server.

### Part B — Transport & Event Loop

Owns every file descriptor in the process.

| In scope | Out of scope |
|---|---|
| Socket creation, bind, listen — one per unique `host:port` | What the bytes mean |
| The single `poll()` loop, watching read and write at once | Status codes, headers, disk lookup |
| `Connection` state machine: read, dispatch, write, close | Config parsing |
| Per-connection input and output buffers | |
| Partial reads and partial writes | |
| Client disconnects, idle timeouts, fd exhaustion, `SIGPIPE` | |
| Registering CGI pipe fds into the *same* poll set | Building the CGI process or env |

- **Input:** the listener list from A.
- **Output:** complete request byte buffers to C; drains C's response buffers back out.
- **Done when:** the server survives the provided testers and sustained stress without
  leaking descriptors, hanging, or dying — and `poll()` appears exactly once in the tree.

### Part C — HTTP & Handlers

Owns the protocol and the decision of what to do with a request.

| In scope | Out of scope |
|---|---|
| Incremental request parser: request line, headers, body | Sockets, `poll()`, timeouts |
| Chunked transfer decoding | Which server/location applies (asks A) |
| `client_max_body_size` enforcement (413) | |
| Response building and accurate status codes | |
| Built-in default error pages | |
| GET, static files, MIME types, autoindex listing | |
| POST uploads (`multipart/form-data`), DELETE, redirects | |
| URI normalisation *before* matching (see section 6) | |

- **Input:** a byte buffer plus the resolved `ServerConfig` / `LocationConfig`.
- **Output:** a fully serialized response buffer.
- **Done when:** a real browser renders the static site and response headers match
  nginx's for the same request.


### Shared — testing and content

The subject says *"Do not test with only one program. Write your tests in a more
suitable language, such as Python or Golang."* Someone owns the `www/` tree, the extra
`.conf` files, and a Python test suite. A's parser work finishes first, so this
naturally lands there.

---

## 4. Contracts

Two interfaces. Pin these down before splitting; everything else is private.

### A exposes

Declared in `include/config/`. See `Config.hpp`, `ServerConfig.hpp`,
`LocationConfig.hpp` and `Listener.hpp` for the full model.

```cpp
bool                   Config::load(const std::string &path);
const std::vector<Listener>    &Config::listeners() const;
const ServerConfig     *Config::matchServer(const Listener &listener,
                                            const std::string &hostHeader) const;

const LocationConfig   *ServerConfig::matchLocation(const std::string &path) const;
std::string             ServerConfig::errorPage(int code) const;

std::string             LocationConfig::resolvePath(const std::string &uri) const;
bool                    LocationConfig::isMethodAllowed(const std::string &m) const;
std::string             LocationConfig::cgiInterpreter(const std::string &ext) const;
```

`resolvePath()` returns an empty string when the mapped path escapes `root` - the
caller answers 403. It does no disk I/O: no `stat`, no existence check, no `index`
handling. Those belong to C's static-file handler.

`load()` returns `false` and fills `Config::error()` rather than throwing, so a bad
config can never take the process down.

A listener maps to a **list** of candidate server blocks, not one: several `server`
blocks may share a `host:port`. B binds the keys; C resolves the value list per request
once the `Host` header is parsed.

### B to C

```cpp
HttpRequest::State  feed(const char* data, size_t n);   // Incomplete | Complete | Error
Result              RequestHandler::handle(const HttpRequest&, const Config&);
const std::string&  HttpResponse::buffer() const;       // B drains across poll writes
```

`handle()` cannot always return a finished response: a CGI request must be able to
return *"pending — here are the fds to poll"*. Decide that return type **before**
writing B's state machine; retrofitting it later means rewriting the loop.

---

## 5. Request lifecycle

1. **B** — `poll()` reports a listening socket is readable; `accept()` a client.
2. **B** — `poll()` reports the client is readable; `recv()` into its input buffer.
3. **C** — `feed()` the new bytes. Not complete yet? Return to the loop.
4. **C** — normalise the URI: strip the query string, percent-decode, resolve `.` / `..`.
5. **A** — resolve `host:port` + `Host` to a `ServerConfig`, then the normalised path to
   a `LocationConfig`, then `resolvePath()` to a filesystem path.
6. **C** — enforce the location's rules: allowed method (405), body size (413),
   redirect (301), empty path from `resolvePath()` (403).
7. **C** — run the handler: read a file, generate an autoindex page, store an upload,
   delete a file, or start a CGI.
8. **C** — serialize status line, headers, and body into one buffer.
9. **B** — `poll()` reports the client is writable; `send()` part of the buffer.
   Repeat until drained.
10. **B** — keep the connection alive or close it; reset state either way.

At no point does C call `recv`, `send`, or `poll`. At no point does B inspect a header.

---

## 6. Resolution rules

Both are **code**, not config; nginx works the same way. The config only supplies inputs.

### Which server block

1. Narrow to blocks listening on the connection's `host:port`.
2. Exact `server_name` match against the request's `Host` header.
3. No match: the **first block declared** for that `host:port` is the default.

The `Host` header is normalised first - `:port` stripped, then lowercased - and
`server_name` values are lowercased at load, so the comparison is a plain `==`.
Host names are case-insensitive (RFC 9110); paths are not.

Wildcards, regex and nginx's `default_server` flag are out of scope.

### Which location

Longest matching prefix wins. Order in the file is irrelevant. The subject states no
regex is required, which removes nginx's `=`, `^~`, `~` and `~*` modifiers entirely.

Two rules that matter more than the algorithm:

- **Normalise before matching — C's job, step 4 above:** strip the query string,
  percent-decode, then resolve `.` and `..`, in that order. Matching first and
  normalising later lets `/uploads/../../etc/passwd` inherit `/uploads`' permissions
  and then escape the root. `resolvePath()` re-checks containment afterwards regardless,
  so a caller that forgets cannot open a hole.
- **Location paths are normalised at load:** `location /trailing/` is stored as
  `/trailing`, so both spellings behave identically.
- **Prefix boundary:** nginx's prefix match is a pure string prefix, so `location /assets`
  also matches `/assetsfoo`. Requiring the match to end at a `/` or end-of-string avoids
  that surprise. It deviates from nginx; nothing in the evaluation tests it.

### `root` semantics

The subject's example is **alias** semantics, not nginx's `root`:

> if URL `/kapouet` is rooted to `/tmp/www`, URL `/kapouet/pouic/toto/pouet` will search
> for `/tmp/www/pouic/toto/pouet`.

The location prefix is **stripped**. Real nginx `root` would yield
`/tmp/www/kapouet/pouic/toto/pouet`. Follow the subject.

---

## 7. Config grammar

> Class diagrams and dataflow for the tokenizer/parser: [CONFIG_MODULE.md](CONFIG_MODULE.md)

Defined by `config/default.conf`, which is the specification.

**Server level:** `listen` (`host:port`, bare `port`, or bare `host` — port 80),
`server_name`, `root`, `index`, `autoindex`, `client_max_body_size`,
`error_page <code> <path>`

**Location level:** `allow_methods`, `root`, `index`, `autoindex`,
`client_max_body_size`, `return <code> <url>`, `upload_store <dir>`,
`cgi_ext <ext> <interpreter>`

Decisions baked in:

- Locations **inherit** server-level directives unless overridden — so `LocationConfig`
  needs a *"was this set?"* flag per field, not just a value.
- Body sizes are **plain bytes**, no `1m` / `10M` suffixes.
- Presence of `upload_store` means uploads are authorised; there is no separate toggle.
- `error_page` takes **one code per directive**.

---

## 8. File layout and Makefile ownership

```
include/
  webserv.hpp                                            shared constants
  config/   Config.hpp  ServerConfig.hpp  LocationConfig.hpp
            Listener.hpp  ConfigParser.hpp  ConfigTokenizer.hpp     (A)   exists
  net/      Socket.hpp  EventLoop.hpp  Connection.hpp               (B)   to write
  http/     HttpRequest.hpp  HttpResponse.hpp
            RequestHandler.hpp  HttpStatus.hpp                      (C)   to write
  cgi/      Cgi.hpp                                                 (B+C) to write
  bonus/    bonus-only headers
src/
  config/  net/  http/  cgi/  main.cpp
tests/
  configs/  valid/ and invalid/ fixtures
  unit/     matching.cpp  paths.cpp - compiled and run by tests/run_tests.sh
```

`Listener` lives in `config/`, not `net/`: A produces it, B only consumes it. Keeping it
on A's side means B depends on A's headers and never the reverse.

Split the Makefile source list per owner so three people adding files never collide on
the same line:

```make
SRC_CONFIG = config/...
SRC_NET    = net/...
SRC_HTTP   = http/...
SRC        = main.cpp $(SRC_CONFIG) $(SRC_NET) $(SRC_HTTP)
```

---

## 9. Subject requirement → owner

| Requirement (subject §) | Owner |
|---|---|
| Config file as argument or default path (IV.1) | A |
| Interface:port pairs, multiple ports, different content (IV.3) | A + B |
| Error page configuration (IV.3) | A |
| Default error pages when none provided (IV.1) | C |
| `client_max_body_size` (IV.3) | A defines, C enforces |
| Accepted methods per route (IV.3) | A defines, C enforces |
| HTTP redirection (IV.3) | A defines, C emits |
| Route → directory mapping (IV.3) | A |
| Directory listing on/off (IV.3) | A defines, C generates |
| Default file for a directory (IV.3) | A defines, C resolves |
| Upload authorisation and storage location (IV.3) | A defines, C stores |
| CGI by file extension (IV.3) | A defines, C + B execute |
| Single `poll()`, non-blocking, no `errno` after I/O (IV.1) | **B alone** |
| Never hang indefinitely (IV.1) | B |
| Handle client disconnections (IV.1) | B |
| Browser compatibility (IV.1) | C |
| Accurate status codes (IV.1) | C |
| GET, POST, DELETE (IV.1) | C |
| Fully static website (IV.1) | C |
| File upload (IV.1) | C |
| Resilience under stress (IV.1) | B |
| Never crash, even on OOM (II) | all |
| Makefile, C++98, `-Wall -Wextra -Werror` (II) | all |
| README (V) | all |

