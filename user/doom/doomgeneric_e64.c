#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>

static int FrameBufferFd = -1;
static int *FrameBuffer  = 0;

static int KeyboardFd          = -1;
static int KeyboardUsesConsole = 0;

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex  = 0;

typedef struct
{
    unsigned char key;
    int ticks;
} pending_key_t;

static pending_key_t s_PendingKeyUps[KEYQUEUE_SIZE];
static unsigned int s_PendingKeyUpCount = 0;

#define CONSOLE_HOLD_TICKS 6

static unsigned int s_PositionX = 0;
static unsigned int s_PositionY = 0;

static unsigned int s_ScreenWidth  = 0;
static unsigned int s_ScreenHeight = 0;
static unsigned int s_ScreenPitch  = 0; // Bytes per row (may differ from width*4 due to alignment)

static unsigned char convertConsoleKey(unsigned char scancode)
{
    unsigned char key = 0;

    switch (scancode) {
    case 226: // up arrow
        key = KEY_UPARROW;
        break;
    case 227: // down arrow
        key = KEY_DOWNARROW;
        break;
    case 228: // left arrow
        key = KEY_LEFTARROW;
        break;
    case 229: // right arrow
        key = KEY_RIGHTARROW;
        break;
    case '\n':
    case '\r':
        key = KEY_ENTER;
        break;
    case '\b':
        key = KEY_BACKSPACE;
        break;
    case ' ':
        key = KEY_USE;
        break;
    case 'w':
    case 'W':
        key = KEY_UPARROW;
        break;
    case 's':
    case 'S':
        key = KEY_DOWNARROW;
        break;
    case 'a':
    case 'A':
        key = KEY_LEFTARROW;
        break;
    case 'd':
    case 'D':
        key = KEY_RIGHTARROW;
        break;
    case 'f':
    case 'F':
        key = KEY_FIRE;
        break;
    case 'y':
    case 'Y':
        key = 'y';
        break;
    default:
        if (scancode >= 'A' && scancode <= 'Z') {
            key = (unsigned char)tolower(scancode);
        } else {
            key = scancode;
        }
        break;
    }

    return key;
}

static unsigned char convertScancode(unsigned char scancode)
{
    unsigned char key = 0;

    switch (scancode) {
    case 0x11: // W
        key = KEY_UPARROW;
        break;
    case 0x1F: // S
        key = KEY_DOWNARROW;
        break;
    case 0x1E: // A
        key = KEY_LEFTARROW;
        break;
    case 0x20: // D
        key = KEY_RIGHTARROW;
        break;
    case 0x9C: // keypad enter release?
    case 0x1C:
        key = KEY_ENTER;
        break;
    case 0x01:
        key = KEY_ESCAPE;
        break;
    case 0xCB:
    case 0x4B:
        key = KEY_LEFTARROW;
        break;
    case 0xCD:
    case 0x4D:
        key = KEY_RIGHTARROW;
        break;
    case 0xC8:
    case 0x48:
        key = KEY_UPARROW;
        break;
    case 0xD0:
    case 0x50:
        key = KEY_DOWNARROW;
        break;
    case 0x1D:
        key = KEY_FIRE;
        break;
    case 0x39:
        key = KEY_USE;
        break;
    case 0x2A:
    case 0x36:
        key = KEY_RSHIFT;
        break;
    case 0x15:
        key = 'y';
        break;
    default:
        break;
    }

    return key;
}

static void addKeyToQueueRaw(int pressed, unsigned char rawCode)
{
    unsigned char key = KeyboardUsesConsole
        ? convertConsoleKey(rawCode)
        : convertScancode(rawCode);

    if (key == 0) {
        return;
    }

    unsigned short keyData = (pressed << 8) | key;

    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex++;
    s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;

    if (KeyboardUsesConsole && pressed) {
        for (unsigned int i = 0; i < s_PendingKeyUpCount; ++i) {
            if (s_PendingKeyUps[i].key == key) {
                s_PendingKeyUps[i].ticks = CONSOLE_HOLD_TICKS;
                return;
            }
        }

        if (s_PendingKeyUpCount < KEYQUEUE_SIZE) {
            s_PendingKeyUps[s_PendingKeyUpCount].key   = key;
            s_PendingKeyUps[s_PendingKeyUpCount].ticks = CONSOLE_HOLD_TICKS;
            ++s_PendingKeyUpCount;
        }
    }
}

