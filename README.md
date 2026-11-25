# mywm

A minimal X11 window manager for student coding workflows. This repository
implements the specification in [`docs/specification.docx`](docs/specification.docx);
M0 and M1 are complete.

## Status

| Area | State |
|---|---|
| Build system, config, man page, examples | done |
| WM role, EWMH identity, EWMH client lists | done |
| Client lifecycle, borders, focus history | done |
| Sloppy focus, keyboard focus, safe close | done |
| Super+drag move and Super+right-drag resize | done |
| Master-stack and monocle layouts | done |
| Nine tags, toggleview, last tag, per-tag layout | done |
| Directional focus/swap, gaps, layout keys | done |
| Dialogs, rules, fullscreen, RandR monitors | not yet |
| IPC server, scratchpad, PiP, status bar | not yet |

The current binary is a real tiling WM for a single screen. It owns the root
window, tracks clients, draws focus borders, tiles visible clients, and hides
clients on inactive tags. It is safe to run in a nested Xephyr session.

## Build

Debian/Ubuntu dependencies:

```sh
sudo apt install build-essential pkg-config \
  libxcb1-dev libxcb-keysyms1-dev x11proto-dev
```

`x11proto-dev` provides `<X11/keysym.h>`, which is used for the keymap.
`libxcb-randr0-dev` will be added when multi-monitor support lands.

Build:

```sh
make
```

Install:

```sh
sudo make install
```

This installs `mywm`, `mywmctl`, the man page, and an X session desktop file.

## Try it safely

Run it inside Xephyr instead of replacing your current desktop:

```sh
Xephyr :1 -screen 1280x800 -ac &
DISPLAY=:1 ./mywm
DISPLAY=:1 xterm &
```

Quit with `Super+Shift+q`.

Run the smoke and M1 integration tests:

```sh
make test
```

`tests/smoke.sh` checks startup, EWMH identity, window mapping, and clean
shutdown. `tests/m1_integration.py` drives real key events through XTEST to
check tiling geometry, monocle, tag hiding/restoring, client movement, and
EWMH client lists. It is skipped automatically if `python-xlib` is unavailable.

## Keybindings

| Binding | Action |
|---|---|
| `Super+Return` | Spawn terminal (`xterm`) |
| `Super+e` | Spawn editor (`xterm -e nvim`) |
| `Super+b` | Spawn browser (`firefox`) |
| `Super+d` | Spawn launcher (`rofi -show drun`) |
| `Super+q` | Close focused window (safe close, then kill) |
| `Super+Shift+q` | Quit the WM |
| `Super+Shift+r` | Restart the WM in place |
| `Super+Tab` | Toggle the last tag set |
| `Super+1`…`Super+9` | View tag |
| `Super+Shift+1`…`Super+Shift+9` | Move focused window to tag |
| `Super+Ctrl+1`…`Super+Ctrl+9` | Toggle tag alongside current |
| `Super+h/j/k/l` | Focus left/down/up/right |
| `Super+Shift+h/j/k/l` | Swap with left/down/up/right |
| `Super+Ctrl+h` / `Super+Ctrl+l` | Shrink/grow master area |
| `Super+Space` | Cycle tile → monocle |
| `Super+Shift+Space` | Toggle floating on focused window |
| `Super+left-drag` | Move floating window |
| `Super+right-drag` | Resize floating window |

The remaining keymap from the specification is added with dialogs, fullscreen,
IPC, and workflow milestones.

## Configuration

Edit [`config.h`](config.h), run `make`, then press `Super+Shift+r` to restart
the WM with the new binary. There is no runtime config parser by design.

Session autostart is read from `~/.config/mywm/autostart` after startup.

## Repository layout

```text
mywm/
├── Makefile
├── README.md
├── TODO.md
├── config.h
├── mywm.c              # WM; single file for now
├── mywmctl.c           # IPC client; server lands with M3
├── docs/
│   └── specification.docx
├── examples/
│   ├── autostart
│   ├── mywm.desktop
│   └── xinitrc
├── man/
│   └── mywm.1
├── scripts/
│   ├── statusbar
│   ├── study
│   └── zen
└── tests/
    ├── m1_integration.py
    └── smoke.sh
```

## Milestones

See [`TODO.md`](TODO.md). The immediate next step is M2: dialogs and rules,
fullscreen, extended EWMH, and restart persistence.

## License

MIT. See [`LICENSE`](LICENSE).
