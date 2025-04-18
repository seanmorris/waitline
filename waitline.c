#include <stdio.h>
#include "waitline.h"

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
	if(!hasPipeOut)
	{
		return fwrite(str, 1, str_length, stdout);
	}

	return waitline_real_shell_write(str, str_length);
}

static size_t waitline_shell_ub_write(const char *str, size_t str_length)
{
	return -1;
}

static int waitline_shell_run(void)
{
	return 0;
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

static const zend_module_dep waitline_deps[] = {
	ZEND_MOD_END
};

zend_module_entry waitline_module_entry = {
	STANDARD_MODULE_HEADER_EX, NULL,
	waitline_deps,
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
