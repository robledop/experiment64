#include "wm_client.h"
#include <array.h>
#include <fcntl.h>
#include <mouse.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wm/video_context.h>
#include <wm/window.h>
#include <wm/wm_protocol.h>

#include "crash_dialog.h"
#include "taskbar.h"

// Defined in main.c
typedef struct { int pid; int status; crash_info_t info; } crash_entry_t;
extern const crash_entry_t *crash_log_find(int pid);

client_manager_t *g_mgr;
static window_t *g_parent;
static pthread_mutex_t g_wm_state_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ensure_client_manager_initialized(void)
{
    if (!g_mgr || !g_mgr->initialized) {
        panic("Client manager not initialized");
    }
}

static void client_destroy_shm_buffers(client_window_t *cw, uint16_t width, uint16_t height);

static const char *path_basename(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            last = p + 1;
    }
    return last;
}

static int client_register_connection(client_manager_t *mgr, int cmd_fd, int evt_fd, int client_pid,
                                      const char *path)
{
    if (!mgr || cmd_fd < 0 || evt_fd < 0 || client_pid <= 0)
        return -1;

    for (size_t i = 0; i < arr_len(mgr->connections); i++) {
        if (mgr->connections[i].cmd_fd == cmd_fd && mgr->connections[i].client_pid == client_pid &&
            mgr->connections[i].evt_fd == evt_fd)
            return 0;
    }

    client_connection_t conn = {.cmd_fd = cmd_fd, .evt_fd = evt_fd, .client_pid = client_pid};
    if (path) {
        const char *base = path_basename(path);
        snprintf(conn.name, sizeof(conn.name), "%s", base);
    }
    arr_push(mgr->connections, conn);
    return 0;
}

static void client_unregister_connection_by_cmd_fd(client_manager_t *mgr, int cmd_fd)
{
    if (!mgr)
        return;

    for (size_t i = 0; i < arr_len(mgr->connections); i++) {
        if (mgr->connections[i].cmd_fd != cmd_fd)
            continue;

        arr_remove_at(mgr->connections, i);
        return;
    }
}

static const client_connection_t *client_find_connection_by_cmd_fd(const client_manager_t *mgr, int cmd_fd)
{
    if (!mgr || cmd_fd < 0)
        return nullptr;

    for (size_t i = 0; i < arr_len(mgr->connections); i++) {
        auto connection = arr_get(mgr->connections, i);
        if (connection.cmd_fd == cmd_fd)
            return &arr_get(mgr->connections, i);
    }

    return nullptr;
}

static int client_register_window(client_manager_t *mgr, client_window_t *window)
{
    if (!mgr || !window)
        return -1;

    arr_push(mgr->windows, window);
    return 0;
}

static int client_find_window_index_by_id(const client_manager_t *mgr, uint32_t window_id)
{
    if (!mgr)
        return -1;

    for (size_t i = 0; i < arr_len(mgr->windows); i++) {
        auto window = arr_get(mgr->windows, i);
        if (window && window->window_id == window_id)
            return (int)i;
    }

    return -1;
}

static void client_remove_window_at(client_manager_t *mgr, int index)
{
    if (!mgr || index < 0)
        return;

    arr_remove_at(mgr->windows, (size_t)index);
}

static client_window_t *client_find_first_child_window(const client_manager_t *mgr, const client_window_t *parent)
{
    if (!mgr || !parent)
        return nullptr;

    for (size_t i = 0; i < arr_len(mgr->windows); i++) {
        client_window_t *candidate = arr_get(mgr->windows, i);
        if (candidate && candidate->window.parent == (window_t *)parent)
            return candidate;
    }

    return nullptr;
}

static void client_destroy_window_resources(client_window_t *cw)
{
    if (!cw)
        return;

    window_remove_child(cw->window.parent, &cw->window);
    client_destroy_shm_buffers(cw, cw->content_width, cw->content_height);

    if (cw->window.title)
        free(cw->window.title);
    if (cw->window.children)
        arr_free(cw->window.children);

    free(cw);
}

