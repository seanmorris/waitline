/* waitline extension for PHP */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <emscripten.h>
#include <stdio.h>
#include "waitline.h"

#include "php.h"
#include "sapi/cli/cli.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"

#include "zend_execute.h"
#include "zend_highlight.h"
#include "zend_exceptions.h"

#if PHP_MAJOR_VERSION >= 8
# include "zend_attributes.h"
#else
# include <stdbool.h>
#endif

/* For compatibility with older PHP versions */
#ifndef ZEND_PARSE_PARAMETERS_NONE
#define ZEND_PARSE_PARAMETERS_NONE() \
	ZEND_PARSE_PARAMETERS_START(0, 0) \
	ZEND_PARSE_PARAMETERS_END()
#endif

#define GET_SHELL_CB(cb) \
	do { \
		(cb) = NULL; \
		cli_shell_callbacks_t *(*get_callbacks)(void); \
		get_callbacks = dlsym(RTLD_DEFAULT, "php_cli_get_shell_callbacks"); \
		if (get_callbacks) { \
			(cb) = get_callbacks(); \
		} \
	} while(0)

bool hasPipeIn  = 0;
bool hasPipeOut = 0;
bool hasPipeErr = 0;

EM_ASYNC_JS(int, waitline_real_consume_stdin_line, (char *buf, int max_length), {

	let input;

	Module.triggerStdin();

	if(Module.inputDataQueue.length)
	{
		input = String(Module.inputDataQueue.shift());
	}
	else
	{
		let a, r;
		const promise = new Promise((accept, reject) => [a, r] = [ accept, reject ]);
		Module.awaitingInput = a;
		input = String(await promise);
	}

	if(input.length >= max_length)
	{
		input = input.slice(0, max_length - 1);
	}

	stringToUTF8(input, buf, input.length + 1);

	return input.length;
});

int waitline_consume_stdin_line(char *buf)
{
	return waitline_real_consume_stdin_line(buf, WAITLINE_MAX_INPUT);
}

EM_ASYNC_JS(int, waitline_real_shell_write, (const char *str, size_t str_length), {
	const pipeTo = Module.pipeOut;

	console.log(pipeTo);

});

static size_t waitline_shell_write(const char *str, size_t str_length)
{
	// if(!hasPipeOut)
	// {
	// 	return fwrite(str, 1, str_length, stdout);
	// }

	// return waitline_real_shell_write(str, str_length);
	return -1;
}

static size_t waitline_shell_ub_write(const char *str, size_t str_length)
{
	return -1;
}

static int waitline_shell_run(void)
{
	char *line = emalloc(WAITLINE_MAX_INPUT);
	size_t size = 4096, pos = 0, len;
	char *code = emalloc(size);

	if (PG(auto_prepend_file) && PG(auto_prepend_file)[0]) {
		zend_file_handle prepend_file;

		zend_stream_init_filename(&prepend_file, PG(auto_prepend_file));
		zend_execute_scripts(ZEND_REQUIRE, NULL, 1, &prepend_file);
		zend_destroy_file_handle(&prepend_file);
	}

	EG(exit_status) = 0;

	while (waitline_consume_stdin_line(line) != NULL)
	{
		if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
		{
			free(line);
			break;
		}

		if (!pos && !*line)
		{
			free(line);
			continue;
		}

		len = strlen(line);

		if(line[0] == '#' && line[1] != '[')
		{
			char *param = strstr(&line[1], "=");

			if(param)
			{
				zend_string *cmd;
				param++;
				cmd = zend_string_init(&line[1], param - &line[1] - 1, 0);

				zend_alter_ini_entry_chars_ex(cmd, param, strlen(param), PHP_INI_USER, PHP_INI_STAGE_RUNTIME, 0);
				zend_string_release_ex(cmd, 0);
				continue;
			}
		}

		if(pos + len + 2 > size)
		{
			size = pos + len + 2;
			code = erealloc(code, size);
		}

		memcpy(&code[pos], line, len);
		pos += len;
		code[pos] = '\n';
		code[++pos] = '\0';

		memset(line, 0, WAITLINE_MAX_INPUT);

		zend_try
		{
			zend_eval_stringl(code, pos, NULL, "php shell code");

		} zend_end_try();

		pos = 0;

		php_write("\n", 1);

		if(EG(exception))
		{
			zend_exception_error(EG(exception), E_WARNING);
		}
	}

	efree(code);

	return EG(exit_status);
}

PHP_MINIT_FUNCTION(waitline)
{
	cli_shell_callbacks_t *cb;

	GET_SHELL_CB(cb);

	if(cb)
	{
		cb->cli_shell_write = waitline_shell_write;
		cb->cli_shell_ub_write = waitline_shell_ub_write;
		cb->cli_shell_run = waitline_shell_run;
	}

	hasPipeIn = EM_ASM_INT({ return !!Module.pipeIn; });
	hasPipeOut = EM_ASM_INT({ return !!Module.pipeOut; });
	hasPipeErr = EM_ASM_INT({ return !!Module.pipeErr; });

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(waitline)
{
	cli_shell_callbacks_t *cb;

	GET_SHELL_CB(cb);

	if(cb)
	{
		cb->cli_shell_write = NULL;
		cb->cli_shell_ub_write = NULL;
		cb->cli_shell_run = NULL;
	}

	return SUCCESS;
}

PHP_MINFO_FUNCTION(waitline)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Waitline Support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

zend_module_entry waitline_module_entry = {
	STANDARD_MODULE_HEADER,
	"waitline",
	NULL,                    /* zend_function_entry */
	PHP_MINIT(waitline),     /* PHP_MINIT - Module initialization */
	PHP_MSHUTDOWN(waitline), /* PHP_MSHUTDOWN - Module shutdown */
	NULL,                    /* PHP_RINIT - Request initialization */
	NULL,                    /* PHP_RSHUTDOWN - Request shutdown */
	PHP_MINFO(waitline),     /* PHP_MINFO - Module info */
	PHP_WAITLINE_VERSION,    /* Version */
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_WAITLINE
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(waitline)
#endif