static void flushPendingKeyUps(void)
{
    if (!KeyboardUsesConsole || s_PendingKeyUpCount == 0)
        return;

    unsigned int writeIndex = 0;

    for (unsigned int i = 0; i < s_PendingKeyUpCount; ++i) {
        pending_key_t entry = s_PendingKeyUps[i];
        entry.ticks--;
        if (entry.ticks <= 0) {
            addKeyToQueueRaw(0, entry.key);
        } else {
            s_PendingKeyUps[writeIndex++] = entry;
        }
    }

    s_PendingKeyUpCount = writeIndex;
}


struct termios orig_termios;

void disableRawMode()
{
    if (!KeyboardUsesConsole) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    // Clear screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

void enableRawMode()
{
    if (!KeyboardUsesConsole) {
        return;
    }
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void DG_Init()
{
    FrameBufferFd = open("/dev/fb0", O_RDWR);

    if (FrameBufferFd >= 0) {
        printf("Getting screen width...");
        ioctl(FrameBufferFd, FB_IOCTL_GET_WIDTH, &s_ScreenWidth);
        printf("%d\n", s_ScreenWidth);

        printf("Getting screen height...");
        ioctl(FrameBufferFd, FB_IOCTL_GET_HEIGHT, &s_ScreenHeight);
        printf("%d\n", s_ScreenHeight);

        if (0 == s_ScreenWidth || 0 == s_ScreenHeight) {
            printf("Unable to obtain screen info!");
            exit();
        }

        uint32_t fb_addr  = 0;
        ioctl(FrameBufferFd, FB_IOCTL_GET_FBADDR, &fb_addr);
        ioctl(FrameBufferFd, FB_IOCTL_GET_PITCH, &s_ScreenPitch);
        printf("Screen pitch: %d bytes\n", s_ScreenPitch);

        size_t fb_map_size = (size_t)s_ScreenPitch * (size_t)s_ScreenHeight;

        FrameBuffer = mmap((void *)fb_addr,
                           fb_map_size,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_FIXED,
                           FrameBufferFd,
                           0);

        if (FrameBuffer != MAP_FAILED) {
            printf("FrameBuffer mmap success\n");
        } else {
            printf("FrameBuffermmap failed\n");
            FrameBuffer = NULL;
            close(FrameBufferFd);
            FrameBufferFd = -1;
            exit();
        }
    } else {
        printf("Opening FrameBuffer device failed!\n");
        exit();
    }

    KeyboardFd = open("/dev/keyboard", O_RDONLY);
    if (KeyboardFd < 0) {
        KeyboardFd          = STDIN_FILENO;
        KeyboardUsesConsole = 1;
    }

    ioctl(KeyboardFd, 1, (void *)1);

    int argPosX = 0;
    int argPosY = 0;

    argPosX = M_CheckParmWithArgs("-posx", 1);
    if (argPosX > 0) {
        sscanf(myargv[argPosX + 1], "%d", &s_PositionX);
    }

    argPosY = M_CheckParmWithArgs("-posy", 1);
    if (argPosY > 0) {
        sscanf(myargv[argPosY + 1], "%d", &s_PositionY);
    }

    // Leave console in cooked mode until we are ready to draw the first frame.
    // enableRawMode();
}

static void handleKeyInput()
{
    if (KeyboardFd < 0) {
        return;
    }

    unsigned char scancode  = 0;
    static int extendedScan = 0;
    static int escState     = 0;

    while (read(KeyboardFd, &scancode, 1) > 0) {
        if (scancode == 0) {
            continue;
        }

        if (KeyboardUsesConsole) {
            switch (escState) {
            case 0:
                if (scancode == 0x1b) {
                    escState = 1;
                    continue;
                }
                addKeyToQueueRaw(1, scancode);
                break;
            case 1:
                if (scancode == '[') {
                    escState = 2;
                } else {
                    addKeyToQueueRaw(1, 0x1b);
                    addKeyToQueueRaw(1, scancode);
                    escState = 0;
                }
                break;
            case 2:
                switch (scancode) {
                case 'A':
                    addKeyToQueueRaw(1, 226);
                    break;
                case 'B':
                    addKeyToQueueRaw(1, 227);
                    break;
                case 'C':
                    addKeyToQueueRaw(1, 229);
                    break;
                case 'D':
                    addKeyToQueueRaw(1, 228);
                    break;
                default:
                    break;
                }
                escState = 0;
                break;
            }
            continue;
        }

        if (scancode == 0xE0) {
            extendedScan = 1;
            continue;
        }

        unsigned char code = scancode & 0x7F;
        if (extendedScan) {
            code |= 0x80;
            extendedScan = 0;
        }

        int pressed = !(scancode & 0x80);
        addKeyToQueueRaw(pressed, code);
    }
}

void DG_DrawFrame()
{
    static int raw_enabled;
    if (!raw_enabled) {
        enableRawMode();
        raw_enabled = 1;
    }
    flushPendingKeyUps();

    if (FrameBuffer) {
        uint32_t *src  = (uint32_t *)DG_ScreenBuffer;
        uint32_t *dst  = (uint32_t *)FrameBuffer;
        const int srcW = DOOMGENERIC_RESX;
        const int srcH = DOOMGENERIC_RESY;
        const int dstW = s_ScreenWidth;
        const int dstH = s_ScreenHeight;
        const int dstPitchPixels = s_ScreenPitch / 4; // Convert bytes to pixels

        for (int y = 0; y < dstH; ++y) {
            int srcY         = (y * srcH) / dstH;
            uint32_t *srcRow = src + srcY * srcW;
            uint32_t *dstRow = dst + (y + s_PositionY) * dstPitchPixels + s_PositionX;

            for (int x = 0; x < dstW; ++x) {
                int srcX  = (x * srcW) / dstW;
                dstRow[x] = srcRow[srcX];
            }
        }
    }

    handleKeyInput();
}

void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs()
{
    struct timeval tp;
    struct timezone tzp;

    gettimeofday(&tp, &tzp);

    return (tp.tv_sec * 1000) + (tp.tv_usec / 1000); /* return milliseconds */
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
        //key queue is empty

        return 0;
    } else {
        unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
        s_KeyQueueReadIndex++;
        s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

        *pressed = keyData >> 8;
        *doomKey = keyData & 0xFF;

        return 1;
    }
}

void DG_SetWindowTitle([[maybe_unused]] const char *title)
{
}

int main(int argc, char **argv)
{
    const char *default_iwad = "/doom.wad";
    const char *candidate_iwads[] = {
        "/doom.wad",
        "/bin/doom.wad",
        "/mnt/doom.wad",
        "doom.wad",
    };
    int need_default_iwad = 1;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-iwad") == 0)
        {
            need_default_iwad = 0;
            break;
        }
    }

    const char *found_wad = nullptr;
    if (need_default_iwad)
    {
        for (size_t i = 0; i < sizeof(candidate_iwads) / sizeof(candidate_iwads[0]); i++)
        {
            int fd = open(candidate_iwads[i], O_RDONLY);
            if (fd >= 0)
            {
                close(fd);
                found_wad = candidate_iwads[i];
                break;
            }
        }
    }

    const char *argv_buf[64];
    int new_argc = argc;
    char **new_argv = argv;
    if (need_default_iwad && found_wad && argc + 2 < (int)(sizeof(argv_buf) / sizeof(argv_buf[0])))
    {
        for (int i = 0; i < argc; i++)
            argv_buf[i] = argv[i];
        argv_buf[new_argc++] = "-iwad";
        argv_buf[new_argc++] = found_wad;
        new_argv = (char **)argv_buf;
    }

    doomgeneric_Create(new_argc, new_argv);

    while (1) {
        doomgeneric_Tick();
    }


    return 0;
}