static void client_destroy_window_recursive(client_manager_t *mgr, client_window_t *cw)
{
    if (!mgr || !cw)
        return;

    uint32_t target_id = cw->window_id;

    while (1) {
        int idx = client_find_window_index_by_id(mgr, target_id);
        if (idx < 0)
            return;

        client_window_t *current = arr_get(mgr->windows, (size_t)idx);
        client_window_t *child   = client_find_first_child_window(mgr, current);
        if (!child || child == current)
            break;

        client_destroy_window_recursive(mgr, child);
    }

    int idx = client_find_window_index_by_id(mgr, target_id);
    if (idx >= 0) {
        client_window_t *current = arr_get(mgr->windows, (size_t)idx);
        client_remove_window_at(mgr, idx);
        client_destroy_window_resources(current);
        int button_index = taskbar_find_window(&current->window);
        taskbar_remove_button(button_index);
    }
}

static void client_destroy_connection_windows(client_manager_t *mgr, int cmd_fd)
{
    if (!mgr || cmd_fd < 0)
        return;

    while (1) {
        client_window_t *window = nullptr;

        for (size_t i = 0; i < arr_len(mgr->windows); i++) {
            auto w = arr_get(mgr->windows, i);
            if (w && w->cmd_fd == cmd_fd) {
                window = w;
                break;
            }
        }

        if (!window)
            return;

        client_destroy_window_recursive(mgr, window);
    }
}

static void client_inner_dims_from_window_dims(uint16_t window_width, uint16_t window_height, uint16_t *inner_width,
                                               uint16_t *inner_height)
{
    if (!inner_width || !inner_height)
        return;

    if (window_width <= 2 * WIN_BORDER_WIDTH || window_height <= WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH) {
        *inner_width  = 0;
        *inner_height = 0;
        return;
    }

    *inner_width  = (uint16_t)(window_width - 2 * WIN_BORDER_WIDTH);
    *inner_height = (uint16_t)(window_height - WIN_TITLE_HEIGHT - WIN_BORDER_WIDTH);
}

void wm_state_lock(void)
{
    pthread_mutex_lock(&g_wm_state_mutex);
}

void wm_state_unlock(void)
{
    pthread_mutex_unlock(&g_wm_state_mutex);
}

void client_window_paint_handler(const window_t *window)
{
    auto cw = (const client_window_t *)window;

    uint16_t inner_w = 0;
    uint16_t inner_h = 0;
    client_inner_dims_from_window_dims(window->width, window->height, &inner_w, &inner_h);

    if (!inner_w || !inner_h) {
        context_fill_rect(window->context, 0, 0, window->width, window->height, WIN_BGCOLOR);
        return;
    }

    const uint8_t front_index = cw->front_buffer <= 1 ? cw->front_buffer : 0;
    uint32_t *front_buffer    = cw->shm_buffers[front_index];
    if (!front_buffer || !cw->content_width || !cw->content_height) {
        context_fill_rect(window->context, 0, 0, inner_w, inner_h, WIN_BGCOLOR);
        return;
    }

    uint16_t draw_w = cw->content_width < inner_w ? cw->content_width : inner_w;
    uint16_t draw_h = cw->content_height < inner_h ? cw->content_height : inner_h;
    context_draw_bitmap(window->context, 0, 0, draw_w, draw_h, front_buffer);

    if (draw_w < inner_w)
        context_fill_rect(window->context, draw_w, 0, inner_w - draw_w, inner_h, WIN_BGCOLOR);
    if (draw_h < inner_h)
        context_fill_rect(window->context, 0, draw_h, draw_w, inner_h - draw_h, WIN_BGCOLOR);
}

void client_window_mousedown_handler(window_t *window, int16_t x, int16_t y)
{
    auto cw = (const client_window_t *)window;
    if (cw->evt_fd < 0)
        return;

    wm_event_mouse_t ev = {0};
    ev.type             = WM_EVENT_MOUSE;
    ev.window_id        = cw->window_id;
    ev.x                = x;
    ev.y                = y;
    ev.buttons          = MOUSE_LEFT;
    write(cw->evt_fd, &ev, sizeof(ev));
}

