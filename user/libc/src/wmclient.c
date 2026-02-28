// ReSharper disable CppDFAConstantParameter
#include <array.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wm/wm_protocol.h>
#include <wm/wmclient.h>

static wm_window_t **g_windows = nullptr;

static pthread_mutex_t g_state_lock   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_state_cv      = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_create_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_present_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_reader_started = 0;
static int g_reader_dead    = 0;

typedef struct {
    uint8_t type;

    union {
        wm_event_window_created_t window_created;
        wm_event_mouse_t mouse;
        wm_event_key_t key;
        wm_event_window_resized_t window_resized;
        wm_event_window_closed_t window_closed;
    } payload;
} wm_client_event_t;

static wm_client_event_t *g_event_queue = nullptr;

typedef struct {
    uint8_t type;

    union {
        wm_event_window_created_t window_created;
        wm_event_mouse_t mouse;
        wm_event_key_t key;
        wm_event_window_resized_msg_t window_resized_msg;
        wm_event_window_closed_t window_closed;
        wm_event_invalidated_t invalidated;
    } payload;
} wm_raw_event_t;

static wm_window_t *wm_find_window_locked(uint32_t window_id)
{
    for (size_t i = 0; i < arr_len(g_windows); i++) {
        auto w = arr_get(g_windows, i);
        if (w && w->window_id == window_id)
            return w;
    }

    return nullptr;
}

static void wm_unregister_window_locked(uint32_t window_id)
{
    for (size_t i = 0; i < arr_len(g_windows); i++) {
        auto w = arr_get(g_windows, i);
        if (w && w->window_id == window_id) {
            arr_remove_at(g_windows, i);
            return;
        }
    }
}

static int wm_read_exact(int fd, void *buf, size_t count)
{
    size_t total = 0;
    auto bytes   = (uint8_t *)buf;

    while (total < count) {
        ssize_t n = read(fd, bytes + total, count - total);
        if (n <= 0)
            return -1;
        total += (size_t)n;
    }

    return 0;
}

static void wm_unmap_window_buffers(wm_window_t *win)
{
    const size_t buf_size = (size_t)win->width * (size_t)win->height * 4;
    for (int i = 0; i < 2; i++) {
        if (win->buffers[i] && buf_size)
            munmap(win->buffers[i], buf_size);
        if (win->shm_fds[i] >= 0)
            close(win->shm_fds[i]);
        win->buffers[i] = nullptr;
        win->shm_fds[i] = -1;
    }

    win->buffer = nullptr;
}

static int wm_map_named_buffer(const char *shm_name, size_t size, uint32_t **out_buffer, int *out_fd)
{
    if (!shm_name || !out_buffer || !out_fd || size == 0)
        return -1;

    int shm_fd = shm_open(shm_name, 0, 0);
    if (shm_fd < 0)
        return -1;

    void *new_buf = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (new_buf == MAP_FAILED) {
        close(shm_fd);
        return -1;
    }

    *out_buffer = (uint32_t *)new_buf;
    *out_fd     = shm_fd;
    return 0;
}

static int wm_remap_window_buffers(wm_window_t *win, const char shm_names[2][WM_SHM_NAME_MAX], uint8_t front_buffer,
                                   uint16_t width, uint16_t height)
{
    if (!win || !shm_names || width == 0 || height == 0)
        return -1;

    if (front_buffer > 1)
        front_buffer = 0;

    const size_t new_size    = (size_t)width * (size_t)height * 4;
    uint32_t *new_buffers[2] = {nullptr};
    int new_fds[2]           = {-1, -1};

    for (int i = 0; i < 2; i++) {
        if (wm_map_named_buffer(shm_names[i], new_size, &new_buffers[i], &new_fds[i]) != 0) {
            for (int j = 0; j < 2; j++) {
                if (new_buffers[j])
                    munmap(new_buffers[j], new_size);
                if (new_fds[j] >= 0)
                    close(new_fds[j]);
            }
            return -1;
        }
    }

    wm_unmap_window_buffers(win);

    win->buffers[0]   = new_buffers[0];
    win->buffers[1]   = new_buffers[1];
    win->shm_fds[0]   = new_fds[0];
    win->shm_fds[1]   = new_fds[1];
    win->width        = width;
    win->height       = height;
    win->front_buffer = front_buffer;
    win->back_buffer  = (uint8_t)(front_buffer ^ 1u);
    win->buffer       = win->buffers[win->back_buffer];

    return 0;
}

