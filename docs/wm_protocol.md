# Window Manager Protocol

The window manager (WM) runs as a user-space process that owns the framebuffer.
Client processes run in separate address spaces and communicate with the WM over
pipes, rendering into shared memory buffers.

## Architecture

```
Client Process          WM Process
+-----------+          +------------+
| draw into |  pipes   | read cmds  |
| shm buffer|--------->| composite  |
|           |<---------| send events|
+-----------+  events  +------------+
      |                      |
      v                      v
  shm_open()            shm_open()
  mmap(shm_fd)          mmap(shm_fd)
      |                      |
      +--- same phys pages --+
```

## Launching a client

When the WM launches a client:

1. Creates two pipe pairs (command and event).
2. Forks the process.
3. In the child: uses `dup2` to place the event read end at fd 3 (`WM_EVT_FD`)
   and the command write end at fd 4 (`WM_CMD_FD`), then execs the client
   binary.
4. In the parent: spawns a reader thread that processes commands from the
   client's command pipe.
5. A single client connection can create multiple windows (including child
   windows) on that same pipe pair.

## Protocol messages

All messages are fixed-size structs prefixed with a `uint8_t type` field.
Definitions are in `user/libc/include/wm/wm_protocol.h`.

### Client -> WM (commands via fd 4)

| Type                         | Struct                         | Description                                                    |
|------------------------------|--------------------------------|----------------------------------------------------------------|
| `WM_MSG_CREATE_WINDOW`       | `wm_msg_create_window_t`       | Request a new window with position, size, and title            |
| `WM_MSG_WINDOW_INSERT_CHILD` | `wm_msg_window_insert_child_t` | Reparent an existing client window under another client window |
| `WM_MSG_INVALIDATE`          | `wm_msg_invalidate_t`          | Present a specific client buffer index and repaint a region    |
| `WM_MSG_DESTROY_WINDOW`      | `wm_msg_destroy_window_t`      | Close and destroy a window                                     |

### WM -> Client (events via fd 3)

| Type                             | Struct                          | Description                                                                                                                                   |
|----------------------------------|---------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|
| `WM_EVENT_WINDOW_CREATED`        | `wm_event_window_created_t`     | Window created; contains both shm names and initial front id (consumed internally by `wm_create_window()`, not returned from `wm_next_event`) |
| `WM_EVENT_WINDOW_CHILD_INSERTED` | *(type byte only)*              | Acknowledgment that a child window was reparented                                                                                             |
| `WM_EVENT_MOUSE`                 | `wm_event_mouse_t`              | Mouse click in the window's client area                                                                                                       |
| `WM_EVENT_KEY`                   | `wm_event_key_t`                | Key press/release for the focused client                                                                                                      |
| `WM_EVENT_WINDOW_RESIZED`        | `wm_event_window_resized_msg_t` | Client area resized (wire struct includes `front_buffer` and `shm_names`; libc extracts the smaller `wm_event_window_resized_t` for callers)  |
| `WM_EVENT_WINDOW_CLOSED`         | `wm_event_window_closed_t`      | Window was closed by the WM                                                                                                                   |
| `WM_EVENT_INVALIDATED`           | `wm_event_invalidated_t`        | Internal present acknowledgment with the new front-buffer id                                                                                  |

### Keyboard event encoding

- `wm_event_key_t.keycode` carries a set-1 scancode value.
- `wm_event_key_t.pressed` is `1` for key press (make) and `0` for release (break).
- For extended (`0xE0`) scancodes, WM sets bit `0x80` in `keycode` so clients can distinguish them from non-extended keys.

### Resize events

- Client windows can be resized by dragging the bottom-right corner.
- WM reallocates both client shared-memory buffers and sends a resize event.
- The libc WM client layer remaps both buffers before returning `WM_EVENT_WINDOW_RESIZED` from `wm_next_event`.

### Present synchronization

- `WM_MSG_INVALIDATE` is acknowledged by WM with `WM_EVENT_INVALIDATED` after compositing.
- The libc WM client layer waits for this acknowledgment in `wm_invalidate_region()` before
  switching the client-visible draw buffer.
- `WM_EVENT_INVALIDATED` is handled internally by libc and is not returned from
  `wm_next_event`.

## Window creation flow

1. Client sends `WM_MSG_CREATE_WINDOW` with desired width, height, title.
2. WM allocates two shared memory regions, mmaps both, and creates a
   `client_window_t` in the window tree.
3. WM responds with `WM_EVENT_WINDOW_CREATED` containing both shm names plus the
   current front-buffer index.
4. Client opens and maps both shms (`width * height * 4` bytes each, 32-bit
   ARGB), draws into the back buffer, and presents it.
5. Client sends `WM_MSG_INVALIDATE` to flip/composite.

## Client library

`user/libc/include/wm/wmclient.h` provides a simple API:

```c
wm_window_t *wm_create_window(int16_t x, int16_t y, uint16_t w, uint16_t h,
                              uint16_t flags, uint16_t parent_id, const char *title);
void wm_window_insert_child(uint16_t parent_id, uint16_t child_id);
void wm_invalidate(wm_window_t *win);
void wm_invalidate_region(wm_window_t *win, int16_t x, int16_t y, uint16_t w, uint16_t h);
void wm_invalidate_all(wm_window_t *win);
void wm_destroy_window(wm_window_t *win);
int wm_next_event(void *event_buf, uint8_t *out_type);
void wm_shutdown_events(void);
```

`user/libc/include/wm/imui.h` provides a lightweight immediate-mode layer on top
of `wmclient` for building widgets directly while rendering to the window's back buffer.

`wm_shutdown_events()` marks the event stream as closed and wakes threads blocked in `wm_next_event()`.

## Demo clients

- `user/wmclient_demo.c` creates a window, draws four colored rectangles, and
  paints a red dot wherever the user clicks. It also reacts to key press events
  by drawing colored blocks derived from the keycode.
- `user/calculator.c` is a standalone calculator client executable launched by
  the WM desktop button (`/bin/calculator`) and uses `wm/imui.h` for
  immediate-mode button widgets.
- `user/term.c` is a minimal terminal client launched as
  `/bin/term`. It opens a PTY pair, runs `/bin/sh` on the slave side,
  translates WM key events into PTY input bytes, supports basic ANSI
  cursor/erase/color sequences, and updates PTY winsize on WM resize events.
- `user/doom/doomgeneric_e64.c` also supports WM mode: when launched from WM it
  creates a client window, consumes WM key events, and remaps its shm buffer on
  WM resize events.

## Limits

- Client connections and windows use dynamic arrays with no fixed upper limit.
- `WM_TITLE_MAX`: 64-byte window title.
- `WM_SHM_NAME_MAX`: 64-byte shared memory name.