static void client_shm_name(uint32_t window_id, uint32_t generation, uint8_t index, char *out_name, size_t out_size)
{
    if (!out_name || out_size == 0)
        return;
    snprintf(out_name, out_size, "wm_win_%u_%u_%u", window_id, generation, (uint32_t)index);
}

static int client_map_shm_buffer(const char *name, size_t size, uint32_t **out_buffer)
{
    if (!name || !out_buffer || size == 0)
        return -1;

    int shm_fd = shm_open(name, O_CREATE, size);
    if (shm_fd < 0)
        return -1;

    void *mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);

    if (mapped == MAP_FAILED)
        return -1;

    *out_buffer = (uint32_t *)mapped;
    memset(mapped, 0, size);
    return 0;
}

static void client_destroy_shm_buffers(client_window_t *cw, uint16_t width, uint16_t height)
{
    if (!cw)
        return;

    const size_t buf_size = (size_t)width * (size_t)height * 4;
    for (int i = 0; i < 2; i++) {
        if (cw->shm_buffers[i] && buf_size)
            munmap(cw->shm_buffers[i], buf_size);
        cw->shm_buffers[i] = nullptr;

        if (cw->shm_names[i][0]) {
            shm_unlink(cw->shm_names[i]);
            cw->shm_names[i][0] = '\0';
        }
    }
}

static int client_create_shm_buffers(uint32_t window_id, uint32_t generation, uint16_t width, uint16_t height,
                                     uint32_t *out_buffers[2], char out_names[2][WM_SHM_NAME_MAX])
{
    if (!out_buffers || !out_names || width == 0 || height == 0)
        return -1;

    const size_t buf_size = (size_t)width * (size_t)height * 4;
    for (int i = 0; i < 2; i++) {
        out_buffers[i]  = nullptr;
        out_names[i][0] = '\0';
    }

    for (uint8_t i = 0; i < 2; i++) {
        client_shm_name(window_id, generation, i, out_names[i], WM_SHM_NAME_MAX);
        if (client_map_shm_buffer(out_names[i], buf_size, &out_buffers[i]) != 0) {
            for (int j = 0; j < 2; j++) {
                if (out_buffers[j])
                    munmap(out_buffers[j], buf_size);
                if (out_names[j][0])
                    shm_unlink(out_names[j]);
            }
            return -1;
        }
    }

    return 0;
}

static int client_resize_shm_buffers(client_window_t *cw, uint16_t old_content_w, uint16_t old_content_h,
                                     uint16_t new_content_w, uint16_t new_content_h)
{
    if (!cw || !old_content_w || !old_content_h || !new_content_w || !new_content_h)
        return -1;

    const uint32_t next_generation     = cw->shm_generation + 1;
    uint32_t *new_buffers[2]           = {nullptr};
    char new_names[2][WM_SHM_NAME_MAX] = {{0}};

    if (client_create_shm_buffers(
            cw->window_id, next_generation, new_content_w, new_content_h, new_buffers, new_names) != 0) {
        return -1;
    }

    const uint8_t old_front_index = cw->front_buffer <= 1 ? cw->front_buffer : 0;
    const uint32_t *old_front     = cw->shm_buffers[old_front_index];
    const uint16_t copy_w         = old_content_w < new_content_w ? old_content_w : new_content_w;
    const uint16_t copy_h         = old_content_h < new_content_h ? old_content_h : new_content_h;

    if (old_front) {
        for (uint8_t i = 0; i < 2; i++) {
            for (uint16_t row = 0; row < copy_h; row++) {
                memcpy(new_buffers[i] + (size_t)row * new_content_w,
                       old_front + (size_t)row * old_content_w,
                       (size_t)copy_w * 4);
            }
        }
    }

    client_destroy_shm_buffers(cw, old_content_w, old_content_h);

    cw->shm_buffers[0] = new_buffers[0];
    cw->shm_buffers[1] = new_buffers[1];
    strncpy(cw->shm_names[0], new_names[0], WM_SHM_NAME_MAX - 1);
    cw->shm_names[0][WM_SHM_NAME_MAX - 1] = '\0';
    strncpy(cw->shm_names[1], new_names[1], WM_SHM_NAME_MAX - 1);
    cw->shm_names[1][WM_SHM_NAME_MAX - 1] = '\0';
    cw->content_width                     = new_content_w;
    cw->content_height                    = new_content_h;
    cw->front_buffer                      = 0;
    cw->shm_generation                    = next_generation;

    return 0;
}

