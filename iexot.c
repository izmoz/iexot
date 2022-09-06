/*** includes ***/
#include "iexot.h"
#include "linked_list.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
/*** defines ***/
#define CTRL_KEY(k) ((k)&0x1f)
#define SHIFT_KEY(k) ((k)&0x5f)
#define IEXOT_VERSION "0.0.1"
#define IEXOT_TITLE_TOP_PADDING 3

#define IEXOT_TAB_WIDTH 4

#define HL_HIGHLIGHT_NUMBERS (1 >> 0)

/*** enums ***/
enum KEYS {
    BACKSPACE = 127,
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
};
enum EDITOR_HIGHLIGHT { HL_NORMAL = 0, HL_NUMBER, HL_MATCH, HL_STRINGS };

struct editor_syntax {
    char *filetype;
    char **filematch;
    int flags;
};
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
} erow;
void erow_free(erow *row) {
    if (!row)
        return;
    free(row->chars);
    free(row);
}
/*** filetypes ***/

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};

struct editor_syntax HLDB[] = {
    {"c", C_HL_extensions, HL_HIGHLIGHT_NUMBERS},
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** editor ***/
struct editor_config {
    int cx, cy, rx;
    int saved_cx, saved_cy;
    int flag_mv_line;
    int prevx;
    char *filename;
    char status_msg[100];
    time_t status_msg_time;
    int nmodifications;
    int input_turn;
    unsigned scrnrows;
    unsigned scrncols;
    unsigned nrows;
    unsigned rowoff;
    unsigned coloff;
    erow *row;
    struct termios orig_termios;
    struct editor_syntax *syntax;

