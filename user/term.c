#include <wm/wmclient.h>
#include <wm/wm_protocol.h>
#include <wm/video_context.h>
#include <pty.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wm/window.h>
#include <signal.h>

#define DEFAULT_TERM_COLS 80
#define DEFAULT_TERM_ROWS 25
#define DEFAULT_TERM_WIDTH ((uint16_t)(DEFAULT_TERM_COLS * VESA_CHAR_WIDTH))
#define DEFAULT_TERM_HEIGHT ((uint16_t)(DEFAULT_TERM_ROWS * VESA_LINE_HEIGHT))

#define TERM_CURSOR_COLOR 0xFF63C6FFu
#define TERM_STATUS_COLOR 0xFFFFAA66u

#define SCANCODE_TABLE_SIZE 84

static constexpr char scancode_to_char[SCANCODE_TABLE_SIZE] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+'
};

static constexpr char scancode_to_char_shifted[SCANCODE_TABLE_SIZE] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+'
};

static constexpr uint32_t ansi_palette[16] = {
    0xFF0E1012u,
    0xFFCC5555u,
    0xFF55AA55u,
    0xFFCCAA55u,
    0xFF4A86CFu,
    0xFFAA66CCu,
    0xFF44B8C8u,
    0xFFD7DCE2u,
    0xFF6E7681u,
    0xFFFF7777u,
    0xFF77DD77u,
    0xFFFFDD77u,
    0xFF70A7FFu,
    0xFFD090FFu,
    0xFF77E5FFu,
    0xFFFFFFFFu,
};

typedef enum
{
    TERM_PARSER_NORMAL = 0,
    TERM_PARSER_ESC = 1,
    TERM_PARSER_CSI = 2,
} term_parser_state_t;

typedef struct
{
    wm_window_t *win;
    video_context_t *context;
    int master_fd;
    int shell_pid;
    bool running;
    bool shell_exited;

    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool caps_lock;

    uint16_t rows;
    uint16_t cols;
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint16_t saved_row;
    uint16_t saved_col;

    uint8_t default_fg;
    uint8_t default_bg;
    uint8_t current_fg;
    uint8_t current_bg;
    bool bold;
    bool reverse;

    term_parser_state_t parser_state;
    char csi_buf[48];
    uint8_t csi_len;

    char *cells;
    uint8_t *fg;
    uint8_t *bg;

    pthread_mutex_t lock;
} terminal_state_t;


static terminal_state_t term = {};

static void sigint_handler(const int sig)
{
    (void)sig;
}

static size_t terminal_cell_index(uint16_t row, uint16_t col)
{
    return (size_t)row * (size_t)term.cols + (size_t)col;
}

static uint8_t terminal_effective_fg()
{
    return term.reverse ? term.current_bg : term.current_fg;
}

static uint8_t terminal_effective_bg()
{
    return term.reverse ? term.current_fg : term.current_bg;
}

static void terminal_set_cell_locked(uint16_t row, uint16_t col, char ch)
{
    const size_t idx = terminal_cell_index(row, col);
    term.cells[idx]  = ch;
    term.fg[idx]     = terminal_effective_fg();
    term.bg[idx]     = terminal_effective_bg();
}

static void terminal_fill_cells_locked(size_t start, size_t end)
{
    if (start >= end)
        return;

    const uint8_t fg = terminal_effective_fg();
    const uint8_t bg = terminal_effective_bg();
    for (size_t i = start; i < end; i++) {
        term.cells[i] = ' ';
        term.fg[i]    = fg;
        term.bg[i]    = bg;
    }
}

static void terminal_reset_attributes()
{
    term.current_fg = term.default_fg;
    term.current_bg = term.default_bg;
    term.bold       = false;
    term.reverse    = false;
}

static void terminal_clear_locked()
{
    term.cursor_row   = 0;
    term.cursor_col   = 0;
    term.saved_row    = 0;
    term.saved_col    = 0;
    term.parser_state = TERM_PARSER_NORMAL;
    term.csi_len      = 0;
    term.csi_buf[0]   = '\0';
    terminal_reset_attributes();

    const size_t total = (size_t)term.rows * (size_t)term.cols;
    for (size_t i = 0; i < total; i++) {
        term.cells[i] = ' ';
        term.fg[i]    = term.default_fg;
        term.bg[i]    = term.default_bg;
    }
}

