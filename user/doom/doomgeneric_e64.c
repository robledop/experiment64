#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <wm/wm_protocol.h>
#include <wm/wmclient.h>

#include <doomkeys.h>
#include <m_argv.h>
#include "doomgeneric.h"
#include "wm/window.h"

static int FrameBufferFd       = -1;
static int *FrameBuffer        = nullptr;
static wm_window_t *DoomWindow = nullptr;
static int WMMode              = 0;
static int WMWindowClosed      = 0;
static pthread_t WMEventThread;
static int WMEventThreadRunning        = 0;
static pthread_mutex_t s_FrameMutex    = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_KeyQueueMutex = PTHREAD_MUTEX_INITIALIZER;

static int KeyboardFd          = -1;
static int KeyboardUsesConsole = 0;

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex  = 0;

typedef struct {
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

static int *s_WmWindowFrameBuffer        = nullptr;
static unsigned int s_WmWindowWidth      = 0;
static unsigned int s_WmWindowHeight     = 0;
static unsigned int s_WmWindowPitch      = 0;
static int *s_WmFbFrameBuffer            = nullptr;
static unsigned int s_WmFbWidth          = 0;
static unsigned int s_WmFbHeight         = 0;
static unsigned int s_WmFbPitch          = 0;
static size_t s_WmFbMapSize              = 0;
static int s_WmRenderToFramebuffer       = 0;
static int s_WmAltPressed                = 0;
static int s_WmAltEnterToggleKeyConsumed = 0;

static int runningInWm(void)
{
    struct stat evt_stat = {0};
    struct stat cmd_stat = {0};
    return fstat(WM_EVT_FD, &evt_stat) == 0 && fstat(WM_CMD_FD, &cmd_stat) == 0;
}

int print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int rc = 0;
    if (!runningInWm()) {
        rc = vprintf(format, args);
    } else {
        // char buffer[256];
        // rc = vsnprintf(buffer, sizeof(buffer), format, args);
        // wm_debug_print(buffer);
    }
    va_end(args);
    return rc;
}

int dg_putchar(int c)
{
    if (runningInWm())
        return c;

    unsigned char ch = (unsigned char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1)
        return EOF;

    return (int)ch;
}

int dg_puts(const char *s)
{
    if (runningInWm())
        return 0;

    size_t len = strlen(s);
    if ((len > 0 && write(STDOUT_FILENO, s, len) != (ssize_t)len) || write(STDOUT_FILENO, "\n", 1) != 1)
        return EOF;

    return 0;
}

static void suppressStdIoForWm(void)
{
    if (!runningInWm())
        return;

    int devNullFd = open("/dev/null", O_WRONLY);
    if (devNullFd < 0)
        return;

    (void)dup2(devNullFd, STDOUT_FILENO);
    (void)dup2(devNullFd, STDERR_FILENO);

    if (devNullFd > STDERR_FILENO)
        close(devNullFd);
}

static int isAltScancode(uint8_t keycode)
{
    return keycode == 0x38 || keycode == 0xB8;
}

static int isEnterScancode(uint8_t keycode)
{
    return keycode == 0x1C || keycode == 0x9C;
}

static void useWmWindowRenderTargetLocked(void)
{
    FrameBuffer             = s_WmWindowFrameBuffer;
    s_ScreenWidth           = s_WmWindowWidth;
    s_ScreenHeight          = s_WmWindowHeight;
    s_ScreenPitch           = s_WmWindowPitch;
    s_WmRenderToFramebuffer = 0;
}

static void useWmFramebufferRenderTargetLocked(void)
{
    FrameBuffer             = s_WmFbFrameBuffer;
    s_ScreenWidth           = s_WmFbWidth;
    s_ScreenHeight          = s_WmFbHeight;
    s_ScreenPitch           = s_WmFbPitch;
    s_WmRenderToFramebuffer = 1;
}

