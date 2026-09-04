# waitline

`waitline` is an Emscripten-only PHP extension that makes the interactive PHP
CLI and PHP's readline-compatible functions usable in browser and Node.js Wasm
runtimes. It replaces a native blocking terminal read with an asynchronous
JavaScript-backed, complete-line input transport.

The extension supports PHP 8.0 through PHP 8.5.

## What It Does

Waitline serves two related purposes:

- It installs PHP CLI shell callbacks so `php -a` can consume lines supplied by
  a JavaScript host.
- It exposes the public function surface of PHP's `ext/readline` without
  linking GNU Readline or libedit.

The extension module remains named `waitline`. Consequently,
`extension_loaded('waitline')` is true while `extension_loaded('readline')` is
false. Portable PHP code should test `function_exists('readline')` when it needs
the function API.

`READLINE_LIB` is set to `waitline`, allowing applications to distinguish this
complete-line Wasm backend from GNU Readline and libedit.

PHPDBG uses its own Wasm input bridge for debugger commands. Waitline may be
present in the same build, and PHP code can still call its functions there, but
waitline is not the implementation of the phpdbg command prompt.

## Readline-Compatible API

Waitline implements all 13 functions currently exposed by `ext/readline`.

### PHP Function Reference

- `readline(?string $prompt = null): string|false` waits for one complete line
  from the host. The optional prompt is delivered to the host, not written to
  PHP stdout. The returned line has no trailing line delimiter; `false` means
  the host signaled end-of-input.
- `readline_info(?string $var_name = null, $value = null): mixed` reads or
  changes readline-compatible state. With no variable name it returns every
  supported value. A writable value returns its previous setting when changed;
  an unknown variable name returns `null`.
- `readline_add_history(string $prompt)` copies one line, including an empty
  line, into the in-memory history and succeeds. Its declared return type is
  `bool` through PHP 8.4 and literal `true` on PHP 8.5.
- `readline_clear_history()` removes all in-memory history entries and
  succeeds. Its declared return type is `bool` through PHP 8.4 and literal
  `true` on PHP 8.5.
- `readline_list_history(): array` returns a new, numerically indexed array of
  history lines in insertion order.
- `readline_read_history(?string $filename = null): bool` reads a history file
  through PHP's stream layer, strips its line delimiters, and appends the lines
  to memory. It returns `false` when `open_basedir` rejects the path or the file
  cannot be opened.
- `readline_write_history(?string $filename = null): bool` replaces a history
  file with the current in-memory entries, writing one line per entry. It
  returns `false` when `open_basedir` rejects the path, the file cannot be
  opened, or a write fails.
- `readline_completion_function(callable $callback): bool` retains a completion
  callback and returns `true`. It exists for API compatibility; waitline has no
  character-level editor that invokes the callback automatically.
- `readline_callback_handler_install(string $prompt, callable $callback)`
  replaces the current line callback and retains its prompt. Its declared
  return type is `bool` through PHP 8.4 and literal `true` on PHP 8.5.
- `readline_callback_read_char(): void` consumes one complete host-submitted
  line and invokes the installed line callback. It does nothing when no handler
  is installed and passes `null` to the handler at end-of-input.
- `readline_callback_handler_remove(): bool` removes the installed line
  callback. It returns `true` when a callback was removed and `false` when none
  was installed.
- `readline_redisplay(): void` writes the retained prompt followed by the
  retained line buffer through PHP's output layer.
- `readline_on_new_line(): void` is a compatibility no-op because complete-line
  host input does not maintain a native terminal cursor.

### Line Input and Prompts

`readline()` returns one complete UTF-8 line without its trailing `\n` or
`\r\n`. It returns `false` when the host signals end-of-input.

Prompts are delivered to the JavaScript host rather than written to PHP stdout.
The host owns rendering the prompt, which prevents the browser terminal from
displaying it twice. A direct `readline('Name: ')` call supplies `Name: ` as the
current prompt. The custom interactive shell supplies `null`, allowing its host
to retain the normal `php> ` fallback.

### `readline_info()` State

Calling `readline_info()` without a variable name returns all supported state.
Writable values return their previous value when changed.

- Writable: `line_buffer`, `done`, `pending_input`,
  `completion_suppress_append`, `completion_append_character`, `readline_name`,
  and `attempted_completion_over`.
- Read-only: `point`, `end`, `mark`, `prompt`, `terminal_name`, and
  `library_version`.

Because waitline has no native terminal, `terminal_name` is an empty string and
`library_version` identifies waitline itself.

### History

History is retained in memory for the lifetime of the extension runtime.
`readline_read_history()` appends file entries to that in-memory list, while
`readline_write_history()` replaces the destination file with the current
list. Without an explicit filename, both functions use `$HOME/.history`, or
`.history` when `HOME` is unavailable. PHP stream wrappers and `open_basedir`
checks remain in effect.

