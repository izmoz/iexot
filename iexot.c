#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include "iexot.h"

#define CTRL_KEY(k) ((k) & 0x1f)

/* original state to apply it to the termial when the program exits*/
struct termios orig_termios;

void die(const char *s) {
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
char editor_read_key() {
	int nread;
	char c;
	while((nread=read(STDIN_FILENO,&c,1)) != 1) {
		if(nread != -1 && errno != EAGAIN)
			die("read");
	}
	return c;
}
void editor_process_keypress() {
	char c = editor_read_key();
	switch (c) {
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}
int main(int argc, char **argv) {
	enable_raw_mode();
	while(1) {
		editor_process_keypress();
	}
	return 0;
}
