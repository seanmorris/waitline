<?php

/** @generate-function-entries */

function readline(?string $prompt = null): string|false {}

/**
 * @param int|string|bool|null $value
 * @return array|int|string|bool|null
 */
function readline_info(?string $var_name = null, $value = null): mixed {}

#if PHP_VERSION_ID >= 80500
function readline_add_history(string $prompt): true {}
#else
function readline_add_history(string $prompt): bool {}
#endif

#if PHP_VERSION_ID >= 80500
function readline_clear_history(): true {}
#else
function readline_clear_history(): bool {}
#endif

function readline_list_history(): array {}

function readline_read_history(?string $filename = null): bool {}

function readline_write_history(?string $filename = null): bool {}

function readline_completion_function(callable $callback): bool {}

#if PHP_VERSION_ID >= 80500
function readline_callback_handler_install(string $prompt, callable $callback): true {}
#else
function readline_callback_handler_install(string $prompt, callable $callback): bool {}
#endif

function readline_callback_read_char(): void {}

function readline_callback_handler_remove(): bool {}

function readline_redisplay(): void {}

function readline_on_new_line(): void {}
