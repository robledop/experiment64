# A WM Client Opens a Window and Presents One Frame

This walkthrough traces a single GUI application — `wmclient_demo` — from
`main()` through opening a window and pushing one rendered frame to the
compositor, then back. The interesting part is that this round-trip crosses a
process boundary twice: client to WM over one pipe, WM to client over another.
Following it end to end is the clearest way to see how this kernel's window
manager actually presents pixels, and why a fire-and-forget-sounding call like
`wm_invalidate_region` actually blocks.

## Scope

This covers the synchronous `INVALIDATE` -> `INVALIDATED` present round-trip for
an already-launched client: how the client requests a present, how the server
composites the client's shared-memory buffer into its scene graph, and how the
client learns the present completed and swaps buffers.

The round-trip ends when the frame is composited into the WM's video context
(step 9). Whether or when the WM then flips its own framebuffer to the physical
screen is a separate flow and is not traced here. Client *launch* (the
fork/exec/pipe plumbing in `client_launch`) is touched only where it explains
how the client gets its two fds.

## Files in play

- `user/wmclient_demo.c` — the demo client; `main()` creates a window, renders, presents, then loops on events.
- `user/libc/include/wm/wm_protocol.h` — the wire format: message/event enums and packed structs sent over the two pipes.
- `user/libc/src/wmclient.c` — client-side library: `wm_create_window`, `wm_invalidate_region` (presents and **blocks**), and the background `wm_event_reader_thread`.
- `user/wmlib/imui.c` — immediate-mode UI helper; tracks a dirty rect across a frame and calls `wm_invalidate_region` at frame end.
- `user/wm/wm_client.c` — server-side per-client logic: `client_reader_thread` reads commands, `handle_invalidate` flips the front buffer and triggers compositing.
- `user/wm/wm_client.h` — server structs, notably `client_window_t` (embeds `window_t` as its first member).
- `user/libc/include/wm/wmclient.h` — client struct `wm_window_t`.
- `user/wmlib/window.c` — scene-graph node `window_t`: `window_invalidate` -> `window_paint` is where compositing happens.

## Three structs named "window"

This path involves three differently-scoped structs whose names all contain
"window". Keep them straight or the casts below look like type errors:

- **`wm_window_t`** (`wmclient.h:9`) — the *client-side* handle returned by
  `wm_create_window`. Lives in the client process. Holds the mmap'd buffer
  pointers, `front_buffer`/`back_buffer` indices, and the
  `presents_requested`/`presents_completed` counters that drive the blocking
  present.
- **`window_t`** (`window.h:40`) — a *scene-graph node* in the WM's window tree.
  Has a parent, children, a paint callback, and a `video_context_t`. Shared code
  in `user/wmlib/` operates on this type for both server windows and the demo's
  own widgets.
- **`client_window_t`** (`wm_client.h:10`) — the *server's* per-client record. Its
  **first member is `window_t window`** (`wm_client.h:12`), so a
  `client_window_t *` is also a valid `window_t *`. That embedding is
  load-bearing: the server freely casts `(window_t *)cw` and back to
  `(client_window_t *)window` (e.g. `wm_client.c:240`, `:541`, `:586`), letting
  the generic scene-graph painter call into client-specific paint code.

## The walk

1. **The client has no connect call.** `main()` (`wmclient_demo.c:29`) just calls
   `wm_create_window(...)` (`wmclient_demo.c:32`). There is no socket, no
   handshake. The "connection" was established when the WM launched this process:
   in `client_launch` the child `dup2`s the event pipe onto fd 3 and the command
   pipe onto fd 4 (`wm_client.c:800-801`) before `exec`. The client library
   simply hardcodes those numbers as `WM_EVT_FD = 3` and `WM_CMD_FD = 4`
   (`wm_protocol.h:36-37`). CMD flows client->server (fd 4); EVT flows
   server->client (fd 3).