static void terminal_context_free(video_context_t *context)
{
    if (!context)
        return;

    if (context->clip_rects) {
        context_clear_clip_rects(context);
        free(context->clip_rects);
    }

    free(context);
}

static int terminal_allocate_grid(uint16_t cols, uint16_t rows)
{
    if (cols == 0 || rows == 0)
        return -1;

    const size_t total = (size_t)cols * (size_t)rows;
    char *cells        = malloc(total);
    uint8_t *fg        = malloc(total);
    uint8_t *bg        = malloc(total);
    if (!cells || !fg || !bg) {
        free(cells);
        free(fg);
        free(bg);
        return -1;
    }

    free(term.cells);
    free(term.fg);
    free(term.bg);

    term.cells = cells;
    term.fg    = fg;
    term.bg    = bg;
    term.cols  = cols;
    term.rows  = rows;

    terminal_clear_locked();
    return 0;
}

static int terminal_resize_grid_locked(uint16_t new_cols, uint16_t new_rows)
{
    if (new_cols == 0 || new_rows == 0)
        return -1;

    if (new_cols == term.cols && new_rows == term.rows)
        return 0;

    const size_t new_total = (size_t)new_cols * (size_t)new_rows;
    char *new_cells        = malloc(new_total);
    uint8_t *new_fg        = malloc(new_total);
    uint8_t *new_bg        = malloc(new_total);
    if (!new_cells || !new_fg || !new_bg) {
        free(new_cells);
        free(new_fg);
        free(new_bg);
        return -1;
    }

    for (size_t i = 0; i < new_total; i++) {
        new_cells[i] = ' ';
        new_fg[i]    = term.default_fg;
        new_bg[i]    = term.default_bg;
    }

    const uint16_t copy_cols = new_cols < term.cols ? new_cols : term.cols;
    const uint16_t copy_rows = new_rows < term.rows ? new_rows : term.rows;

    for (uint16_t row = 0; row < copy_rows; row++) {
        const size_t old_off = (size_t)row * (size_t)term.cols;
        const size_t new_off = (size_t)row * (size_t)new_cols;
        memcpy(new_cells + new_off, term.cells + old_off, copy_cols);
        memcpy(new_fg + new_off, term.fg + old_off, copy_cols);
        memcpy(new_bg + new_off, term.bg + old_off, copy_cols);
    }

    free(term.cells);
    free(term.fg);
    free(term.bg);

    term.cells = new_cells;
    term.fg    = new_fg;
    term.bg    = new_bg;
    term.cols  = new_cols;
    term.rows  = new_rows;

    if (term.cursor_row >= term.rows)
        term.cursor_row = term.rows - 1;
    if (term.cursor_col >= term.cols)
        term.cursor_col = term.cols - 1;
    if (term.saved_row >= term.rows)
        term.saved_row = term.rows - 1;
    if (term.saved_col >= term.cols)
        term.saved_col = term.cols - 1;

    return 0;
}

static void terminal_scroll_up_locked()
{
    if (term.rows <= 1)
        return;

    const size_t row_size  = term.cols;
    const size_t move_size = (size_t)(term.rows - 1) * row_size;

    memmove(term.cells, term.cells + row_size, move_size);
    memmove(term.fg, term.fg + row_size, move_size);
    memmove(term.bg, term.bg + row_size, move_size);
    const size_t start = (size_t)(term.rows - 1) * row_size;
    terminal_fill_cells_locked(start, start + row_size);
}

static void terminal_newline_locked()
{
    if (term.cursor_row + 1 >= term.rows)
        terminal_scroll_up_locked();
    else
        term.cursor_row++;
}

static void terminal_put_char_locked(char ch)
{
    terminal_set_cell_locked(term.cursor_row, term.cursor_col, ch);

    if (term.cursor_col + 1 >= term.cols) {
        term.cursor_col = 0;
        terminal_newline_locked();
        return;
    }

    term.cursor_col++;
}

static void terminal_erase_line_locked(int mode)
{
    const size_t row_start = (size_t)term.cursor_row * term.cols;

    if (mode == 2) {
        terminal_fill_cells_locked(row_start, row_start + term.cols);
        return;
    }

    if (mode == 1) {
        terminal_fill_cells_locked(row_start, row_start + (size_t)term.cursor_col + 1);
        return;
    }

    terminal_fill_cells_locked(row_start + term.cursor_col, row_start + term.cols);
}

