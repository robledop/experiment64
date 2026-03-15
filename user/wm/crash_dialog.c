#include "crash_dialog.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wm/button.h>
#include <wm/desktop.h>
#include <wm/video_context.h>
#include <wm/window.h>

#define DIALOG_WIDTH 450
#define DIALOG_PADDING 12
#define LINE_HEIGHT (VESA_CHAR_HEIGHT + 4)

#define DIALOG_BG_COLOR 0xFFF0F0F0
#define DIALOG_TEXT_COLOR 0xFF1A1A1A
#define DIALOG_HEADER_COLOR 0xFFCC0000
#define DIALOG_HINT_COLOR 0xFF666666

typedef struct
{
    window_t window;
    char app_name[64];
    int sig;
    crash_info_t info;
} crash_dialog_t;

static void crash_dialog_close(const window_t *window)
{
    window_t *parent = window->parent;
    if (parent) {
        window_remove_child(parent, (window_t *)window);
        window_paint(parent, nullptr, 1);
    }
}

static void crash_dialog_dismiss(button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    window_t *dialog = ((window_t *)button)->parent;
    if (dialog)
        crash_dialog_close(dialog);
}

// Paint function uses content-relative coordinates (0,0 = top-left of content area).
// The WM sets translate_x/y on the context before calling this.
static void crash_dialog_paint(const window_t *window)
{
    const crash_dialog_t *dlg = (const crash_dialog_t *)window;

    int cw = window->width - 2 * WIN_BORDER_WIDTH;
    int ch = window->height - WIN_TITLE_HEIGHT - WIN_BORDER_WIDTH;

    context_fill_rect(window->context, 0, 0, (unsigned)cw, (unsigned)ch, DIALOG_BG_COLOR);

    int text_x = DIALOG_PADDING;
    int text_y = DIALOG_PADDING;
    char line[128];

    // Line 1: app name crashed
    snprintf(line, sizeof(line), "%s crashed", dlg->app_name[0] ? dlg->app_name : "Unknown");
    context_draw_text(window->context, line, text_x, text_y, DIALOG_HEADER_COLOR);
    text_y += LINE_HEIGHT;

    // Line 2: signal name
    snprintf(line, sizeof(line), "Signal: %s (%d)", strsignal(dlg->sig), dlg->sig);
    context_draw_text(window->context, line, text_x, text_y, DIALOG_TEXT_COLOR);
    text_y += LINE_HEIGHT;

    // Line 3: fault address
    if (dlg->info.fault_rip) {
        snprintf(line, sizeof(line), "Fault address: 0x%lx", dlg->info.fault_rip);
        context_draw_text(window->context, line, text_x, text_y, DIALOG_TEXT_COLOR);
    }
    text_y += LINE_HEIGHT;

    // Stack trace
    if (dlg->info.frame_count > 0) {
        text_y += LINE_HEIGHT / 2;
        context_draw_text(window->context, "Stack trace:", text_x, text_y, DIALOG_TEXT_COLOR);
        text_y += LINE_HEIGHT;

        for (int i = 0; i < dlg->info.frame_count; i++) {
            snprintf(line, sizeof(line), "  [0x%lx]", dlg->info.frames[i]);
            context_draw_text(window->context, line, text_x, text_y, DIALOG_TEXT_COLOR);
            text_y += LINE_HEIGHT;
        }
    }

    // addr2line hint
    text_y += LINE_HEIGHT / 2;
    snprintf(line, sizeof(line), "addr2line -e user/build/%s <addr>",
             dlg->app_name[0] ? dlg->app_name : "binary");
    context_draw_text(window->context, line, text_x, text_y, DIALOG_HINT_COLOR);
}

void crash_dialog_show(window_t *parent, const char *app_name, int sig, const crash_info_t *info)
{
    if (!parent)
        return;

    // Calculate height based on stack trace depth
    int line_count = 4; // header + signal + fault addr + blank
    if (info && info->frame_count > 0)
        line_count += 1 + info->frame_count; // "Stack trace:" + frames
    line_count += 2; // addr2line hint + padding for button

    int dialog_height = DIALOG_PADDING * 2 + line_count * LINE_HEIGHT + WIN_TITLE_HEIGHT + 40;

    int16_t dx = (int16_t)((parent->width - DIALOG_WIDTH) / 2);
    int16_t dy = (int16_t)((parent->height - dialog_height) / 2);

    crash_dialog_t *dlg = malloc(sizeof(crash_dialog_t));
    if (!dlg)
        return;

    if (!window_init((window_t *)dlg, dx, dy, DIALOG_WIDTH, (uint16_t)dialog_height,
                     WIN_CLOSEABLE, parent->context)) {
        free(dlg);
        return;
    }

    // Store crash data for repaint
    if (app_name)
        snprintf(dlg->app_name, sizeof(dlg->app_name), "%s", app_name);
    else
        dlg->app_name[0] = '\0';
    dlg->sig = sig;
    if (info)
        dlg->info = *info;
    else
        memset(&dlg->info, 0, sizeof(dlg->info));

    dlg->window.paint_function = crash_dialog_paint;
    dlg->window.close_function = crash_dialog_close;
    window_set_title((window_t *)dlg, "Application Crash");

    // OK button — coordinates relative to content area
    int content_h = dialog_height - WIN_TITLE_HEIGHT - WIN_BORDER_WIDTH;
    int btn_w = 80;
    int btn_h = 26;
    int btn_x = (DIALOG_WIDTH - 2 * WIN_BORDER_WIDTH - btn_w) / 2;
    int btn_y = content_h - btn_h - DIALOG_PADDING;

    button_t *ok = button_new((int16_t)btn_x, (int16_t)btn_y, (int16_t)btn_w, (int16_t)btn_h);
    if (ok) {
        ok->onmousedown = crash_dialog_dismiss;
        window_set_title((window_t *)ok, "OK");
        window_insert_child((window_t *)dlg, (window_t *)ok);
    }

    window_insert_child(parent, (window_t *)dlg);
    window_raise((window_t *)dlg, 1);
}