2. **Window creation is synchronous over the pipe pair.** `wm_create_window`
   (`wmclient.c:490`) first starts the background event reader thread
   (`wm_ensure_reader_thread`, `wmclient.c:493`), writes a
   `WM_MSG_CREATE_WINDOW` to fd 4 (`wmclient.c:509`), then blocks in
   `wm_wait_for_window_created_event` (`wmclient.c:517`) until the reader thread
   delivers the matching `WM_EVENT_WINDOW_CREATED`. The reply carries the two SHM
   buffer names (`wm_event_window_created_t.shm_names`, `wm_protocol.h:98`), which
   the client `shm_open`/`mmap`s in `wm_remap_window_buffers`
   (`wmclient.c:544`, `:166`). After this, both processes have the same two
   buffers mapped.

3. **Server side of create.** The WM's per-client `client_reader_thread`
   (`wm_client.c:630`) reads the command byte, then `handle_create_window`
   (`wm_client.c:513`) allocates a `client_window_t`, creates the two SHM buffers
   (`client_create_shm_buffers`, `wm_client.c:523`), runs `window_init` on the
   embedded `window_t` (`wm_client.c:541`), installs
   `client_window_paint_handler` as that node's paint callback
   (`wm_client.c:568`), inserts it into the scene graph under the WM root
   (`window_insert_child`, `wm_client.c:586`), and writes
   `WM_EVENT_WINDOW_CREATED` back on the event fd (`wm_client.c:599`). Note
   `front_buffer` starts at 0 (`wm_client.c:558`).

4. **Rendering targets the back buffer.** Back in the client, `imui_init`
   (`imui.c:163`) points a `video_context_t` at `win->buffer`, which
   `wm_remap_window_buffers` set to the *back* buffer
   (`win->buffer = win->buffers[win->back_buffer]`, `wmclient.c:208`). The demo's
   `render_screen` draws rects and a button via `imui_fill_rect` / `imui_button`;
   each draw call also expands a per-frame dirty rectangle through
   `imui_track_dirty` (`imui.c:40`). Drawing lands in the back buffer; the server
   is still showing the front buffer.

5. **Frame end requests a present.** `imui_end_frame` (`imui.c:234`) is a no-op
   if nothing was drawn (`!ui->has_dirty`, `imui.c:236`) — that is why the demo's
   very first frame presents (it drew) but an empty frame would not. Otherwise it
   converts the accumulated dirty bounds to x/y/w/h and calls
   `wm_invalidate_region` (`imui.c:243`).

6. **`wm_invalidate_region` presents and then BLOCKS** (`wmclient.c:563`). Despite
   the name, this is the synchronous heart of the flow:
   - It takes `g_present_lock` (`wmclient.c:571`), which serializes presents so
     only one frame is in flight per process.
   - Under `g_state_lock` it reads the back-buffer index and computes
     `wait_for = ++win->presents_requested` (`wmclient.c:579`). Capturing
     `wait_for` as a snapshot, not re-reading the live counter, is what makes the
     wait condition stable.
   - It writes `WM_MSG_INVALIDATE` to fd 4 with `buffer_index = back`
     (`wmclient.c:583-592`). The client is telling the server: *the buffer you
     should now show is this one.*
   - It then sleeps on `g_state_cv` until
     `win->presents_completed >= wait_for` (`wmclient.c:603-604`). The calling
     thread is parked until the server confirms the present.

7. **Server reads the invalidate command.** `client_reader_thread` reads the
   `WM_MSG_INVALIDATE` body and dispatches `handle_invalidate` under the WM state
   lock (`wm_client.c:676-686`).

8. **`handle_invalidate` flips the front buffer and composites synchronously**
   (`wm_client.c:480`). It looks up the `client_window_t` by id
   (`find_client_by_window_id`, `wm_client.c:482`), sets
   `cw->front_buffer = msg->buffer_index` if that index is valid
   (`wm_client.c:486-487`) — so the client's *back* buffer becomes the server's
   *front* — then calls `client_invalidate_region` (`wm_client.c:489`). That maps
   client content coords into the decorated window (offsetting past the border and
   title bar) and calls `window_invalidate` (`wm_client.c:426`).
   `window_invalidate` (`window.c:335`) builds a dirty-region list and calls
   `window_paint` (`window.c:363`), which sets up clipping and ultimately invokes
   the node's paint callback — `client_window_paint_handler` (`wm_client.c:238`).
   That handler reads `cw->shm_buffers[front_index]` and blits it into the WM's
   `video_context_t` with `context_draw_bitmap` (`wm_client.c:260`). All of this
   runs on the server's reader thread before it replies — the present is
   composited, not merely queued.

