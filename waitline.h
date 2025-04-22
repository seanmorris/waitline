/* waitline extension for PHP */

#ifndef PHP_WAITLINE_H
# define PHP_WAITLINE_H

# include "zend_API.h"

extern zend_module_entry waitline_module_entry;
# define phpext_waitline_ptr &waitline_module_entry

# define PHP_WAITLINE_VERSION "0.0.0"

# define WAITLINE_MAX_INPUT 500

# if defined(ZTS) && defined(COMPILE_DL_WAITLINE)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#endif

int waitline_consume_stdin_line(char *buf);
int waitline_real_consume_stdin_line(char *buf, int max_length);