    Node *current_search_match;
    Node *search_list_head;
    Node *search_list_tail;
} config;
/*** row operations ***/
int editor_cx_to_rx(erow *row, int cx) {
    size_t i;
    int rx = 0;
    for (i = 0; i < cx; i++) {
        if (row->chars[i] == '\t')
            rx += (IEXOT_TAB_WIDTH - 1) - (rx % IEXOT_TAB_WIDTH);
        rx++;
    }
    return rx;
}
/*** syntax highlighting ***/
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
void editor_update_syntax(erow *row) {
    if (row->size < 1)
        return;
    row->hl = realloc(row->hl, row->size);
    if (!row->hl)
        die("editor_update_syntax: hl realloc");
    memset(row->hl, HL_NORMAL, row->rsize);
    if(!config.syntax)
        return;

    int i = 0;
    int prev_sep = 1;
    while (i < row->size) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
        if (config.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_hl == HL_NUMBER || prev_sep)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }
        prev_sep = is_separator(c);
        i++;
    }
}
void editor_select_highlight() {
    config.syntax = NULL;
    if(!config.filename) return;
    char *ext = strrchr(config.filename,'.');
    for(unsigned int j=0; j< HLDB_ENTRIES; j++) {
        struct editor_syntax *s = &HLDB[j];
        unsigned int i=0;
        while(s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(config.filename, s->filematch[i]))) {
                    config.syntax = s;
                    int filerow;
                    for(filerow = 0;filerow<config.nrows;filerow++)
                        editor_update_syntax(&config.row[filerow]);
                    return;
            }
            i++;
        }
    }
}
int editor_syntax_to_color(int hl) {
    switch (hl) {
    case HL_NUMBER:
        return 31;
    case HL_MATCH:
        return 34;
    default:
        return 37;
    }
}
void editor_update_row(erow *row) {
    size_t tabs = 0;
    for (size_t i = 0; i < row->size; i++)
        if (row->chars[i] == '\t')
            tabs++;
    free(row->render);
    size_t space_to_allocate =
        row->size - tabs + 1 +
        (IEXOT_TAB_WIDTH * tabs); // extra 1 byte for null-terminator
    row->render = malloc(space_to_allocate);
    if (!row->render)
        editor_destroy();
    size_t idx = 0;
    for (size_t j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            size_t t = 0;
            while (t < IEXOT_TAB_WIDTH) {
                row->render[idx + t] = ' ';
                t++;
            }
            idx += t;
        } else
            row->render[idx++] = row->chars[j];
    }
    row->render[idx] = '\0';
    row->rsize = idx;
    editor_update_syntax(row);
}
void editor_row_insert_char(erow *row, int at, int c) {
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    config.nmodifications++;
    editor_update_row(row);
}
void editor_free_row(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}
void editor_del_row(int at) {
    if (at < 0 || at >= config.nrows)
        return;
    editor_free_row(&config.row[at]);
    memmove(&config.row[at], &config.row[at + 1],
            sizeof(erow) * (config.nrows - at - 1));
    config.nrows--;
    config.nmodifications++;
}
void editor_row_append_string(erow *row, const char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    config.nmodifications++;
}
void editor_row_del_char(erow *row, int at) {
    if (at < 0 || at >= row->size + 1)
        return;
    memmove(&row->chars[at - 1], &row->chars[at], row->size - at);
    row->size--;
    config.nmodifications++;
    editor_update_row(row);
}
void editor_append_line(int at, const char *s, size_t len) {
    if (at < 0 || at > config.nrows)
        return;
    config.row = realloc(config.row, sizeof(erow) * (config.nrows + 1));
    memmove(&config.row[at + 1], &config.row[at],
            sizeof(erow) * (config.nrows - at));

    config.row[at].size = len;
    config.row[at].chars = malloc(len + 1);
    memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = NULL;
    config.row[at].hl = NULL;
    editor_update_row(&config.row[at]);

    config.nrows++;
    config.nmodifications++;
}
/*** editor operations ***/
void editor_insert_char(int c) {
    if (config.cy == config.nrows)
        editor_append_line(config.nrows, "", 0);
    editor_row_insert_char(&config.row[config.cy], config.cx, c);
    config.cx++;
}
void editor_insert_new_line() {
    if (config.cx == 0) {
        editor_append_line(config.cy, "", 0);
    } else {
        erow *row = &config.row[config.cy];
        editor_append_line(config.cy + 1, &row->chars[config.cx],
                           row->size - config.cx);
        row = &config.row[config.cy];
        row->size = config.cx;
        row->chars[row->size] = '\0';
        editor_update_row(row);
    }
    config.cy++;
    config.cx = 0;
}
void editor_jmp_next_word() {
    size_t sz = config.row[config.cy].size;
    char *line = config.row[config.cy].chars;
    int i;
    static int flag = 0;
    static int flag_punct = 0;
    if (isspace(line[config.cx]))
        flag = 1;
    else if (isalpha(line[config.cx]))
        flag = 0;
    if (ispunct(line[config.cx]))
        flag_punct = 1;
    else
        flag_punct = 0;
    for (i = 0; i + config.cx < sz; i++) {
        if (isspace(line[config.cx + i]))
            flag = 1;
        if (!ispunct(line[config.cx + i]))
            flag_punct = 0;
        if (!flag_punct && ispunct(line[config.cx + i]) &&
            line[config.cx + i] != '_') {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_RIGHT);
            flag_punct = 1;
            flag = 1;
            return;
        }
        if (flag && isalpha(line[i + config.cx])) {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_RIGHT);
            flag = 0;
            return;
        }
        if (flag && isdigit(line[config.cx + i])) {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_RIGHT);
            flag = 0;
            return;
        }
    }
    if (config.cx + i == sz) {
        for (size_t j = 0; j <= i; j++)
            editor_move_cursor(ARROW_RIGHT);
        flag = 0;
    }
}
void editor_jmp_prev_word() {
    char *line = config.row[config.cy].chars;
    int i;
    static int flag = 0;
    static int flag_punct = 0;
    if (isspace(line[config.cx]))
        flag = 1;
    else if (isalpha(line[config.cx]))
        flag = 0;
    if (ispunct(line[config.cx]))
        flag_punct = 1;
    else
        flag_punct = 0;
    for (i = 0; config.cx - i >= 0; i++) {
        if (isspace(line[config.cx - i]))
            flag = 1;
        if (!ispunct(line[config.cx - i]))
            flag_punct = 0;
        if (!flag_punct && ispunct(line[config.cx - i]) &&
            line[config.cx - i] != '_') {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_LEFT);
            flag_punct = 1;
            flag = 1;
            return;
        }
        if (flag && isalpha(line[config.cx - i])) {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_LEFT);
            flag = 0;
            return;
        }
        if (flag && isdigit(line[config.cx - i])) {
            for (size_t j = 0; j < i; j++)
                editor_move_cursor(ARROW_LEFT);
            flag = 0;
            return;
        }
    }
    if (config.cx - i <= 0) {
        for (size_t j = 0; j <= i; j++)
            editor_move_cursor(ARROW_LEFT);
        flag = 0;
    }
}
// TODO: refactor it
void editor_jmp_line_boundaries(int arg) {
    if (config.row[config.cy].size > 1) {
        if (arg == 0) { // 0 means "jump on the beggining of line"
            config.cx = 0;
            for (; isspace(config.row[config.cy].chars[config.cx]); config.cx++)
                ;
        } else if (arg == 1) // 1 means "jump on the end of line"
            config.cx = config.row[config.cy].size - 1;
    }
}
int editor_chrptr_to_cx(char *p) {
    int i;
    for (i = 0; i < config.row[config.cy].size &&
                p != (config.row[config.cy].chars + i);
         i++)
        ;
    return i;
}
void editor_find_callback(char *pattern, int k) {
    char *p = NULL;
    if (k == 'r' || k == '\x1b') {
        config.cx = config.saved_cx;
        config.cy = config.saved_cy;
        return;
    }
    config.search_list_head = NULL;
    config.search_list_tail = NULL;
    for (size_t i = 0; i < config.nrows; i++) {
        erow *row = &config.row[i];
        editor_update_syntax(row);
        if ((p = strstr(row->chars, pattern))) {
            Node *match = create_node(i, p, row->hl);
            push_back(match, &config.search_list_head,
                      &config.search_list_tail);
            config.current_search_match =
                realloc(config.current_search_match, sizeof(Node *));
            if (!config.current_search_match)
                die("config.current_search_match realloc returns NULL");
            config.current_search_match = config.search_list_head;
            config.cy = config.current_search_match->cy;
            config.cx = editor_chrptr_to_cx(config.current_search_match->p);

            memset(&row->hl[p - row->chars], HL_MATCH, strlen(pattern));
        }
    }
    if (!config.search_list_head) {
        config.cx = config.saved_cx;
        config.cy = config.saved_cy;
    }
}
void editor_find() {
    config.saved_cx = config.cx;
    config.saved_cy = config.cy;
    list_free(config.search_list_head, config.search_list_tail);
    config.search_list_head = NULL;
    config.search_list_tail = NULL;
    char *pattern = editor_prompt("Search: %s", editor_find_callback);
    if (NULL == pattern)
        return;
}
char *editor_prompt(char *prompt, void (*callback)(char *p, int k)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';
    while (1) {
        editor_set_status_msg(prompt, buf);
        editor_clear_scrn();
        int c = editor_read_key();
        if (c == BACKSPACE) {
            if (buflen != 0)
                buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editor_set_status_msg("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editor_set_status_msg("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback)
            callback(buf, c);
    }
}
void editor_del_char() {
    if (config.cy == config.nrows)
        return;
    if (config.cx == 0 && config.cy == 0)
        return;
    erow *row = &config.row[config.cy];
    if (config.cx > 0) {
        editor_row_del_char(&config.row[config.cy], config.cx);
        config.cx--;
    } else {
        config.cx = config.row[config.cy - 1].size; // -1?
        editor_row_append_string(&config.row[config.cy - 1], row->chars,
                                 row->size);
        editor_del_row(config.cy);
        config.cy--;
    }
}
/*** file i/o ***/
char *editor_rows_to_string(int *buflen) {
    size_t totlen = 0;
    size_t j;
    for (j = 0; j < config.nrows; j++)
        totlen += config.row[j].size + 1;
    *buflen = totlen;
    char *buf = malloc(totlen + 1);
    char *p = buf;
    for (size_t i = 0; i < config.nrows; i++) {
        memcpy(p, config.row[i].chars, config.row[i].size);
        p += config.row[i].size;
        *p = '\n';
        p++;
    }
    return buf;
}
void editor_open(const char *filename) {
    free(config.filename);
    config.filename = strdup(filename);
    editor_select_highlight();
    FILE *fp = fopen(filename, "r");
    char *line = NULL;
    if (!fp)
        die("fopen");
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
            linelen--;
        if (linelen == 0 && line[0] == '\n')
            line[linelen++] = ' ';
        editor_append_line(config.nrows, line, linelen);
    }
    config.nmodifications = 0;
}
void editor_save() {
    if (config.filename == NULL) {
        config.filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
        if (!config.filename) {
            editor_set_status_msg("Save aborted.");
            return;
        }
        editor_select_highlight();
    }
    int len;
    char *buf = editor_rows_to_string(&len);
    int fd = open(config.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                const time_t saved = time(NULL);
                struct tm *time = localtime(&saved);
                char save_msg[50];
                snprintf(save_msg, sizeof(save_msg), "Saved at %d:%d:%d",
                         time->tm_hour, time->tm_min, time->tm_sec);
                editor_set_status_msg(save_msg);
                config.nmodifications = 0;
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_msg("Can't save! Error: %s", strerror(errno));
}
/*** append-buffer ***/
struct abuf {
    char *b;
    size_t len;
};
#define ABUF_INIT                                                              \
    { NULL, 0 }
void ab_append(struct abuf *ab, const char *s, size_t len) {
    char *new = realloc(ab->b, ab->len + len);
    if (!new)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
void ab_free(struct abuf *ab) { free(ab->b); }
/*** terminal ***/
int get_cursor_position(unsigned *rows, unsigned *cols) {
    char buf[32];
    unsigned i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int get_win_size(unsigned *rows, unsigned *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return get_cursor_position(rows, cols);
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}
void editor_init() {
    config.nrows = 0;
    config.rowoff = 0;
    config.coloff = 0;
    config.row = NULL;
    config.filename = NULL;
    config.cx = config.cy = config.rx;
    config.saved_cx = config.saved_cy = 0;
    config.prevx = 0;
    config.flag_mv_line = 0;
    if (get_win_size(&config.scrnrows, &config.scrncols) == -1)
        die("get_win_size");
    config.scrnrows -=
        2; // decrementing 2 lines for status bar and status message
    config.status_msg[0] = '\0';
    config.status_msg_time = 0;
    config.nmodifications = 0;
    config.search_list_head = NULL;
    config.search_list_tail = NULL;
    config.current_search_match = NULL;
    config.syntax = NULL;
}
void editor_destroy() {
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);
    erow_free(config.row);
    free(config.current_search_match);
    exit(0);
}
void die(const char *s) {
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}
void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.orig_termios) == -1)
        die("tcsetattr");
}
void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &config.orig_termios) == -1)
        die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = config.orig_termios;
    raw.c_oflag &= ~(OPOST);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_lflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/*** output ***/
