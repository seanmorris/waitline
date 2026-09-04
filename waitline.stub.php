<?php

/** @generate-function-entries */

/**
 * Reads one complete line from the host input transport.
 *
 * The prompt is forwarded to the host and is not written to PHP output.
 * The returned line does not include its trailing line delimiter.
 */
function readline(?string $prompt = null): string|false {}

/**
 * Gets or sets waitline's internal readline-compatible state.
 *
 * With no variable name, returns all supported state. When setting a writable
 * value, returns its previous value.
 *
 * @param int|string|bool|null $value
 * @return array|int|string|bool|null
 */
function readline_info(?string $var_name = null, $value = null): mixed {}

#if PHP_VERSION_ID >= 80500
/** Adds a line to the in-memory history. */
function readline_add_history(string $prompt): true {}
#else
/** Adds a line to the in-memory history. */
function readline_add_history(string $prompt): bool {}
#endif

#if PHP_VERSION_ID >= 80500
/** Removes every line from the in-memory history. */
function readline_clear_history(): true {}
#else
/** Removes every line from the in-memory history. */
function readline_clear_history(): bool {}
#endif

/**
 * Returns the in-memory history in insertion order.
 *
 * @return array<int, string>
 * @refcount 1
 */
function readline_list_history(): array {}

/**
 * Appends lines from a history file to the in-memory history.
 *
 * When no filename is supplied, uses $HOME/.history, or .history if HOME is
 * unavailable.
 */
function readline_read_history(?string $filename = null): bool {}

/**
 * Writes the complete in-memory history to a file, replacing its contents.
 *
 * When no filename is supplied, uses $HOME/.history, or .history if HOME is
 * unavailable.
 */
function readline_write_history(?string $filename = null): bool {}

/**
 * Registers a readline-compatible completion callback.
 *
 * Waitline receives complete lines from its host and has no in-Wasm terminal
 * editor, so it retains this callback for API compatibility but does not invoke
 * it automatically.
 */
function readline_completion_function(callable $callback): bool {}

#if PHP_VERSION_ID >= 80500
/**
 * Installs a callback that receives lines consumed by
 * readline_callback_read_char().
 */
function readline_callback_handler_install(string $prompt, callable $callback): true {}
#else
/**
 * Installs a callback that receives lines consumed by
 * readline_callback_read_char().
 */
function readline_callback_handler_install(string $prompt, callable $callback): bool {}
#endif

/**
 * Reads one complete host-submitted line and invokes the installed callback.
 *
 * The callback receives null when the host signals end-of-input.
 */
function readline_callback_read_char(): void {}

/**
 * Removes the installed line callback.
 *
 * Returns false when no callback is installed.
 */
function readline_callback_handler_remove(): bool {}

/** Writes the current prompt and line buffer to PHP output. */
function readline_redisplay(): void {}

/**
 * Marks a new logical input line for readline API compatibility.
 *
 * Waitline's complete-line host transport requires no additional action.
 */
function readline_on_new_line(): void {}
