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

static client_manager_t *g_mgr;
static window_t *g_parent;
static pthread_mutex_t g_wm_state_mutex = PTHREAD_MUTEX_INITIALIZER;

static void client_inner_dims_from_window_dims(uint16_t window_width, uint16_t window_height,
                                               uint16_t *inner_width, uint16_t *inner_height)
{
    if (!inner_width || !inner_height)
        return;

    if (window_width <= 2 * WIN_BORDER_WIDTH || window_height <= WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH) {
        *inner_width = 0;
        *inner_height = 0;
        return;
    }

    *inner_width = (uint16_t)(window_width - 2 * WIN_BORDER_WIDTH);
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

void client_manager_init(client_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->next_window_id = 1;
}

static client_window_t *find_client_by_window_id(client_manager_t *mgr, uint32_t window_id)
{
    for (int i = 0; i < mgr->count; i++)
    {
        if (mgr->clients[i] && mgr->clients[i]->window_id == window_id)
            return mgr->clients[i];
    }
    return nullptr;
}

void client_window_paint_handler(const window_t *window)
{
    const client_window_t *cw = (const client_window_t *)window;

    uint16_t inner_w = 0;
    uint16_t inner_h = 0;
    client_inner_dims_from_window_dims(window->width, window->height, &inner_w, &inner_h);

    if (!inner_w || !inner_h) {
        context_fill_rect(window->context, 0, 0, window->width, window->height, WIN_BGCOLOR);
        return;
    }

    context_fill_rect(window->context, 0, 0, inner_w, inner_h, WIN_BGCOLOR);

    if (cw->shm_buffer && cw->content_width && cw->content_height)
    {
        uint16_t draw_w = cw->content_width < inner_w ? cw->content_width : inner_w;
        uint16_t draw_h = cw->content_height < inner_h ? cw->content_height : inner_h;
        context_draw_bitmap(window->context, 0, 0, draw_w, draw_h, cw->shm_buffer);
    }
}

void client_window_mousedown_handler(const window_t *window, int16_t x, int16_t y)
{
    const client_window_t *cw = (const client_window_t *)window;
    if (cw->evt_fd < 0)
        return;

    wm_event_mouse_t ev = {0};
    ev.type = WM_EVENT_MOUSE;
    ev.window_id = cw->window_id;
    ev.x = x;
    ev.y = y;
    ev.buttons = MOUSE_LEFT;
    write(cw->evt_fd, &ev, sizeof(ev));
}

static int client_resize_shm_buffer(client_window_t *cw, uint16_t old_content_w, uint16_t old_content_h,
                                    uint16_t new_content_w, uint16_t new_content_h)
{
    if (!cw || !cw->shm_buffer || !old_content_w || !old_content_h || !new_content_w || !new_content_h)
        return -1;

    const uint32_t next_generation = cw->shm_generation + 1;
    char new_shm_name[WM_SHM_NAME_MAX] = {0};
    snprintf(new_shm_name, sizeof(new_shm_name), "wm_win_%u_%u", cw->window_id, next_generation);

    const size_t new_size = (size_t)new_content_w * (size_t)new_content_h * 4;
    const size_t old_size = (size_t)old_content_w * (size_t)old_content_h * 4;

    int shm_fd = shm_open(new_shm_name, O_CREATE, new_size);
    if (shm_fd < 0)
        return -1;

    void *new_map = mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (new_map == MAP_FAILED) {
        close(shm_fd);
        shm_unlink(new_shm_name);
        return -1;
    }
    close(shm_fd);

    memset(new_map, 0, new_size);

    const uint16_t copy_w = old_content_w < new_content_w ? old_content_w : new_content_w;
    const uint16_t copy_h = old_content_h < new_content_h ? old_content_h : new_content_h;

    uint32_t *dst = (uint32_t *)new_map;
    const uint32_t *src = cw->shm_buffer;
    for (uint16_t row = 0; row < copy_h; row++) {
        memcpy(dst + (size_t)row * new_content_w,
               src + (size_t)row * old_content_w,
               (size_t)copy_w * 4);
    }

    munmap(cw->shm_buffer, old_size);
    shm_unlink(cw->shm_name);

    cw->shm_buffer = (uint32_t *)new_map;
    cw->content_width = new_content_w;
    cw->content_height = new_content_h;
    cw->shm_generation = next_generation;
    strncpy(cw->shm_name, new_shm_name, WM_SHM_NAME_MAX - 1);
    cw->shm_name[WM_SHM_NAME_MAX - 1] = '\0';

    return 0;
}

static void client_window_resize_handler(const window_t *window, uint16_t old_width, uint16_t old_height)
{
    client_window_t *cw = (client_window_t *)window;
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

    if (client_resize_shm_buffer(cw, old_content_w, old_content_h, new_content_w, new_content_h) != 0)
        return;

    wm_event_window_resized_msg_t ev = {0};
    ev.type = WM_EVENT_WINDOW_RESIZED;
    ev.window_id = cw->window_id;
    ev.width = cw->content_width;
    ev.height = cw->content_height;
    strncpy(ev.shm_name, cw->shm_name, WM_SHM_NAME_MAX - 1);
    ev.shm_name[WM_SHM_NAME_MAX - 1] = '\0';

    write(cw->evt_fd, &ev, sizeof(ev));
}

void client_dispatch_key_event(const client_manager_t *mgr, const window_t *parent, uint8_t keycode, uint8_t pressed)
{
    if (!mgr || !parent || !parent->active_child)
        return;

    client_window_t *active_client = nullptr;
    for (int i = 0; i < mgr->count; i++)
    {
        client_window_t *cw = mgr->clients[i];
        if (cw && (window_t *)cw == parent->active_child)
        {
            active_client = cw;
            break;
        }
    }

    if (!active_client || active_client->evt_fd < 0)
        return;

    wm_event_key_t ev = {0};
    ev.type = WM_EVENT_KEY;
    ev.window_id = active_client->window_id;
    ev.keycode = keycode;
    ev.pressed = pressed ? 1 : 0;

    const ssize_t n = write(active_client->evt_fd, &ev, sizeof(ev));
    (void)n;
}

static void handle_create_window(client_manager_t *mgr, window_t *parent,
                                 int cmd_fd, int evt_fd, int client_pid,
                                 const wm_msg_create_window_t *msg)
{
    if (mgr->count >= WM_MAX_CLIENTS)
        return;

    uint32_t wid = mgr->next_window_id++;
    char shm_name[WM_SHM_NAME_MAX];
    snprintf(shm_name, sizeof(shm_name), "wm_win_%u", wid);

    size_t buf_size = (size_t)msg->width * (size_t)msg->height * 4;
    int shm_fd = shm_open(shm_name, O_CREATE, buf_size);
    if (shm_fd < 0)
        return;

    void *buf = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (buf == MAP_FAILED)
    {
        close(shm_fd);
        shm_unlink(shm_name);
        return;
    }
    close(shm_fd);

    uint16_t win_w = msg->width + 2 * WIN_BORDER_WIDTH;
    uint16_t win_h = msg->height + WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH;

    client_window_t *cw = calloc(1, sizeof(client_window_t));
    if (!cw)
    {
        munmap(buf, buf_size);
        shm_unlink(shm_name);
        return;
    }

    if (!window_init((window_t *)cw, msg->x, msg->y, win_w, win_h, 0, nullptr))
    {
        munmap(buf, buf_size);
        shm_unlink(shm_name);
        free(cw);
        return;
    }

    cw->shm_buffer = (uint32_t *)buf;
    cw->content_width = msg->width;
    cw->content_height = msg->height;
    cw->window_id = wid;
    cw->shm_generation = 0;
    cw->cmd_fd = cmd_fd;
    cw->evt_fd = evt_fd;
    cw->client_pid = client_pid;
    strncpy(cw->shm_name, shm_name, WM_SHM_NAME_MAX - 1);

    cw->window.paint_function = client_window_paint_handler;
    cw->window.mousedown_function = client_window_mousedown_handler;
    cw->window.resize_function = client_window_resize_handler;

    if (msg->title[0])
        window_set_title((window_t *)cw, msg->title);

    window_insert_child(parent, (window_t *)cw);
    window_paint((window_t *)cw, nullptr, 1);

    mgr->clients[mgr->count++] = cw;

    wm_event_window_created_t resp = {0};
    resp.type = WM_EVENT_WINDOW_CREATED;
    resp.window_id = wid;
    resp.width = msg->width;
    resp.height = msg->height;
    strncpy(resp.shm_name, shm_name, WM_SHM_NAME_MAX - 1);
    write(evt_fd, &resp, sizeof(resp));
}

static void handle_invalidate(client_manager_t *mgr,
                               [[maybe_unused]] window_t *parent,
                               const wm_msg_invalidate_t *msg)
{
    client_window_t *cw = find_client_by_window_id(mgr, msg->window_id);
    if (!cw)
        return;

    if (msg->x < 0 || msg->y < 0)
        return;

    if ((uint16_t)msg->x >= cw->content_width || (uint16_t)msg->y >= cw->content_height)
        return;

    uint16_t w = msg->width;
    uint16_t h = msg->height;

    if ((uint32_t)msg->x + (uint32_t)w > cw->content_width)
        w = (uint16_t)(cw->content_width - (uint16_t)msg->x);
    if ((uint32_t)msg->y + (uint32_t)h > cw->content_height)
        h = (uint16_t)(cw->content_height - (uint16_t)msg->y);

    if (!w || !h)
        return;

    int ox = WIN_BORDER_WIDTH + msg->x;
    int oy = WIN_TITLE_HEIGHT + msg->y;
    window_invalidate((window_t *)cw, oy, ox,
                      oy + h - 1,
                      ox + w - 1);
}

static void handle_destroy_window(client_manager_t *mgr,
                                   [[maybe_unused]] window_t *parent,
                                   const wm_msg_destroy_window_t *msg)
{
    for (int i = 0; i < mgr->count; i++)
    {
        if (mgr->clients[i] && mgr->clients[i]->window_id == msg->window_id)
        {
            client_window_t *cw = mgr->clients[i];
            mgr->clients[i] = mgr->clients[mgr->count - 1];
            mgr->clients[mgr->count - 1] = nullptr;
            mgr->count--;

            int idx = list_find(parent->children, cw);
            if (idx >= 0)
                list_remove_at(parent->children, (unsigned int)idx);
            if (parent->active_child == (window_t *)cw)
                parent->active_child = nullptr;

            if (cw->shm_buffer)
                munmap(cw->shm_buffer, (size_t)cw->content_width * (size_t)cw->content_height * 4);
            shm_unlink(cw->shm_name);
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
    struct client_thread_args *args = (struct client_thread_args *)arg;
    int cmd_fd = args->cmd_fd;
    int evt_fd = args->evt_fd;
    int client_pid = args->client_pid;
    free(args);

    while (1)
    {
        uint8_t type_byte = 0;
        ssize_t n = read(cmd_fd, &type_byte, 1);
        if (n <= 0)
            break;

        switch (type_byte)
        {
        case WM_MSG_CREATE_WINDOW:
        {
            wm_msg_create_window_t msg;
            msg.type = type_byte;
            n = read(cmd_fd, &msg.width, sizeof(msg) - 1);
            if (n != (ssize_t)(sizeof(msg) - 1))
                goto done;
            wm_state_lock();
            handle_create_window(g_mgr, g_parent, cmd_fd, evt_fd, client_pid, &msg);
            wm_state_unlock();
            break;
        }
        case WM_MSG_INVALIDATE:
        {
            wm_msg_invalidate_t msg;
            msg.type = type_byte;
            n = read(cmd_fd, &msg.window_id, sizeof(msg) - 1);
            if (n != (ssize_t)(sizeof(msg) - 1))
                goto done;
            wm_state_lock();
            handle_invalidate(g_mgr, g_parent, &msg);
            wm_state_unlock();
            break;
        }
        case WM_MSG_DESTROY_WINDOW:
        {
            wm_msg_destroy_window_t msg;
            msg.type = type_byte;
            n = read(cmd_fd, &msg.window_id, sizeof(msg) - 1);
            if (n != (ssize_t)(sizeof(msg) - 1))
                goto done;
            wm_state_lock();
            handle_destroy_window(g_mgr, g_parent, &msg);
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

int client_launch(client_manager_t *mgr, window_t *parent, const char *path,
                  int16_t default_x, int16_t default_y)
{
    (void)default_x;
    (void)default_y;

    g_mgr = mgr;
    g_parent = parent;

    int cmd_pipe[2];
    int evt_pipe[2];

    if (pipe(cmd_pipe) < 0)
        return -1;
    if (pipe(evt_pipe) < 0)
    {
        close(cmd_pipe[0]);
        close(cmd_pipe[1]);
        return -1;
    }

    int pid = fork();
    if (pid < 0)
    {
        close(cmd_pipe[0]);
        close(cmd_pipe[1]);
        close(evt_pipe[0]);
        close(evt_pipe[1]);
        return -1;
    }

    if (pid == 0)
    {
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
    if (!args)
    {
        close(cmd_pipe[0]);
        close(evt_pipe[1]);
        return -1;
    }

    args->cmd_fd = cmd_pipe[0];
    args->evt_fd = evt_pipe[1];
    args->client_pid = pid;

    pthread_t thread;
    pthread_create(&thread, nullptr, client_reader_thread, args);
    pthread_detach(thread);

    return pid;
}