void editor_draw_rows(struct abuf *ab) {
    size_t y;
    for (y = 0; y < config.scrnrows; ++y) {
        size_t filerow = y + config.rowoff;
        if (filerow >= config.nrows) {
            if (config.nrows == 0 && y == IEXOT_TITLE_TOP_PADDING) {
                char welcome[80];
                size_t welcomelen =
                    sprintf(welcome, "IEXOT EDITOR VERSION %s", IEXOT_VERSION);
                if (welcomelen > config.scrncols)
                    welcomelen = config.scrncols;
                int padding = (config.scrncols - welcomelen) / 2 -
                              1; // -1 because of tilda
                if (padding) {
                    ab_append(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    ab_append(ab, " ", 1);
                }
                ab_append(ab, welcome, welcomelen);
            } else if (config.nrows == 0 && y > IEXOT_TITLE_TOP_PADDING) {
                ab_append(ab, "~", 1);
            }
        } else {
            int len = config.row[filerow].rsize - config.coloff;
            if (len < 0)
                len = 0;
            if (len > config.scrncols)
                len = config.scrncols;
            char *c = &config.row[filerow].render[config.coloff];
            unsigned char *hl = &config.row[filerow].hl[config.coloff];
            int cur_color = -1;
            for (size_t j = 0; j < len; j++) {
                if (hl[j] == HL_NORMAL) {
                    if (cur_color != -1) {
                        ab_append(ab, "\x1b[39m", 5);
                        cur_color = -1;
                    }
                    ab_append(ab, &c[j], 1);
                } else {
                    int color = editor_syntax_to_color(hl[j]);
                    if (cur_color != color) {
                        cur_color = color;
                        char buf[16];
                        int clen =
                            snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        ab_append(ab, buf, clen);
                    }
                    ab_append(ab, &c[j], 1);
                }
            }
            ab_append(ab, "\x1b[39m", 5);
            // ab_append(ab, c, len);
        }
        ab_append(ab, "\x1b[K", 3); // escape sequence to clear the screen
        if (y < config.scrnrows)
            ab_append(ab, "\r\n", 2);
    }
}
void editor_draw_statusbar(struct abuf *ab) {
    ab_append(ab, "\x1b[7m", 4);
    char lstatus[100], rstatus[100];
    int l_len =
        snprintf(lstatus, sizeof(lstatus), "\"%.20s\"%s | %d lines",
                 config.filename ? config.filename : "[Unknown]",
                 config.nmodifications > 0 ? " (modified)" : "", config.nrows);
    int r_len = snprintf(rstatus, sizeof(rstatus), "%s | %d : %d : %d",
                         config.syntax ? config.syntax->filetype : "no ft",
                         config.cy + 1, config.cx + 1, config.nrows);
    if (l_len > config.scrncols)
        l_len = config.scrncols;
    if (r_len > config.scrncols - l_len)
        r_len = config.scrncols - l_len;
    ab_append(ab, lstatus, l_len);
    while (l_len < config.scrncols) {
        if (config.scrncols - l_len == r_len) {
            ab_append(ab, rstatus, r_len);
            break;
        } else {
            ab_append(ab, " ", 1);
            l_len++;
        }
    }
    ab_append(ab, "\x1b[m", 3);
    ab_append(ab, "\r\n", 2);
}
void editor_draw_messagebar(struct abuf *ab) {
    ab_append(ab, "\x1b[K", 3);
    int msglen = strlen(config.status_msg);
    if (msglen > config.scrncols)
        msglen = config.scrncols;
    if (msglen && time(NULL) - config.status_msg_time < 1)
        ab_append(ab, config.status_msg, msglen);
}
void editor_set_status_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(config.status_msg, sizeof(config.status_msg), fmt, ap);
    va_end(ap);
    config.status_msg_time = time(NULL);
}
void editor_scroll() {
    config.rx = 0;
    if (config.cy < config.nrows)
        config.rx = editor_cx_to_rx(&config.row[config.cy], config.cx);

    if (config.cy < config.rowoff)
        config.rowoff = config.cy;
    if (config.cy >= config.rowoff + config.scrnrows)
        config.rowoff = config.cy - config.scrnrows + 1;

    if (config.rx < config.coloff)
        config.coloff = config.rx;
    if (config.rx >= config.coloff + config.scrncols)
        config.coloff = config.rx - config.scrncols + 1;
}
void editor_clear_scrn() {
    editor_scroll();
    struct abuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6); // hide the cursor when repainting
    ab_append(&ab, "\x1b[H", 3);    // escape sequence to move the cursor

    editor_draw_rows(&ab);
    editor_draw_statusbar(&ab);
    editor_draw_messagebar(&ab);
    char buf[100];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", config.cy - config.rowoff + 1,
             config.rx - config.coloff + 1); // updating cursor position

    ab_append(&ab, buf, strlen(buf));
    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

