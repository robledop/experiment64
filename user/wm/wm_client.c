#include "wm_client.h"
#include <wm/window.h>
#include <wm/video_context.h>
#include <wm/wm_protocol.h>
#include <mouse.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "wm/button.h"

client_manager_t *g_mgr;
static window_t *g_parent;
static pthread_mutex_t g_wm_state_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ensure_client_manager_initialized(void)
{
    if (!g_mgr || !g_mgr->initialized) {
        panic("Client manager not initialized");
    }
}

static void client_inner_dims_from_window_dims(uint16_t window_width, uint16_t window_height,
                                               uint16_t *inner_width, uint16_t *inner_height)
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

void client_window_mousedown_handler(const window_t *window, int16_t x, int16_t y)
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

static int client_create_shm_buffers(uint32_t window_id,
                                     uint32_t generation,
                                     uint16_t width,
                                     uint16_t height,
                                     uint32_t *out_buffers[2],
                                     char out_names[2][WM_SHM_NAME_MAX])
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

    if (client_create_shm_buffers(cw->window_id,
                                  next_generation,
                                  new_content_w,
                                  new_content_h,
                                  new_buffers,
                                  new_names) != 0) {
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
    window_invalidate((window_t *)cw,
                      oy,
                      ox,
                      oy + h - 1,
                      ox + w - 1);
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

static void handle_invalidate([[maybe_unused]] window_t *parent,
                              const wm_msg_invalidate_t *msg)
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

void test_button_mousedown(const window_t *button_window, int16_t x, int16_t y)
{
    static int count = 0;
    count++;
    char text[50] = {0};
    snprintf(text, sizeof(text), "clicked %d", count);
    window_set_title((window_t *)button_window, text);
}


void test_button_paint(const window_t *button_window)
{
    button_t *button = (button_t *)button_window;

    uint32_t border_color;
    if (button->color_toggle) {
        border_color = WIN_TITLE_COLOR;
    } else {
        border_color = WIN_BGCOLOR - 0x101010;
    }

    context_fill_rect(button_window->context, 1, 1, button_window->width - 1, button_window->height - 1, WIN_BGCOLOR);
    context_draw_rect(button_window->context, 0, 0, button_window->width, button_window->height, 0xFF000000);
    context_draw_rect(button_window->context, 3, 3, button_window->width - 6, button_window->height - 6, border_color);
    context_draw_rect(button_window->context, 4, 4, button_window->width - 8, button_window->height - 8, border_color);

    int title_len = button_window->title ? (int)strlen(button_window->title) : 0;

    title_len *= VESA_CHAR_WIDTH;

    if (button_window->title) {
        context_draw_text(button_window->context,
                          button_window->title,
                          (button_window->width / 2) - (title_len / 2),
                          (button_window->height / 2) - 6,
                          WIN_TEXT_COLOR);
    }
}

static void handle_window_insert_child(const wm_msg_window_insert_child_t *msg)
{
    auto child = find_client_by_window_id(msg->child_id);
    if (!child)
        return;
    auto parent = find_client_by_window_id(msg->parent_id);
    if (!parent)
        return;

    child->window.paint_function     = test_button_paint;
    child->window.mousedown_function = test_button_mousedown;
    window_set_title(&child->window, "Button");
    window_insert_child(&parent->window, &child->window);
    window_paint(&child->window, nullptr, 1);
}

static void handle_create_window(client_manager_t *mgr, window_t *parent,
                                 int cmd_fd, int evt_fd, int client_pid,
                                 const wm_msg_create_window_t *msg)
{
    if (mgr->count >= WM_MAX_CLIENTS)
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
    cw->cmd_fd         = cmd_fd;
    cw->evt_fd         = evt_fd;
    cw->client_pid     = client_pid;
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

    window_insert_child(parent, (window_t *)cw);
    window_paint((window_t *)cw, nullptr, 1);

    mgr->clients[mgr->count++] = cw;

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
    write(evt_fd, &resp, sizeof(resp));
}

void handle_destroy_window([[maybe_unused]] window_t *parent,
                           const wm_msg_destroy_window_t *msg)
{
    ensure_client_manager_initialized();
    for (int i = 0; i < g_mgr->count; i++) {
        if (g_mgr->clients[i] && g_mgr->clients[i]->window_id == msg->window_id) {
            client_window_t *cw = g_mgr->clients[i];
            // kill(cw->client_pid, SIGTERM);
            // waitpid(cw->client_pid, nullptr, WNOHANG);
            g_mgr->clients[i]                = g_mgr->clients[g_mgr->count - 1];
            g_mgr->clients[g_mgr->count - 1] = nullptr;
            g_mgr->count--;

            int idx = list_find(parent->children, cw);
            if (idx >= 0)
                list_remove_at(parent->children, (unsigned int)idx);
            if (parent->active_child == (window_t *)cw)
                parent->active_child = nullptr;

            client_destroy_shm_buffers(cw, cw->content_width, cw->content_height);
            free(cw);

            window_paint(parent, nullptr, 1);
            break;
        }
    }
}

struct client_thread_args
{
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
        case WM_MSG_CREATE_WINDOW: {
            wm_msg_create_window_t msg;
            msg.type = type_byte;
            n        = read(cmd_fd, &msg.width, sizeof(msg) - 1);
            if (n != (ssize_t)(sizeof(msg) - 1))
                goto done;
            wm_state_lock();
            handle_create_window(g_mgr, g_parent, cmd_fd, evt_fd, client_pid, &msg);
            wm_state_unlock();
            break;
        }
        case WM_MSG_WINDOW_INSERT_CHILD: {
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
        case WM_MSG_INVALIDATE: {
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
        case WM_MSG_DESTROY_WINDOW: {
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
    close(cmd_fd);
    close(evt_fd);
    return nullptr;
}

int client_launch(window_t *parent, const char *path,
                  int16_t default_x, int16_t default_y)
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

        exec(path);
        exit(1);
    }

    close(cmd_pipe[1]);
    close(evt_pipe[0]);

    struct client_thread_args *args = malloc(sizeof(struct client_thread_args));
    if (!args) {
        close(cmd_pipe[0]);
        close(evt_pipe[1]);
        return -1;
    }

    args->cmd_fd     = cmd_pipe[0];
    args->evt_fd     = evt_pipe[1];
    args->client_pid = pid;

    pthread_t thread;
    pthread_create(&thread, nullptr, client_reader_thread, args);
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

    for (int i = 0; i < g_mgr->count; i++) {
        if (g_mgr->clients[i] && g_mgr->clients[i]->window_id == window_id)
            return g_mgr->clients[i];
    }
    return nullptr;
}

void client_dispatch_key_event(const window_t *parent, uint8_t keycode, uint8_t pressed)
{
    if (!g_mgr || !parent || !parent->active_child)
        return;

    ensure_client_manager_initialized();

    client_window_t *active_client = nullptr;
    for (int i = 0; i < g_mgr->count; i++) {
        client_window_t *cw = g_mgr->clients[i];
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