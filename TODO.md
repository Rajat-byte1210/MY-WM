# mywm implementation checklist

Derived from `docs/specification.docx` (v0.1). M0 and M1 are implemented and
covered by the Xephyr integration test in `tests/m1_integration.py`.

## M0 — Bootstrap

- [x] Connect to X, acquire `SubstructureRedirect`, fail cleanly if taken
- [x] Intern core atoms and publish `_NET_SUPPORTING_WM_CHECK`
- [x] Spawn, safe close, restart, panic quit
- [x] `poll()` event loop with XCB fd + signal self-pipe
- [x] Client list: manage/unmanage, `_NET_CLIENT_LIST`
- [x] Border width/color, restore border on unmanage
- [x] Focus stack and focus recovery after close/hide
- [x] Super+drag move and Super+right-drag resize
- [x] Handle synthetic `ConfigureNotify` for tiled clients

## M1 — Tiling and tags

- [x] Master-stack layout with `nmaster` and `mfact`
- [x] Monocle layout
- [x] Nine tags per monitor; view, tag, toggleview, last tag
- [x] Per-tag layout memory
- [x] Directional focus and swap
- [x] Gaps and border geometry

## M2 — Real applications

- [ ] Dialog/transient floating, size hints, floating persistence
- [ ] Fullscreen add/remove/toggle
- [ ] Window rules (`WM_CLASS`, title, type)
- [ ] Extended EWMH: struts, urgent hints, `_NET_WM_STATE` messages
- [ ] Restart persistence (`_MYWM_*` properties)
- [ ] `WM_TAKE_FOCUS` / `WM_DELETE_WINDOW` edge-case validation
- [ ] Autostart error reporting

## M3 — Workflow

- [ ] IPC server on `$XDG_RUNTIME_DIR/mywm-<display>.sock`
- [ ] `status`, `view`, `tag`, `toggleview`, `layout`, `pip`, `spawn`, `close`,
      `version`, `quit`
- [ ] Scratchpad terminal
- [ ] PiP slot
- [ ] Status-bar integration
- [ ] Project profile scripts

## M4 — Multi-monitor

- [ ] RandR 1.5 monitor enumeration
- [ ] Per-monitor tags and focus
- [ ] Work areas and `_NET_WM_STRUT_PARTIAL`
- [ ] Hotplug and orphan reassignment

## M5 — Student extras

- [ ] Window swallowing
- [ ] Zen mode, study timer, cheatsheet
- [ ] Screenshot, clipboard, notes/calculator bindings
- [ ] Lock/idle, volume/brightness/media keys

## M6 — Polish

- [ ] Unit tests for layout and rules
- [ ] Extended Xephyr integration tests
- [ ] Multi-day daily-driver trial
- [ ] Man page and keymap final pass
- [ ] Sanitizer/Valgrind clean run
- [ ] Resource budget verified
