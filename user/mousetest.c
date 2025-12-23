#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <mouse.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    int mousefd = open("/dev/mouse", O_RDONLY);
    if (mousefd < 0) {
        printf("mousetest: cannot open /dev/mouse\n");
        exit(1);
    }

    struct ps2_mouse_packet me;
    while (1) {
        int n = read(mousefd, &me, sizeof(me));
        if (n != (int)sizeof(me)) {
            printf("mousetest: read error (got %d bytes)\n", n);
            break;
        }
        if (me.flags & MOUSE_LEFT) {
            printf("Left button pressed at (%d, %d)\n", me.x, me.y);
        } else if (me.flags & MOUSE_RIGHT) {
            printf("Right button pressed at (%d, %d)\n", me.x, me.y);
        } else if (me.flags & MOUSE_MIDDLE) {
            printf("Middle button pressed at (%d, %d) - exiting\n", me.x, me.y);
            break;
        } else {
            printf("Mouse event: x=%d, y=%d, flags=0x%x\n", me.x, me.y, me.flags);
        }
    }

    close(mousefd);
    exit(0);
}

