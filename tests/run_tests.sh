#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

BIN=./webserv
PASSED=0
FAILED=0

ok()
{
	echo "OK   $1"
	PASSED=$((PASSED + 1))
}

ko()
{
	echo "KO   $1"
	FAILED=$((FAILED + 1))
}

run_test()
{
	NAME=$1
	EXPECTED=$2
	shift 2
	"$@" >/dev/null 2>&1
	STATUS=$?
	if [ $STATUS -eq $EXPECTED ]; then
		ok "$NAME"
	else
		ko "$NAME (expected exit $EXPECTED, got $STATUS)"
	fi
}

expect_output()
{
	NAME=$1
	NEEDLE=$2
	shift 2
	if "$@" 2>&1 | grep -qF "$NEEDLE"; then
		ok "$NAME"
	else
		ko "$NAME (missing: $NEEDLE)"
	fi
}

expect_count()
{
	NAME=$1
	NEEDLE=$2
	WANT=$3
	shift 3
	GOT=$("$@" 2>&1 | grep -cF "$NEEDLE")
	if [ "$GOT" -eq "$WANT" ]; then
		ok "$NAME"
	else
		ko "$NAME (expected $WANT of '$NEEDLE', got $GOT)"
	fi
}

if [ ! -x $BIN ]; then
	echo "KO   $BIN not found, run make first"
	exit 1
fi

echo "-- arguments"
run_test "no argument"           0 $BIN
run_test "one argument"          0 $BIN config/default.conf
run_test "too many arguments"    1 $BIN a b
run_test "missing config file"   1 $BIN config/does_not_exist.conf
run_test "config path is a dir"  1 $BIN config

echo "-- valid configs are accepted"
for CONF in tests/configs/valid/*.conf; do
	run_test "valid   $(basename "$CONF")" 0 $BIN "$CONF"
done

echo "-- invalid configs are rejected"
for CONF in tests/configs/invalid/*.conf; do
	run_test "invalid $(basename "$CONF")" 1 $BIN "$CONF"
done

echo "-- tokenizer tolerates formatting"
expect_output "compact: no spaces at all"   "location[/a] root"    $BIN tests/configs/valid/compact.conf
expect_output "comments are skipped"        "listen 0.0.0.0:8105"  $BIN tests/configs/valid/comments.conf
expect_output "CRLF line endings"           "listen 0.0.0.0:8106"  $BIN tests/configs/valid/crlf.conf

echo "-- listen forms"
expect_output "bare port gets default host" "listen 0.0.0.0:8090"    $BIN tests/configs/valid/listen_forms.conf
expect_output "host:port is split"          "listen 127.0.0.1:8091"  $BIN tests/configs/valid/listen_forms.conf
expect_output "host alone gets port 80"     "listen localhost:80"    $BIN tests/configs/valid/listen_forms.conf
expect_count  "duplicate listen deduped"    "server[0] listen 0.0.0.0:8090" 1  $BIN tests/configs/valid/listen_forms.conf
expect_count  "listeners are unique"        "listener 0.0.0.0:8090" 1          $BIN tests/configs/valid/listen_forms.conf

echo "-- inheritance"
expect_output "root inherited"        "location[/child] root www/site"             $BIN tests/configs/valid/inherit.conf
expect_output "index inherited"       "location[/child] index home.html"           $BIN tests/configs/valid/inherit.conf
expect_output "autoindex inherited"   "location[/child] autoindex on"              $BIN tests/configs/valid/inherit.conf
expect_output "body size inherited"   "location[/child] client_max_body_size 4096" $BIN tests/configs/valid/inherit.conf
expect_output "root overridden"       "location[/own] root www/other"              $BIN tests/configs/valid/inherit.conf
expect_output "autoindex overridden"  "location[/own] autoindex off"               $BIN tests/configs/valid/inherit.conf
expect_output "body size overridden"  "location[/own] client_max_body_size 8192"   $BIN tests/configs/valid/inherit.conf

echo "-- directives are stored"
expect_output "methods kept in order"  "location[/] allow_methods DELETE"    $BIN tests/configs/valid/multi_server.conf
expect_output "server_name list"       "server_name b.local"                 $BIN tests/configs/valid/multi_server.conf
expect_output "cgi extension mapping"  "cgi_ext .py /usr/bin/python3"        $BIN tests/configs/valid/multi_server.conf
expect_output "redirect stored"        "return 301 /"                        $BIN tests/configs/valid/multi_server.conf
expect_output "second server parsed"   "server[1] listen 0.0.0.0:8103"       $BIN tests/configs/valid/multi_server.conf
expect_output "upload store stored"    "upload_store www/uploads"            $BIN config/default.conf
expect_output "error page stored"      "error_page 413 www/errors/413.html"  $BIN config/default.conf

echo "-- error messages point at the right line"
expect_output "unknown directive line"   "line 3:"                  $BIN tests/configs/invalid/unknown_server_directive.conf
expect_output "missing semicolon line"   "line 2:"                  $BIN tests/configs/invalid/missing_semicolon.conf
expect_output "missing semicolon cause" "'listen' expects 1"       $BIN tests/configs/invalid/missing_semicolon.conf
expect_output "names the bad directive"  "autoindeks"               $BIN tests/configs/invalid/unknown_server_directive.conf
expect_output "names the bad method"     "PATCH"                    $BIN tests/configs/invalid/bad_method.conf
expect_output "reports missing listen"   "no 'listen'"              $BIN tests/configs/invalid/no_listen.conf
expect_output "reports duplicate loc"    "duplicate location '/a'"  $BIN tests/configs/invalid/duplicate_location.conf

echo "-- unit tests against the config objects"
UNIT_SRC="src/config/Config.cpp src/config/ConfigParser.cpp \
	src/config/ConfigTokenizer.cpp src/config/Listener.cpp \
	src/config/LocationConfig.cpp src/config/ServerConfig.cpp"
UNIT_CXXFLAGS="${UNIT_CXXFLAGS:--Wall -Wextra -Werror -std=c++98}"
for UNIT_TEST in tests/unit/*.cpp; do
	UNIT_BIN="$(mktemp -u /tmp/webserv_unit.XXXXXX)"
	if c++ $UNIT_CXXFLAGS -Iinclude \
		"$UNIT_TEST" $UNIT_SRC -o "$UNIT_BIN" 2>/dev/null; then
		UNIT_OUT=$("$UNIT_BIN")
		echo "$UNIT_OUT" | grep -v "^unit passed:"
		PASSED=$((PASSED + $(echo "$UNIT_OUT" | grep -c "^OK")))
		FAILED=$((FAILED + $(echo "$UNIT_OUT" | grep -c "^KO")))
		rm -f "$UNIT_BIN"
	else
		ko "$(basename "$UNIT_TEST") failed to compile"
	fi
done

echo
echo "passed: $PASSED   failed: $FAILED"
[ $FAILED -eq 0 ]
