#!/usr/bin/env bash
#
# Copyright (c) 2025 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT

set -u

PROTO=${1:-}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NOOS_DIR=$(dirname "$SCRIPT_DIR")
LIBIIO_DIR=$(dirname "$NOOS_DIR")
HOST_BUILD=${NOOS_HOST_BUILD:-$LIBIIO_DIR/build}
UTILS_DIR=${NOOS_UTILS_DIR:-$HOST_BUILD/utils}
SAMPLE=$NOOS_DIR/samples/iiod/main.c
IIOD_PORT=30431
USB_VID_PID=0456:b673
UTIL_TIMEOUT=${UTIL_TIMEOUT:-40}
STREAM_SAMPLES=${STREAM_SAMPLES:-256}
UTIL_NAMES="iio_info iio_attr iio_rwdev"
V0_BUILD=${NOOS_V0_BUILD:-}

ADC_DEV=iio-adc
ADC_CHN=voltage0
GAME_DEV=snake

ALL_GROUPS="info context adc game stream stream_v0"
RUN_GROUPS=$ALL_GROUPS

usage() {
	cat >&2 <<USAGE
usage: $(basename "$0") {uart|network|usb} [group ...]

Builds and flashes the matching firmware preset, then drives the host
utilities against the board over that transport and checks what they print.
Where run_hw_tests.sh exercises the library API, this exercises the client
programs a user actually runs.

Groups (default: all, in this order):
  info      iio_info lists the context, the adc and the sample device
  context   iio_attr -C and the device listing
  adc       iio_attr on the adc channel and device attributes
  game      iio_attr round-trips on the sample device
  stream    iio_rwdev captures a buffer of samples
  stream_v0 a libiio v0 iio_readdev captures one, then iio_rwdev again
            (skipped unless NOOS_V0_BUILD is set)

Environment overrides:
  NOOS_TESTS_PORT   serial device for the uart protocol (default: autodetect)
  NOOS_TESTS_HOST   board address for the network protocol (default: 192.0.2.1)
  NOOS_TESTS_URI    full libiio URI, skips all detection
  NOOS_HOST_BUILD   host libiio build directory (default: <libiio>/build)
  NOOS_UTILS_DIR    directory holding iio_info/iio_attr/iio_rwdev
  UTIL_TIMEOUT      seconds a single utility may run (default: 40)
  STREAM_SAMPLES    samples captured by the stream groups (default: 256)
  NOOS_V0_BUILD     libiio v0.x build directory holding tests/iio_readdev
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
	shift
	;;
*)
	usage
	;;
esac

if [ "$#" -gt 0 ]; then
	for want in "$@"; do
		case " $ALL_GROUPS " in
		*" $want "*)
			;;
		*)
			die "unknown group '$want'"
			;;
		esac
	done
	RUN_GROUPS="$*"
fi

