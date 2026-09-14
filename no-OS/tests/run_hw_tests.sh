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
ALL_SUITES="$TESTS test_concurrent"
MAX_PARALLEL=${MAX_PARALLEL:-4}

usage() {
	cat >&2 <<USAGE
usage: $(basename "$0") {uart|network|usb|network-multi} [suite ...]

Builds and flashes the matching firmware preset, then runs the _remote test
binaries against the board over that transport, printing every assertion as
it happens.

network-multi uses the network firmware and runs the suites concurrently, in
batches of MAX_PARALLEL, so several clients are attached to the board at once.

Suites (default: all but test_concurrent, in this order):
  test_context test_device test_channel test_rw test_buffer test_attr
  test_concurrent

Environment overrides:
  NOOS_TESTS_PORT   serial device for the uart protocol (default: autodetect)
  NOOS_TESTS_HOST   board address for the network protocol (default: 192.0.2.1)
  NOOS_TESTS_URI    full libiio URI, skips all detection
  MAX_PARALLEL      concurrent clients for network-multi (default: 4)
  SKIP_FLASH=1      reuse whatever is already running on the board
  QUIET_BUILD=1     hide the compiler output
USAGE
	exit 1
}

die() {
	echo "error: $*" >&2
	exit 1
}

MODE=$PROTO
MULTI=0

case $PROTO in
uart|network|usb)
	shift
	;;
network-multi)
	MULTI=1
	PROTO=network
	shift
	;;
*)
	usage
	;;
esac

if [ "$#" -gt 0 ]; then
	for want in "$@"; do
		case " $ALL_SUITES " in
		*" $want "*)
			;;
		*)
			die "unknown suite '$want'"
			;;
		esac
	done
	TESTS="$*"
fi

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

	if [ "${QUIET_BUILD:-0}" = "1" ]; then
		# shellcheck disable=SC2086
		cmake --build "$NOOS_DIR/build-tests" --target $targets >/dev/null || \
			die "cannot build the remote test binaries"
	else
		# shellcheck disable=SC2086
		cmake --build "$NOOS_DIR/build-tests" --target $targets || \
			die "cannot build the remote test binaries"
	fi
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
	local host=${NOOS_TESTS_HOST:-192.0.2.1}

	if ! timeout 5 bash -c "cat < /dev/null > /dev/tcp/$host/$IIOD_PORT" \
	     2>/dev/null; then
		cat >&2 <<MSG
error: $host:$IIOD_PORT is not reachable.

Hand the board's ethernet adapter to WSL so nothing has to be forwarded on the
windows side. In an administrator PowerShell:

  usbipd list
  usbipd attach --wsl --busid <the ethernet adapter's busid>

Then in WSL, on the interface that appears:

  sudo ip addr add 192.0.2.2/24 dev <interface>
  sudo ip link set <interface> up
  ping -c3 192.0.2.1

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
echo "== running the $MODE suite against $URI =="

passed=0
failed=0
timedout=0
results=""
total_suites=$(set -- $TESTS; echo $#)
n=0
log=$(mktemp)
trap 'rm -f "$log"' EXIT

record_result() {
	local t=$1 rc=$2 logfile=$3
	local counts

	counts=$(awk '/^Passed:/{p=$2} /^Failed:/{f=$2} END{if (p != "") printf "%s/%s", p, p+f}' "$logfile")
	[ -n "$counts" ] && counts=" ($counts assertions)"

	case $rc in
	0)
		passed=$((passed + 1))
		results="$results\n  PASS    $t$counts"
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
		results="$results\n  FAIL    $t (exit $rc)$counts"
		;;
	esac
}

check_binaries() {
	local t

	for t in $TESTS; do
		[ -x "$TEST_DIR/${t}_remote" ] || die "$TEST_DIR/${t}_remote is missing"
	done
}

run_sequential() {
	local t

	for t in $TESTS; do
		n=$((n + 1))

		echo
		echo "======== [$n/$total_suites] $t ========"
		stdbuf -oL -eL timeout "$TIMEOUT" "$TEST_DIR/${t}_remote" 2>&1 | tee "$log"
		record_result "$t" "${PIPESTATUS[0]}" "$log"
	done
}

run_batch() {
	local logdir=$1
	shift

	local jobs="" t pid rc

	for t in "$@"; do
		stdbuf -oL -eL timeout "$TIMEOUT" "$TEST_DIR/${t}_remote" \
			>"$logdir/$t.log" 2>&1 &
		jobs="$jobs $!:$t"
		echo "  launched $t as pid $!"
	done

	for pid in $jobs; do
		t=${pid#*:}
		pid=${pid%%:*}

		wait "$pid"
		rc=$?
		n=$((n + 1))

		echo
		echo "======== [$n/$total_suites] $t ========"
		cat "$logdir/$t.log"
		record_result "$t" "$rc" "$logdir/$t.log"
	done
}

run_concurrent() {
	local logdir batch count t

	logdir=$(mktemp -d)
	batch=""
	count=0

	for t in $TESTS; do
		batch="$batch $t"
		count=$((count + 1))

		if [ "$count" -eq "$MAX_PARALLEL" ]; then
			echo
			echo "-------- batch of $count clients --------"
			# shellcheck disable=SC2086
			run_batch "$logdir" $batch
			batch=""
			count=0
		fi
	done

	if [ "$count" -gt 0 ]; then
		echo
		echo "-------- batch of $count clients --------"
		# shellcheck disable=SC2086
		run_batch "$logdir" $batch
	fi

	rm -rf "$logdir"
}

check_binaries

if [ "$MULTI" = "1" ]; then
	run_concurrent
else
	run_sequential
fi

echo
echo "== $MODE summary ($URI) =="
printf '%b\n' "$results"
echo "  passed $passed, failed $failed, timed out $timedout"

[ "$failed" -eq 0 ] && [ "$timedout" -eq 0 ]
