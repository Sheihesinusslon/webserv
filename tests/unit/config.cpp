#include "webserv.hpp"
#include "config/Config.hpp"

#include <iostream>
#include <string>

static int	g_passed = 0;
static int	g_failed = 0;

static void	check(const std::string &name, bool condition)
{
	std::cout << (condition ? "OK   " : "KO   ") << name << std::endl;
	if (condition)
		g_passed++;
	else
		g_failed++;
}

static void	checkEqual(const std::string &name, const std::string &got,
				const std::string &want)
{
	if (got == want)
		check(name, true);
	else
	{
		std::cout << "KO   " << name << " (want '" << want << "', got '"
			<< got << "')" << std::endl;
		g_failed++;
	}
}

static void	checkEqual(const std::string &name, std::size_t got, std::size_t want)
{
	if (got == want)
		check(name, true);
	else
	{
		std::cout << "KO   " << name << " (want " << want << ", got " << got
			<< ")" << std::endl;
		g_failed++;
	}
}

static bool	load(Config &config, const std::string &path)
{
	if (config.load(path))
		return (true);
	std::cout << "KO   load " << path << ": " << config.error() << std::endl;
	g_failed++;
	return (false);
}

static const LocationConfig	*declared(const ServerConfig &server,
								const std::string &path)
{
	std::size_t	i;

	for (i = 0; i < server.locations.size(); i++)
	{
		if (server.locations[i].path == path)
			return (&server.locations[i]);
	}
	return (NULL);
}

static void	testFormatting(void)
{
	Config	compact;
	Config	comments;
	Config	crlf;

	std::cout << "-- tokenizer tolerates formatting" << std::endl;
	if (load(compact, "tests/configs/valid/compact.conf"))
	{
		check("compact: server parsed",
			compact.servers()[0].listensOn(Listener(DEFAULT_HOST, 8104)));
		checkEqual("compact: both locations parsed",
			compact.servers()[0].locations.size(), 2);
		check("compact: /a exists", declared(compact.servers()[0], "/a") != NULL);
	}
	if (load(comments, "tests/configs/valid/comments.conf"))
	{
		check("comments: listen survives inline comment",
			comments.servers()[0].listensOn(Listener(DEFAULT_HOST, 8105)));
		checkEqual("comments: root survives comment lines",
			comments.servers()[0].root, "www/site");
		checkEqual("comments: location survives in-block comment",
			comments.servers()[0].locations.size(), 1);
	}
	if (load(crlf, "tests/configs/valid/crlf.conf"))
	{
		check("CRLF: listen parsed",
			crlf.servers()[0].listensOn(Listener(DEFAULT_HOST, 8106)));
		check("CRLF: no stray \\r in location path",
			declared(crlf.servers()[0], "/") != NULL);
	}
}

static void	testListenForms(void)
{
	Config	config;

	std::cout << "-- listen forms" << std::endl;
	if (!load(config, "tests/configs/valid/listen_forms.conf"))
		return ;
	const ServerConfig	&server = config.servers()[0];
	check("bare port gets the default host",
		server.listensOn(Listener(DEFAULT_HOST, 8090)));
	check("host:port is split", server.listensOn(Listener("127.0.0.1", 8091)));
	check("host alone gets port 80", server.listensOn(Listener("localhost", 80)));
	checkEqual("duplicate listen deduped inside the server",
		server.listens.size(), 3);
	checkEqual("config listeners are unique", config.listeners().size(), 3);
	check("deduped list still contains each distinct listener",
		config.listeners()[0] == Listener(DEFAULT_HOST, 8090)
		&& config.listeners()[1] == Listener("127.0.0.1", 8091)
		&& config.listeners()[2] == Listener("localhost", 80));
	check("listener key format",
		Listener("127.0.0.1", 8091).key() == "127.0.0.1:8091");
}

static void	testListenHosts(void)
{
	Config	config;

	std::cout << "-- listen host spellings" << std::endl;
	if (!load(config, "tests/configs/valid/listen_hosts.conf"))
		return ;
	const ServerConfig	&server = config.servers()[0];
	check("underscore host kept", server.listensOn(Listener("my_host.local", 8500)));
	check("ipv4-mapped ipv6 kept",
		server.listensOn(Listener("[::ffff:192.0.2.1]", 8501)));
	check("ipv6 zone id kept", server.listensOn(Listener("[fe80::1%eth0]", 8502)));
	check("bracketed ipv6 with port", server.listensOn(Listener("[::1]", 8503)));
	check("bare ipv6 gets port 80", server.listensOn(Listener("[::1]", 80)));
	check("ipv6 wildcard kept", server.listensOn(Listener("[::]", 8504)));
	checkEqual("six distinct listeners", config.listeners().size(), 6);
}