static int client_invalidate_region(client_window_t *cw, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    if (!cw)
        return -1;

    if (x < 0 || y < 0)
        return -1;

    if ((uint16_t)x >= cw->content_width || (uint16_t)y >= cw->content_height)
        return -1;

    uint16_t w = width;
    uint16_t h = height;

    if ((uint32_t)x + (uint32_t)w > cw->content_width)
        w = (uint16_t)(cw->content_width - (uint16_t)x);
    if ((uint32_t)y + (uint32_t)h > cw->content_height)
        h = (uint16_t)(cw->content_height - (uint16_t)y);

    if (!w || !h)
        return -1;

    int ox = WIN_BORDER_WIDTH + x;
    int oy = WIN_TITLE_HEIGHT + y;
    window_invalidate((window_t *)cw, oy, ox, oy + h - 1, ox + w - 1);
    return 0;
}

static void client_window_close_handler(const window_t *window)
{
    auto cw                      = (client_window_t *)window;
    wm_event_window_closed_t evt = {};
    evt.type                     = WM_EVENT_WINDOW_CLOSED;
    evt.window_id                = cw->window_id;
    write(cw->evt_fd, &evt, sizeof(evt));
}

static void client_window_resize_handler(const window_t *window, uint16_t old_width, uint16_t old_height)
{
    auto cw = (client_window_t *)window;
    if (!cw || cw->evt_fd < 0)
        return;

    uint16_t old_content_w = 0;
    uint16_t old_content_h = 0;
    uint16_t new_content_w = 0;
    uint16_t new_content_h = 0;

    client_inner_dims_from_window_dims(old_width, old_height, &old_content_w, &old_content_h);
    client_inner_dims_from_window_dims(window->width, window->height, &new_content_w, &new_content_h);

    if (!old_content_w || !old_content_h || !new_content_w || !new_content_h)
        return;

    if (old_content_w == new_content_w && old_content_h == new_content_h)
        return;

    if (client_resize_shm_buffers(cw, old_content_w, old_content_h, new_content_w, new_content_h) != 0)
        return;

    wm_event_window_resized_msg_t ev = {0};
    ev.type                          = WM_EVENT_WINDOW_RESIZED;
    ev.window_id                     = cw->window_id;
    ev.width                         = cw->content_width;
    ev.height                        = cw->content_height;
    ev.front_buffer                  = cw->front_buffer;
    strncpy(ev.shm_names[0], cw->shm_names[0], WM_SHM_NAME_MAX - 1);
    ev.shm_names[0][WM_SHM_NAME_MAX - 1] = '\0';
    strncpy(ev.shm_names[1], cw->shm_names[1], WM_SHM_NAME_MAX - 1);
    ev.shm_names[1][WM_SHM_NAME_MAX - 1] = '\0';

    write(cw->evt_fd, &ev, sizeof(ev));
}

static void handle_invalidate([[maybe_unused]] window_t *parent, const wm_msg_invalidate_t *msg)
{
    client_window_t *cw = find_client_by_window_id(msg->window_id);
    if (!cw || cw->evt_fd < 0)
        return;

    if (msg->buffer_index <= 1 && cw->shm_buffers[msg->buffer_index])
        cw->front_buffer = msg->buffer_index;

    client_invalidate_region(cw, msg->x, msg->y, msg->width, msg->height);

    wm_event_invalidated_t ev = {0};
    ev.type                   = WM_EVENT_INVALIDATED;
    ev.window_id              = cw->window_id;
    ev.front_buffer           = cw->front_buffer;
    write(cw->evt_fd, &ev, sizeof(ev));
}

static void handle_window_insert_child(const wm_msg_window_insert_child_t *msg)
{
    auto child = find_client_by_window_id(msg->child_id);
    if (!child)
        return;
    auto parent = find_client_by_window_id(msg->parent_id);
    if (!parent)
        return;

    if (child->cmd_fd != parent->cmd_fd)
        return;

    window_insert_child(&parent->window, &child->window);
}

