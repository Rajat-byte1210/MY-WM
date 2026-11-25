#!/usr/bin/env python3
"""M0/M1 Xephyr integration checks driven through XTEST.

Verifies tiling geometry, monocle layout, tag hiding/restoring, moving a
client to a tag, and EWMH client-list state. Requires python-xlib and XTEST.
"""
import os
import shutil
import subprocess
import tempfile
import time

from Xlib import X, XK, display
from Xlib.ext import xtest

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISPLAY = os.environ.get("MYWM_TEST_DISPLAY", ":98")
env = os.environ.copy()
env["DISPLAY"] = DISPLAY
env["PATH"] = HERE + os.pathsep + env.get("PATH", "")
host_env = os.environ.copy()

procs = []
tmpdir = tempfile.mkdtemp(prefix="mywm-int.")
logs = []


def start(cmd, nested=True):
    log = open(os.path.join(tmpdir, f"proc-{len(procs)}.log"), "wb")
    logs.append(log)
    p = subprocess.Popen(cmd, env=env if nested else host_env,
                         stdout=log, stderr=subprocess.STDOUT)
    procs.append(p)
    return p


def stop_all():
    for p in reversed(procs):
        if p.poll() is None:
            p.terminate()
    for p in reversed(procs):
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
    for log in logs:
        log.close()
    shutil.rmtree(tmpdir, ignore_errors=True)


def connect():
    for _ in range(60):
        try:
            return display.Display(DISPLAY)
        except Exception:
            time.sleep(0.1)
    raise RuntimeError("Xephyr did not start")


def key(d, keysym, mods=()):
    codes = []
    for m in mods:
        code = d.keysym_to_keycode(XK.string_to_keysym(m))
        codes.append(code)
        xtest.fake_input(d, X.KeyPress, code)

    code = d.keysym_to_keycode(XK.string_to_keysym(keysym))
    xtest.fake_input(d, X.KeyPress, code)
    xtest.fake_input(d, X.KeyRelease, code)

    for mod_code in reversed(codes):
        xtest.fake_input(d, X.KeyRelease, mod_code)
    d.sync()
    time.sleep(0.35)


def card(d, name):
    prop = d.screen().root.get_full_property(d.intern_atom(name),
                                             X.AnyPropertyType)
    return list(prop.value) if prop else []


def windows_by_title(d):
    found = {}
    for w in d.screen().root.query_tree().children:
        try:
            title = w.get_wm_name()
        except Exception:
            continue
        if title in ("mywm-int-a", "mywm-int-b"):
            found[title] = w
    return found


def geom(w):
    g = w.get_geometry()
    return (g.x, g.y, g.width, g.height)


def mapped(w):
    return w.get_attributes().map_state != X.IsUnmapped


try:
    start(["Xephyr", DISPLAY, "-screen", "1024x768", "-ac", "-nolisten",
           "tcp"], nested=False)
    d = connect()
    time.sleep(0.5)
    start([os.path.join(HERE, "mywm")])
    time.sleep(0.5)
    start(["xterm", "-T", "mywm-int-a", "-e", "sleep", "30"])
    start(["xterm", "-T", "mywm-int-b", "-e", "sleep", "30"])

    wins = {}
    for _ in range(50):
        wins = windows_by_title(d)
        if len(wins) == 2:
            break
        time.sleep(0.2)
    assert len(wins) == 2, f"expected 2 windows, got {sorted(wins)}"

    assert len(card(d, "_NET_CLIENT_LIST")) == 2, "EWMH client list is wrong"
    assert card(d, "_NET_CURRENT_DESKTOP")[0] == 0

    tiled = sorted(geom(w) for w in wins.values())
    assert tiled == [(10, 10, 600, 748), (622, 10, 392, 748)], tiled

    key(d, "space", ("Super_L",))
    assert all(geom(w) == (10, 10, 1004, 748) for w in wins.values()), \
        "monocle geometry is wrong"

    key(d, "space", ("Super_L",))
    tiled = sorted(geom(w) for w in wins.values())
    assert tiled == [(10, 10, 600, 748), (622, 10, 392, 748)], tiled

    key(d, "2", ("Super_L",))
    assert card(d, "_NET_CURRENT_DESKTOP")[0] == 1
    assert not any(mapped(w) for w in wins.values()), "tag 2 did not hide clients"

    key(d, "1", ("Super_L",))
    assert card(d, "_NET_CURRENT_DESKTOP")[0] == 0
    assert all(mapped(w) for w in wins.values()), "tag 1 did not restore clients"

    key(d, "2", ("Super_L", "Shift_L"))
    assert sum(mapped(w) for w in wins.values()) == 1, \
        "moving a client to another tag did not hide it"
    assert len(card(d, "_NET_CLIENT_LIST")) == 2, "moving a client lost it"

    key(d, "2", ("Super_L",))
    assert card(d, "_NET_CURRENT_DESKTOP")[0] == 1
    assert sum(mapped(w) for w in wins.values()) == 1, \
        "tag 2 should show exactly the moved client"

    print("PASS: M0/M1 geometry, tags, layout, focus hiding, EWMH client list")
finally:
    stop_all()
