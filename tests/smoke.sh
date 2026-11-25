#!/bin/sh
# Start mywm inside Xephyr, check EWMH identity, map an xterm, and quit.
set -eu

cd "$(dirname "$0")/.."

TEST_DISPLAY=${MYWM_TEST_DISPLAY:-:99}

for tool in Xephyr xterm xprop xwininfo xdpyinfo; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "SKIP: $tool is not installed"
        exit 0
    fi
done

if [ ! -x ./mywm ]; then
    echo "FAIL: ./mywm not built (run make first)"
    exit 1
fi

if [ -e "/tmp/.X11-unix/X${TEST_DISPLAY#:}" ]; then
    echo "SKIP: display $TEST_DISPLAY is already in use"
    exit 0
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/mywm-test.XXXXXX")
wm_pid=
xterm_pid=
xephyr_pid=

cleanup() {
    [ -n "$wm_pid" ] && kill "$wm_pid" 2>/dev/null || true
    [ -n "$xterm_pid" ] && kill "$xterm_pid" 2>/dev/null || true
    [ -n "$xephyr_pid" ] && kill "$xephyr_pid" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

Xephyr "$TEST_DISPLAY" -screen 1024x768 -ac -nolisten tcp \
    >"$tmpdir/xephyr.log" 2>&1 &
xephyr_pid=$!

ready=0
i=0
while [ "$i" -lt 50 ]; do
    if DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.1
    i=$((i + 1))
done

if [ "$ready" -ne 1 ]; then
    echo "FAIL: Xephyr did not start"
    cat "$tmpdir/xephyr.log"
    exit 1
fi

DISPLAY="$TEST_DISPLAY" ./mywm >"$tmpdir/mywm.log" 2>&1 &
wm_pid=$!
sleep 0.5

if ! kill -0 "$wm_pid" 2>/dev/null; then
    echo "FAIL: mywm exited during startup"
    cat "$tmpdir/mywm.log"
    exit 1
fi

if ! DISPLAY="$TEST_DISPLAY" xprop -root _NET_SUPPORTING_WM_CHECK \
        | grep -q '0x'; then
    echo "FAIL: _NET_SUPPORTING_WM_CHECK was not published"
    cat "$tmpdir/mywm.log"
    exit 1
fi

if ! DISPLAY="$TEST_DISPLAY" ./mywm --version | grep -q '^mywm '; then
    echo "FAIL: --version output is wrong"
    exit 1
fi

DISPLAY="$TEST_DISPLAY" xterm -T mywm-smoke -e sleep 5 \
    >"$tmpdir/xterm.log" 2>&1 &
xterm_pid=$!

found=0
i=0
while [ "$i" -lt 25 ]; do
    if DISPLAY="$TEST_DISPLAY" xwininfo -root -tree 2>/dev/null \
            | grep -q 'mywm-smoke'; then
        found=1
        break
    fi
    sleep 0.2
    i=$((i + 1))
done

if [ "$found" -ne 1 ]; then
    echo "FAIL: xterm was not mapped"
    DISPLAY="$TEST_DISPLAY" xwininfo -root -tree || true
    cat "$tmpdir/mywm.log"
    exit 1
fi

kill -TERM "$wm_pid"
if ! wait "$wm_pid"; then
    echo "FAIL: mywm did not exit cleanly on SIGTERM"
    cat "$tmpdir/mywm.log"
    exit 1
fi
wm_pid=

if [ -n "$xterm_pid" ]; then
    kill "$xterm_pid" 2>/dev/null || true
    wait "$xterm_pid" 2>/dev/null || true
    xterm_pid=
fi

echo "PASS: startup, EWMH identity, window mapping, and clean shutdown"

if command -v python3 >/dev/null 2>&1 &&
   python3 -c 'import Xlib, Xlib.ext.xtest' >/dev/null 2>&1; then
    MYWM_TEST_DISPLAY="$TEST_DISPLAY" python3 tests/m1_integration.py
else
    echo "SKIP: M1 integration checks (python-xlib/XTEST unavailable)"
fi