static void handle_create_window(client_manager_t *mgr, window_t *parent, int cmd_fd, const wm_msg_create_window_t *msg)
{
    const client_connection_t *connection = client_find_connection_by_cmd_fd(mgr, cmd_fd);
    if (!connection)
        return;

    uint32_t wid                          = mgr->next_window_id++;
    uint32_t *mapped_buffers[2]           = {nullptr};
    char mapped_names[2][WM_SHM_NAME_MAX] = {{0}};

    if (client_create_shm_buffers(wid, 0, msg->width, msg->height, mapped_buffers, mapped_names) != 0)
        return;

    uint16_t win_w = msg->width + 2 * WIN_BORDER_WIDTH;
    uint16_t win_h = msg->height + WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH;

    client_window_t *cw = calloc(1, sizeof(client_window_t));
    if (!cw) {
        const size_t buf_size = (size_t)msg->width * (size_t)msg->height * 4;
        for (int i = 0; i < 2; i++) {
            if (mapped_buffers[i])
                munmap(mapped_buffers[i], buf_size);
            if (mapped_names[i][0])
                shm_unlink(mapped_names[i]);
        }
        return;
    }

    if (!window_init((window_t *)cw, msg->x, msg->y, win_w, win_h, msg->flags, nullptr)) {
        const size_t buf_size = (size_t)msg->width * (size_t)msg->height * 4;
        for (int i = 0; i < 2; i++) {
            if (mapped_buffers[i])
                munmap(mapped_buffers[i], buf_size);
            if (mapped_names[i][0])
                shm_unlink(mapped_names[i]);
        }
        free(cw);
        return;
    }

    cw->shm_buffers[0] = mapped_buffers[0];
    cw->shm_buffers[1] = mapped_buffers[1];
    cw->content_width  = msg->width;
    cw->content_height = msg->height;
    cw->window_id      = wid;
    cw->front_buffer   = 0;
    cw->shm_generation = 0;
    cw->cmd_fd         = connection->cmd_fd;
    cw->evt_fd         = connection->evt_fd;
    cw->client_pid     = connection->client_pid;
    strncpy(cw->shm_names[0], mapped_names[0], WM_SHM_NAME_MAX - 1);
    cw->shm_names[0][WM_SHM_NAME_MAX - 1] = '\0';
    strncpy(cw->shm_names[1], mapped_names[1], WM_SHM_NAME_MAX - 1);
    cw->shm_names[1][WM_SHM_NAME_MAX - 1] = '\0';

    cw->window.paint_function     = client_window_paint_handler;
    cw->window.mousedown_function = client_window_mousedown_handler;
    cw->window.resize_function    = client_window_resize_handler;
    cw->window.close_function     = client_window_close_handler;

    if (msg->title[0])
        window_set_title((window_t *)cw, msg->title);

    if (client_register_window(mgr, cw) != 0) {
        client_destroy_shm_buffers(cw, cw->content_width, cw->content_height);
        if (cw->window.title)
            free(cw->window.title);
        if (cw->window.children)
            arr_free(cw->window.children);
        free(cw);
        return;
    }

    window_insert_child(parent, (window_t *)cw);
    window_paint((window_t *)cw, nullptr, 1);

    wm_event_window_created_t resp = {0};
    resp.type                      = WM_EVENT_WINDOW_CREATED;
    resp.window_id                 = wid;
    resp.width                     = msg->width;
    resp.height                    = msg->height;
    resp.front_buffer              = cw->front_buffer;
    strncpy(resp.shm_names[0], cw->shm_names[0], WM_SHM_NAME_MAX - 1);
    resp.shm_names[0][WM_SHM_NAME_MAX - 1] = '\0';
    strncpy(resp.shm_names[1], cw->shm_names[1], WM_SHM_NAME_MAX - 1);
    resp.shm_names[1][WM_SHM_NAME_MAX - 1] = '\0';
    write(connection->evt_fd, &resp, sizeof(resp));

    taskbar_add_button(cw->window.title, (window_t *)cw);
    window_raise((window_t *)cw, true);
}

