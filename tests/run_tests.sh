#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

BIN=./webserv
PASSED=0
FAILED=0

run_test()
{
	NAME=$1
	EXPECTED=$2
	shift 2
	"$@" >/dev/null 2>&1
	STATUS=$?
	if [ $STATUS -eq $EXPECTED ]; then
		echo "OK   $NAME"
		PASSED=$((PASSED + 1))
	else
		echo "KO   $NAME (expected exit $EXPECTED, got $STATUS)"
		FAILED=$((FAILED + 1))
	fi
}

if [ ! -x $BIN ]; then
	echo "KO   $BIN not found, run make first"
	exit 1
fi

run_test "no argument"        0 $BIN
run_test "one argument"       0 $BIN config/default.conf
run_test "too many arguments" 1 $BIN a b

echo "passed: $PASSED   failed: $FAILED"
[ $FAILED -eq 0 ]
