#ifndef WEBSERV_HPP
# define WEBSERV_HPP

# include <iostream>
# include <map>
# include <sstream>
# include <string>
# include <vector>

# define WEBSERV_NAME "webserv"
# define DEFAULT_CONFIG "config/default.conf"

# define DEFAULT_HOST "0.0.0.0"
# define DEFAULT_PORT 80
# define DEFAULT_INDEX "index.html"
# define DEFAULT_CLIENT_MAX_BODY_SIZE 1048576

# define MIN_PORT 1
# define MAX_PORT 65535

# define MIN_STATUS_CODE 300
# define MAX_STATUS_CODE 599

# define METHOD_GET "GET"
# define METHOD_POST "POST"
# define METHOD_DELETE "DELETE"

# define MAX_SERVER_NAMES 64
# define MAX_ALLOW_METHODS 8

#endif