void handle_destroy_window([[maybe_unused]] window_t *parent, const wm_msg_destroy_window_t *msg)
{
    ensure_client_manager_initialized();

    int window_idx = client_find_window_index_by_id(g_mgr, msg->window_id);
    if (window_idx < 0)
        return;

    client_window_t *cw = arr_get(g_mgr->windows, window_idx);
    if (!cw)
        return;

    client_destroy_window_recursive(g_mgr, cw);

    if (parent)
        window_paint(parent, nullptr, 1);
}

struct client_thread_args {
    int cmd_fd;
    int evt_fd;
    int client_pid;
};


static void *client_reader_thread(void *arg)
{
    ensure_client_manager_initialized();

    auto args      = (struct client_thread_args *)arg;
    int cmd_fd     = args->cmd_fd;
    int evt_fd     = args->evt_fd;
    int client_pid = args->client_pid;
    free(args);

    while (1) {
        uint8_t type_byte = 0;
        ssize_t n         = read(cmd_fd, &type_byte, 1);
        if (n <= 0)
            break;

        switch (type_byte) {
        case WM_MSG_CREATE_WINDOW:
            {
                wm_msg_create_window_t msg;
                msg.type = type_byte;
                n        = read(cmd_fd, &msg.width, sizeof(msg) - 1);
                if (n != (ssize_t)(sizeof(msg) - 1))
                    goto done;
                wm_state_lock();

                client_window_t *parent_window = find_client_by_window_id(msg.parent_id);
                window_t *window               = parent_window ? &parent_window->window : g_parent;

                handle_create_window(g_mgr, window, cmd_fd, &msg);
                wm_state_unlock();
                break;
            }
        case WM_MSG_WINDOW_INSERT_CHILD:
            {
                wm_msg_window_insert_child_t msg;
                msg.type = type_byte;
                n        = read(cmd_fd, &msg.parent_id, sizeof(msg) - 1);
                if (n != (ssize_t)(sizeof(msg) - 1))
                    goto done;
                wm_state_lock();
                handle_window_insert_child(&msg);
                wm_state_unlock();

                break;
            }
        case WM_MSG_INVALIDATE:
            {
                wm_msg_invalidate_t msg;
                msg.type = type_byte;
                n        = read(cmd_fd, &msg.window_id, sizeof(msg) - 1);
                if (n != (ssize_t)(sizeof(msg) - 1))
                    goto done;
                wm_state_lock();
                handle_invalidate(g_parent, &msg);
                wm_state_unlock();
                break;
            }
        case WM_MSG_DESTROY_WINDOW:
            {
                wm_msg_destroy_window_t msg;
                msg.type = type_byte;
                n        = read(cmd_fd, &msg.window_id, sizeof(msg) - 1);
                if (n != (ssize_t)(sizeof(msg) - 1))
                    goto done;
                wm_state_lock();
                handle_destroy_window(g_parent, &msg);
                wm_state_unlock();
                break;
            }
        default:
            goto done;
        }
    }

done:
    wm_state_lock();

    // Look up the connection name before destroying it.
    const char *app_name = nullptr;
    const client_connection_t *conn = client_find_connection_by_cmd_fd(g_mgr, cmd_fd);
    char name_buf[64] = {};
    if (conn && conn->name[0]) {
        snprintf(name_buf, sizeof(name_buf), "%s", conn->name);
        app_name = name_buf;
    }

    client_destroy_connection_windows(g_mgr, cmd_fd);
    client_unregister_connection_by_cmd_fd(g_mgr, cmd_fd);

    // Check if the client crashed (signaled termination).
    const crash_entry_t *entry = crash_log_find(client_pid);
    if (entry && WIFSIGNALED(entry->status) && g_parent) {
        printf("wm: client '%s' pid=%d killed by signal %d, showing crash dialog\n",
               app_name ? app_name : "?", client_pid, WTERMSIG(entry->status));
        crash_dialog_show(g_parent, app_name, WTERMSIG(entry->status), &entry->info);
    } else {
        printf("wm: client pid=%d disconnected (entry=%p status=%d)\n",
               client_pid, (void *)entry, entry ? entry->status : -999);
    }

    if (g_parent)
        window_paint(g_parent, nullptr, 1);
    wm_state_unlock();

    close(cmd_fd);
    close(evt_fd);
    (void)client_pid;
    return nullptr;
}