static int mapWmFramebufferLocked(void)
{
    if (s_WmFbFrameBuffer)
        return 0;

    if (FrameBufferFd < 0) {
        FrameBufferFd = open("/dev/fb0", O_RDWR);
        if (FrameBufferFd < 0)
            return -1;
    }

    if (ioctl(FrameBufferFd, FB_IOCTL_GET_WIDTH, &s_WmFbWidth) != 0 ||
        ioctl(FrameBufferFd, FB_IOCTL_GET_HEIGHT, &s_WmFbHeight) != 0 ||
        ioctl(FrameBufferFd, FB_IOCTL_GET_PITCH, &s_WmFbPitch) != 0 || s_WmFbWidth == 0 || s_WmFbHeight == 0 ||
        s_WmFbPitch == 0) {
        close(FrameBufferFd);
        FrameBufferFd = -1;
        s_WmFbWidth   = 0;
        s_WmFbHeight  = 0;
        s_WmFbPitch   = 0;
        return -1;
    }

    s_WmFbMapSize = (size_t)s_WmFbPitch * (size_t)s_WmFbHeight;
    void *map     = mmap(NULL, s_WmFbMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, FrameBufferFd, 0);
    if (map == MAP_FAILED) {
        close(FrameBufferFd);
        FrameBufferFd     = -1;
        s_WmFbMapSize     = 0;
        s_WmFbWidth       = 0;
        s_WmFbHeight      = 0;
        s_WmFbPitch       = 0;
        s_WmFbFrameBuffer = nullptr;
        return -1;
    }

    s_WmFbFrameBuffer = (int *)map;
    return 0;
}

static void toggleWmRenderTarget(void)
{
    pthread_mutex_lock(&s_FrameMutex);

    if (!s_WmRenderToFramebuffer) {
        if (mapWmFramebufferLocked() == 0)
            useWmFramebufferRenderTargetLocked();
    } else {
        useWmWindowRenderTargetLocked();
    }

    pthread_mutex_unlock(&s_FrameMutex);
}

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

