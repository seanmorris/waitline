/* waitline extension for PHP */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <dlfcn.h>
#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "php.h"
#include "main/fopen_wrappers.h"
#include "main/php_streams.h"
#include "sapi/cli/cli.h"
#include "ext/standard/info.h"

#include "zend_exceptions.h"
#include "zend_execute.h"
#include "zend_highlight.h"

#include "waitline.h"
#include "waitline_arginfo.h"

#if PHP_MAJOR_VERSION >= 8
# include "zend_attributes.h"
#else
# include <stdbool.h>
#endif

/* For compatibility with older PHP versions. */
#ifndef ZEND_PARSE_PARAMETERS_NONE
# define ZEND_PARSE_PARAMETERS_NONE() \
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
	} while (0)

#define WAITLINE_SAFE_STRING(value) ((value) ? (value) : "")
#define WAITLINE_LIBRARY_VERSION "waitline " PHP_WAITLINE_VERSION

static zval waitline_completion_callback;
static zval waitline_line_callback;

static char **waitline_history;
static size_t waitline_history_size;
static size_t waitline_history_capacity;

static char *waitline_line_buffer;
static size_t waitline_line_buffer_length;
static char *waitline_prompt;
static size_t waitline_prompt_length;
static char *waitline_readline_name;
static size_t waitline_readline_name_length;

static zend_long waitline_point;
static zend_long waitline_end;
static zend_long waitline_mark;
static zend_long waitline_done;
static zend_long waitline_pending_input;
static zend_long waitline_attempted_completion_over;
static unsigned char waitline_completion_append_character = ' ';
static bool waitline_completion_suppress_append;

bool hasPipeIn  = false;
bool hasPipeOut = false;
bool hasPipeErr = false;

static size_t waitline_write(const char *value, size_t value_length)
{
	return php_write((void *) value, value_length);
}

static void waitline_replace_buffer(
	char **target,
	size_t *target_length,
	const char *value,
	size_t value_length
)
{
	char *replacement = pemalloc(value_length + 1, true);

	if (value_length) {
		memcpy(replacement, value, value_length);
	}
	replacement[value_length] = '\0';

	if (*target) {
		pefree(*target, true);
	}

	*target = replacement;
	*target_length = value_length;
}

static void waitline_reset_callback(zval *callback)
{
	if (Z_TYPE_P(callback) != IS_UNDEF) {
		zval_ptr_dtor(callback);
		ZVAL_UNDEF(callback);
	}
}

static void waitline_clear_history_entries(void)
{
	size_t index;

	for (index = 0; index < waitline_history_size; index++) {
		pefree(waitline_history[index], true);
	}

	waitline_history_size = 0;
}

static void waitline_free_history(void)
{
	waitline_clear_history_entries();

	if (waitline_history) {
		pefree(waitline_history, true);
		waitline_history = NULL;
	}

	waitline_history_capacity = 0;
}

static void waitline_add_history_entry(const char *line, size_t line_length)
{
	char *entry;

	if (waitline_history_size == waitline_history_capacity) {
		size_t capacity = waitline_history_capacity ? waitline_history_capacity * 2 : 16;

		if (capacity < waitline_history_capacity || capacity > SIZE_MAX / sizeof(char *)) {
			zend_error_noreturn(E_ERROR, "waitline history is too large");
		}

		waitline_history = perealloc(
			waitline_history,
			capacity * sizeof(char *),
			true
		);
		waitline_history_capacity = capacity;
	}

	entry = pemalloc(line_length + 1, true);
	if (line_length) {
		memcpy(entry, line, line_length);
	}
	entry[line_length] = '\0';
	waitline_history[waitline_history_size++] = entry;
}

/*
 * PhpCliWeb and PhpCliNode both expose inputDataQueue/awaitingInput and resolve
 * awaitingInput from provideInput(). Keeping this transport in one place makes
 * readline() and the custom interactive shell consume input identically.
 */
