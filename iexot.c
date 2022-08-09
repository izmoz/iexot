/*current bugs:
 *  - blank lines throw seg fault
 */

/** includes ***/
#include "iexot.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k)&0x1f)
#define IEXOT_VERSION "0.0.1"
#define IEXOT_TITLE_TOP_PADDING 3
#define IEXOT_STATUS_BAR_PADDING 5

#define IEXOT_TAB_WIDTH 4

enum keys {
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
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;
void erow_free(erow *row) {
    if (!row)
        return;
    free(row->chars);
    free(row);
}
struct editor_config {
    int cx, cy, rx;
    char *filename;
    unsigned scrnrows;
    unsigned scrncols;
    unsigned nrows;
    unsigned rowoff;
    unsigned coloff;
    erow *row;
    struct termios orig_termios;
} config;

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
}
void editor_append_line(const char *s, size_t len) {
    config.row = realloc(config.row, sizeof(erow) * (config.nrows + 1));

    size_t at = config.nrows;
    config.row[at].size = len;
    config.row[at].chars = malloc(len + 1);
    memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = NULL;
    editor_update_row(&config.row[at]);

    config.nrows++;
}
/*** file i/o ***/
void editor_open(const char *filename) {
    free(config.filename);
    config.filename = strdup(filename);
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
        editor_append_line(line, linelen);
    }
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
    config.cx = config.cy = config.rx = 0;
    if (get_win_size(&config.scrnrows, &config.scrncols) == -1)
        die("get_win_size");
    config.scrnrows--; // decrementing 1 line for status bar
}
void editor_destroy() {
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);
    erow_free(config.row);
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
            } else {
                ab_append(ab, "\r\n", 1);
            }
        } else {
            int len = config.row[filerow].rsize - config.coloff;
            if (len < 0)
                len = 0;
            if (len > config.scrncols)
                len = config.scrncols;
            ab_append(ab, &config.row[filerow].render[config.coloff], len);
        }
        ab_append(ab, "\x1b[K", 3); // escape sequence to clear the screen

        if (y < config.scrnrows)
            ab_append(ab, "\r\n", 2);
    }
}
void editor_draw_statusbar(struct abuf *ab) {
    ab_append(ab,"\x1b[7m",4);
    char lstatus[100], rstatus[100];
    int l_len =
        snprintf(lstatus, sizeof(lstatus), "\"%.20s\" | %d lines",
                 config.filename ? config.filename : "[Unknown]", config.nrows);
    int r_len = snprintf(rstatus, sizeof(rstatus), "%d : %d : %d",config.cy+1,config.cx+1,config.nrows);
    if(l_len>config.scrncols)
        l_len=config.scrncols;
    if(r_len > config.scrncols-l_len)
        r_len=config.scrncols-l_len;
    ab_append(ab,lstatus,l_len);
    while(l_len<config.scrncols) {
        if(config.scrncols - l_len==r_len) {
            ab_append(ab,rstatus,r_len);
            break;
        } else {
            ab_append(ab," ",1);
            l_len++;
        }
    }
    ab_append(ab,"\x1b[m",3);
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
    }
    return c;
}
void editor_move_cursor(int k) {
    struct erow *current_row =
        (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    switch (k) {
    case ARROW_LEFT:
        if (config.cx != 0)
            config.cx--;
        else if (config.cx == 0 && config.cy > 0) {
            config.cy--;
            config.cx = config.row[config.cy].size - 1;
        }
        break;
    case ARROW_RIGHT:
        if (current_row && config.cx < current_row->size - 1)
            config.cx++;
        else if (config.cx == config.row[config.cy].size - 1 &&
                 config.cy < config.nrows - 1) {
            config.cy++;
            config.cx = 0;
        }
        break;
    case ARROW_UP:
        if (config.cy != 0)
            config.cy--;
        break;
    case ARROW_DOWN:
        if (config.cy < config.nrows - 1)
            config.cy++;
        break;
    }
    current_row = (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    int rowlen = (current_row) ? current_row->size - 1 : 0;
    if (config.cx > rowlen)
        config.cx = rowlen;
}
void editor_process_keypress() {
    int c = editor_read_key();
    struct erow *current_row =
        (config.cy >= config.nrows) ? NULL : &config.row[config.cy];
    switch (c) {
    case CTRL_KEY('q'):
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
    }
}
int main(int argc, char **argv) {
    enable_raw_mode();
    editor_init();
    if (argc >= 2)
        editor_open(argv[1]);
    while (1) {
        editor_clear_scrn();
        editor_process_keypress();
    }
}