static void terminal_erase_display_locked(int mode)
{
    const size_t cursor = terminal_cell_index(term.cursor_row, term.cursor_col);
    const size_t total  = (size_t)term.rows * (size_t)term.cols;

    if (mode == 2) {
        terminal_fill_cells_locked(0, total);
        return;
    }

    if (mode == 1) {
        terminal_fill_cells_locked(0, cursor + 1);
        return;
    }

    terminal_fill_cells_locked(cursor, total);
}

static int csi_parse_params(const char *buf, int *params, int max_params)
{
    if (!buf || !params || max_params <= 0)
        return 0;

    int count      = 0;
    int value      = 0;
    bool has_value = false;

    for (size_t i = 0; buf[i] != '\0'; i++) {
        const char ch = buf[i];
        if (ch >= '0' && ch <= '9') {
            value     = value * 10 + (ch - '0');
            has_value = true;
            continue;
        }

        if (ch == ';') {
            if (count < max_params)
                params[count++] = has_value ? value : -1;
            value     = 0;
            has_value = false;
            continue;
        }

        break;
    }

    if (count < max_params)
        params[count++] = has_value ? value : -1;

    return count;
}

static int csi_param_or_default(const int *params, int count, int index, int default_value)
{
    if (!params || index >= count)
        return default_value;
    if (params[index] < 0)
        return default_value;
    return params[index];
}

static void terminal_apply_sgr_code(int code)
{
    if (code < 0)
        code = 0;

    switch (code) {
    case 0:
        terminal_reset_attributes();
        return;
    case 1:
        term.bold = true;
        return;
    case 22:
        term.bold = false;
        return;
    case 7:
        term.reverse = true;
        return;
    case 27:
        term.reverse = false;
        return;
    case 39:
        term.current_fg = term.default_fg;
        return;
    case 49:
        term.current_bg = term.default_bg;
        return;
    default:
        break;
    }

    if (code >= 30 && code <= 37) {
        uint8_t idx = (uint8_t)(code - 30);
        if (term.bold)
            idx = (uint8_t)(idx + 8);
        term.current_fg = idx;
        return;
    }

    if (code >= 90 && code <= 97) {
        term.current_fg = (uint8_t)(code - 90 + 8);
        return;
    }

    if (code >= 40 && code <= 47) {
        term.current_bg = (uint8_t)(code - 40);
        return;
    }

    if (code >= 100 && code <= 107)
        term.current_bg = (uint8_t)(code - 100 + 8);
}

