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

/* original state to apply it to the termial when the program exits*/
struct editor_config {
    unsigned scrnrows; 
    unsigned scrncols; 
    struct termios orig_termios;
} config;

/*** terminal ***/
int get_win_size(unsigned *rows, unsigned *cols) {
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0) { return -1;
    } else {
        *rows=ws.ws_row;
        *cols=ws.ws_col;
    }
    return 0;
}

void init() {
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
void draw_rows() {
	size_t y;
	for(y=1;y<config.scrnrows;++y) {
		char str[100];
		sprintf(str,"%3zu\r\n",y);
		if(write(STDOUT_FILENO,str,6) == -1)
			die("write rows");
	}
	// write(STDOUT_FILENO,"~\r\n",3);
}

void clear_scrn() {
	write(STDIN_FILENO,"\x1b[2J",4);
	write(STDIN_FILENO,"\x1b[H",3);
	draw_rows();
	write(STDIN_FILENO,"\x1b[H",3);
        write(STDIN_FILENO,"\x1b[4C",4);
}

/*** input ***/
char editor_read_key() {
	int nread;
	char c;
	while((nread=read(STDIN_FILENO,&c,1)) != 1) {
		// if(nread != -1 && errno != EAGAIN)
		// 	exit(0);
	}
	return c;
}
void editor_process_keypress() {
	char c = editor_read_key();
	switch (c) {
        case CTRL_KEY('q'):
			write(STDIN_FILENO,"\x1b[2J",4);
			write(STDIN_FILENO,"\x1b[H",3);
			exit(0);
			break;
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
