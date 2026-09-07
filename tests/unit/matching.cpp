#include "webserv.hpp"
#include "config/Config.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static int	g_passed = 0;
static int	g_failed = 0;

static void	check(const std::string &name, bool condition)
{
	if (condition)
	{
		std::cout << "OK   " << name << std::endl;
		g_passed++;
	}
	else
	{
		std::cout << "KO   " << name << std::endl;
		g_failed++;
	}
}

static void	checkEqual(const std::string &name, const std::string &got,
				const std::string &want)
{
	if (got == want)
	{
		std::cout << "OK   " << name << std::endl;
		g_passed++;
	}
	else
	{
		std::cout << "KO   " << name << " (want '" << want
			<< "', got '" << got << "')" << std::endl;
		g_failed++;
	}
}

static std::string	rootOf(const ServerConfig *server)
{
	if (server == NULL)
		return ("<null>");
	return (server->root);
}

static std::string	pathOf(const LocationConfig *location)
{
	if (location == NULL)
		return ("<null>");
	return (location->path);
}

static std::string	matchedRoot(const Config &config, const std::string &host,
				int port, const std::string &hostHeader)
{
	return (rootOf(config.matchServer(Listener(host, port), hostHeader)));
}

static std::string	matchedPath(const ServerConfig &server,
				const std::string &path)
{
	return (pathOf(server.matchLocation(path)));
}

static void	testVirtualHosting(const Config &config)
{
	std::cout << "-- matchServer: virtual hosting" << std::endl;
	checkEqual("exact name picks first server",
		matchedRoot(config, DEFAULT_HOST, 8081, "webserv.test"), "www/site");
	checkEqual("exact name picks second server",
		matchedRoot(config, DEFAULT_HOST, 8081, "second.test"), "www/second");
	checkEqual("alias name picks second server",
		matchedRoot(config, DEFAULT_HOST, 8081, "alias.test"), "www/second");
	checkEqual("unknown host falls back to first declared",
		matchedRoot(config, DEFAULT_HOST, 8081, "nonsense.test"), "www/site");
	checkEqual("empty host falls back to first declared",
		matchedRoot(config, DEFAULT_HOST, 8081, ""), "www/site");

	std::cout << "-- matchServer: host header forms" << std::endl;
	checkEqual("port is stripped from host header",
		matchedRoot(config, DEFAULT_HOST, 8081, "second.test:8081"), "www/second");
	checkEqual("host match is case-insensitive",
		matchedRoot(config, DEFAULT_HOST, 8081, "SECOND.TEST"), "www/second");
	checkEqual("config name case is normalised too",
		matchedRoot(config, DEFAULT_HOST, 8081, "mixed.test"), "www/second");
	checkEqual("case-insensitive with port",
		matchedRoot(config, DEFAULT_HOST, 8081, "Webserv.Test:8081"), "www/site");
	checkEqual("ipv6 literal with port",
		matchedRoot(config, DEFAULT_HOST, 8081, "[::1]:8081"), "www/second");
	checkEqual("bare ipv6 literal keeps its colons",
		matchedRoot(config, DEFAULT_HOST, 8081, "[::1]"), "www/second");
	checkEqual("unterminated bracket is left alone",
		matchedRoot(config, DEFAULT_HOST, 8081, "[::1"), "www/site");

	std::cout << "-- matchServer: listener narrows the candidates" << std::endl;
	checkEqual("name not listening here falls back",
		matchedRoot(config, DEFAULT_HOST, 8080, "second.test"), "www/site");
	checkEqual("only server on 8082 wins whatever the host",
		matchedRoot(config, DEFAULT_HOST, 8082, "webserv.test"), "www/second");
	checkEqual("unknown listener matches nothing",
		matchedRoot(config, DEFAULT_HOST, 9999, "webserv.test"), "<null>");
	checkEqual("right port, wrong interface matches nothing",
		matchedRoot(config, "127.0.0.1", 8081, "webserv.test"), "<null>");

	std::cout << "-- listeners" << std::endl;
	check("three unique listeners", config.listeners().size() == 3);
	check("server 0 listens on 8081",
		config.servers()[0].listensOn(Listener(DEFAULT_HOST, 8081)));
	check("server 1 listens on 8081",
		config.servers()[1].listensOn(Listener(DEFAULT_HOST, 8081)));
	check("server 0 does not listen on 8082",
		!config.servers()[0].listensOn(Listener(DEFAULT_HOST, 8082)));
	checkEqual("listener key format",
		Listener("127.0.0.1", 8080).key(), "127.0.0.1:8080");
}