static void terminal_apply_csi_locked(char final_char)
{
    int params[16]        = {0};
    const int param_count = csi_parse_params(term.csi_buf, params, 16);

    switch (final_char) {
    case 'A': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        if ((uint16_t)n > term.cursor_row)
            term.cursor_row = 0;
        else
            term.cursor_row = (uint16_t)(term.cursor_row - n);
        break;
    }
    case 'B': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        uint16_t next = (uint16_t)(term.cursor_row + n);
        if (next >= term.rows)
            term.cursor_row = term.rows - 1;
        else
            term.cursor_row = next;
        break;
    }
    case 'C': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        uint16_t next = (uint16_t)(term.cursor_col + n);
        if (next >= term.cols)
            term.cursor_col = term.cols - 1;
        else
            term.cursor_col = next;
        break;
    }
    case 'D': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        if ((uint16_t)n > term.cursor_col)
            term.cursor_col = 0;
        else
            term.cursor_col = (uint16_t)(term.cursor_col - n);
        break;
    }
    case 'E': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        for (int i = 0; i < n; i++)
            terminal_newline_locked();
        term.cursor_col = 0;
        break;
    }
    case 'F': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        if ((uint16_t)n > term.cursor_row)
            term.cursor_row = 0;
        else
            term.cursor_row = (uint16_t)(term.cursor_row - n);
        term.cursor_col = 0;
        break;
    }
    case 'G': {
        int col = csi_param_or_default(params, param_count, 0, 1);
        if (col < 1)
            col = 1;
        if (col > term.cols)
            col = term.cols;
        term.cursor_col = (uint16_t)(col - 1);
        break;
    }
    case 'd': {
        int row = csi_param_or_default(params, param_count, 0, 1);
        if (row < 1)
            row = 1;
        if (row > term.rows)
            row = term.rows;
        term.cursor_row = (uint16_t)(row - 1);
        break;
    }
    case 'H':
    case 'f': {
        int row = csi_param_or_default(params, param_count, 0, 1);
        int col = csi_param_or_default(params, param_count, 1, 1);
        if (row < 1)
            row = 1;
        if (col < 1)
            col = 1;
        if (row > term.rows)
            row = term.rows;
        if (col > term.cols)
            col = term.cols;
        term.cursor_row = (uint16_t)(row - 1);
        term.cursor_col = (uint16_t)(col - 1);
        break;
    }
    case 'J':
        terminal_erase_display_locked(csi_param_or_default(params, param_count, 0, 0));
        break;
    case 'K':
        terminal_erase_line_locked(csi_param_or_default(params, param_count, 0, 0));
        break;
    case 'X': {
        int n = csi_param_or_default(params, param_count, 0, 1);
        if (n < 1)
            n = 1;
        uint16_t end_col = (uint16_t)(term.cursor_col + n);
        if (end_col > term.cols)
            end_col = term.cols;
        const size_t start = terminal_cell_index(term.cursor_row, term.cursor_col);
        const size_t end   = terminal_cell_index(term.cursor_row, end_col);
        terminal_fill_cells_locked(start, end);
        break;
    }
    case 's':
        term.saved_row = term.cursor_row;
        term.saved_col = term.cursor_col;
        break;
    case 'u':
        term.cursor_row = term.saved_row < term.rows ? term.saved_row : (uint16_t)(term.rows - 1);
        term.cursor_col = term.saved_col < term.cols ? term.saved_col : (uint16_t)(term.cols - 1);
        break;
    case 'm': {
        if (param_count == 0)
            terminal_apply_sgr_code(0);
        else {
            for (int i = 0; i < param_count; i++)
                terminal_apply_sgr_code(params[i]);
        }
        break;
    }
    default:
        break;
    }
}

static void terminal_process_byte_locked(terminal_state_t *term, uint8_t byte)
{
    if (term->parser_state == TERM_PARSER_ESC) {
        switch (byte) {
        case '[':
            term->parser_state = TERM_PARSER_CSI;
            term->csi_len    = 0;
            term->csi_buf[0] = '\0';
            return;
        case '7':
            term->saved_row = term->cursor_row;
            term->saved_col    = term->cursor_col;
            term->parser_state = TERM_PARSER_NORMAL;
            return;
        case '8':
            term->cursor_row = term->saved_row < term->rows ? term->saved_row : (uint16_t)(term->rows - 1);
            term->cursor_col   = term->saved_col < term->cols ? term->saved_col : (uint16_t)(term->cols - 1);
            term->parser_state = TERM_PARSER_NORMAL;
            return;
        case 'D':
            terminal_newline_locked();
            term->parser_state = TERM_PARSER_NORMAL;
            return;
        case 'E':
            terminal_newline_locked();
            term->cursor_col   = 0;
            term->parser_state = TERM_PARSER_NORMAL;
            return;
        case 'c':
            terminal_clear_locked();
            term->parser_state = TERM_PARSER_NORMAL;
            return;
        default:
            term->parser_state = TERM_PARSER_NORMAL;
            break;
        }
    }

    if (term->parser_state == TERM_PARSER_CSI) {
        if (byte >= '@' && byte <= '~') {
            terminal_apply_csi_locked((char)byte);
            term->parser_state = TERM_PARSER_NORMAL;
            term->csi_len      = 0;
            term->csi_buf[0]   = '\0';
            return;
        }

        if (term->csi_len + 1 < sizeof(term->csi_buf)) {
            term->csi_buf[term->csi_len++] = (char)byte;
            term->csi_buf[term->csi_len]   = '\0';
        }
        return;
    }

    switch (byte) {
    case 0x1B:
        term->parser_state = TERM_PARSER_ESC;
        return;
    case '\r':
        term->cursor_col = 0;
        return;
    case '\n':
        terminal_newline_locked();
        return;
    case '\b':
        if (term->cursor_col > 0)
            term->cursor_col--;
        return;
    case '\t': {
        int spaces = 8 - (term->cursor_col % 8);
        if (spaces <= 0)
            spaces = 8;
        for (int i = 0; i < spaces; i++)
            terminal_put_char_locked(' ');
        return;
    }
    default:
        break;
    }

    if (byte >= 32 && byte <= 126)
        terminal_put_char_locked((char)byte);
}

