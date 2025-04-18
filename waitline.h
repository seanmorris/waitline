# define PHP_WAITLINE_VERSION "0.0.0"

#define WAITLINE_MAX_INPUT 500

int waitline_consume_stdin_line(char *buf);
int waitline_real_consume_stdin_line(char *buf, int max_length);