build_utils() {
	local missing="" u

	for u in $UTIL_NAMES; do
		[ -x "$UTILS_DIR/$u" ] || missing="$missing $u"
	done

	[ -n "$missing" ] || return 0

	[ -z "${NOOS_UTILS_DIR:-}" ] || die "missing in $UTILS_DIR:$missing"

	echo "== building the host utilities in $HOST_BUILD =="
	cmake -B "$HOST_BUILD" -S "$LIBIIO_DIR" \
	      -DWITH_SERIAL_BACKEND=ON -DWITH_USB_BACKEND=ON \
	      -DWITH_NETWORK_BACKEND=ON -DWITH_EXAMPLES=OFF \
	      -DWITH_TESTS=OFF -DWITH_IIOD=OFF >/dev/null || \
		die "cannot configure $HOST_BUILD"

	# shellcheck disable=SC2086
	cmake --build "$HOST_BUILD" --target $UTIL_NAMES >/dev/null || \
		die "cannot build the host utilities"
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

port_open() {
	local deadline=$((SECONDS + 60))

	# The ethernet link takes a while to come up after a flash.
	until timeout 5 bash -c "cat < /dev/null > /dev/tcp/$1/$IIOD_PORT" \
		2>/dev/null; do
		[ "$SECONDS" -lt "$deadline" ] || return 1
		sleep 1
	done
}

resolve_network() {
	local host=${NOOS_TESTS_HOST:-192.0.2.1}

	port_open "$host" || die "$host:$IIOD_PORT is not reachable"

	URI="ip:$host"
}

resolve_usb() {
	local deadline=$((SECONDS + 60))

	# The board re-enumerates after the flash; give usbipd time to attach it
	until lsusb -d "$USB_VID_PID" >/dev/null 2>&1; do
		[ "$SECONDS" -lt "$deadline" ] || \
			die "no $USB_VID_PID device visible; attach it with usbipd first"
		sleep 1
	done

	URI="usb:"
}

wait_board() {
	local deadline=$((SECONDS + 60))

	# A client that connects while the board still boots desyncs the link
	# for later clients too, so wait for a clean answer first.
	until LD_LIBRARY_PATH=$HOST_BUILD timeout 4 \
		"$HOST_BUILD/utils/iio_info" -u "$URI" >/dev/null 2>&1; do
		[ "$SECONDS" -lt "$deadline" ] || die "$URI does not answer"
	done
}

read_geometry() {
	GAME_W=$(sed -n 's/^#define GAME_W[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$SAMPLE")
	GAME_H=$(sed -n 's/^#define GAME_H[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$SAMPLE")

	[ -n "$GAME_W" ] && [ -n "$GAME_H" ] || \
		die "cannot read GAME_W/GAME_H from $SAMPLE"
}

passed=0
failed=0
timedout=0
skipped=0
group_failed=0
results=""
OUT=""
RC=0

pass() {
	passed=$((passed + 1))
	printf '    PASS  %s\n' "$1"
}

fail() {
	failed=$((failed + 1))
	group_failed=$((group_failed + 1))
	printf '    FAIL  %s\n' "$1"
	[ "$#" -lt 2 ] || printf '          %s\n' "$2"
}

count() {
	tr -d ' '
}

run_util() {
	local name=$1

	shift
	OUT=$(LD_LIBRARY_PATH=$HOST_BUILD timeout "$UTIL_TIMEOUT" \
		"$UTILS_DIR/$name" -u "$URI" "$@" 2>&1)
	RC=$?

	[ "$RC" != "124" ] || timedout=$((timedout + 1))
}

attr_dev() {
	run_util iio_attr -d "$@"
}

attr_chn() {
	run_util iio_attr -c "$@"
}

assert_rc() {
	if [ "$RC" = "$1" ]; then
		pass "$2"
	elif [ "$RC" = "124" ]; then
		fail "$2" "timed out after ${UTIL_TIMEOUT}s"
	else
		fail "$2" "exit $RC: $(printf '%s' "$OUT" | head -1)"
	fi
}

assert_eq() {
	if [ "$1" = "$2" ]; then
		pass "$3"
	else
		fail "$3" "expected '$2', got '$1'"
	fi
}

assert_match() {
	if printf '%s\n' "$1" | grep -qE "$2"; then
		pass "$3"
	else
		fail "$3" "no line matching /$2/"
	fi
}

assert_no_match() {
	local hit

	hit=$(printf '%s\n' "$1" | grep -E "$2" | head -1)

	if [ -z "$hit" ]; then
		pass "$3"
	else
		fail "$3" "$hit"
	fi
}

assert_int_in() {
	case $1 in
	''|*[!0-9]*)
		fail "$4" "not an integer: '$1'"
		return
		;;
	esac

	if [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]; then
		pass "$4"
	else
		fail "$4" "$1 outside [$2, $3]"
	fi
}

group_info() {
	run_util iio_info
	assert_rc 0 "iio_info exits 0"
	assert_no_match "$OUT" 'ERROR' "iio_info reports no errors"
	assert_match "$OUT" 'Backend description string: no-OS' \
		     "the backend identifies itself as no-OS"
	assert_match "$OUT" "iio:device0: $ADC_DEV \(buffer capable\)" \
		     "$ADC_DEV is device 0 and buffer capable"
	assert_match "$OUT" "$ADC_CHN: .*input, index: 0, format: be:u12/16>>0" \
		     "$ADC_CHN keeps its 12-bit input format"
	assert_match "$OUT" "Scan element 0: $ADC_CHN" \
		     "$ADC_CHN is a scan element of the buffer"
	assert_match "$OUT" "iio:device1: $GAME_DEV" \
		     "$GAME_DEV is device 1"
	assert_no_match "$OUT" 'value: *$' "every listed attribute has a value"
}

group_context() {
	run_util iio_attr -C
	assert_rc 0 "iio_attr -C exits 0"
	assert_match "$OUT" '^uri: ' "the context reports its uri"

	run_util iio_attr -d
	assert_rc 0 "iio_attr -d lists the devices"
	assert_match "$OUT" "$ADC_DEV" "the listing names $ADC_DEV"
	assert_match "$OUT" "$GAME_DEV" "the listing names $GAME_DEV"
}

group_adc() {
	local first

	attr_chn "$ADC_DEV" "$ADC_CHN" raw
	assert_rc 0 "raw is readable"
	first=$OUT
	assert_int_in "$first" 0 4095 "raw fits the 12-bit range"

	attr_chn "$ADC_DEV" "$ADC_CHN" raw
	assert_int_in "$OUT" 0 4095 "a second raw read also converts"

	attr_chn "$ADC_DEV" "$ADC_CHN" scale
	assert_match "$OUT" '^[0-9]+\.[0-9]+$' "scale is a decimal number"

	attr_chn "$ADC_DEV" "$ADC_CHN" gain
	assert_eq "$OUT" "1" "gain is 1"

	attr_chn "$ADC_DEV" "$ADC_CHN" reference
	assert_eq "$OUT" "Internal" "the reference is internal"

	attr_chn "$ADC_DEV" "$ADC_CHN" differential
	assert_eq "$OUT" "0" "the channel is single-ended"

	attr_chn "$ADC_DEV" "$ADC_CHN" process
	assert_int_in "$OUT" 0 3300 "process is a plausible millivolt reading"

	attr_dev "$ADC_DEV" internal_ref_voltage
	assert_eq "$OUT" "1250" "the internal reference is 1250 mV"

	attr_dev "$ADC_DEV" no_such_attr
	assert_rc 1 "an unknown attribute is refused"
}

group_game() {
	local dirs="up left down right" d before frame other steps state

	attr_dev "$GAME_DEV" reset 1
	assert_rc 0 "reset is accepted"

	attr_dev "$GAME_DEV" score
	assert_eq "$OUT" "0" "the score is 0 after a reset"

	attr_dev "$GAME_DEV" state
	assert_eq "$OUT" "running" "the game runs after a reset"

	attr_dev "$GAME_DEV" frame
	assert_rc 0 "a frame is readable"
	frame=$OUT

	assert_eq "$(printf '%s\n' "$frame" | wc -l | count)" "$GAME_H" \
		  "the frame has $GAME_H rows"
	assert_eq "$(printf '%s\n' "$frame" | \
		     awk -v w="$GAME_W" 'length($0) != w' | wc -l | count)" "0" \
		  "every row is $GAME_W cells wide"
	assert_eq "$(printf '%s' "$frame" | tr -cd '@' | wc -c | count)" "1" \
		  "the frame holds exactly one head"
	assert_eq "$(printf '%s' "$frame" | tr -d '.@o*\n' | wc -c | count)" "0" \
		  "the frame uses only the game glyphs"

	attr_dev "$GAME_DEV" frame
	other=$OUT

	if [ "$frame" != "$other" ]; then
		pass "reading a frame advances the game"
	else
		fail "reading a frame advances the game" "two frames are identical"
	fi

	for d in $dirs; do
		attr_dev "$GAME_DEV" direction "$d"
		attr_dev "$GAME_DEV" frame
		attr_dev "$GAME_DEV" direction
		assert_eq "$OUT" "$d" "steering $d takes effect on the next tick"
	done

	attr_dev "$GAME_DEV" direction w
	attr_dev "$GAME_DEV" frame
	attr_dev "$GAME_DEV" direction
	assert_eq "$OUT" "up" "a single wasd key steers too"

	attr_dev "$GAME_DEV" direction
	before=$OUT
	attr_dev "$GAME_DEV" direction sideways
	attr_dev "$GAME_DEV" frame
	attr_dev "$GAME_DEV" direction
	assert_eq "$OUT" "$before" "a word that only starts like a key is ignored"

	attr_dev "$GAME_DEV" direction nowhere
	attr_dev "$GAME_DEV" frame
	attr_dev "$GAME_DEV" direction
	assert_eq "$OUT" "$before" "an unknown direction is ignored"

	attr_dev "$GAME_DEV" reset 1
	attr_dev "$GAME_DEV" direction up
	steps=0
	state=running

	while [ "$steps" -lt $((GAME_H + 2)) ]; do
		attr_dev "$GAME_DEV" frame
		attr_dev "$GAME_DEV" state
		state=$OUT
		steps=$((steps + 1))

		[ "$state" != "over" ] || break
	done

	assert_eq "$state" "over" "the snake dies against the top wall"

	attr_dev "$GAME_DEV" high_score
	assert_int_in "$OUT" 0 $((GAME_W * GAME_H)) "the high score is kept"

	attr_dev "$GAME_DEV" reset 1
	attr_dev "$GAME_DEV" state
	assert_eq "$OUT" "running" "a reset revives the game"

	attr_dev "$GAME_DEV" score
	assert_eq "$OUT" "0" "a reset clears the score"
}

# Check a capture of STREAM_SAMPLES be:u12/16 samples; $2 names the client
check_capture() {
	local capture=$1 client=$2 bytes max distinct

	bytes=$(wc -c <"$capture" | count)
	assert_eq "$bytes" "$((STREAM_SAMPLES * 2))" \
		  "the $client capture is $((STREAM_SAMPLES * 2)) bytes"

	max=$(od -An -tu2 --endian=big -v "$capture" | tr ' ' '\n' | grep -E '^[0-9]+$' | \
	      sort -n | tail -1)
	distinct=$(od -An -tu2 --endian=big -v "$capture" | tr ' ' '\n' | \
		   grep -E '^[0-9]+$' | sort -u | wc -l | count)

	assert_int_in "${max:-0}" 0 4095 "every $client sample fits the 12-bit range"

	if [ "${distinct:-0}" -gt 1 ]; then
		pass "the $client capture is not a constant pattern"
	else
		fail "the $client capture is not a constant pattern" \
		     "$distinct distinct value(s)"
	fi
}

# Capture STREAM_SAMPLES samples with the v1 iio_rwdev into $1
capture_v1() {
	LD_LIBRARY_PATH=$HOST_BUILD timeout "$UTIL_TIMEOUT" \
		"$UTILS_DIR/iio_rwdev" -u "$URI" -s "$STREAM_SAMPLES" \
		"$ADC_DEV" "$ADC_CHN" >"$1" 2>/dev/null
	RC=$?
	OUT=""

	[ "$RC" != "124" ] || timedout=$((timedout + 1))
}

group_stream() {
	local capture

	capture=$(mktemp)

	capture_v1 "$capture"
	assert_rc 0 "iio_rwdev captures $STREAM_SAMPLES samples"
	check_capture "$capture" "v1"

	rm -f "$capture"
}

group_stream_v0() {
	local capture

	if [ -z "$V0_BUILD" ]; then
		skipped=$((skipped + 1))
		echo "    SKIP  NOOS_V0_BUILD is not set"
		return
	fi

	[ -x "$V0_BUILD/tests/iio_readdev" ] || \
		die "no tests/iio_readdev in $V0_BUILD"

	capture=$(mktemp)

	# v0 speaks the ascii protocol: OPEN, READBUF and CLOSE
	OUT=$(LD_LIBRARY_PATH=$V0_BUILD timeout "$UTIL_TIMEOUT" \
		"$V0_BUILD/tests/iio_readdev" -u "$URI" -s "$STREAM_SAMPLES" \
		"$ADC_DEV" "$ADC_CHN" 2>&1 >"$capture")
	RC=$?

	[ "$RC" != "124" ] || timedout=$((timedout + 1))
	assert_rc 0 "the v0 iio_readdev captures $STREAM_SAMPLES samples"
	check_capture "$capture" "v0"

	# The board must hand the link back to a v1 client afterwards
	capture_v1 "$capture"
	assert_rc 0 "iio_rwdev still captures after the v0 client"
	check_capture "$capture" "v1"

	rm -f "$capture"
}

build_utils
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

wait_board
read_geometry

echo
echo "== running the utility checks against $URI =="

for group in $RUN_GROUPS; do
	group_failed=0

	echo
	echo "======== $group ========"
	"group_$group"

	if [ "$group_failed" -eq 0 ]; then
		results="$results\n  PASS    $group"
	else
		results="$results\n  FAIL    $group ($group_failed)"
	fi
done

echo
echo "== $PROTO utility summary ($URI) =="
printf '%b\n' "$results"
echo "  passed $passed, failed $failed, timed out $timedout, skipped $skipped"

[ "$failed" -eq 0 ]