static void terminal_render_locked(terminal_state_t *term)
{
    term->context->buffer = term->win->buffer;

    const uint32_t default_bg = ansi_palette[term->default_bg & 0x0Fu];
    context_fill_rect(term->context, 0, 0, term->win->width, term->win->height, default_bg);

    for (uint16_t row = 0; row < term->rows; row++) {
        const int y = (int)row * VESA_LINE_HEIGHT;
        for (uint16_t col = 0; col < term->cols; col++) {
            const size_t idx = terminal_cell_index(row, col);
            const int x      = (int)col * VESA_CHAR_WIDTH;

            const uint8_t bg_idx    = (uint8_t)(term->bg[idx] & 0x0F);
            const uint32_t bg_color = ansi_palette[bg_idx];
            if (bg_idx != term->default_bg)
                context_fill_rect(term->context, x, y, VESA_CHAR_WIDTH, VESA_LINE_HEIGHT, bg_color);

            const char ch = term->cells[idx];
            if (ch != ' ') {
                const uint8_t fg_idx    = (uint8_t)(term->fg[idx] & 0x0F);
                const uint32_t fg_color = ansi_palette[fg_idx];
                context_draw_char(term->context, ch, x, y + 1, fg_color);
            }
        }
    }

    if (term->cursor_row < term->rows && term->cursor_col < term->cols) {
        const int cursor_x = (int)term->cursor_col * VESA_CHAR_WIDTH;
        const int cursor_y = (int)term->cursor_row * VESA_LINE_HEIGHT + (VESA_CHAR_HEIGHT + 1);
        context_fill_rect(term->context, cursor_x, cursor_y, VESA_CHAR_WIDTH, 2, TERM_CURSOR_COLOR);
    }

    if (term->shell_exited)
        context_draw_text(term->context, "[shell exited]", 4, term->win->height - VESA_LINE_HEIGHT, TERM_STATUS_COLOR);

    wm_invalidate_all(term->win);
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int terminal_set_pty_winsize(int fd, uint16_t rows, uint16_t cols, uint16_t width, uint16_t height)
{
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = width,
        .ws_ypixel = height,
    };
    return ioctl(fd, TIOCSWINSZ, &ws);
}

static uint16_t terminal_cols_from_width(uint16_t width)
{
    uint16_t cols = (uint16_t)(width / VESA_CHAR_WIDTH);
    return cols ? cols : 1;
}

static uint16_t terminal_rows_from_height(uint16_t height)
{
    uint16_t rows = (uint16_t)(height / VESA_LINE_HEIGHT);
    return rows ? rows : 1;
}

static size_t terminal_translate_key(terminal_state_t *term, const wm_event_key_t *ev, char *out, size_t out_cap)
{
    if (!term || !ev || !out || out_cap == 0)
        return 0;

    const bool extended = (ev->keycode & 0x80u) != 0;
    const uint8_t code  = (uint8_t)(ev->keycode & 0x7Fu);
    const bool pressed  = ev->pressed != 0;

    if (extended) {
        if (code == 0x1D)
            term->ctrl_pressed = pressed;
        else if (code == 0x38)
            term->alt_pressed = pressed;
    } else {
        if (code == 0x2A || code == 0x36)
            term->shift_pressed = pressed;
        else if (code == 0x1D)
            term->ctrl_pressed = pressed;
        else if (code == 0x38)
            term->alt_pressed = pressed;
        else if (code == 0x3A && pressed)
            term->caps_lock = !term->caps_lock;
    }

    if (!pressed)
        return 0;

    if (extended) {
        const char *sequence = nullptr;
        switch (code) {
        case 0x48:
            sequence = "\x1b[A";
            break;
        case 0x50:
            sequence = "\x1b[B";
            break;
        case 0x4D:
            sequence = "\x1b[C";
            break;
        case 0x4B:
            sequence = "\x1b[D";
            break;
        case 0x47:
            sequence = "\x1b[H";
            break;
        case 0x4F:
            sequence = "\x1b[F";
            break;
        case 0x49:
            sequence = "\x1b[5~";
            break;
        case 0x51:
            sequence = "\x1b[6~";
            break;
        case 0x52:
            sequence = "\x1b[2~";
            break;
        case 0x53:
            sequence = "\x1b[3~";
            break;
        case 0x1C:
            sequence = "\n";
            break;
        default:
            break;
        }

        if (!sequence)
            return 0;

        const size_t seq_len = strlen(sequence);
        if (seq_len > out_cap)
            return 0;

        memcpy(out, sequence, seq_len);
        return seq_len;
    }

    if (code >= SCANCODE_TABLE_SIZE)
        return 0;

    bool use_shift = term->shift_pressed;
    int c          = (unsigned char)scancode_to_char[code];
    if (term->caps_lock && c >= 'a' && c <= 'z')
        use_shift = !use_shift;

    c = use_shift
        ? (unsigned char)scancode_to_char_shifted[code]
        : (unsigned char)scancode_to_char[code];

    if (term->ctrl_pressed) {
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 1;
    }

    if (c == 0)
        return 0;

    out[0] = (char)c;
    return 1;
}

