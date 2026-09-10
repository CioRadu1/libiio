#!/usr/bin/env bash
#
# Copyright (c) 2025 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT

set -u

PROTO=${1:-}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NOOS_DIR=$(dirname "$SCRIPT_DIR")
TEST_DIR=$NOOS_DIR/build-tests/tests
IIOD_PORT=30431
USB_VID_PID=0456:b673
TIMEOUT=60

TESTS="test_context test_device test_channel test_rw test_buffer test_attr"

usage() {
	cat >&2 <<USAGE
usage: $(basename "$0") {uart|network|usb}

Builds and flashes the matching firmware preset, then runs the _remote test
binaries against the board over that transport.

Environment overrides:
  NOOS_TESTS_PORT   serial device for the uart protocol (default: autodetect)
  NOOS_TESTS_HOST   board address for the network protocol
  NOOS_TESTS_URI    full libiio URI, skips all detection
  SKIP_FLASH=1      reuse whatever is already running on the board
USAGE
	exit 1
}

die() {
	echo "error: $*" >&2
	exit 1
}

case $PROTO in
uart|network|usb)
	;;
*)
	usage
	;;
esac

build_tests() {
	echo "== building host test binaries =="
	cmake -B "$NOOS_DIR/build-tests" -S "$NOOS_DIR" -G Ninja \
	      -DWITH_TESTS=ON -DTESTS_DEBUG=ON >/dev/null || \
		die "cannot configure build-tests"

	local targets=""
	local t
	for t in $TESTS; do
		targets="$targets ${t}_remote"
	done

	# shellcheck disable=SC2086
	cmake --build "$NOOS_DIR/build-tests" --target $targets >/dev/null || \
		die "cannot build the remote test binaries"
}

flash_board() {
	if [ "${SKIP_FLASH:-0}" = "1" ]; then
		echo "== SKIP_FLASH=1, keeping the current firmware =="
		return
	fi

	echo "== building and flashing the $PROTO firmware =="
	cmake --workflow --preset "flash-$PROTO" || \
		die "the flash-$PROTO workflow failed"
}

prompt_usbip() {
	cat <<MSG

The board was just reset, so WSL lost its USB/IP attachment.
In an administrator PowerShell on Windows, run:

  usbipd list
  usbipd attach --wsl --busid <the board's busid>

Do not attach the LAN9500A ethernet adapter.
MSG
	read -r -p "Press enter once the board is attached: " _
}

resolve_uart() {
	local port=${NOOS_TESTS_PORT:-}

	if [ -z "$port" ]; then
		port=$(ls /dev/ttyACM* 2>/dev/null | head -1)
	fi

	[ -n "$port" ] || die "no /dev/ttyACM* found; set NOOS_TESTS_PORT"
	[ -r "$port" ] || die "$port is not readable"

	URI="serial:$port,115200"
}

resolve_network() {
	local host=${NOOS_TESTS_HOST:-}

	if [ -z "$host" ]; then
		host=$(ip route | awk '/default/{print $3; exit}')
	fi

	[ -n "$host" ] || die "cannot determine the windows host address"

	if ! timeout 5 bash -c "cat < /dev/null > /dev/tcp/$host/$IIOD_PORT" \
	     2>/dev/null; then
		cat >&2 <<MSG
error: $host:$IIOD_PORT is not reachable.

WSL2 cannot route to the board directly, so Windows has to forward the port.
In an administrator PowerShell, run once:

  netsh interface portproxy add v4tov4 listenport=$IIOD_PORT \\
        listenaddress=0.0.0.0 connectport=$IIOD_PORT connectaddress=192.0.2.1

Then re-run this script.
MSG
		exit 1
	fi

	URI="ip:$host"
}

resolve_usb() {
	if ! lsusb -d "$USB_VID_PID" >/dev/null 2>&1; then
		die "no $USB_VID_PID device visible; attach it with usbipd first"
	fi

	URI="usb:"
}

build_tests
flash_board

if [ -n "${NOOS_TESTS_URI:-}" ]; then
	URI=$NOOS_TESTS_URI
else
	case $PROTO in
	uart)
		prompt_usbip
		resolve_uart
		;;
	network)
		resolve_network
		;;
	usb)
		prompt_usbip
		resolve_usb
		;;
	esac
fi

export NOOS_TESTS_URI=$URI

echo
echo "== running the $PROTO suite against $URI =="

passed=0
failed=0
timedout=0
results=""

for t in $TESTS; do
	bin=$TEST_DIR/${t}_remote

	[ -x "$bin" ] || die "$bin is missing"

	echo
	echo "---- $t ----"
	timeout "$TIMEOUT" "$bin"
	rc=$?

	case $rc in
	0)
		passed=$((passed + 1))
		results="$results\n  PASS    $t"
		;;
	124)
		timedout=$((timedout + 1))
		results="$results\n  TIMEOUT $t"
		echo "$t exceeded ${TIMEOUT}s"
		if [ "$PROTO" = "usb" ]; then
			echo "release the device on windows with: usbipd detach --busid <busid>"
		fi
		;;
	*)
		failed=$((failed + 1))
		results="$results\n  FAIL    $t (exit $rc)"
		;;
	esac
done

echo
echo "== $PROTO summary ($URI) =="
printf '%b\n' "$results"
echo "  passed $passed, failed $failed, timed out $timedout"

[ "$failed" -eq 0 ] && [ "$timedout" -eq 0 ]
