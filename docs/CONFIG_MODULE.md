# Config module — tokenizing & parsing

How `config/default.conf` becomes a `Config` object tree.

---

## Flow

```
  config/default.conf                     text
          |
          v
  +-------------------+
  | ConfigTokenizer   |  readFile() -> scan()
  +-------------------+
          |
          v
   vector<Token>                          160 tokens
          |
          v
  +-------------------+
  | ConfigParser      |  parseServer() -> parseLocation()
  +-------------------+
          |
          v
   vector<ServerConfig>                   raw structs
          |
          v
  +-------------------+
  | Config            |  validate() -> normalizeNames()
  |                   |  -> inherit() -> collectListeners()
  +-------------------+
          |
          v
   Config, read-only for the rest of the process
```

Any step can fail: it sets `error()` and `main` exits 1. Nothing throws.

The tokenizer and parser are **stack temporaries** — they die when `load()` returns.
Only `Config` survives.

---

## Relationships

```
  Config
  |
  +-- _servers : vector<ServerConfig>            1..n
  |        |
  |        +-- listens    : vector<Listener>     1..n
  |        +-- locations  : vector<LocationConfig>  0..n
  |
  +-- _listeners : vector<Listener>              all listens, flattened + deduped
```

Your `default.conf` produces:

```
  Config
  |
  +-- server[0]  listens: 127.0.0.1:8080, 0.0.0.0:8081
  |              locations: /  /assets  /kapouet  /uploads  /cgi-bin  /old-site
  |
  +-- server[1]  listens: 0.0.0.0:8082
  |              locations: /  /api
  |
  +-- _listeners: 127.0.0.1:8080  0.0.0.0:8081  0.0.0.0:8082
```

| class | holds | answers |
|---|---|---|
| `Config` | all servers + unique listeners | `matchServer(listener, host)` |
| `ServerConfig` | one `server { }` block | `matchLocation(path)`, `errorPage(code)` |
| `LocationConfig` | one `location { }` block | `isMethodAllowed()`, `cgiInterpreter()` |
| `Listener` | one `host:port` | `key()` -> `"0.0.0.0:8081"` |

`matchServer()` and `matchLocation()` return pointers **into** `Config`; `NULL` = no match.
Valid as long as `Config` is alive.

---

## Matching rules

### Which server — `matchServer(listener, hostHeader)`

```
  1. keep only servers that listen on this host:port
  2. of those, the first whose server_name equals the Host header
  3. no name match -> the FIRST one declared for that listener (the default)
  4. nothing listens there -> NULL
```

The `Host` header is normalised first: `"Second.Test:8081"` -> strip `:port` ->
lowercase -> `"second.test"`. `server_name` values are lowercased at load time, so the
comparison is a plain `==`. Host names are case-insensitive (RFC 9110); paths are not.

Step 1 plus step 2 is **virtual hosting** — several sites on one socket, told apart
only by `Host`:

```
  0.0.0.0:8081  +--> Host: webserv.test  -> server[0]   www/site
                +--> Host: second.test   -> server[1]   www/second
                +--> Host: anything else -> server[0]   (first declared)
```

### Which location — `matchLocation(path)`

```
  longest matching prefix wins; order in the file is irrelevant
```

Two refinements:

- **The prefix must end on a boundary.** `/assetsfoo` does *not* match `location /assets`
  — the match has to end at a `/` or at end-of-string. (nginx uses a pure string prefix
  and would match it; we deliberately don't.)
- **Trailing slashes are stripped at load.** `location /trailing/` is stored as
  `/trailing`, so both spellings behave the same.

Worked example. A server declaring four locations:

```
  location /          location /a          location /a/b          location /assets
```

| request path | matches | why |
|---|---|---|
| `/` | `/` | only `/` is a prefix |
| `/index.html` | `/` | no other location is a prefix |
| `/assets/x.png` | `/assets` | both `/` and `/assets` fit; `/assets` is longer |
| `/assetsfoo` | `/` | `/assets` is rejected by the boundary rule |
| `/a/b/c/d` | `/a/b` | `/`, `/a` and `/a/b` all fit; longest wins |
| `/a/x` | `/a` | `/a/b` is not a prefix of `/a/x` |
| `/a/bc` | `/a` | `/a/b` is rejected by the boundary rule |
| `/nope` | `/` | nothing more specific fits, so the catch-all takes it |

The boundary rule, up close — the character right after the prefix must be `/`
or nothing at all:


If this server had no `location /`, the rows above that fall back to `/` would return
`NULL` instead — meaning no location matched at all.

---

## Grammar

```
  config     = server+
  server     = "server" "{" ( directive | location )* "}"
  location   = "location" path "{" directive* "}"
  directive  = name word* ";"
```

One `{ }` level in the file = one recursion level in the parser:

```
  server {                 ->  parseServer()
      listen 8081;         ->      applyServerDirective()
      location /a {        ->      parseLocation()
          allow_methods    ->          applyLocationDirective()
      }
  }
```

---