static void addKeyToQueueRawLocked(int pressed, unsigned char rawCode)
{
    unsigned char key = KeyboardUsesConsole ? convertConsoleKey(rawCode) : convertScancode(rawCode);

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

static void addKeyToQueueRaw(int pressed, unsigned char rawCode)
{
    pthread_mutex_lock(&s_KeyQueueMutex);
    addKeyToQueueRawLocked(pressed, rawCode);
    pthread_mutex_unlock(&s_KeyQueueMutex);
}

static void flushPendingKeyUps(void)
{
    if (!KeyboardUsesConsole)
        return;

    pthread_mutex_lock(&s_KeyQueueMutex);

    if (s_PendingKeyUpCount == 0) {
        pthread_mutex_unlock(&s_KeyQueueMutex);
        return;
    }

    unsigned int writeIndex = 0;

    for (unsigned int i = 0; i < s_PendingKeyUpCount; ++i) {
        pending_key_t entry = s_PendingKeyUps[i];
        entry.ticks--;
        if (entry.ticks <= 0) {
            addKeyToQueueRawLocked(0, entry.key);
        } else {
            s_PendingKeyUps[writeIndex++] = entry;
        }
    }

    s_PendingKeyUpCount = writeIndex;
    pthread_mutex_unlock(&s_KeyQueueMutex);
}

struct termios orig_termios;

void disableRawMode()
{
    if (WMMode || DoomWindow) {
        WMMode = 0;

        if (WMEventThreadRunning) {
            close(WM_EVT_FD);
            pthread_join(WMEventThread, nullptr);
            WMEventThreadRunning = 0;
        }

        if (DoomWindow) {
            wm_destroy_window(DoomWindow);
            DoomWindow = nullptr;
        }

        if (s_WmFbFrameBuffer && s_WmFbMapSize) {
            munmap(s_WmFbFrameBuffer, s_WmFbMapSize);
            s_WmFbFrameBuffer = nullptr;
            s_WmFbMapSize     = 0;
        }

        if (FrameBufferFd >= 0) {
            close(FrameBufferFd);
            FrameBufferFd = -1;
        }

        FrameBuffer                   = nullptr;
        s_WmWindowFrameBuffer         = nullptr;
        s_WmWindowWidth               = 0;
        s_WmWindowHeight              = 0;
        s_WmWindowPitch               = 0;
        s_WmFbWidth                   = 0;
        s_WmFbHeight                  = 0;
        s_WmFbPitch                   = 0;
        s_WmRenderToFramebuffer       = 0;
        s_WmAltPressed                = 0;
        s_WmAltEnterToggleKeyConsumed = 0;
        return;
    }

    if (!KeyboardUsesConsole) {
        if (KeyboardFd >= 0) {
            ioctl(KeyboardFd, KDFLUSH, NULL);
        }
        return;
    }

    int kb = open("/dev/keyboard", O_RDONLY);
    if (kb >= 0) {
        ioctl(kb, KDFLUSH, NULL);
        close(kb);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);

    unsigned char discard;
    while (read(STDIN_FILENO, &discard, 1) > 0)
        ;

    write(STDOUT_FILENO, "\x1b[2J", 4);
}

void enableRawMode()
{
    atexit(disableRawMode);

    if (!KeyboardUsesConsole) {
        return;
    }
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


static void *wmEventThreadMain([[maybe_unused]] void *arg)
{
    uint8_t event_type = 0;
    uint8_t event_buf[sizeof(wm_event_window_resized_msg_t)];

    while (WMMode && !WMWindowClosed) {
        if (wm_next_event(event_buf, &event_type) != 0)
            break;

        switch (event_type) {
        case WM_EVENT_KEY:
            {
                auto ev = (wm_event_key_t *)event_buf;
                if (!DoomWindow || ev->window_id != DoomWindow->window_id)
                    break;

                if (isAltScancode(ev->keycode)) {
                    s_WmAltPressed = ev->pressed ? 1 : 0;
                    break;
                }

                if (isEnterScancode(ev->keycode)) {
                    if (s_WmAltEnterToggleKeyConsumed) {
                        if (!ev->pressed)
                            s_WmAltEnterToggleKeyConsumed = 0;
                        break;
                    }

                    if (s_WmAltPressed && ev->pressed) {
                        toggleWmRenderTarget();
                        s_WmAltEnterToggleKeyConsumed = 1;
                        break;
                    }

                    if (s_WmAltPressed && !ev->pressed) {
                        s_WmAltEnterToggleKeyConsumed = 0;
                        break;
                    }
                }

                addKeyToQueueRaw(ev->pressed ? 1 : 0, ev->keycode);
                break;
            }
        case WM_EVENT_WINDOW_RESIZED:
            {
                auto ev = (wm_event_window_resized_t *)event_buf;
                if (!DoomWindow || ev->window_id != DoomWindow->window_id)
                    break;

                pthread_mutex_lock(&s_FrameMutex);
                s_WmWindowFrameBuffer = (int *)DoomWindow->buffer;
                s_WmWindowWidth       = DoomWindow->width;
                s_WmWindowHeight      = DoomWindow->height;
                s_WmWindowPitch       = (uint32_t)DoomWindow->width * 4U;
                if (!s_WmRenderToFramebuffer)
                    useWmWindowRenderTargetLocked();
                pthread_mutex_unlock(&s_FrameMutex);
                break;
            }
        case WM_EVENT_WINDOW_CLOSED:
            {
                auto ev = (wm_event_window_closed_t *)event_buf;
                if (DoomWindow && ev->window_id == DoomWindow->window_id)
                    WMWindowClosed = 1;
                goto done;
            }
        default:
            break;
        }
    }

done:
    WMWindowClosed = 1;
    return nullptr;
}

void DG_Init()
{
    int argPosX = M_CheckParmWithArgs("-posx", 1);
    if (argPosX > 0) {
        sscanf(myargv[argPosX + 1], "%d", &s_PositionX);
    }

    int argPosY = M_CheckParmWithArgs("-posy", 1);
    if (argPosY > 0) {
        sscanf(myargv[argPosY + 1], "%d", &s_PositionY);
    }

    if (runningInWm()) {
        int16_t wm_pos_x = 80;
        int16_t wm_pos_y = 60;
        if (argPosX > 0)
            wm_pos_x = (int16_t)s_PositionX;
        if (argPosY > 0)
            wm_pos_y = (int16_t)s_PositionY;

        DoomWindow = wm_create_window(
            wm_pos_x, wm_pos_y, DOOMGENERIC_RESX, DOOMGENERIC_RESY, WIN_CLOSEABLE | WIN_MINIMIZABLE, 0, "Doom");
        if (!DoomWindow) {
            print("doom: failed to create window\n");
            exit();
        }

        WMMode                        = 1;
        WMWindowClosed                = 0;
        s_WmRenderToFramebuffer       = 0;
        s_WmAltPressed                = 0;
        s_WmAltEnterToggleKeyConsumed = 0;
        s_WmWindowFrameBuffer         = (int *)DoomWindow->buffer;
        s_WmWindowWidth               = DoomWindow->width;
        s_WmWindowHeight              = DoomWindow->height;
        s_WmWindowPitch               = (uint32_t)DoomWindow->width * 4U;
        useWmWindowRenderTargetLocked();
        atexit(disableRawMode);

        if (pthread_create(&WMEventThread, nullptr, wmEventThreadMain, nullptr) != 0) {
            print("doom: failed to create WM event thread\n");
            wm_destroy_window(DoomWindow);
            DoomWindow  = nullptr;
            FrameBuffer = nullptr;
            exit();
        }
        WMEventThreadRunning = 1;
        return;
    }

    FrameBufferFd = open("/dev/fb0", O_RDWR);

    if (FrameBufferFd >= 0) {
        print("Getting screen width...");
        ioctl(FrameBufferFd, FB_IOCTL_GET_WIDTH, &s_ScreenWidth);
        print("%d\n", s_ScreenWidth);

        print("Getting screen height...");
        ioctl(FrameBufferFd, FB_IOCTL_GET_HEIGHT, &s_ScreenHeight);
        print("%d\n", s_ScreenHeight);

        if (0 == s_ScreenWidth || 0 == s_ScreenHeight) {
            print("Unable to obtain screen info!");
            exit();
        }

        ioctl(FrameBufferFd, FB_IOCTL_GET_PITCH, &s_ScreenPitch);
        print("Screen pitch: %d bytes\n", s_ScreenPitch);

        size_t fb_map_size = (size_t)s_ScreenPitch * (size_t)s_ScreenHeight;

        FrameBuffer = mmap(NULL, fb_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, FrameBufferFd, 0);

        if (FrameBuffer != MAP_FAILED) {
            print("FrameBuffer mmap success\n");
        } else {
            print("FrameBuffermmap failed\n");
            FrameBuffer = NULL;
            close(FrameBufferFd);
            FrameBufferFd = -1;
            exit();
        }
    } else {
        print("Opening FrameBuffer device failed!\n");
        exit();
    }

    KeyboardFd = open("/dev/keyboard", O_RDONLY);
    if (KeyboardFd < 0) {
        KeyboardFd          = STDIN_FILENO;
        KeyboardUsesConsole = 1;
    }

    ioctl(KeyboardFd, 1, (void *)1);

    // Leave console in cooked mode until we are ready to draw the first frame.
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
    if (!WMMode && !raw_enabled) {
        enableRawMode();
        raw_enabled = 1;
    }

    if (WMMode && WMWindowClosed) {
        exit();
    }

    if (KeyboardUsesConsole) {
        flushPendingKeyUps();
    }

    pthread_mutex_lock(&s_FrameMutex);

    if (FrameBuffer && s_ScreenWidth && s_ScreenHeight) {
        auto src                 = (uint32_t *)DG_ScreenBuffer;
        auto dst                 = (uint32_t *)FrameBuffer;
        const int dstW           = (int)s_ScreenWidth;
        const int dstH           = (int)s_ScreenHeight;
        const int dstPitchPixels = (int)(s_ScreenPitch / 4U);
        const int drawOffsetX    = WMMode ? 0 : (int)s_PositionX;
        const int drawOffsetY    = WMMode ? 0 : (int)s_PositionY;

        for (int y = 0; y < dstH; ++y) {
            constexpr int srcH = DOOMGENERIC_RESY;
            constexpr int srcW = DOOMGENERIC_RESX;
            int srcY           = (y * srcH) / dstH;
            uint32_t *srcRow   = src + srcY * srcW;
            uint32_t *dstRow   = dst + (y + drawOffsetY) * dstPitchPixels + drawOffsetX;

            for (int x = 0; x < dstW; ++x) {
                int srcX  = (x * srcW) / dstW;
                dstRow[x] = srcRow[srcX];
            }
        }
    }

    pthread_mutex_unlock(&s_FrameMutex);

    if (WMMode && DoomWindow && !s_WmRenderToFramebuffer) {
        wm_invalidate_all(DoomWindow);

        pthread_mutex_lock(&s_FrameMutex);
        s_WmWindowFrameBuffer = (int *)DoomWindow->buffer;
        s_WmWindowWidth       = DoomWindow->width;
        s_WmWindowHeight      = DoomWindow->height;
        s_WmWindowPitch       = (uint32_t)DoomWindow->width * 4U;
        if (!s_WmRenderToFramebuffer)
            useWmWindowRenderTargetLocked();
        pthread_mutex_unlock(&s_FrameMutex);
    } else if (!WMMode) {
        handleKeyInput();
    }
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
    pthread_mutex_lock(&s_KeyQueueMutex);

    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
        pthread_mutex_unlock(&s_KeyQueueMutex);

        return 0;
    } else {
        unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
        s_KeyQueueReadIndex++;
        s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

        *pressed = keyData >> 8;
        *doomKey = keyData & 0xFF;

        pthread_mutex_unlock(&s_KeyQueueMutex);
        return 1;
    }
}

void DG_SetWindowTitle([[maybe_unused]] const char *title)
{
}

void clear_screen(void)
{
    if (FrameBufferFd < 0)
        return;

    print("\033[2J\033[H");
}

int main(int argc, char **argv)
{
    suppressStdIoForWm();

    atexit(clear_screen);

    int need_default_iwad = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-iwad") == 0) {
            need_default_iwad = 0;
            break;
        }
    }

    const char *found_wad = nullptr;
    if (need_default_iwad) {
        const char *candidate_iwads[] = {
            "/doom.wad",
            "/bin/doom.wad",
            "/mnt/doom.wad",
            "doom.wad",
        };
        for (size_t i = 0; i < sizeof(candidate_iwads) / sizeof(candidate_iwads[0]); i++) {
            int fd = open(candidate_iwads[i], O_RDONLY);
            if (fd >= 0) {
                close(fd);
                found_wad = candidate_iwads[i];
                break;
            }
        }
    }

    const char *argv_buf[64];
    int new_argc    = argc;
    char **new_argv = argv;
    if (need_default_iwad && found_wad && argc + 2 < (int)(sizeof(argv_buf) / sizeof(argv_buf[0]))) {
        for (int i = 0; i < argc; i++)
            argv_buf[i] = argv[i];
        argv_buf[new_argc++] = "-iwad";
        argv_buf[new_argc++] = found_wad;
        new_argv             = (char **)argv_buf;
    }

    doomgeneric_Create(new_argc, new_argv);

    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}