/** includes ***/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "iexot.h"

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define IEXOT_VERSION "0.0.1"
#define IEXOT_TITLE_TOP_PADDING 3
// #define IEXOT_WIDTH 66
/* char *IEXOT_LABEL = " ___ _______  _____ _____   _____ ____ ___ _____ ___  ____\x1b[E\x1b[H11"\
          "|_ _| ____\\ \\/ / _ \\_   _| | ____|  _ \\_ _|_   _/ _ \\|  _ \\\x1b[E\x1b[H10"\
          " | ||  _|  \\  / | | || |   |  _| | | | | |  | || | | | |_) |\x1b[E\x1b[H10"\
          " | || |___ /  \\ |_| || |   | |___| |_| | |  | || |_| |  _ <\x1b[E\x1b[H10"\
          "|___|_____/_/\\_\\___/ |_|   |_____|____/___| |_| \\___/|_| \\_\\\x1b[E\x1b[H11"\
; */
enum keys {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
};
struct editor_config {
    int cx,cy;
    unsigned scrnrows; 
    unsigned scrncols; 
    struct termios orig_termios;
} config;

/*** append-buffer ***/
struct abuf {
    char *b;
    size_t len;
};
#define ABUF_INIT {NULL,0}
void ab_append(struct abuf *ab, const char *s, size_t len) {
    char *new = realloc(ab->b, ab->len+len);
    if(!new) return;
    memcpy(&new[ab->len],s,len);
    ab->b=new;
    ab->len+=len;
}
void ab_free(struct abuf *ab) { free(ab->b); }
/*** terminal ***/
int get_cursor_position(unsigned *rows, unsigned *cols) {
    char buf[32];
    unsigned i=0;
    if(write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;
    while(i<sizeof(buf)-1) {
        if(read(STDIN_FILENO,&buf[i],1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i]='\0';
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2],"%d;%d",rows,cols) != 2) return -1;
    return 0;
}

int get_win_size(unsigned *rows, unsigned *cols) {
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0) { 
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows,cols);
    } else {
        *rows=ws.ws_row;
        *cols=ws.ws_col;
        return 0;
    }
}
void init() {
    config.cx=config.cy=0;
    if(get_win_size(&config.scrnrows, &config.scrncols) == -1) die("get_win_size");
}
void die(const char *s) {
    write(STDIN_FILENO,"\x1b[2J",4);
	write(STDIN_FILENO,"\x1b[H",3);
	perror(s);
	exit(1);
}
void disable_raw_mode() { 
	if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&config.orig_termios) == -1)
			die("tcsetattr");
}
void enable_raw_mode() {
	if(tcgetattr(STDIN_FILENO, &config.orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_raw_mode);

	struct termios raw = config.orig_termios;
	raw.c_oflag &= ~(OPOST);
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG );
	raw.c_lflag |= (CS8);
	raw.c_cc[VMIN]=0;
	raw.c_cc[VTIME]=10;

	if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw) == -1)
		die("tcsetattr");
}

/*** output ***/
void draw_rows(struct abuf *ab) {
	size_t y;
	for(y=1;y<config.scrnrows;++y) {
        if(y==IEXOT_TITLE_TOP_PADDING) {
            char welcome[80];
            size_t welcomelen=sprintf(welcome,"IEXOT EDITOR VERSION %s",IEXOT_VERSION);
            if(welcomelen > config.scrncols)
                welcomelen = config.scrncols;
            int padding = (config.scrncols - welcomelen) / 2 - 1; // -1 because of tilda
            if(padding) {
                ab_append(ab,"~",1);
                padding--;
            }
            while(padding--) {
                ab_append(ab," ",1);
            }
            ab_append(ab,welcome,welcomelen);
        } else {
            ab_append(ab, "~", 1);
        }
        ab_append(ab, "\x1b[K", 3);     // escape sequence to clear the screen

        if(y<config.scrnrows-1)
            ab_append(ab, "\r\n", 2);
        }
}
void clear_scrn() {
    struct abuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6);     // hide the cursor when repainting
    ab_append(&ab, "\x1b[H", 3);        // escape sequence to move the cursor

	draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf),"\x1b[%d;%dH",config.cy+1,config.cx+1);

    ab_append(&ab,buf,strlen(buf));
    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO,ab.b,ab.len);
    ab_free(&ab);
}

/*** input ***/
int editor_read_key() {
	int nread;
	char c;
	while((nread=read(STDIN_FILENO,&c,1)) != 1) {
		// if(nread != -1 && errno != EAGAIN)
		// 	exit(0);
	}
    if(c=='\x1b') {
        char seq[3];
        if(read(STDIN_FILENO,&seq[0],1) != 1 || read(STDIN_FILENO,&seq[1],1) != 1) return '\x1b';
        if(seq[0] == '[') {
            if(seq[1] > '0' &&  seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2],1) == -1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '5' : return PAGE_UP;
                        case '6' : return PAGE_DOWN;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A' : return ARROW_UP;
                    case 'B' : return ARROW_DOWN;
                    case 'D' : return ARROW_LEFT;
                    case 'C' : return ARROW_RIGHT;
                    default  : return '\x1b';
                }
            }
        }
    }
	return c;
}
void editor_move_cursor(int k) {
    switch(k) {
        case ARROW_LEFT:
            if(config.cx!=0)
                config.cx--;
            break;
        case ARROW_RIGHT:
            if(config.cx!=config.scrncols-1)
                config.cx++;
            break;
        case ARROW_UP:
            if(config.cy!=0)
                config.cy--;
            break;
        case ARROW_DOWN:
            if(config.cy!=config.scrnrows-1)
                config.cy++;
            break;
    }
}
void editor_process_keypress() {
	int c = editor_read_key();
	switch (c) {
        case CTRL_KEY('q'):
			write(STDIN_FILENO,"\x1b[2J",4);
			write(STDIN_FILENO,"\x1b[H",3);
			exit(0);
			break;
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            editor_move_cursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times=config.scrnrows;
                while(times--) {
                    editor_move_cursor(c==PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            }

	}
}
int main(int argc, char **argv) {
	enable_raw_mode();
    init();
	while(1) {
		clear_scrn();
		editor_process_keypress();
	}
}
