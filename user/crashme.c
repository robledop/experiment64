#include <wm/wmclient.h>
#include <unistd.h>

int main(void)
{
    wm_window_t *win = wm_create_window(100, 100, 200, 100, 0, 0, "crashme");
    if (!win)
        return 1;

    // Draw a red rectangle so we know it's alive
    uint32_t *buf = win->buffers[win->back_buffer];
    for (int i = 0; i < 200 * 100; i++)
        buf[i] = 0xFFFF0000;
    wm_invalidate_region(win, 0, 0, 200, 100);

    // Wait so the window is visible, then crash
    usleep(500000);

    // Trigger a segfault
    volatile int *p = (volatile int *)0;
    *p = 42;

    return 0;
}
