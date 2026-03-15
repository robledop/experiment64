# Doom (doomgeneric)

The Doom port builds against upstream doomgeneric at a pinned commit. The build
clones the upstream repository into `third_party/doomgeneric` and applies the
patches in `user/doom/patches/` before compiling.

Pinned upstream commit: `fc601639494e089702a1ada082eb51aaafc03722`.

## Setup

- Run `make doom` or `make image.hdd` to fetch, patch, and build.
- If you want to manage the source manually, run `user/doom/prepare_doomgeneric.sh`.
- To override the clone location, set `DOOMGENERIC_ROOT=/path` before building.

## Runtime modes

- Outside WM: Doom opens `/dev/fb0` and renders full-screen, reading input from
  `/dev/keyboard` (or stdin fallback).
- Inside WM: Doom detects WM protocol FDs (`WM_EVT_FD=3`, `WM_CMD_FD=4`),
  creates a WM client window, renders into the back shared-memory buffer, and
  presents via the WM client API (`wm_invalidate*` presents and flips after WM acknowledgment).
- Inside WM: pressing `ALT+ENTER` toggles rendering between the WM client window
  buffer and direct rendering to `/dev/fb0`.
- Inside WM: Doom redirects `stdout`/`stderr` to `/dev/null` at startup to
  avoid terminal log output while the WM client is running.
- WM key events are translated to Doom keys in a dedicated event thread, and
  WM resize events update the local framebuffer pointer and dimensions from the
  existing `DoomWindow` struct fields (no buffer remapping is done in the handler).
