void init();
void die(const char *s);
void disable_raw_mode();
void enable_raw_mode();
 
int editor_read_key();
void editor_process_keypress();
void editor_destroy();
void editor_set_status_msg(const char *fmt, ...);
void editor_move_cursor(int k);
char *editor_prompt(char *prompt, void (*)(char *, int));
 
void editor_clear_scrn();
 
int get_win_size(unsigned *rows, unsigned *cols);
int get_cursor_position(unsigned *rows, unsigned *cols);