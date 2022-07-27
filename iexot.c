#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>

/* original state to apply it to the termial when the program exits*/
struct termios orig_termios;

void disable_raw_mode() { 
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios);
}
void enable_raw_mode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disable_raw_mode);

	struct termios raw = orig_termios;
	raw.c_oflag &= ~(OPOST);
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG );
	raw.c_lflag |= (CS8);

	tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw);
}
int main(int argc, char **argv) {
	enable_raw_mode();
	char c;
	while(read(STDIN_FILENO,&c,1)==1 && c!='q') {
		if(iscntrl(c))
			printf("%d\r\n",c);
		else
			printf("%d ('%c')\r\n",c,c);
	}
	return 0;
}