static void *terminal_reader_thread(void *arg)
{
    auto terminal = (terminal_state_t *)arg;
    uint8_t buf[256];

    while (__atomic_load_n(&terminal->running, __ATOMIC_RELAXED)) {
        const ssize_t n = read(terminal->master_fd, buf, sizeof(buf));
        if (n <= 0)
            break;

        pthread_mutex_lock(&terminal->lock);
        for (ssize_t i = 0; i < n; i++)
            terminal_process_byte_locked(terminal, buf[i]);
        terminal_render_locked(terminal);
        pthread_mutex_unlock(&terminal->lock);
    }

    const bool was_running = __atomic_exchange_n(&terminal->running, false, __ATOMIC_RELAXED);
    if (was_running) {
        pthread_mutex_lock(&terminal->lock);
        terminal->shell_exited = true;
        pthread_mutex_unlock(&terminal->lock);

        wm_shutdown_events();
    }

    return nullptr;
}

static void *terminal_shell_wait_thread(void *arg)
{
    auto terminal = (terminal_state_t *)arg;
    if (!terminal || terminal->shell_pid <= 0)
        return nullptr;

    int status = 0;
    if (waitpid(terminal->shell_pid, &status, 0) != terminal->shell_pid)
        return nullptr;

    const bool was_running = __atomic_exchange_n(&terminal->running, false, __ATOMIC_RELAXED);
    if (was_running) {
        pthread_mutex_lock(&terminal->lock);
        terminal->shell_exited = true;
        pthread_mutex_unlock(&terminal->lock);

        wm_shutdown_events();
    }

    return nullptr;
}

static int spawn_shell_on_pty(int *master_fd_out, int *slave_fd_out, int *child_pid_out,
                              uint16_t rows, uint16_t cols, uint16_t width, uint16_t height)
{
    if (!master_fd_out || !slave_fd_out || !child_pid_out)
        return -1;

    int pty_fds[2] = {-1, -1};
    if (openpty(pty_fds) != 0)
        return -1;

    const int master_fd = pty_fds[0];
    const int slave_fd  = pty_fds[1];

    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = width,
        .ws_ypixel = height,
    };
    ioctl(slave_fd, TIOCSWINSZ, &ws);

    const int pid = fork();
    if (pid < 0) {
        close(master_fd);
        close(slave_fd);
        return -1;
    }

    if (pid == 0) {
        if (dup2(slave_fd, STDIN_FILENO) < 0)
            exit(120);
        if (dup2(slave_fd, STDOUT_FILENO) < 0)
            exit(121);
        if (dup2(slave_fd, STDERR_FILENO) < 0)
            exit(122);

        if (master_fd != STDIN_FILENO && master_fd != STDOUT_FILENO && master_fd != STDERR_FILENO)
            close(master_fd);
        if (slave_fd != STDIN_FILENO && slave_fd != STDOUT_FILENO && slave_fd != STDERR_FILENO)
            close(slave_fd);

        exec("/bin/sh");

        exit(127);
    }

    *master_fd_out = master_fd;
    *slave_fd_out  = slave_fd;
    *child_pid_out = pid;
    return 0;
}

static int terminal_rebuild_context_locked(terminal_state_t *term)
{
    video_context_t *new_context = context_new(term->win->buffer,
                                               term->win->width,
                                               term->win->height,
                                               (uint32_t)term->win->width * 4U);
    if (!new_context)
        return -1;

    terminal_context_free(term->context);
    term->context = new_context;
    return 0;
}

