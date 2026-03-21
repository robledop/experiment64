#include <stdio.h>
#include <wm/video_context.h>
#include <wm/desktop.h>
#include <wm/window.h>
#include <wm/bmp.h>
#include <wm/button.h>
#include <mouse.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/signal.h>
#include <attributes.h>
#include <sys/wait.h>
#include "wm_client.h"
#include "wm.h"
#include "taskbar.h"

#define TASKBAR_HEIGHT 40

static uint32_t *fb;
static desktop_t *desktop;
static bool wm_should_exit = false;
static int mousefd;
static int keyboardfd;
static video_context_t *context;
static client_manager_t client_mgr;

/** @brief Callback invoked by window_raise() to update the taskbar. */
static void wm_raise_notify(window_t *window)
{
    int idx = taskbar_find_window(window);
    taskbar_button_activate(idx);
}


#define CRASH_LOG_SIZE 16

typedef struct
{
    int pid;
    int status;
    crash_info_t info;
} crash_entry_t;

static crash_entry_t crash_log[CRASH_LOG_SIZE];
static volatile int crash_log_head;

USED static void sigaction_handler(int signum)
{
    if (signum == SIGCHLD) {
        int status;
        crash_info_t info;
        int pid;
        while ((pid = wait4(-1, &status, WNOHANG, &info)) > 0) {
            int idx = crash_log_head % CRASH_LOG_SIZE;
            crash_log[idx] = (crash_entry_t){.pid = pid, .status = status, .info = info};
            crash_log_head++;
        }
    }
}

const crash_entry_t *crash_log_find(int pid)
{
    int head = crash_log_head;
    for (int i = head - 1; i >= 0 && i >= head - CRASH_LOG_SIZE; i--) {
        const crash_entry_t *e = &crash_log[i % CRASH_LOG_SIZE];
        if (e->pid == pid)
            return e;
    }
    return nullptr;
}

static void wm_configure_sigchld(void)
{
    struct sigaction sa = {};
    sa.sa_handler       = sigaction_handler;
    sa.sa_flags         = 0;
    if (sigaction(SIGCHLD, &sa, nullptr) != 0)
        printf("wm: failed to configure SIGCHLD\n");
}

desktop_t* desktop_get()
{
    return desktop;
}

client_manager_t* client_mgr_get()
{
    return &client_mgr;
}


void spawn_calculator([[maybe_unused]] struct button *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    if (client_launch((window_t *)desktop, "/bin/calculator", 115, 60) < 0) {
        printf("wm: failed to launch calculator\n");
    }
}

void doom_button_handler([[maybe_unused]] struct button *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    if (client_launch((window_t *)desktop, "/bin/doom", 220, 60) < 0) {
        printf("wm: failed to launch doom\n");
    }
}

void demo_button_handler([[maybe_unused]] button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    client_launch((window_t *)desktop, "/bin/wmclient_demo", 50, 60);
}

void terminal_button_handler([[maybe_unused]] button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    if (client_launch((window_t *)desktop, "/bin/term", 140, 70) < 0)
        printf("wm: failed to launch terminal\n");
}

void exit_button_handler([[maybe_unused]] button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    wm_should_exit = true;
}

static void *wm_process_mouse_events([[maybe_unused]] void *arg)
{
    // ReSharper disable once CppDFALoopConditionNotUpdated
    while (!wm_should_exit) {
        struct ps2_mouse_packet mp;
        const ssize_t n = read(mousefd, &mp, sizeof(mp));
        if (n == (ssize_t)sizeof(mp)) {
            if (mp.x < 0)
                mp.x = 0;
            if (mp.y < 0)
                mp.y = 0;
            if (mp.x >= (int16_t)context->width)
                mp.x = (int16_t)(context->width - 1);
            if (mp.y >= (int16_t)context->height)
                mp.y = (int16_t)(context->height - 1);
            wm_state_lock();
            desktop_process_mouse(desktop, (uint16_t)mp.x, (uint16_t)mp.y, mp.flags);
            wm_state_unlock();
        } else {
            yield();
        }
    }

    pthread_exit(nullptr);
}

static void *wm_process_keyboard_events([[maybe_unused]] void *arg)
{
    bool extended = false;

    while (!wm_should_exit) {
        uint8_t scancode = 0;
        const ssize_t n  = read(keyboardfd, &scancode, sizeof(scancode));
        if (n != (ssize_t)sizeof(scancode)) {
            yield();
            continue;
        }

        if (scancode == 0x00)
            continue;
        if (scancode == 0xE0) {
            extended = true;
            continue;
        }

        const uint8_t code    = (uint8_t)(scancode & 0x7F);
        const uint8_t keycode = extended ? (uint8_t)(code | 0x80) : code;
        const uint8_t pressed = (scancode & 0x80) ? 0 : 1;
        extended              = false;

        wm_state_lock();
        client_dispatch_key_event((window_t *)desktop, keycode, pressed);
        wm_state_unlock();
    }

    pthread_exit(nullptr);
}

