#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

BIN=./webserv
CHECK="$BIN -t"
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

if [ ! -x $BIN ]; then
	echo "KO   $BIN not found, run make first"
	exit 1
fi

echo "-- arguments"
run_test "no argument"           0 $CHECK
run_test "one argument"          0 $CHECK config/default.conf
run_test "too many arguments"    1 $BIN a b
run_test "unknown flag"          1 $BIN -x
run_test "-t twice"              1 $BIN -t -t
run_test "missing config file"   1 $CHECK config/does_not_exist.conf
run_test "config path is a dir"  1 $CHECK config

echo "-- -t validates, -T validates and dumps"
expect_output "-t reports success on stderr"   "test is successful"  $BIN -t config/default.conf
if [ -z "$($BIN -t config/default.conf 2>/dev/null)" ]; then ok "-t prints nothing on stdout"; else ko "-t prints nothing on stdout"; fi
expect_output "-T dumps the config"            "server[0] listen"    $BIN -T config/default.conf
expect_output "-T also reports success"        "test is successful"  $BIN -T config/default.conf
run_test      "-t and -T together rejected"    1 $BIN -t -T
run_test      "-T on an invalid config exits 1" 1 $BIN -T tests/configs/invalid/bad_port.conf

echo "-- valid configs are accepted"
for CONF in tests/configs/valid/*.conf; do
	run_test "valid   $(basename "$CONF")" 0 $CHECK "$CONF"
done

echo "-- invalid configs are rejected"
for CONF in tests/configs/invalid/*.conf; do
	run_test "invalid $(basename "$CONF")" 1 $CHECK "$CONF"
done

echo "-- error messages point at the right line"
expect_output "unknown directive line"   "line 3:"                  $CHECK tests/configs/invalid/unknown_server_directive.conf
expect_output "missing semicolon line"   "line 2:"                  $CHECK tests/configs/invalid/missing_semicolon.conf
expect_output "missing semicolon cause" "'listen' expects 1"       $CHECK tests/configs/invalid/missing_semicolon.conf
expect_output "names the bad directive"  "autoindeks"               $CHECK tests/configs/invalid/unknown_server_directive.conf
expect_output "names the bad method"     "PATCH"                    $CHECK tests/configs/invalid/bad_method.conf
expect_output "reports missing listen"   "no 'listen'"              $CHECK tests/configs/invalid/no_listen.conf
expect_output "reports duplicate loc"    "duplicate location '/a'"  $CHECK tests/configs/invalid/duplicate_location.conf

echo "-- server blocks must stay reachable"
expect_output "duplicate server_name"    "duplicate server_name 'shop.test' on listener 0.0.0.0:8300" \
	$CHECK tests/configs/invalid/duplicate_server_name.conf
expect_output "two nameless defaults"    "two server blocks without 'server_name' on listener 0.0.0.0:8301" \
	$CHECK tests/configs/invalid/two_default_servers.conf

echo "-- listen values must be a real host"
expect_output "junk host rejected"       "invalid listen value" \
	$CHECK tests/configs/invalid/bad_listen_host.conf
expect_output "junk host with port too"  "invalid listen value" \
	$CHECK tests/configs/invalid/bad_listen_host_port.conf

echo "-- server: bind, answer HTTP, stop on SIGINT"
SOCK_OUT="$(mktemp /tmp/webserv_sock.XXXXXX)"
$BIN tests/configs/valid/bind.conf > "$SOCK_OUT" 2>&1 &
SOCK_PID=$!
( sleep 10; kill -9 $SOCK_PID 2>/dev/null ) 2>/dev/null &
WATCHDOG=$!
sleep 0.3
if ss -ltn 2>/dev/null | grep -q ":8700 "; then ok "port 8700 is listening"; else ko "port 8700 is listening"; fi
if ss -ltn 2>/dev/null | grep -q "127.0.0.1:8701 "; then ok "port 8701 is loopback only"; else ko "port 8701 is loopback only"; fi
REPLY=$(timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/8700; printf "GET / HTTP/1.1\r\nHost: x\r\n\r\n" >&3; cat <&3' 2>/dev/null)
case "$REPLY" in
	"HTTP/1.1 200 OK"*) ok "answers HTTP on 8700" ;;
	*) ko "answers HTTP on 8700" ;;
esac
REPLY=$(timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/8701; printf "GET / HTTP/1.1\r\n\r\n" >&3; cat <&3' 2>/dev/null)
case "$REPLY" in
	*"Hello, world!"*) ok "answers HTTP on 8701" ;;
	*) ko "answers HTTP on 8701" ;;
esac
if kill -0 $SOCK_PID 2>/dev/null; then ok "server keeps running after requests"; else ko "server keeps running after requests"; fi
kill -INT $SOCK_PID
wait $SOCK_PID
SOCK_RC=$?
kill $WATCHDOG 2>/dev/null; wait $WATCHDOG 2>/dev/null
if [ $SOCK_RC -eq 0 ]; then ok "SIGINT: exits 0"; else ko "SIGINT: exits 0 (got $SOCK_RC)"; fi
if ss -ltn 2>/dev/null | grep -q ":8700 "; then ko "ports released on exit"; else ok "ports released on exit"; fi
if grep -q "listening on 0.0.0.0:8700" "$SOCK_OUT"; then ok "announces its listeners"; else ko "announces its listeners"; fi

$BIN tests/configs/valid/bind.conf > /dev/null 2>&1 &
SOCK_PID=$!
sleep 0.3
timeout 5 $BIN tests/configs/valid/bind.conf > "$SOCK_OUT" 2>&1
SOCK_RC=$?
if [ $SOCK_RC -eq 1 ]; then ok "second instance on the same ports exits 1"; else ko "second instance on the same ports exits 1 (got $SOCK_RC)"; fi
if grep -q "cannot bind: Address already in use" "$SOCK_OUT"; then ok "port conflict names the reason"; else ko "port conflict names the reason"; fi
kill -INT $SOCK_PID 2>/dev/null
wait $SOCK_PID 2>/dev/null
rm -f "$SOCK_OUT"

echo "-- out of memory: drop the client, keep serving"
# Probe: can the binary start under a memory cap at all? A sanitizer build cannot
# (ASan reserves shadow memory far beyond the cap). Its startup failure must not
# be mistaken for a finding, so the probe runs with ASAN_OPTIONS cleared - otherwise
# CI's log_path would capture it as a report.
if (ulimit -v 65536; ASAN_OPTIONS= $BIN -t tests/configs/valid/bind.conf) > /dev/null 2>&1; then
	SOCK_OUT="$(mktemp /tmp/webserv_oom.XXXXXX)"
	(ulimit -v 65536; exec $BIN tests/configs/valid/bind.conf) > "$SOCK_OUT" 2>&1 &
	SOCK_PID=$!
	sleep 0.3
	head -c 300M /dev/zero | timeout 20 bash -c 'cat > /dev/tcp/127.0.0.1/8700' 2>/dev/null
	sleep 0.3
	if grep -q "bad_alloc" "$SOCK_OUT"; then ok "allocation failure is caught and logged"; else ko "allocation failure is caught and logged"; fi
	if kill -0 $SOCK_PID 2>/dev/null; then ok "server survives it"; else ko "server survives it"; fi
	REPLY=$(timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/8700; printf "GET / HTTP/1.1\r\n\r\n" >&3; cat <&3' 2>/dev/null)
	case "$REPLY" in
		"HTTP/1.1 200 OK"*) ok "still answers other clients" ;;
		*) ko "still answers other clients" ;;
	esac
	kill -INT $SOCK_PID 2>/dev/null
	wait $SOCK_PID 2>/dev/null
	SOCK_RC=$?
	if [ $SOCK_RC -eq 0 ]; then ok "still exits 0 on SIGINT"; else ko "still exits 0 on SIGINT (got $SOCK_RC)"; fi
	rm -f "$SOCK_OUT"
else
	echo "SKIP the binary cannot start under ulimit -v (sanitizer build)"
fi

echo "-- unit tests against the config objects"
UNIT_SRC="src/config/Config.cpp src/config/ConfigParser.cpp \
	src/config/ConfigTokenizer.cpp src/config/Listener.cpp \
	src/config/LocationConfig.cpp src/config/ServerConfig.cpp \
	src/net/Connection.cpp src/net/EventLoop.cpp src/net/Socket.cpp"
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