EM_ASYNC_JS(char *, waitline_real_read_line, (const char *prompt, size_t prompt_length), {
	const queue = Array.isArray(Module.inputDataQueue)
		? Module.inputDataQueue
		: (Module.inputDataQueue = []);
	const currentPrompt = prompt ? UTF8ToString(prompt, prompt_length) : null;
	let input;

	if (queue.length) {
		input = queue.shift();
	} else {
		let resolveInput;
		const pendingInput = new Promise(resolve => {
			resolveInput = resolve;
		});

		Module.awaitingInput = resolveInput;

		if (Module.triggerStdin) {
			Module.triggerStdin(currentPrompt);
		} else {
			console.warn('Module does not implement `.triggerStdin()`');
		}

		Module.readyForInput && Module.readyForInput();
		input = await pendingInput;

		if (Module.awaitingInput === resolveInput) {
			Module.awaitingInput = null;
		}
	}

	if (input === null || input === undefined) {
		return 0;
	}

	input = String(input);

	/* readline(3) returns a line without its trailing line delimiter. */
	if (input.endsWith('\n')) {
		input = input.slice(0, -1);
		if (input.endsWith('\r')) {
			input = input.slice(0, -1);
		}
	}

	const inputLength = lengthBytesUTF8(input);
	const buffer = _malloc(inputLength + 1);

	if (!buffer) {
		return 0;
	}

	stringToUTF8(input, buffer, inputLength + 1);
	return buffer;
});

static char *waitline_read_line(
	const char *prompt,
	size_t prompt_length,
	zend_bool update_prompt
)
{
	char *line;

	if (prompt && update_prompt) {
		waitline_replace_buffer(
			&waitline_prompt,
			&waitline_prompt_length,
			prompt,
			prompt_length
		);
	}

	line = waitline_real_read_line(prompt, prompt_length);
	if (!line) {
		return NULL;
	}

	waitline_replace_buffer(
		&waitline_line_buffer,
		&waitline_line_buffer_length,
		line,
		strlen(line)
	);
	waitline_point = waitline_line_buffer_length;
	waitline_end = waitline_line_buffer_length;
	waitline_done = 0;

	return line;
}

int waitline_real_consume_stdin_line(char *buffer, int max_length)
{
	char *line;
	size_t line_length;
	size_t copy_length;

	if (!buffer || max_length <= 0) {
		return -1;
	}

	line = waitline_read_line(NULL, 0, 0);
	if (!line) {
		buffer[0] = '\0';
		return -1;
	}

	line_length = strlen(line);
	copy_length = line_length < (size_t) max_length
		? line_length
		: (size_t) max_length - 1;

	if (copy_length) {
		memcpy(buffer, line, copy_length);
	}
	buffer[copy_length] = '\0';
	free(line);

	return (int) copy_length;
}

int waitline_consume_stdin_line(char *buffer)
{
	return waitline_real_consume_stdin_line(buffer, WAITLINE_MAX_INPUT);
}

static size_t waitline_shell_write(const char *str, size_t str_length)
{
	return (size_t) -1;
}

static size_t waitline_shell_ub_write(const char *str, size_t str_length)
{
	return (size_t) -1;
}

static int waitline_shell_run(void)
{
	char *line = emalloc(WAITLINE_MAX_INPUT);
	size_t size = 4096, pos = 0, len;
	char *code = emalloc(size);
	int line_length;

	if (PG(auto_prepend_file) && PG(auto_prepend_file)[0]) {
		zend_file_handle prepend_file;

		zend_stream_init_filename(&prepend_file, PG(auto_prepend_file));
		zend_execute_scripts(ZEND_REQUIRE, NULL, 1, &prepend_file);
		zend_destroy_file_handle(&prepend_file);
	}

	EG(exit_status) = 0;

	while ((line_length = waitline_consume_stdin_line(line)) >= 0) {
		if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
			break;
		}

		if (!pos && line_length == 0) {
			continue;
		}

		len = (size_t) line_length;

		if (line[0] == '#' && line[1] != '[') {
			char *param = strstr(&line[1], "=");

			if (param) {
				zend_string *cmd;
				param++;
				cmd = zend_string_init(&line[1], param - &line[1] - 1, 0);

				zend_alter_ini_entry_chars_ex(
					cmd,
					param,
					strlen(param),
					PHP_INI_USER,
					PHP_INI_STAGE_RUNTIME,
					0
				);
				zend_string_release_ex(cmd, 0);
				continue;
			}
		}

		if (pos + len + 2 > size) {
			size = pos + len + 2;
			code = erealloc(code, size);
		}

		memcpy(&code[pos], line, len);
		pos += len;
		code[pos] = '\n';
		code[++pos] = '\0';

		zend_try {
			zend_eval_stringl(code, pos, NULL, "php shell code");
		} zend_end_try();

		pos = 0;
		waitline_write("\n", 1);

		if (EG(exception)) {
			zend_exception_error(EG(exception), E_WARNING);
		}
	}

	efree(line);
	efree(code);

	return EG(exit_status);
}

PHP_FUNCTION(readline)
{
	zend_string *prompt = NULL;
	char *line;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(prompt)
	ZEND_PARSE_PARAMETERS_END();

	line = waitline_read_line(
		prompt ? ZSTR_VAL(prompt) : NULL,
		prompt ? ZSTR_LEN(prompt) : 0,
		1
	);

	if (!line) {
		RETURN_FALSE;
	}

	RETVAL_STRING(line);
	free(line);
}

