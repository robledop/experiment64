#include <wm/desktop.h>
#include <wm/video_context.h>
#include <wm/window.h>
#include <stdlib.h>
#include <string.h>

#define CA 0xFF000000
#define CB 0xFFFFFFFF
#define CD 0x00000000

static unsigned int mouse_img[MOUSE_BUFSZ] = {
    CA, CD, CD, CD, CD, CD, CD, CD, CD, CD, CD, CA, CA, CD, CD, CD, CD, CD, CD, CD, CD, CD, CA, CB, CA, CD, CD, CD, CD,
    CD, CD, CD, CD, CA, CB, CB, CA, CD, CD, CD, CD, CD, CD, CD, CA, CB, CB, CB, CA, CD, CD, CD, CD, CD, CD, CA, CB, CB,
    CB, CB, CA, CD, CD, CD, CD, CD, CA, CB, CB, CB, CB, CB, CA, CD, CD, CD, CD, CA, CB, CB, CB, CB, CB, CB, CA, CD, CD,
    CD, CA, CB, CB, CB, CB, CB, CB, CB, CA, CD, CD, CA, CB, CB, CB, CB, CB, CB, CB, CB, CA, CD, CA, CB, CB, CB, CB, CB,
    CB, CB, CB, CB, CA, CA, CA, CA, CA, CB, CB, CB, CA, CA, CA, CA, CD, CD, CD, CD, CA, CB, CB, CA, CD, CD, CD, CD, CD,
    CD, CD, CA, CB, CB, CA, CD, CD, CD, CD, CD, CD, CD, CD, CA, CB, CB, CA, CD, CD, CD, CD, CD, CD, CD, CA, CB, CB, CA,
    CD, CD, CD, CD, CD, CD, CD, CD, CA, CB, CA, CD, CD, CD, CD, CD, CD, CD, CD, CD, CA, CA, CD, CD
};

static void framebuffer_putpixel(const video_context_t *context, int x, int y, uint32_t rgb)
{
    if (x < 0 || x >= (int)context->width || y < 0 || y >= (int)context->height) {
        return;
    }

    uint32_t *pixel = (uint32_t *)((uint8_t *)context->buffer + (uint32_t)y * context->pitch + (uint32_t)x * 4U);
    *pixel          = rgb;
}

desktop_t *desktop_new(video_context_t *context, uint32_t *wallpaper, uint16_t wallpaper_width,
                       uint16_t wallpaper_height)
{
    desktop_t *desktop = (desktop_t *)malloc(sizeof(desktop_t));
    if (!desktop) {
        return desktop;
    }

    if (!window_init((window_t *)desktop, 0, 0, context->width, context->height, WIN_NODECORATION, context)) {
        free(desktop);
        return nullptr;
    }

    desktop->window.paint_function = desktop_paint_handler;

    desktop->window.last_button_state = 0;

    desktop->mouse_x          = (int16_t)(desktop->window.context->width / 2);
    desktop->mouse_y          = (int16_t)(desktop->window.context->height / 2);
    desktop->wallpaper        = wallpaper;
    desktop->wallpaper_width  = wallpaper_width;
    desktop->wallpaper_height = wallpaper_height;

    return desktop;
}

static void desktop_draw_wallpaper(const desktop_t *desktop)
{
    if (!desktop->wallpaper || desktop->wallpaper_width == 0 || desktop->wallpaper_height == 0) {
        context_fill_rect(desktop->window.context,
                          0,
                          0,
                          desktop->window.context->width,
                          desktop->window.context->height,
                          DESKTOP_BACKGROUND_COLOR);
        return;
    }

    // Fill background first (covers areas not occupied by wallpaper).
    context_fill_rect(desktop->window.context,
                      0,
                      0,
                      desktop->window.context->width,
                      desktop->window.context->height,
                      DESKTOP_BACKGROUND_COLOR);

    // Draw wallpaper through the video_context so dirty/clipping regions are respected.
    // (Manual memcpy would ignore clipping and wipe child windows during mouse-driven dirty repaints.)
    context_draw_bitmap(desktop->window.context,
                        0,
                        0,
                        desktop->wallpaper_width,
                        desktop->wallpaper_height,
                        desktop->wallpaper);
}

static void draw_mouse_cursor(const desktop_t *desktop)
{
    for (int y = 0; y < MOUSE_HEIGHT; y++) {
        if ((y + desktop->mouse_y) >= desktop->window.context->height) {
            break;
        }
        for (int x = 0; x < MOUSE_WIDTH; x++) {
            if ((x + desktop->mouse_x) >= desktop->window.context->width) {
                break;
            }
            if (mouse_img[y * MOUSE_WIDTH + x] & 0xFF000000) {
                framebuffer_putpixel(desktop->window.context,
                                     x + desktop->mouse_x,
                                     y + desktop->mouse_y,
                                     mouse_img[y * MOUSE_WIDTH + x]);
            }
        }
    }
}

void desktop_paint_handler(const window_t *desktop_window)
{
    desktop_t *desktop = (desktop_t *)desktop_window;
    desktop_draw_wallpaper(desktop);

    const char *text = "experiment64";
    context_draw_text(desktop_window->context,
                      (char *)text,
                      desktop_window->width - (int)strlen(text) * VESA_CHAR_WIDTH - 10,
                      desktop_window->height - 22,
                      0xFFFFFFFF);
}

void desktop_process_mouse(desktop_t *desktop, uint16_t mouse_x, uint16_t mouse_y, uint16_t mouse_buttons)
{
    window_process_mouse((window_t *)desktop, mouse_x, mouse_y, mouse_buttons);

    int16_t old_x = desktop->mouse_x;
    int16_t old_y = desktop->mouse_y;

    desktop->mouse_x = (int16_t)mouse_x;
    desktop->mouse_y = (int16_t)mouse_y;

    if (old_x == desktop->mouse_x && old_y == desktop->mouse_y) {
        return;
    }

    list_t *dirty_list = list_new();
    if (!dirty_list) {
        return;
    }

    rect_t *old_mouse_rect = rect_new(
        old_y,
        old_x,
        old_y + MOUSE_HEIGHT - 1,
        old_x + MOUSE_WIDTH - 1);
    if (old_mouse_rect) {
        list_add(dirty_list, old_mouse_rect);
    }

    rect_t *new_mouse_rect = rect_new(
        desktop->mouse_y,
        desktop->mouse_x,
        desktop->mouse_y + MOUSE_HEIGHT - 1,
        desktop->mouse_x + MOUSE_WIDTH - 1);
    if (new_mouse_rect) {
        list_add(dirty_list, new_mouse_rect);
    }

    window_paint((window_t *)desktop, dirty_list, 1);

    if (old_mouse_rect) {
        list_remove_at(dirty_list, 0);
        free(old_mouse_rect);
    }
    if (new_mouse_rect) {
        if (dirty_list->count > 0) {
            list_remove_at(dirty_list, 0);
        }
        free(new_mouse_rect);
    }
    free(dirty_list);

    draw_mouse_cursor(desktop);
}