static int wm_event_is_visible(uint8_t type)
{
    switch (type) {
    case WM_EVENT_MOUSE:
    case WM_EVENT_KEY:
    case WM_EVENT_WINDOW_RESIZED:
    case WM_EVENT_WINDOW_CLOSED:
        return 1;
    default:
        return 0;
    }
}

static int wm_event_is_droppable(uint8_t type)
{
    switch (type) {
    case WM_EVENT_MOUSE:
    case WM_EVENT_KEY:
        return 1;
    default:
        return 0;
    }
}

static int wm_queue_drop_first_droppable_locked(void)
{
    for (unsigned int i = 0; i < arr_len(g_event_queue); i++) {
        if (wm_event_is_droppable(g_event_queue[i].type)) {
            arr_remove_at(g_event_queue, i);
            return 1;
        }
    }

    return 0;
}

static void wm_queue_push_locked(const wm_client_event_t *event)
{
    if (!arr_try_push(g_event_queue, *event)) {
        wm_queue_drop_first_droppable_locked();
        if (!arr_try_push(g_event_queue, *event)) {
            return;
        }
    }
    pthread_cond_broadcast(&g_state_cv);
}

static int wm_queue_take_first_type_locked(uint8_t type, wm_client_event_t *out)
{
    for (unsigned int i = 0; i < arr_len(g_event_queue); i++) {
        if (g_event_queue[i].type == type) {
            if (out)
                *out = g_event_queue[i];
            arr_remove_at(g_event_queue, i);
            return 1;
        }
    }

    return 0;
}

static int wm_queue_take_next_visible_locked(wm_client_event_t *out)
{
    for (unsigned int i = 0; i < arr_len(g_event_queue); i++) {
        if (wm_event_is_visible(g_event_queue[i].type)) {
            if (out)
                *out = g_event_queue[i];
            arr_remove_at(g_event_queue, i);
            return 1;
        }
    }

    return 0;
}

static int wm_read_raw_event(wm_raw_event_t *raw)
{
    memset(raw, 0, sizeof(*raw));

    if (wm_read_exact(WM_EVT_FD, &raw->type, 1) != 0)
        return -1;

    switch (raw->type) {
    case WM_EVENT_WINDOW_CREATED:
        raw->payload.window_created.type = raw->type;
        if (wm_read_exact(WM_EVT_FD, &raw->payload.window_created.window_id, sizeof(raw->payload.window_created) - 1) !=
            0) {
            return -1;
        }
        return 0;
    case WM_EVENT_MOUSE:
        raw->payload.mouse.type = raw->type;
        if (wm_read_exact(WM_EVT_FD, &raw->payload.mouse.window_id, sizeof(raw->payload.mouse) - 1) != 0)
            return -1;
        return 0;
    case WM_EVENT_KEY:
        raw->payload.key.type = raw->type;
        if (wm_read_exact(WM_EVT_FD, &raw->payload.key.window_id, sizeof(raw->payload.key) - 1) != 0)
            return -1;
        return 0;
    case WM_EVENT_WINDOW_RESIZED:
        raw->payload.window_resized_msg.type = raw->type;
        if (wm_read_exact(WM_EVT_FD,
                          &raw->payload.window_resized_msg.window_id,
                          sizeof(raw->payload.window_resized_msg) - 1) != 0) {
            return -1;
        }
        return 0;
    case WM_EVENT_WINDOW_CLOSED:
        raw->payload.window_closed.type = raw->type;
        if (wm_read_exact(WM_EVT_FD, &raw->payload.window_closed.window_id, sizeof(raw->payload.window_closed) - 1) !=
            0)
            return -1;
        return 0;
    case WM_EVENT_INVALIDATED:
        raw->payload.invalidated.type = raw->type;
        if (wm_read_exact(WM_EVT_FD, &raw->payload.invalidated.window_id, sizeof(raw->payload.invalidated) - 1) != 0)
            return -1;
        return 0;
    default:
        return -1;
    }
}