PHP_FUNCTION(readline_info)
{
	zend_string *what = NULL;
	zval *value = NULL;
	zend_long old_value;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|S!z!", &what, &value) == FAILURE) {
		RETURN_THROWS();
	}

	if (!what) {
		array_init(return_value);
		add_assoc_stringl(
			return_value,
			"line_buffer",
			WAITLINE_SAFE_STRING(waitline_line_buffer),
			waitline_line_buffer_length
		);
		add_assoc_long(return_value, "point", waitline_point);
		add_assoc_long(return_value, "end", waitline_end);
		add_assoc_long(return_value, "mark", waitline_mark);
		add_assoc_long(return_value, "done", waitline_done);
		add_assoc_long(return_value, "pending_input", waitline_pending_input);
		add_assoc_stringl(
			return_value,
			"prompt",
			WAITLINE_SAFE_STRING(waitline_prompt),
			waitline_prompt_length
		);
		add_assoc_string(return_value, "terminal_name", "");
		if (waitline_completion_append_character) {
			add_assoc_stringl(
				return_value,
				"completion_append_character",
				(char *) &waitline_completion_append_character,
				1
			);
		} else {
			add_assoc_string(return_value, "completion_append_character", "");
		}
		add_assoc_bool(
			return_value,
			"completion_suppress_append",
			waitline_completion_suppress_append
		);
		add_assoc_string(return_value, "library_version", WAITLINE_LIBRARY_VERSION);
		add_assoc_stringl(
			return_value,
			"readline_name",
			WAITLINE_SAFE_STRING(waitline_readline_name),
			waitline_readline_name_length
		);
		add_assoc_long(
			return_value,
			"attempted_completion_over",
			waitline_attempted_completion_over
		);
		return;
	}

	if (zend_string_equals_literal_ci(what, "line_buffer")) {
		RETVAL_STRINGL(
			WAITLINE_SAFE_STRING(waitline_line_buffer),
			waitline_line_buffer_length
		);
		if (value) {
			if (!try_convert_to_string(value)) {
				RETURN_THROWS();
			}
			waitline_replace_buffer(
				&waitline_line_buffer,
				&waitline_line_buffer_length,
				Z_STRVAL_P(value),
				Z_STRLEN_P(value)
			);
			waitline_end = waitline_line_buffer_length;
		}
	} else if (zend_string_equals_literal_ci(what, "point")) {
		RETVAL_LONG(waitline_point);
	} else if (zend_string_equals_literal_ci(what, "end")) {
		RETVAL_LONG(waitline_end);
	} else if (zend_string_equals_literal_ci(what, "mark")) {
		RETVAL_LONG(waitline_mark);
	} else if (zend_string_equals_literal_ci(what, "done")) {
		old_value = waitline_done;
		if (value) {
			waitline_done = zval_get_long(value);
		}
		RETVAL_LONG(old_value);
	} else if (zend_string_equals_literal_ci(what, "pending_input")) {
		old_value = waitline_pending_input;
		if (value) {
			if (!try_convert_to_string(value)) {
				RETURN_THROWS();
			}
			waitline_pending_input = Z_STRLEN_P(value)
				? (unsigned char) Z_STRVAL_P(value)[0]
				: 0;
		}
		RETVAL_LONG(old_value);
	} else if (zend_string_equals_literal_ci(what, "prompt")) {
		RETVAL_STRINGL(
			WAITLINE_SAFE_STRING(waitline_prompt),
			waitline_prompt_length
		);
	} else if (zend_string_equals_literal_ci(what, "terminal_name")) {
		RETVAL_EMPTY_STRING();
	} else if (zend_string_equals_literal_ci(what, "completion_suppress_append")) {
		bool old_suppress_append = waitline_completion_suppress_append;
		if (value) {
			waitline_completion_suppress_append = zend_is_true(value);
		}
		RETVAL_BOOL(old_suppress_append);
	} else if (zend_string_equals_literal_ci(what, "completion_append_character")) {
		unsigned char old_character = waitline_completion_append_character;
		if (value) {
			if (!try_convert_to_string(value)) {
				RETURN_THROWS();
			}
			waitline_completion_append_character = Z_STRLEN_P(value)
				? (unsigned char) Z_STRVAL_P(value)[0]
				: 0;
		}
		if (old_character) {
			RETVAL_STRINGL((char *) &old_character, 1);
		} else {
			RETVAL_EMPTY_STRING();
		}
	} else if (zend_string_equals_literal_ci(what, "library_version")) {
		RETVAL_STRING(WAITLINE_LIBRARY_VERSION);
	} else if (zend_string_equals_literal_ci(what, "readline_name")) {
		RETVAL_STRINGL(
			WAITLINE_SAFE_STRING(waitline_readline_name),
			waitline_readline_name_length
		);
		if (value) {
			if (!try_convert_to_string(value)) {
				RETURN_THROWS();
			}
			waitline_replace_buffer(
				&waitline_readline_name,
				&waitline_readline_name_length,
				Z_STRVAL_P(value),
				Z_STRLEN_P(value)
			);
		}
	} else if (zend_string_equals_literal_ci(what, "attempted_completion_over")) {
		old_value = waitline_attempted_completion_over;
		if (value) {
			waitline_attempted_completion_over = zval_get_long(value);
		}
		RETVAL_LONG(old_value);
	}
}

