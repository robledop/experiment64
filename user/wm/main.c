#include <stdio.h>
#include <wm/video_context.h>
#include <wm/desktop.h>
#include <wm/window.h>
#include <wm/bmp.h>
#include <wm/button.h>
#include <mouse.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <wm/calculator.h>

#include "pthread.h"

static uint32_t *fb;
static desktop_t *desktop;
static bool wm_should_exit = false;
static int mousefd;
static int keyboardfd;
static video_context_t *context;
calculator_t *calculator = {};

void spawn_calculator([[maybe_unused]] const struct button *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    if (calculator) {
        return;
    }

    calculator = calculator_new();
    window_insert_child((window_t *)desktop, (window_t *)calculator);
    window_move((window_t *)calculator, button->window.context->width / 2, button->window.context->height / 2);
}

void doom_button_handler([[maybe_unused]] const struct button *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    const int pid = fork();
    if (pid == 0) {
        // char *args[] = {(char *)"doom", nullptr};
        exec("/bin/doom");
        exit();
    }
    if (pid < 0) {
        printf("wm: failed to fork for doom\n");
    } else {
        wait(nullptr);
        window_paint((window_t *)desktop, nullptr, 1);
    }
}

void exit_button_handler([[maybe_unused]] const button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
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
            desktop_process_mouse(desktop, (uint16_t)mp.x, (uint16_t)mp.y, mp.flags);
        }
    }

    close(mousefd);
    pthread_exit(nullptr);
}

void wm_process_events(void)
{
    pthread_t thread;
    pthread_create(&thread, nullptr, wm_process_mouse_events, nullptr);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

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

    uint32_t *pixels = nullptr;
    uint32_t wp_w    = 0, wp_h = 0;
    if (bitmap_load_argb("/var/wpaper.bmp", &pixels, &wp_w, &wp_h) != 0) {
        pixels = nullptr;
        wp_w   = wp_h = 0;
    }
    context = context_new(fb, (uint16_t)fb_width, (uint16_t)fb_height, fb_pitch);
    desktop = desktop_new(context, pixels, (uint16_t)wp_w, (uint16_t)wp_h);

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

    window_paint((window_t *)desktop, nullptr, 1);

    wm_process_events();

    pthread_exit(nullptr);
}