static void clear_screen()
{
    printf("\033[2J\033[H");
}

int main(void)
{
    atexit(clear_screen);
    wm_configure_sigchld();
    signal(SIGINT, SIG_IGN);

    int wm_pid = getpid();
    ioctl(STDIN_FILENO, TIOCSPGRP, &wm_pid);

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        printf("wm: unable to open /dev/fb0\n");
        exit(1);
    }

    uint32_t fb_width  = 0;
    uint32_t fb_height = 0;
    uint32_t fb_pitch  = 0;
    uint64_t fb_addr   = 0;

    if (ioctl(fd, FB_IOCTL_GET_WIDTH, &fb_width) != 0 ||
        ioctl(fd, FB_IOCTL_GET_HEIGHT, &fb_height) != 0 ||
        ioctl(fd, FB_IOCTL_GET_PITCH, &fb_pitch) != 0 ||
        ioctl(fd, FB_IOCTL_GET_FBADDR, &fb_addr) != 0) {
        printf("wm: failed to get framebuffer info\n");
        close(fd);
        exit(1);
    }

    size_t fb_map_size = (size_t)fb_pitch * (size_t)fb_height;

    void *map = mmap(NULL, fb_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        printf("wm: mmap failed\n");
        close(fd);
        exit(1);
    }
    fb = (uint32_t *)map;
    close(fd);

    mousefd = open("/dev/mouse", O_RDONLY);
    if (mousefd < 0) {
        printf("wm: cannot open /dev/mouse\n");
        exit(1);
    }

    keyboardfd = open("/dev/keyboard", O_RDONLY);
    if (keyboardfd < 0) {
        printf("wm: cannot open /dev/keyboard\n");
        exit(1);
    }

    client_manager_init(&client_mgr);

    uint32_t *pixels = nullptr;
    uint32_t wp_w    = 0, wp_h = 0;
    if (bitmap_load_argb("/var/wpaper.bmp", &pixels, &wp_w, &wp_h) != 0) {
        pixels = nullptr;
        wp_w   = wp_h = 0;
    }
    context = context_new(fb, (uint16_t)fb_width, (uint16_t)fb_height, fb_pitch);
    desktop = desktop_new(context, pixels, (uint16_t)wp_w, (uint16_t)wp_h);
    window_set_title((window_t *)desktop, "Desktop");

    button_t *exit_button    = button_new(10, 10, 100, 30);
    exit_button->onmousedown = exit_button_handler;
    window_set_title((window_t *)exit_button, "Exit");
    window_insert_child((window_t *)desktop, (window_t *)exit_button);

    button_t *launch_button    = button_new(115, 10, 100, 30);
    launch_button->onmousedown = spawn_calculator;
    window_set_title((window_t *)launch_button, "Calculator");
    window_insert_child((window_t *)desktop, (window_t *)launch_button);

    button_t *doom_button    = button_new(220, 10, 100, 30);
    doom_button->onmousedown = doom_button_handler;
    window_set_title((window_t *)doom_button, "Doom");
    window_insert_child((window_t *)desktop, (window_t *)doom_button);

    button_t *demo_button    = button_new(325, 10, 100, 30);
    demo_button->onmousedown = demo_button_handler;
    window_set_title((window_t *)demo_button, "Demo");
    window_insert_child((window_t *)desktop, (window_t *)demo_button);

    button_t *terminal_button    = button_new(430, 10, 100, 30);
    terminal_button->onmousedown = terminal_button_handler;
    window_set_title((window_t *)terminal_button, "Terminal");
    window_insert_child((window_t *)desktop, (window_t *)terminal_button);

    /* Register raise callback so the taskbar updates when windows are raised */
    window_set_raise_notify(wm_raise_notify);

    taskbar_init(0, (int16_t)(desktop->window.height - TASKBAR_HEIGHT), desktop->window.width, TASKBAR_HEIGHT);
    window_set_title(taskbar_get(), "Taskbar");

    window_insert_child((window_t *)desktop, taskbar_get());

    window_paint((window_t *)desktop, nullptr, 1);

    pthread_t mouse_thread;
    pthread_t keyboard_thread;
    pthread_create(&mouse_thread, nullptr, wm_process_mouse_events, nullptr);
    pthread_create(&keyboard_thread, nullptr, wm_process_keyboard_events, nullptr);

    pthread_join(mouse_thread, nullptr);
    pthread_join(keyboard_thread, nullptr);

    client_manager_cleanup(&client_mgr);

    int no_fg = 0;
    ioctl(STDIN_FILENO, TIOCSPGRP, &no_fg);
    ioctl(keyboardfd, KDFLUSH, nullptr);
    close(keyboardfd);
    close(mousefd);

    return 0;
}