/*** input ***/
int editor_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // if(nread != -1 && errno != EAGAIN)
        // 	exit(0);
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
            read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] > '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) == -1)
                    return '\x1b';
                if (seq[2] == '~') {
                    /*
                     Switch statement for sequences ends with '~', e.g
                     PAGE UP KEY - <Esc>[5~
                     HOME    KEY - <Esc>[7~
                    */
                    switch (seq[1]) {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            } else {
                /*
                 Switch statement for sequences starts with '[', e.g
                 UP KEY   - <Esc>[A
                 HOME KEY - <Esc>[F
                */
                switch (seq[1]) {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'F':
                    return END_KEY;
                case 'H':
                    return HOME_KEY;
                default:
                    return '\x1b';
                }
            }
        } else if (seq[0] == 'O') {
            /*
             Switch statement for sequences starts with 'O', e.g
             END  KEY - <Esc>OF
             HOME KEY - <Esc>OH
            */
            switch (seq[1]) {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
    } else {
        switch (c) {
        case CTRL_KEY('h'):
            return ARROW_LEFT;
        case CTRL_KEY('j'):
            return ARROW_DOWN;
        case CTRL_KEY('k'):
            return ARROW_UP;
        case CTRL_KEY('l'):
            return ARROW_RIGHT;
        }
    }
    return c;
}
void editor_move_cursor(int k) {
    struct erow *current_row =
        (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    config.flag_mv_line = 0;
    switch (k) {
    case ARROW_LEFT:
        if (config.cx != 0)
            config.cx--;
        else if (config.cx == 0 && config.cy > 0) {
            config.cy--;
            config.cx = config.row[config.cy].size;
        }
        config.prevx = 0;
        break;
    case ARROW_RIGHT:
        if (current_row && config.cx < current_row->size)
            config.cx++;
        else if (config.cx == config.row[config.cy].size &&
                 config.cy < config.nrows - 1) {
            config.cy++;
            config.cx = 0;
        }
        config.prevx = 0;
        break;
    case ARROW_UP:
        if (config.cy != 0) {
            config.cy--;
            config.flag_mv_line = 1;
        }
        break;
    case ARROW_DOWN:
        if (config.cy < config.nrows - 1) {
            config.cy++;
            config.flag_mv_line = 1;
        }
        break;
    }
    // TODO: rewrite this shit code.
    if (config.cx > config.prevx)
        config.prevx = config.cx;
    current_row = (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    int rowlen = (current_row) ? current_row->size : 0;
    if(config.flag_mv_line/*  && (config.prevx > rowlen || config.prevx < rowlen) */)
        config.cx = config.prevx;
    if (config.flag_mv_line && config.cx > rowlen) {
        config.cx = rowlen;
    }
}
void editor_process_keypress() {
    int c = editor_read_key();
    struct erow *current_row =
        (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    switch (c) {
    case '\r':
        editor_insert_new_line();
        break;
    case CTRL_KEY('q'):
        if (config.nmodifications > 0) {
            char msg_buf[100];
            sprintf(msg_buf,
                    "You have %d unsaved changes. Do you really want to quit? "
                    "(y/n) ",
                    config.nmodifications);
            strcat(msg_buf, "%s");
            char *ans = editor_prompt(msg_buf, NULL);
            if (0 == strcmp(ans, "y"))
                editor_destroy();
        } else
            editor_destroy();
        break;
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
    case ARROW_DOWN:
        editor_move_cursor(c);
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        if (c == PAGE_UP) {
            config.cy = config.rowoff;
        } else if (c == PAGE_DOWN) {
            config.cy = config.scrnrows - 1 + config.rowoff;
            if (config.cy > config.nrows)
                config.cy = config.nrows - 1;
        }
        int times = config.scrnrows;
        while (times--) {
            editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    }

    case HOME_KEY:
        config.cx = 0;
        break;
    case END_KEY:
        if (current_row && config.cy < config.nrows)
            config.cx = current_row->size - 1;
        break;
    case BACKSPACE:
    case DEL_KEY:
        if (c == DEL_KEY)
            editor_move_cursor(ARROW_RIGHT);
        editor_del_char();
        break;
    case '\x1b':
        break;
    case CTRL_KEY('s'):
        editor_save();
        break;
    case CTRL_KEY('w'):
        editor_jmp_next_word();
        break;
    case CTRL_KEY('b'):
        editor_jmp_prev_word();
        break;
    case CTRL_KEY('e'):
        editor_jmp_line_boundaries(1);
        break;
    case CTRL_KEY('a'):
        editor_jmp_line_boundaries(0);
        break;
    case CTRL_KEY('g'): {
        char ch = editor_read_key();
        switch (ch) {
        case 's':
            config.cx = config.cy = 0;
            break;
        case 'e':
            config.cy = config.nrows - 1;
            break;
        case 'm':
            config.cy = config.nrows / 2;
            break;
        }
        break;
    }
    case CTRL_KEY('t'):
        editor_find();
        break;
    case CTRL_KEY('f'):
        editor_find();
        break;
    case CTRL_KEY('n'): {
        char ch = editor_read_key();
        switch (ch) {
        case 'n':
            if (config.current_search_match->next)
                config.current_search_match = config.current_search_match->next;
            break;
        case 'p':
            if (config.current_search_match->prev)
                config.current_search_match = config.current_search_match->prev;
            break;
        }
        if (config.current_search_match) {
            config.cy = config.current_search_match->cy;
            config.cx = editor_chrptr_to_cx(config.current_search_match->p);
        } else
            return;
        break;
    }
    default:
        editor_insert_char(c);
        break;
    }
}
int main(int argc, char **argv) {
    enable_raw_mode();
    editor_init();
    if (argc >= 2)
        editor_open(argv[1]);
    editor_set_status_msg("Ctrl-S = save | Ctrl-Q = quit");
    while (1) {
        editor_clear_scrn();
        editor_process_keypress();
    }
}
