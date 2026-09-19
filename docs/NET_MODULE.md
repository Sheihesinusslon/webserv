# Net module — sockets, connections, the event loop

How the server listens, accepts clients, and moves bytes without ever blocking.
Companion to [CONFIG_MODULE.md](CONFIG_MODULE.md); module boundaries are in
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## The three classes

```
  Socket        one listening door        owns the fd from socket()+bind()+listen()
  Connection    one connected client      owns the fd from accept(), plus two buffers
  EventLoop     the only place that waits owns every Socket and Connection, calls poll()
```

```
  EventLoop
  |
  +-- _sockets      : vector<Socket*>          one per Config::listeners()
  |                                             127.0.0.1:8080  0.0.0.0:8081  0.0.0.0:8082
  |
  +-- _connections  : map<fd, Connection*>     grows on accept(), shrinks on close
                         |
                         +-- _in   : string    bytes received, not yet a full request
                         +-- _out  : string    bytes to send, not yet sent
                         +-- _close: bool      close once _out is drained
                         +-- _lastActivity     for the idle timeout
```

`Config` is read-only input. `Listener` comes from the config module; the net module
never includes anything else from it.

---

## Startup

```
  Config::listeners()
        |
        v            for each host:port
  Socket::open()     getaddrinfo -> socket -> SO_REUSEADDR -> bind -> O_NONBLOCK -> listen
        |
        v
  EventLoop::run()
```

`getaddrinfo` handles IPv4, IPv6 and hostnames in one call. IPv6 hosts arrive from the
config with brackets (`[::1]`); `Socket` strips them before resolving.

A bind failure is fatal at startup: `open()` returns `false`, `error()` names the
listener and the reason, main exits 1. Sockets already opened are released.

---

## One iteration of the loop

```
  runOnce(timeout)
  |
  |  1. buildPollSet
  |       listeners     -> POLLIN
  |       connections   -> POLLIN, and POLLOUT only if _out is not empty
  |
  |  2. poll(fds, count, timeout)           <-- the single poll() in the program
  |
  |  3. dispatch, for every fd that poll marked ready
  |       listener readable   -> accept()  -> new Connection, set O_NONBLOCK
  |       connection error    -> close it
  |       connection readable -> recv() once into _in
  |                              request complete?  -> fill _out, mark close
  |       connection writable -> send() once from _out, erase what went out
  |                              _out empty and marked close? -> close it
  |
  |  4. closeIdle  -> drop connections silent for longer than CLIENT_TIMEOUT_SEC
  |
  v
  run() repeats this until a stop is requested
```

Every read and write happens *only* because `poll()` said that fd was ready. Each call
does one `recv` or one `send`, then returns to the loop. Nothing ever waits on a client.

---

## Rules the loop keeps (the grade-0 ones)

```
  poll() is called in exactly one place            grep "poll(" src/  -> 1 hit
  recv/send/accept exist only in net/               nothing else touches a socket
  errno is never read after recv or send            <= 0 means "close", full stop
  POLLOUT is requested only when there is output    otherwise poll() returns instantly forever
  every fd is O_NONBLOCK                            fcntl(fd, F_SETFL, O_NONBLOCK), the only form allowed
  poll() has a timeout (1 s)                        so idle connections get swept even when nothing happens
```

---

## Lifetime of one request

```
  client                          EventLoop                          state
  ------                          ---------                          -----
  connect  ------------------->   listener POLLIN -> accept()        _connections += fd
  "GET / HT" ----------------->   POLLIN -> recv -> _in = "GET / HT"  waiting
  "TP/1.1\r\n\r\n" ----------->   POLLIN -> recv -> _in complete      _out = response, close=true
                                  next poll asks POLLOUT
           <-------------------   POLLOUT -> send -> _out drained     close -> fd removed
```

---

## Shutdown

```
  SIGINT / SIGTERM  ->  handler sets a flag  ->  run() notices after the current poll()
                    ->  closeAll(): every Connection, then every Socket  ->  exit 0
  SIGPIPE           ->  ignored: a client vanishing mid-send must not kill the process
```

---

## What is scaffolding

```cpp
  if (connection.inBuffer().find("\r\n\r\n") != std::string::npos)   // <- HttpRequest::feed()
      connection.outBuffer() = g_helloResponse;                        // <- RequestHandler::handle()
```

Two lines in `EventLoop::onReadable`. "Request complete" currently means "saw a blank
line", and the response is a fixed `200 OK` — that is the seam where the HTTP module
plugs in. Nothing else in the loop should change when it does.

Not yet: keep-alive (every response closes), CGI pipe fds in the poll set, partial-send
coverage (needs a response larger than the socket buffer).

---

## Testing

`tests/unit/loop.cpp` drives `runOnce()` directly with a real client socket: accept,
hang-up, a request split into three fragments, six concurrent clients, idle timeout,
stop. `tests/run_tests.sh` runs the real binary: HTTP over `/dev/tcp`, `SIGINT` -> 0,
ports released, port conflict -> 1.