static void	testInheritance(void)
{
	Config	config;

	std::cout << "-- inheritance" << std::endl;
	if (!load(config, "tests/configs/valid/inherit.conf"))
		return ;
	const LocationConfig	*child = declared(config.servers()[0], "/child");
	const LocationConfig	*own = declared(config.servers()[0], "/own");
	if (child == NULL || own == NULL)
	{
		check("inherit.conf has /child and /own", false);
		return ;
	}
	checkEqual("root inherited", child->root, "www/site");
	checkEqual("index inherited", child->index, "home.html");
	check("autoindex inherited", child->autoindex == true);
	checkEqual("body size inherited", child->clientMaxBodySize, 4096);
	checkEqual("root overridden", own->root, "www/other");
	checkEqual("index overridden", own->index, "other.html");
	check("autoindex overridden", own->autoindex == false);
	checkEqual("body size overridden", own->clientMaxBodySize, 8192);
	check("has-flags reflect the file: /child",
		!child->hasRoot && !child->hasIndex && !child->hasAutoindex
		&& !child->hasClientMaxBodySize);
	check("has-flags reflect the file: /own",
		own->hasRoot && own->hasIndex && own->hasAutoindex
		&& own->hasClientMaxBodySize);
}

static void	testDirectives(void)
{
	Config	multi;
	Config	main;

	std::cout << "-- directives are stored" << std::endl;
	if (load(multi, "tests/configs/valid/multi_server.conf"))
	{
		const ServerConfig		&first = multi.servers()[0];
		const ServerConfig		&second = multi.servers()[1];
		const LocationConfig	*root = declared(first, "/");
		const LocationConfig	*cgi = declared(second, "/cgi");
		const LocationConfig	*gone = declared(second, "/gone");

		checkEqual("two servers parsed", multi.servers().size(), 2);
		check("second server listens on 8103",
			second.listensOn(Listener(DEFAULT_HOST, 8103)));
		checkEqual("server_name list length", first.serverNames.size(), 2);
		check("server_name list content",
			first.hasServerName("a.local") && first.hasServerName("b.local"));
		check("methods kept in order", root != NULL
			&& root->allowMethods.size() == 3
			&& root->allowMethods[0] == METHOD_GET
			&& root->allowMethods[1] == METHOD_POST
			&& root->allowMethods[2] == METHOD_DELETE);
		checkEqual("cgi extension mapping",
			cgi ? cgi->cgiInterpreter(".py") : "<no /cgi>", "/usr/bin/python3");
		check("redirect stored", gone != NULL && gone->hasRedirect()
			&& gone->redirectCode == 301 && gone->redirectTarget == "/");
	}
	if (load(main, "config/default.conf"))
	{
		const LocationConfig	*uploads = declared(main.servers()[0], "/uploads");

		checkEqual("upload store stored",
			uploads ? uploads->uploadStore : "<no /uploads>", "www/uploads");
		check("upload store means uploads allowed",
			uploads != NULL && uploads->uploadsAllowed());
		checkEqual("error page stored", main.servers()[0].errorPage(413),
			"www/errors/413.html");
		checkEqual("unset error page is empty", main.servers()[0].errorPage(418), "");
	}
}

static void	testDefaultServer(void)
{
	Config	config;

	std::cout << "-- nameless server is the listener's default" << std::endl;
	if (!load(config, "tests/configs/valid/default_and_named.conf"))
		return ;
	const Listener		port(DEFAULT_HOST, 8302);
	const ServerConfig	*named = &config.servers()[0];
	const ServerConfig	*nameless = &config.servers()[1];

	checkEqual("both blocks share one listener", config.listeners().size(), 1);
	check("fixture: first block is named", !named->serverNames.empty());
	check("fixture: second block is nameless", nameless->serverNames.empty());
	check("Host matching the name picks the named block",
		config.matchServer(port, "named.test") == named);
	check("unknown Host falls to the nameless block, not the first declared",
		config.matchServer(port, "other.test") == nameless);
	check("empty Host falls to the nameless block",
		config.matchServer(port, "") == nameless);
}

int	main(void)
{
	testFormatting();
	testListenForms();
	testListenHosts();
	testInheritance();
	testDirectives();
	testDefaultServer();
	std::cout << "unit passed: " << g_passed << "   failed: " << g_failed
		<< std::endl;
	return (g_failed == 0 ? 0 : 1);
}