### Callback and Editing Limitations

The host submits complete lines, not individual terminal keystrokes. Therefore:

- Each `readline_callback_read_char()` call consumes one complete submitted
  line and invokes the installed handler. The handler receives `null` at
  end-of-input.
- `readline_completion_function()` retains the callback for API compatibility,
  but there is no in-Wasm line editor to invoke it automatically.
- `readline_redisplay()` writes the retained prompt and line buffer through
  PHP's output layer.
- `readline_on_new_line()` is a compatibility no-op.

## JavaScript Host Contract

The Emscripten module uses the following host-facing fields:

- `Module.inputDataQueue`: queued complete input lines.
- `Module.awaitingInput`: resolver for a read currently waiting on the host.
- `Module.triggerStdin(prompt)`: notification that input is needed. The prompt
  is a string or `null`.
- `Module.readyForInput()`: optional compatibility notification invoked after
  `triggerStdin()`.

If a line is already queued, waitline consumes it immediately. Otherwise it
stores a resolver in `Module.awaitingInput`, calls `Module.triggerStdin()`, and
asynchronously waits for the host to resolve the read. A `null` or `undefined`
result signals end-of-input.

`PhpCliWeb` and `PhpCliNode` in `php-cli-wasm` initialize this contract. Their
`provideInput()` method accepts a line without a delimiter and appends the
newline expected by the module.

## Typical `php-cli-wasm` Usage

Most consumers do not install or call waitline directly. CLI builds include it
and expose input through the runtime wrapper:

```js
import { PhpCliWeb } from 'php-cli-wasm/PhpCliWeb';

const php = new PhpCliWeb({
  version: '8.4',
  interactive: true,
});

const lines = [
  'echo strtoupper("hello from php");',
  'exit',
];

php.addEventListener('stdin-request', async event => {
  const prompt = event.detail.prompt ?? 'php> ';
  const next = lines.shift();

  console.log(prompt);

  if (next !== undefined) {
    await php.provideInput(next);
  }
});

await php.run();
```

For an interactive terminal, render `event.detail.prompt` and call
`provideInput()` when the user submits the current line.

## Building with `php-wasm`

Raw custom builds leave waitline disabled by default. Enable it as a Make
setting or in the selected php-wasm environment file:

```make
WITH_WAITLINE=1
```

The related build settings are:

- `WITH_WAITLINE=1`: vendors and statically compiles the extension.
- `WAITLINE_BRANCH`: branch cloned from the upstream repository; defaults to
  `master`.
- `WAITLINE_DEV_PATH`: local source checkout copied into the build instead of
  cloning the upstream repository.

For example, from a php-wasm checkout:

```sh
make node-cli-mjs \
  PHP_VERSION=8.4 \
  LIB_TYPE=dynamic \
  WITH_WAITLINE=1 \
  WAITLINE_DEV_PATH=/absolute/path/to/waitline
```

Direct extension configuration uses `--enable-waitline`. The resulting module
must be compiled by Emscripten with Asyncify enabled because
`waitline_real_read_line()` is an `EM_ASYNC_JS` function that awaits host input.

## Generated Arginfo

`waitline_arginfo.h` is generated from `waitline.stub.php`; do not edit the
header by hand. Regenerate it with the newest supported PHP branch's
`build/gen_stub.php`, currently PHP 8.5:

```sh
php /path/to/php-8.5/build/gen_stub.php \
  --force-regeneration waitline.stub.php
```

The PHP 8.5 generator is required because the stub contains conditional literal
`true` return types for PHP 8.5 while preserving `bool` for older supported
versions.

## Tests

This repository contains `tests/readline_api.phpt`, which covers API exposure,
history persistence, `readline_info()`, completion registration, and callback
handler lifecycle.

The php-wasm repository adds runtime coverage in
`packages/waitline/test/basic.mjs` for Node.js input, Unicode, blank lines,
prompt delivery, callbacks, history, and continued interactive-shell operation.
Its demo-web Playwright suite also exercises the guided waitline browser demo.

After rebuilding the CLI artifact from a local checkout, the focused Node.js
suite can be run from php-wasm with:

```sh
PHP_VERSION=8.4 LIB_TYPE=dynamic \
  node --test packages/waitline/test/basic.mjs
```

Use the same `PHP_VERSION` and `LIB_TYPE` values that were used to build the
artifact under test.

## Constraints

- Waitline targets Emscripten/Wasm and is not a native terminal readline
  implementation.
- Asyncify and a host implementing the input contract are required.
- Input and callbacks are complete-line oriented; waitline provides no
  character-level editing or tab completion.
- The custom `php -a` bridge accepts at most `WAITLINE_MAX_INPUT - 1` bytes per
  submitted line (currently 499 bytes) and evaluates each submitted line
  independently.
