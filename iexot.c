/*** includes ***/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include "iexot.h"
#include <math.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define MAX_LINES_NUMBER 100

/* original state to apply it to the termial when the program exits*/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
	write(STDIN_FILENO,"\x1b[2J",4);
	write(STDIN_FILENO,"\x1b[H",3);

	perror(s);
	exit(1);
}
void disable_raw_mode() { 
	if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios) == -1)
		die("tcsetattr");
}
void enable_raw_mode() {
	if(tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_raw_mode);

	struct termios raw = orig_termios;
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
	for(y=1;y<MAX_LINES_NUMBER;++y) {
		char str[100];
		sprintf(str,"%zu\r\n",y);
		if(write(STDOUT_FILENO,str,6) == -1)
			die("write rows");
	}
	write(STDOUT_FILENO,"~\r\n",3);
}

void clear_scrn() {
	write(STDIN_FILENO,"\x1b[2J",4);
	write(STDIN_FILENO,"\x1b[H",3);
	draw_rows();
	write(STDIN_FILENO,"\x1b[H",3);
}

/*** input ***/
char editor_read_key() {
	int nread;
	char c;
	while((nread=read(STDIN_FILENO,&c,1)) != 1) {
		/* if(nread != -1 && errno != EAGAIN) */
			/* exit(0); */
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
	while(1) {
		clear_scrn();
		editor_process_keypress();
	}
	return 0;
}