PHP_FUNCTION(readline_add_history)
{
	zend_string *line;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(line)
	ZEND_PARSE_PARAMETERS_END();

	waitline_add_history_entry(ZSTR_VAL(line), ZSTR_LEN(line));
	RETURN_TRUE;
}

PHP_FUNCTION(readline_clear_history)
{
	ZEND_PARSE_PARAMETERS_NONE();
	waitline_clear_history_entries();
	RETURN_TRUE;
}

PHP_FUNCTION(readline_list_history)
{
	size_t index;

	ZEND_PARSE_PARAMETERS_NONE();
	array_init(return_value);

	for (index = 0; index < waitline_history_size; index++) {
		add_next_index_string(return_value, waitline_history[index]);
	}
}

static zend_string *waitline_history_path(zend_string *filename)
{
	const char *home;

	if (filename) {
		return zend_string_copy(filename);
	}

	home = getenv("HOME");
	if (home && *home) {
		return strpprintf(0, "%s/.history", home);
	}

	return zend_string_init(".history", sizeof(".history") - 1, false);
}

PHP_FUNCTION(readline_read_history)
{
	zend_string *filename = NULL;
	zend_string *path;
	php_stream *stream;
	char *line;
	size_t line_length;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(filename)
	ZEND_PARSE_PARAMETERS_END();

	path = waitline_history_path(filename);
	if (php_check_open_basedir(ZSTR_VAL(path))) {
		zend_string_release(path);
		RETURN_FALSE;
	}

	stream = php_stream_open_wrapper(ZSTR_VAL(path), "rb", REPORT_ERRORS, NULL);
	zend_string_release(path);

	if (!stream) {
		RETURN_FALSE;
	}

	while ((line = php_stream_get_line(stream, NULL, 0, &line_length)) != NULL) {
		while (line_length && (
			line[line_length - 1] == '\n' || line[line_length - 1] == '\r'
		)) {
			line_length--;
		}

		waitline_add_history_entry(line, line_length);
		efree(line);
	}

	php_stream_close(stream);
	RETURN_TRUE;
}

PHP_FUNCTION(readline_write_history)
{
	zend_string *filename = NULL;
	zend_string *path;
	php_stream *stream;
	size_t index;
	bool success = true;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(filename)
	ZEND_PARSE_PARAMETERS_END();

	path = waitline_history_path(filename);
	if (php_check_open_basedir(ZSTR_VAL(path))) {
		zend_string_release(path);
		RETURN_FALSE;
	}

	stream = php_stream_open_wrapper(ZSTR_VAL(path), "wb", REPORT_ERRORS, NULL);
	zend_string_release(path);

	if (!stream) {
		RETURN_FALSE;
	}

	for (index = 0; index < waitline_history_size; index++) {
		size_t entry_length = strlen(waitline_history[index]);

		if (
			php_stream_write(stream, waitline_history[index], entry_length)
				!= (ssize_t) entry_length
			|| php_stream_write(stream, "\n", 1) != 1
		) {
			success = false;
			break;
		}
	}

	php_stream_close(stream);
	RETURN_BOOL(success);
}

PHP_FUNCTION(readline_completion_function)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "f", &fci, &fcc) == FAILURE) {
		RETURN_THROWS();
	}

	waitline_reset_callback(&waitline_completion_callback);
	ZVAL_COPY(&waitline_completion_callback, &fci.function_name);
	RETURN_TRUE;
}

PHP_FUNCTION(readline_callback_handler_install)
{
	char *prompt;
	size_t prompt_length;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	if (zend_parse_parameters(
		ZEND_NUM_ARGS(),
		"sf",
		&prompt,
		&prompt_length,
		&fci,
		&fcc
	) == FAILURE) {
		RETURN_THROWS();
	}

	waitline_reset_callback(&waitline_line_callback);
	ZVAL_COPY(&waitline_line_callback, &fci.function_name);
	waitline_replace_buffer(
		&waitline_prompt,
		&waitline_prompt_length,
		prompt,
		prompt_length
	);

	RETURN_TRUE;
}