int main(void)
{
    signal(SIGINT, sigint_handler);
    wm_window_t *window = wm_create_window(140, 70, DEFAULT_TERM_WIDTH, DEFAULT_TERM_HEIGHT, WIN_RESIZABLE | WIN_CLOSEABLE,0, "Terminal");
    if (!window)
        return 1;

    video_context_t *initial_context =
        context_new(window->buffer, window->width, window->height, (uint32_t)window->width * 4U);
    if (!initial_context) {
        wm_destroy_window(window);
        return 2;
    }

    term.win        = window;
    term.context    = initial_context;
    term.default_fg = 7;
    term.default_bg = 0;
    term.current_fg = term.default_fg;
    term.current_bg = term.default_bg;
    term.lock       = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    const uint16_t initial_cols = terminal_cols_from_width(window->width);
    const uint16_t initial_rows = terminal_rows_from_height(window->height);
    if (terminal_allocate_grid(initial_cols, initial_rows) != 0) {
        terminal_context_free(initial_context);
        wm_destroy_window(window);
        return 3;
    }

    int master_fd = -1;
    int slave_fd  = -1;
    int shell_pid = -1;
    if (spawn_shell_on_pty(&master_fd, &slave_fd, &shell_pid, term.rows, term.cols, window->width, window->height) !=
        0) {
        free(term.cells);
        free(term.fg);
        free(term.bg);
        terminal_context_free(initial_context);
        wm_destroy_window(window);
        return 4;
    }
    close(slave_fd);

    term.master_fd = master_fd;
    term.shell_pid = shell_pid;
    term.running   = true;

    pthread_mutex_lock(&term.lock);
    terminal_render_locked(&term);
    pthread_mutex_unlock(&term.lock);

    pthread_t reader_thread = 0;
    pthread_create(&reader_thread, nullptr, terminal_reader_thread, &term);

    pthread_t shell_wait_thread = 0;
    bool shell_wait_started     = false;
    if (pthread_create(&shell_wait_thread, nullptr, terminal_shell_wait_thread, &term) == 0)
        shell_wait_started = true;

    uint8_t event_buf[64];
    uint8_t event_type = 0;

    while (__atomic_load_n(&term.running, __ATOMIC_RELAXED) &&
        wm_next_event(event_buf, &event_type) == 0) {
        if (event_type == WM_EVENT_KEY) {
            auto ev = (const wm_event_key_t *)event_buf;
            if (ev->window_id != term.win->window_id)
                continue;

            char out[8];
            const size_t out_len = terminal_translate_key(&term, ev, out, sizeof(out));
            if (out_len > 0)
                write_all(master_fd, out, out_len);
            continue;
        }

        if (event_type == WM_EVENT_WINDOW_RESIZED) {
            auto ev = (const wm_event_window_resized_t *)event_buf;
            if (ev->window_id != term.win->window_id)
                continue;

            pthread_mutex_lock(&term.lock);

            if (terminal_rebuild_context_locked(&term) == 0) {
                const uint16_t new_cols = terminal_cols_from_width(term.win->width);
                const uint16_t new_rows = terminal_rows_from_height(term.win->height);
                terminal_resize_grid_locked(new_cols, new_rows);
                terminal_set_pty_winsize(term.master_fd, term.rows, term.cols, term.win->width, term.win->height);
                terminal_render_locked(&term);
            }

            pthread_mutex_unlock(&term.lock);
            continue;
        }

        if (event_type == WM_EVENT_WINDOW_CLOSED) {
            auto ev = (const wm_event_window_closed_t *)event_buf;
            if (ev->window_id == term.win->window_id) {
                break;
            }
        }
    }

    __atomic_store_n(&term.running, false, __ATOMIC_RELAXED);
    wm_shutdown_events();
    kill(shell_pid, SIGTERM);
    close(master_fd);

    // if (reader_started) {
    //     pthread_join(reader_thread, nullptr);
    // }

    if (shell_wait_started) {
        pthread_join(shell_wait_thread, nullptr);
    } else {
        int status = 0;
        if (waitpid(shell_pid, &status, WNOHANG) == 0) {
            waitpid(shell_pid, &status, 0);
        }
    }

    free(term.cells);
    free(term.fg);
    free(term.bg);
    terminal_context_free(term.context);
    wm_destroy_window(term.win);
    return 0;
}