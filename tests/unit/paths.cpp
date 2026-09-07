#include "webserv.hpp"
#include "config/Config.hpp"

#include <iostream>
#include <string>

static int	g_passed = 0;
static int	g_failed = 0;

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

static std::string	resolve(const ServerConfig &server, const std::string &uri)
{
	const LocationConfig	*location;

	location = server.matchLocation(uri);
	if (location == NULL)
		return ("<no location>");
	return (location->resolvePath(uri));
}

static std::string	resolveIn(const ServerConfig &server,
				const std::string &locationPath, const std::string &uri)
{
	const LocationConfig	*location;

	location = server.matchLocation(locationPath);
	if (location == NULL)
		return ("<no location>");
	return (location->resolvePath(uri));
}

static void	testSubjectExample(const ServerConfig &server)
{
	std::cout << "-- resolvePath: the subject's example" << std::endl;
	checkEqual("/kapouet rooted to /tmp/www",
		resolve(server, "/kapouet/pouic/toto/pouet"), "/tmp/www/pouic/toto/pouet");
	checkEqual("location prefix is stripped, not appended",
		resolve(server, "/kapouet/a"), "/tmp/www/a");
	checkEqual("exact location path maps to the root itself",
		resolve(server, "/kapouet"), "/tmp/www");
}

static void	testBasicMapping(const ServerConfig &server)
{
	std::cout << "-- resolvePath: basic mapping" << std::endl;
	checkEqual("root location, root uri", resolve(server, "/"), "www/site");
	checkEqual("root location, file", resolve(server, "/index.html"),
		"www/site/index.html");
	checkEqual("root location, nested file", resolve(server, "/a/b/c.txt"),
		"www/site/a/b/c.txt");
	checkEqual("inherited root is used", resolve(server, "/assets/logo.png"),
		"www/site/logo.png");
	checkEqual("deep location path", resolve(server, "/deep/nested/a/b"),
		"/srv/x/a/b");
	checkEqual("absolute root keeps its leading slash",
		resolve(server, "/kapouet/x"), "/tmp/www/x");
	checkEqual("relative root stays relative",
		resolve(server, "/x.txt"), "www/site/x.txt");
}

static void	testRootNormalisation(const ServerConfig &server)
{
	std::cout << "-- resolvePath: root spelling is normalised" << std::endl;
	checkEqual("trailing slash on root", resolve(server, "/slashroot/f.txt"),
		"/var/data/f.txt");
	checkEqual("trailing slash, exact path", resolve(server, "/slashroot"),
		"/var/data");
	checkEqual("dots inside the configured root",
		resolve(server, "/dotty/f"), "/srv/a/c/f");
	checkEqual("root of / serves from the filesystem root",
		resolve(server, "/slashonly/etc/hosts"), "/etc/hosts");
}

static void	testUriNormalisation(const ServerConfig &server)
{
	std::cout << "-- resolvePath: uri is normalised" << std::endl;
	checkEqual("double slashes collapse", resolve(server, "/kapouet//pouic"),
		"/tmp/www/pouic");
	checkEqual("trailing slash dropped", resolve(server, "/kapouet/dir/"),
		"/tmp/www/dir");
	checkEqual("single dot removed", resolve(server, "/kapouet/./x"),
		"/tmp/www/x");
	checkEqual("dotdot that stays inside is fine",
		resolve(server, "/kapouet/a/../b"), "/tmp/www/b");
	checkEqual("many dots that stay inside",
		resolve(server, "/kapouet/a/b/../../c"), "/tmp/www/c");
}

static void	testTraversalRejected(const ServerConfig &server)
{
	std::cout << "-- resolvePath: escapes are rejected" << std::endl;
	checkEqual("classic traversal",
		resolve(server, "/kapouet/../../etc/passwd"), "");
	checkEqual("single step above the root",
		resolve(server, "/kapouet/.."), "");
	checkEqual("deep traversal",
		resolve(server, "/kapouet/a/../../../../etc"), "");
	checkEqual("traversal from the root location",
		resolve(server, "/../etc/passwd"), "");
	checkEqual("traversal that lands on a sibling",
		resolveIn(server, "/deep/nested", "/deep/nested/../other"), "");
	checkEqual("sibling with a shared prefix is not inside",
		resolveIn(server, "/kapouet", "/kapouet/../wwwx"), "");
}

static void	testDefensive(const ServerConfig &server, const ServerConfig &bare)
{
	std::cout << "-- resolvePath: defensive cases" << std::endl;
	checkEqual("uri not under the location is refused",
		resolveIn(server, "/kapouet", "/elsewhere/x"), "");
	checkEqual("prefix boundary is enforced here too",
		resolveIn(server, "/kapouet", "/kapouetfoo"), "");
	checkEqual("uri shorter than the location path",
		resolveIn(server, "/kapouet", "/kap"), "");
	checkEqual("empty uri", resolveIn(server, "/kapouet", ""), "");
	checkEqual("location with no root anywhere",
		resolveIn(bare, "/orphan", "/orphan/x"), "");
}

int	main(void)
{
	Config	config;

	if (!config.load("tests/configs/valid/paths.conf"))
	{
		std::cout << "KO   load paths.conf: " << config.error() << std::endl;
		return (1);
	}
	testSubjectExample(config.servers()[0]);
	testBasicMapping(config.servers()[0]);
	testRootNormalisation(config.servers()[0]);
	testUriNormalisation(config.servers()[0]);
	testTraversalRejected(config.servers()[0]);
	testDefensive(config.servers()[0], config.servers()[1]);
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