static int wm_process_raw_event_locked(const wm_raw_event_t *raw)
{
    wm_client_event_t queued = {0};

    switch (raw->type) {
    case WM_EVENT_WINDOW_CREATED:
        queued.type                   = WM_EVENT_WINDOW_CREATED;
        queued.payload.window_created = raw->payload.window_created;
        wm_queue_push_locked(&queued);
        return 0;
    case WM_EVENT_MOUSE:
        queued.type          = WM_EVENT_MOUSE;
        queued.payload.mouse = raw->payload.mouse;
        wm_queue_push_locked(&queued);
        return 0;
    case WM_EVENT_KEY:
        queued.type        = WM_EVENT_KEY;
        queued.payload.key = raw->payload.key;
        wm_queue_push_locked(&queued);
        return 0;
    case WM_EVENT_WINDOW_RESIZED:
        {
            const wm_event_window_resized_msg_t *msg = &raw->payload.window_resized_msg;

            wm_window_t *win = wm_find_window_locked(msg->window_id);
            if (win && wm_remap_window_buffers(win, msg->shm_names, msg->front_buffer, msg->width, msg->height) != 0)
                return -1;

            queued.type                             = WM_EVENT_WINDOW_RESIZED;
            queued.payload.window_resized.type      = WM_EVENT_WINDOW_RESIZED;
            queued.payload.window_resized.window_id = msg->window_id;
            queued.payload.window_resized.width     = msg->width;
            queued.payload.window_resized.height    = msg->height;
            wm_queue_push_locked(&queued);
            return 0;
        }
    case WM_EVENT_WINDOW_CLOSED:
        queued.type                  = WM_EVENT_WINDOW_CLOSED;
        queued.payload.window_closed = raw->payload.window_closed;
        wm_queue_push_locked(&queued);
        return 0;
    case WM_EVENT_INVALIDATED:
        {
            wm_window_t *win = wm_find_window_locked(raw->payload.invalidated.window_id);
            if (win) {
                uint8_t front = raw->payload.invalidated.front_buffer;
                if (front > 1)
                    front = win->front_buffer <= 1 ? win->front_buffer : 0;

                if (win->buffers[front]) {
                    win->front_buffer = front;
                    win->back_buffer  = (uint8_t)(front ^ 1u);
                    win->buffer       = win->buffers[win->back_buffer];
                }

                if (win->presents_completed < win->presents_requested)
                    win->presents_completed++;
            }

            pthread_cond_broadcast(&g_state_cv);
            return 0;
        }
    default:
        return -1;
    }
}

static void *wm_event_reader_thread([[maybe_unused]] void *arg)
{
    while (1) {
        wm_raw_event_t raw = {0};
        if (wm_read_raw_event(&raw) != 0)
            break;

        pthread_mutex_lock(&g_state_lock);
        const int rc = wm_process_raw_event_locked(&raw);
        if (rc != 0) {
            g_reader_dead = 1;
            pthread_cond_broadcast(&g_state_cv);
            pthread_mutex_unlock(&g_state_lock);
            return nullptr;
        }
        pthread_mutex_unlock(&g_state_lock);
    }

    pthread_mutex_lock(&g_state_lock);
    g_reader_dead = 1;
    pthread_cond_broadcast(&g_state_cv);
    pthread_mutex_unlock(&g_state_lock);

    return nullptr;
}