static void	testLocationMatching(const Config &config)
{
	const ServerConfig	&server = config.servers()[0];
	const ServerConfig	&bare = config.servers()[1];

	std::cout << "-- matchLocation: longest prefix wins" << std::endl;
	checkEqual("root path", matchedPath(server, "/"), "/");
	checkEqual("file under root", matchedPath(server, "/index.html"), "/");
	checkEqual("exact location", matchedPath(server, "/assets"), "/assets");
	checkEqual("file under location",
		matchedPath(server, "/assets/img/logo.png"), "/assets");
	checkEqual("longer location wins", matchedPath(server, "/a/b"), "/a/b");
	checkEqual("deep path takes longest",
		matchedPath(server, "/a/b/c/d"), "/a/b");
	checkEqual("shorter location when longer does not fit",
		matchedPath(server, "/a/x"), "/a");

	std::cout << "-- matchLocation: prefix must end on a boundary" << std::endl;
	checkEqual("/assetsfoo is not under /assets",
		matchedPath(server, "/assetsfoo"), "/");
	checkEqual("/a/bc is not under /a/b", matchedPath(server, "/a/bc"), "/a");
	checkEqual("trailing slash on request is fine",
		matchedPath(server, "/assets/"), "/assets");

	std::cout << "-- matchLocation: config trailing slash normalised" << std::endl;
	checkEqual("/trailing/ stored as /trailing",
		matchedPath(server, "/trailing"), "/trailing");
	checkEqual("path under normalised location",
		matchedPath(server, "/trailing/x"), "/trailing");

	std::cout << "-- matchLocation: no match" << std::endl;
	checkEqual("no catch-all location returns null",
		matchedPath(bare, "/nope"), "<null>");
	checkEqual("matching location still found",
		matchedPath(bare, "/only/deep/path"), "/only");
}

static void	testLocationRules(const Config &config)
{
	const ServerConfig	&server = config.servers()[0];
	const LocationConfig	*root = server.matchLocation("/");
	const LocationConfig	*uploads = server.matchLocation("/uploads");
	const LocationConfig	*cgi = server.matchLocation("/cgi-bin");
	const LocationConfig	*old = server.matchLocation("/old");

	std::cout << "-- location rules" << std::endl;
	check("GET allowed on /", root->isMethodAllowed(METHOD_GET));
	check("DELETE not allowed on /", !root->isMethodAllowed(METHOD_DELETE));
	check("DELETE allowed on /uploads", uploads->isMethodAllowed(METHOD_DELETE));
	check("uploads enabled on /uploads", uploads->uploadsAllowed());
	check("uploads disabled on /", !root->uploadsAllowed());
	checkEqual("cgi interpreter for .py",
		cgi->cgiInterpreter(".py"), "/usr/bin/python3");
	checkEqual("no cgi interpreter for .php", cgi->cgiInterpreter(".php"), "");
	check("no cgi on /", root->cgiInterpreter(".py").empty());
	check("/old redirects", old->hasRedirect());
	check("/old redirect code", old->redirectCode == 301);
	checkEqual("/old redirect target", old->redirectTarget, "/new");
	check("/ does not redirect", !root->hasRedirect());
	checkEqual("configured error page", server.errorPage(404),
		"www/errors/404.html");
	checkEqual("unconfigured error page", server.errorPage(500), "");
	check("inherited root on /assets",
		server.matchLocation("/assets")->root == "www/site");
}

int	main(void)
{
	Config	vhost;
	Config	routing;

	if (!vhost.load("tests/configs/valid/vhost.conf"))
	{
		std::cout << "KO   load vhost.conf: " << vhost.error() << std::endl;
		return (1);
	}
	if (!routing.load("tests/configs/valid/routing.conf"))
	{
		std::cout << "KO   load routing.conf: " << routing.error() << std::endl;
		return (1);
	}
	testVirtualHosting(vhost);
	testLocationMatching(routing);
	testLocationRules(routing);
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
