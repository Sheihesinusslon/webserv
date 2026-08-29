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
  | Config            |  validate() -> inherit() -> collectListeners()
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