9. **Server acknowledges.** After compositing, `handle_invalidate` writes
   `WM_EVENT_INVALIDATED` on the event fd, echoing the now-current
   `cw->front_buffer` back to the client (`wm_client.c:491-495`).

10. **Client's background reader bumps the completion counter.** The client's
    `wm_event_reader_thread` (`wmclient.c:410`) — a *different* thread from the one
    blocked in step 6 — reads the event byte and body via `wm_read_raw_event`
    (`wmclient.c:307`, case at `:346`), then `wm_process_raw_event_locked`
    (`wmclient.c:356`) routes `WM_EVENT_INVALIDATED` to
    `wm_apply_invalidated_event_locked` (`wmclient.c:288`). That function adopts the
    server's `front_buffer`, repoints `win->buffer` to the new *back* buffer for
    the next frame (`wmclient.c:300`), and increments `presents_completed` — but
    only while it is still behind `presents_requested` (`wmclient.c:303-304`), so a
    stray or duplicate event cannot over-count. It then broadcasts `g_state_cv`
    (`wmclient.c:402`).

11. **The blocked present wakes and returns.** The thread parked in step 6 sees
    `presents_completed >= wait_for`, drops `g_state_lock` and `g_present_lock`
    (`wmclient.c:605-607`), and `wm_invalidate_region` returns. The frame is on
    screen (composited into the WM video context) and the client is now drawing
    into the freshly-swapped back buffer. One present round-trip is complete.

## Gotchas

- **There is no connect/handshake API.** The whole transport is two inherited
  fds (3 and 4) set up by `dup2` in the WM's fork child (`wm_client.c:800-801`).
  A program run *outside* the WM has nothing on fd 4; `wm_create_window` detects
  the short `write` and prints "needs to be launched from inside the window
  manager" (`wmclient.c:510-511`).

- **`wm_invalidate_region` blocks.** The name suggests fire-and-forget, but it
  parks the caller on a condition variable until the server acknowledges
  (`wmclient.c:603-604`). `g_present_lock` (`wmclient.c:571`) further guarantees
  only one present is outstanding per client process.

- **Two unrelated "reader threads," one per side.** The server's
  `client_reader_thread` (`wm_client.c:630`) reads *commands* on fd 4. The
  client's `wm_event_reader_thread` (`wmclient.c:410`) reads *events* on fd 3.
  They have similar names and opposite roles; the present completes only because
  the client's event thread, not the blocked render thread, processes the ack.

- **First-member embedding makes the casts legal.** `client_window_t` begins with
  `window_t window` (`wm_client.h:12`). The generic painter in `window.c` only
  knows about `window_t`, but `client_window_paint_handler` casts the same pointer
  back to `client_window_t` to reach the SHM buffers (`wm_client.c:240`). Miss the
  embedding and these casts look like bugs.

- **The buffer index is a cross-process swap, tracked separately on each side.**
  The client's *back* buffer (`buffer_index` in the message) becomes the server's
  *front* buffer (`wm_client.c:487`); the `INVALIDATED` event echoes that index
  back so the client can repoint to the other buffer for the next frame
  (`wmclient.c:300`). Each side keeps its own `front_buffer`/`back_buffer`
  fields (`wm_window_t` vs `client_window_t`) and they reconcile only through this
  round-trip — there is no shared "current buffer" variable.

## See also

- `docs/wm_protocol.md` — the WM IPC protocol, shared-memory compositing model, and double-buffering overview.
- `docs/shm.md` — POSIX shared memory (`shm_open`/`mmap`), the mechanism behind the window buffers.
- `docs/pthreads.md` and `docs/futex.md` — the mutex/condition-variable primitives that implement the blocking present.

Acronyms used here (WM, SHM, IPC, ARGB, fd) are collected in `docs/glossary.md`.
