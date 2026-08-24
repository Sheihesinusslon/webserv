*This project has been created as part of the 42 curriculum by upetrova, ngusev.*

# webserv

## Description

TODO

`webserv` is an HTTP server written from scratch in C++ 98. It parses an
NGINX-inspired configuration file, listens on one or more `interface:port`
pairs, and serves static content, file uploads and CGI scripts to standard web
browsers. All client I/O is non-blocking and driven by a single `poll()` (or
equivalent) call.

## Features

TODO

## Instructions

### Build

```sh
make          # builds ./webserv
make bonus    # builds ./webserv_bonus
make clean    # removes object files
make fclean   # removes object files and binaries
make re       # fclean + all
make test     # builds and runs tests/run_tests.sh
```

The project is compiled with `c++ -Wall -Wextra -Werror -std=c++98`

### Run

```sh
./webserv [configuration file]
```

If no configuration file is given, `config/default.conf` is used.

## Configuration

TODO

## Project structure

```
.
├── Makefile
├── config/           configuration files
├── include/          mandatory headers
│   └── bonus/        bonus-only headers
├── src/              mandatory sources
│   └── bonus/        bonus-only sources
├── tests/            test scripts and the provided testers
└── docs/             subject and reference material
```

## Technical choices

TODO

## Resources

- [Common Gateway Interface (CGI)](https://en.wikipedia.org/wiki/Common_Gateway_Interface) — the CGI link given in the subject
- [RFC 2616 — HTTP/1.1](https://datatracker.ietf.org/doc/html/rfc2616)
- [MDN — HTTP overview](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [NGINX — ngx_http_core_module (server / location directives)](https://nginx.org/en/docs/http/ngx_http_core_module.html)

### AI usage

TODO — describe which tasks and which parts of the project AI was used for.