static int wm_ensure_reader_thread(void)
{
    pthread_mutex_lock(&g_state_lock);

    if (g_reader_started) {
        const int rc = g_reader_dead ? -1 : 0;
        pthread_mutex_unlock(&g_state_lock);
        return rc;
    }

    pthread_t reader_thread = 0;
    if (pthread_create(&reader_thread, nullptr, wm_event_reader_thread, nullptr) != 0) {
        g_reader_dead = 1;
        pthread_cond_broadcast(&g_state_cv);
        pthread_mutex_unlock(&g_state_lock);
        return -1;
    }

    pthread_detach(reader_thread);
    g_reader_started = 1;

    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

static int wm_wait_for_window_created_event(wm_event_window_created_t *out)
{
    pthread_mutex_lock(&g_state_lock);

    while (1) {
        wm_client_event_t queued = {0};
        if (wm_queue_take_first_type_locked(WM_EVENT_WINDOW_CREATED, &queued)) {
            *out = queued.payload.window_created;
            pthread_mutex_unlock(&g_state_lock);
            return 0;
        }

        if (g_reader_dead) {
            pthread_mutex_unlock(&g_state_lock);
            return -1;
        }

        pthread_cond_wait(&g_state_cv, &g_state_lock);
    }
}

void wm_window_insert_child(uint16_t parent_id, uint16_t child_id)
{
    wm_msg_window_insert_child_t msg = {
        .type = WM_MSG_WINDOW_INSERT_CHILD, .parent_id = parent_id, .child_id = child_id};

    write(WM_CMD_FD, &msg, sizeof(msg));
}

wm_window_t *wm_create_window(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags, uint16_t parent_id,
                              const char *title)
{
    if (wm_ensure_reader_thread() != 0) {
        return nullptr;
    }

    wm_msg_create_window_t msg = {0};
    msg.type                   = WM_MSG_CREATE_WINDOW;
    msg.x                      = x;
    msg.y                      = y;
    msg.width                  = width;
    msg.height                 = height;
    msg.flags                  = flags;
    msg.parent_id              = parent_id;

    if (title) {
        size_t len = strlen(title);
        if (len >= WM_TITLE_MAX)
            len = WM_TITLE_MAX - 1;
        memcpy(msg.title, title, len);
        msg.title[len] = '\0';
    }

    pthread_mutex_lock(&g_create_lock);

    const ssize_t n = write(WM_CMD_FD, &msg, sizeof(msg));
    if (n != (ssize_t)sizeof(msg)) {
        printf("This application needs to be launched from inside the window manager\n");
        pthread_mutex_unlock(&g_create_lock);
        return nullptr;
    }

    wm_event_window_created_t resp = {0};
    if (wm_wait_for_window_created_event(&resp) != 0) {
        pthread_mutex_unlock(&g_create_lock);
        return nullptr;
    }

    pthread_mutex_unlock(&g_create_lock);

    wm_window_t *win = malloc(sizeof(wm_window_t));
    if (!win)
        return nullptr;

    memset(win, 0, sizeof(*win));
    win->window_id  = resp.window_id;
    win->shm_fds[0] = -1;
    win->shm_fds[1] = -1;
    win->flags      = flags;

    pthread_mutex_lock(&g_state_lock);

    if (!arr_try_push(g_windows, win)) {
        pthread_mutex_unlock(&g_state_lock);
        free(win);
        return nullptr;
    }

    if (wm_remap_window_buffers(win, resp.shm_names, resp.front_buffer, resp.width, resp.height) != 0) {
        wm_unregister_window_locked(win->window_id);
        pthread_mutex_unlock(&g_state_lock);
        free(win);
        return nullptr;
    }

    if (title) {
        size_t len = strlen(title);
        if (len >= WM_TITLE_MAX)
            len = WM_TITLE_MAX - 1;
        memcpy(win->title, title, len);
        win->title[len] = '\0';
    } else {
        win->title[0] = '\0';
    }

    pthread_mutex_unlock(&g_state_lock);

    return win;
}

void wm_invalidate_region(wm_window_t *win, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    if (!win)
        return;

    if (wm_ensure_reader_thread() != 0)
        return;

    pthread_mutex_lock(&g_present_lock);

    pthread_mutex_lock(&g_state_lock);

    uint8_t back = win->back_buffer;
    if (back > 1 || !win->buffers[back])
        back = (uint8_t)(win->front_buffer ^ 1u);

    const uint32_t wait_for = ++win->presents_requested;

    pthread_mutex_unlock(&g_state_lock);

    wm_msg_invalidate_t msg = {0};
    msg.type                = WM_MSG_INVALIDATE;
    msg.window_id           = win->window_id;
    msg.buffer_index        = back;
    msg.x                   = x;
    msg.y                   = y;
    msg.width               = w;
    msg.height              = h;

    const ssize_t n = write(WM_CMD_FD, &msg, sizeof(msg));
    if (n != (ssize_t)sizeof(msg)) {
        pthread_mutex_lock(&g_state_lock);
        if (win->presents_requested > 0)
            win->presents_requested--;
        pthread_mutex_unlock(&g_state_lock);
        pthread_mutex_unlock(&g_present_lock);
        return;
    }

    pthread_mutex_lock(&g_state_lock);
    while (!g_reader_dead && win->presents_completed < wait_for)
        pthread_cond_wait(&g_state_cv, &g_state_lock);
    pthread_mutex_unlock(&g_state_lock);

    pthread_mutex_unlock(&g_present_lock);
}

void wm_invalidate(wm_window_t *win)
{
    if (!win)
        return;

    wm_invalidate_region(win, 0, 0, win->width, win->height);
}

void wm_invalidate_all(wm_window_t *win)
{
    if (!win)
        return;

    wm_invalidate_region(win, 0, 0, win->width, win->height);
}

void wm_destroy_window(wm_window_t *win)
{
    if (!win)
        return;

    pthread_mutex_lock(&g_present_lock);

    wm_msg_destroy_window_t msg = {0};
    msg.type                    = WM_MSG_DESTROY_WINDOW;
    msg.window_id               = win->window_id;
    write(WM_CMD_FD, &msg, sizeof(msg));

    pthread_mutex_lock(&g_state_lock);
    wm_unregister_window_locked(win->window_id);
    pthread_mutex_unlock(&g_state_lock);

    wm_unmap_window_buffers(win);
    free(win);

    pthread_mutex_unlock(&g_present_lock);
}

static void wm_copy_visible_event(const wm_client_event_t *event, void *event_buf, uint8_t *out_type)
{
    *out_type = event->type;

    if (!event_buf)
        return;

    switch (event->type) {
    case WM_EVENT_MOUSE:
        memcpy(event_buf, &event->payload.mouse, sizeof(event->payload.mouse));
        break;
    case WM_EVENT_KEY:
        memcpy(event_buf, &event->payload.key, sizeof(event->payload.key));
        break;
    case WM_EVENT_WINDOW_RESIZED:
        memcpy(event_buf, &event->payload.window_resized, sizeof(event->payload.window_resized));
        break;
    case WM_EVENT_WINDOW_CLOSED:
        memcpy(event_buf, &event->payload.window_closed, sizeof(event->payload.window_closed));
        break;
    default:
        break;
    }
}

int wm_next_event(void *event_buf, uint8_t *out_type)
{
    if (!out_type)
        return -1;

    if (wm_ensure_reader_thread() != 0)
        return -1;

    pthread_mutex_lock(&g_state_lock);

    while (1) {
        wm_client_event_t queued = {0};
        if (wm_queue_take_next_visible_locked(&queued)) {
            pthread_mutex_unlock(&g_state_lock);
            wm_copy_visible_event(&queued, event_buf, out_type);
            return 0;
        }

        if (g_reader_dead) {
            pthread_mutex_unlock(&g_state_lock);
            return -1;
        }

        pthread_cond_wait(&g_state_cv, &g_state_lock);
    }
}

void wm_shutdown_events(void)
{
    pthread_mutex_lock(&g_state_lock);
    g_reader_dead = 1;
    pthread_cond_broadcast(&g_state_cv);
    pthread_mutex_unlock(&g_state_lock);
}