PHP_FUNCTION(readline_callback_read_char)
{
	char *line;
	zval parameter;
	zval retval;

	ZEND_PARSE_PARAMETERS_NONE();

	if (Z_TYPE(waitline_line_callback) == IS_UNDEF) {
		return;
	}

	line = waitline_read_line(waitline_prompt, waitline_prompt_length, 0);
	if (line) {
		ZVAL_STRING(&parameter, line);
		free(line);
	} else {
		ZVAL_NULL(&parameter);
	}

	ZVAL_UNDEF(&retval);
	call_user_function(
		NULL,
		NULL,
		&waitline_line_callback,
		&retval,
		1,
		&parameter
	);
	zval_ptr_dtor(&parameter);

	if (Z_TYPE(retval) != IS_UNDEF) {
		zval_ptr_dtor(&retval);
	}

}

PHP_FUNCTION(readline_callback_handler_remove)
{
	ZEND_PARSE_PARAMETERS_NONE();

	if (Z_TYPE(waitline_line_callback) == IS_UNDEF) {
		RETURN_FALSE;
	}

	waitline_reset_callback(&waitline_line_callback);
	RETURN_TRUE;
}

PHP_FUNCTION(readline_redisplay)
{
	ZEND_PARSE_PARAMETERS_NONE();

	if (waitline_prompt_length) {
		waitline_write(waitline_prompt, waitline_prompt_length);
	}
	if (waitline_line_buffer_length) {
		waitline_write(waitline_line_buffer, waitline_line_buffer_length);
	}
}

PHP_FUNCTION(readline_on_new_line)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

PHP_MINIT_FUNCTION(waitline)
{
	cli_shell_callbacks_t *callbacks;

	ZVAL_UNDEF(&waitline_completion_callback);
	ZVAL_UNDEF(&waitline_line_callback);
	waitline_replace_buffer(&waitline_line_buffer, &waitline_line_buffer_length, "", 0);
	waitline_replace_buffer(&waitline_prompt, &waitline_prompt_length, "", 0);
	waitline_replace_buffer(
		&waitline_readline_name,
		&waitline_readline_name_length,
		"other",
		sizeof("other") - 1
	);

	REGISTER_STRING_CONSTANT("READLINE_LIB", "waitline", CONST_PERSISTENT);

	GET_SHELL_CB(callbacks);
	if (callbacks) {
		callbacks->cli_shell_write = waitline_shell_write;
		callbacks->cli_shell_ub_write = waitline_shell_ub_write;
		callbacks->cli_shell_run = waitline_shell_run;
	}

	hasPipeIn = EM_ASM_INT({ return !!Module.pipeIn; });
	hasPipeOut = EM_ASM_INT({ return !!Module.pipeOut; });
	hasPipeErr = EM_ASM_INT({ return !!Module.pipeErr; });

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(waitline)
{
	cli_shell_callbacks_t *callbacks;

	GET_SHELL_CB(callbacks);
	if (callbacks) {
		callbacks->cli_shell_write = NULL;
		callbacks->cli_shell_ub_write = NULL;
		callbacks->cli_shell_run = NULL;
	}

	waitline_reset_callback(&waitline_completion_callback);
	waitline_reset_callback(&waitline_line_callback);
	waitline_free_history();

	if (waitline_line_buffer) {
		pefree(waitline_line_buffer, true);
		waitline_line_buffer = NULL;
	}
	if (waitline_prompt) {
		pefree(waitline_prompt, true);
		waitline_prompt = NULL;
	}
	if (waitline_readline_name) {
		pefree(waitline_readline_name, true);
		waitline_readline_name = NULL;
	}

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(waitline)
{
	waitline_reset_callback(&waitline_completion_callback);
	waitline_reset_callback(&waitline_line_callback);
	return SUCCESS;
}

PHP_MINFO_FUNCTION(waitline)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Waitline Support", "enabled");
	php_info_print_table_row(2, "Readline-compatible API", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

zend_module_entry waitline_module_entry = {
	STANDARD_MODULE_HEADER,
	"waitline",
	ext_functions,
	PHP_MINIT(waitline),
	PHP_MSHUTDOWN(waitline),
	NULL,
	PHP_RSHUTDOWN(waitline),
	PHP_MINFO(waitline),
	PHP_WAITLINE_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_WAITLINE
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(waitline)
#endif