int client_launch(window_t *parent, const char *path, int16_t default_x, int16_t default_y)
{
    (void)default_x;
    (void)default_y;
    ensure_client_manager_initialized();

    g_parent = parent;

    int cmd_pipe[2];
    int evt_pipe[2];

    if (pipe(cmd_pipe) < 0)
        return -1;
    if (pipe(evt_pipe) < 0) {
        close(cmd_pipe[0]);
        close(cmd_pipe[1]);
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        close(cmd_pipe[0]);
        close(cmd_pipe[1]);
        close(evt_pipe[0]);
        close(evt_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(cmd_pipe[0]);
        close(evt_pipe[1]);

        dup2(evt_pipe[0], WM_EVT_FD);
        dup2(cmd_pipe[1], WM_CMD_FD);

        if (evt_pipe[0] != WM_EVT_FD)
            close(evt_pipe[0]);
        if (cmd_pipe[1] != WM_CMD_FD)
            close(cmd_pipe[1]);

        /* Close all inherited WM fds (device handles, other clients' pipes)
         * to prevent fd pollution and stale inode refs during mass cleanup.
         * Keep only stdin/stdout/stderr (0-2) and WM protocol fds (3-4). */
        for (int i = WM_CMD_FD + 1; i < 128; i++)
            close(i);

        exec(path); // NOSONAR: OS syscall, not shell exec
        exit(1);
    }

    close(cmd_pipe[1]);
    close(evt_pipe[0]);

    if (client_register_connection(g_mgr, cmd_pipe[0], evt_pipe[1], pid, path) != 0) {
        close(cmd_pipe[0]);
        close(evt_pipe[1]);
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return -1;
    }

    struct client_thread_args *args = malloc(sizeof(struct client_thread_args));
    if (!args) {
        client_unregister_connection_by_cmd_fd(g_mgr, cmd_pipe[0]);
        close(cmd_pipe[0]);
        close(evt_pipe[1]);
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return -1;
    }

    args->cmd_fd     = cmd_pipe[0];
    args->evt_fd     = evt_pipe[1];
    args->client_pid = pid;

    pthread_t thread;
    if (pthread_create(&thread, nullptr, client_reader_thread, args) != 0) {
        free(args);
        client_unregister_connection_by_cmd_fd(g_mgr, cmd_pipe[0]);
        close(cmd_pipe[0]);
        close(evt_pipe[1]);
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return -1;
    }
    pthread_detach(thread);

    return pid;
}

void client_manager_init(client_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));

    mgr->next_window_id = 1;
    mgr->initialized    = true;
    g_mgr               = mgr;
}

client_window_t *find_client_by_window_id(uint32_t window_id)
{
    ensure_client_manager_initialized();

    int idx = client_find_window_index_by_id(g_mgr, window_id);
    if (idx < 0)
        return nullptr;

    return g_mgr->windows[idx];
}

void client_dispatch_key_event(const window_t *parent, uint8_t keycode, uint8_t pressed)
{
    if (!g_mgr || !parent || !parent->active_child)
        return;

    ensure_client_manager_initialized();

    client_window_t *active_client = nullptr;
    for (size_t i = 0; i < arr_len(g_mgr->windows); i++) {
        client_window_t *cw = arr_get(g_mgr->windows, i);
        if (cw && (window_t *)cw == parent->active_child) {
            active_client = cw;
            break;
        }
    }

    if (!active_client || active_client->evt_fd < 0)
        return;

    wm_event_key_t ev = {0};
    ev.type           = WM_EVENT_KEY;
    ev.window_id      = active_client->window_id;
    ev.keycode        = keycode;
    ev.pressed        = pressed ? 1 : 0;

    const ssize_t n = write(active_client->evt_fd, &ev, sizeof(ev));
    (void)n;
}