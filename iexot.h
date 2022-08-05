void init();
void die(const char *s);
void disable_raw_mode();
void enable_raw_mode();

int editor_read_key();
void editor_process_keypress();

void draw_rows();
void clear_scrn();

int get_win_size(unsigned *rows, unsigned *cols);
int get_cursor_position(unsigned *rows, unsigned *cols);
