#include <wm/wmclient.h>
#include <wm/wm_protocol.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define WM_CLIENT_MAX_WINDOWS 16

static wm_window_t *g_windows[WM_CLIENT_MAX_WINDOWS];

static void wm_register_window(wm_window_t *win)
{
    if (!win)
        return;

    for (int i = 0; i < WM_CLIENT_MAX_WINDOWS; i++) {
        if (!g_windows[i]) {
            g_windows[i] = win;
            return;
        }
    }
}

static void wm_unregister_window(uint32_t window_id)
{
    for (int i = 0; i < WM_CLIENT_MAX_WINDOWS; i++) {
        if (g_windows[i] && g_windows[i]->window_id == window_id) {
            g_windows[i] = nullptr;
            return;
        }
    }
}

static wm_window_t *wm_find_window(uint32_t window_id)
{
    for (int i = 0; i < WM_CLIENT_MAX_WINDOWS; i++) {
        if (g_windows[i] && g_windows[i]->window_id == window_id)
            return g_windows[i];
    }
    return nullptr;
}

static int wm_remap_window_buffer(wm_window_t *win, const char *shm_name, uint16_t width, uint16_t height)
{
    if (!win || !shm_name || width == 0 || height == 0)
        return -1;

    int shm_fd = shm_open(shm_name, 0, 0);
    if (shm_fd < 0)
        return -1;

    const size_t new_size = (size_t)width * (size_t)height * 4;
    void *new_buf = mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (new_buf == MAP_FAILED) {
        close(shm_fd);
        return -1;
    }

    if (win->buffer)
        munmap(win->buffer, (size_t)win->width * (size_t)win->height * 4);
    if (win->shm_fd >= 0)
        close(win->shm_fd);

    win->buffer = (uint32_t *)new_buf;
    win->width = width;
    win->height = height;
    win->shm_fd = shm_fd;
    return 0;
}

wm_window_t *wm_create_window(int16_t x, int16_t y, uint16_t width, uint16_t height, const char *title)
{
    wm_msg_create_window_t msg = {0};
    msg.type = WM_MSG_CREATE_WINDOW;
    msg.x = x;
    msg.y = y;
    msg.width = width;
    msg.height = height;
    if (title)
    {
        size_t len = strlen(title);
        if (len >= WM_TITLE_MAX)
            len = WM_TITLE_MAX - 1;
        memcpy(msg.title, title, len);
        msg.title[len] = '\0';
    }

    ssize_t n = write(WM_CMD_FD, &msg, sizeof(msg));
    if (n != (ssize_t)sizeof(msg))
        return nullptr;

    wm_event_window_created_t resp = {0};
    n = read(WM_EVT_FD, &resp, sizeof(resp));
    if (n != (ssize_t)sizeof(resp) || resp.type != WM_EVENT_WINDOW_CREATED)
        return nullptr;

    int shm_fd = shm_open(resp.shm_name, 0, 0);
    if (shm_fd < 0)
        return nullptr;

    size_t buf_size = (size_t)resp.width * (size_t)resp.height * 4;
    void *buf = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (buf == MAP_FAILED)
    {
        close(shm_fd);
        return nullptr;
    }

    wm_window_t *win = malloc(sizeof(wm_window_t));
    if (!win)
    {
        munmap(buf, buf_size);
        close(shm_fd);
        return nullptr;
    }

    win->window_id = resp.window_id;
    win->width = resp.width;
    win->height = resp.height;
    win->buffer = (uint32_t *)buf;
    win->shm_fd = shm_fd;

    wm_register_window(win);
    return win;
}

void wm_invalidate(const wm_window_t *win, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    if (!win)
        return;
    wm_msg_invalidate_t msg = {0};
    msg.type = WM_MSG_INVALIDATE;
    msg.window_id = win->window_id;
    msg.x = x;
    msg.y = y;
    msg.width = w;
    msg.height = h;
    write(WM_CMD_FD, &msg, sizeof(msg));
}

void wm_invalidate_all(const wm_window_t *win)
{
    if (!win)
        return;
    wm_invalidate(win, 0, 0, win->width, win->height);
}

void wm_destroy_window(wm_window_t *win)
{
    if (!win)
        return;
    wm_msg_destroy_window_t msg = {0};
    msg.type = WM_MSG_DESTROY_WINDOW;
    msg.window_id = win->window_id;
    write(WM_CMD_FD, &msg, sizeof(msg));

    wm_unregister_window(win->window_id);

    if (win->buffer)
        munmap(win->buffer, (size_t)win->width * (size_t)win->height * 4);
    if (win->shm_fd >= 0)
        close(win->shm_fd);
    free(win);
}

int wm_next_event(void *event_buf, uint8_t *out_type)
{
    uint8_t type_byte = 0;
    ssize_t n = read(WM_EVT_FD, &type_byte, 1);
    if (n != 1)
        return -1;

    *out_type = type_byte;

    switch (type_byte)
    {
    case WM_EVENT_MOUSE:
    {
        wm_event_mouse_t *ev = (wm_event_mouse_t *)event_buf;
        ev->type = type_byte;
        n = read(WM_EVT_FD, &ev->window_id, sizeof(*ev) - 1);
        return (n == (ssize_t)(sizeof(*ev) - 1)) ? 0 : -1;
    }
    case WM_EVENT_KEY:
    {
        wm_event_key_t *ev = (wm_event_key_t *)event_buf;
        ev->type = type_byte;
        n = read(WM_EVT_FD, &ev->window_id, sizeof(*ev) - 1);
        return (n == (ssize_t)(sizeof(*ev) - 1)) ? 0 : -1;
    }
    case WM_EVENT_WINDOW_RESIZED:
    {
        wm_event_window_resized_msg_t msg = {0};
        msg.type = type_byte;
        n = read(WM_EVT_FD, &msg.window_id, sizeof(msg) - 1);
        if (n != (ssize_t)(sizeof(msg) - 1))
            return -1;

        wm_window_t *win = wm_find_window(msg.window_id);
        if (win && wm_remap_window_buffer(win, msg.shm_name, msg.width, msg.height) != 0)
            return -1;

        if (event_buf) {
            wm_event_window_resized_t *ev = (wm_event_window_resized_t *)event_buf;
            ev->type = type_byte;
            ev->window_id = msg.window_id;
            ev->width = msg.width;
            ev->height = msg.height;
        }

        return 0;
    }
    case WM_EVENT_WINDOW_CLOSED:
    {
        wm_event_window_closed_t *ev = (wm_event_window_closed_t *)event_buf;
        ev->type = type_byte;
        n = read(WM_EVT_FD, &ev->window_id, sizeof(*ev) - 1);
        return (n == (ssize_t)(sizeof(*ev) - 1)) ? 0 : -1;
    }
    default:
        return -1;
    }
}
