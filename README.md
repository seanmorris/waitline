# waitline

`waitline` is the line-reader extension that makes interactive PHP shells usable inside `php-wasm`.
It replaces native blocking STDIN reads with an async JavaScript-backed line source, which is what browser-hosted CLI and `phpdbg` sessions need.

## What It Does

`waitline` hooks PHP CLI shell callbacks and waits for JavaScript to provide the next line of input.
That is how `php -a` style sessions and `phpdbg` prompts can work in environments where normal terminal I/O does not exist.

It also exposes the same PHP function names as `ext/readline`, backed by the
same JavaScript input queue used by the interactive CLI runtime. This lets
Wasm-hosted CLI applications call `readline()`, use history, and use the
callback API without linking a native terminal-editing library.

The extension remains named `waitline`, so `extension_loaded('waitline')` is
true while `extension_loaded('readline')` is false. Portable consumers should
feature-detect `readline()` when they need the function API.

## Readline-Compatible API

The following functions mirror PHP's `ext/readline` surface:

- `readline()` and `readline_info()`
- `readline_add_history()`, `readline_clear_history()`, and `readline_list_history()`
- `readline_read_history()` and `readline_write_history()`
- `readline_completion_function()`
- `readline_callback_handler_install()`, `readline_callback_read_char()`, and `readline_callback_handler_remove()`
- `readline_redisplay()` and `readline_on_new_line()`

`READLINE_LIB` is set to `waitline` so applications can distinguish the
line-oriented Wasm backend from GNU Readline and libedit.

The host transport submits complete lines rather than terminal keystrokes.
Accordingly, each `readline_callback_read_char()` call consumes one submitted
line, and completion callbacks are retained for API compatibility but are not
invoked by an in-Wasm terminal editor.

## Typical Usage

Most consumers do not use `waitline` directly.
The published `php-cli-wasm` and `php-dbg-wasm` style builds are expected to include it already.
You only need to think about `waitline` directly when you are maintaining or debugging custom builds.

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

php.addEventListener('stdin-request', async () => {
  const next = lines.shift();

  if (next !== undefined) {
    await php.provideInput(next);
  }
});

await php.run();
```

For `readline()` and callback-mode reads, the `stdin-request` event includes
the active PHP prompt as `event.detail.prompt`. Interactive shell reads that
do not supply their own prompt report `null`, so a host can retain its normal
shell prompt as the fallback.

## When You Need It

Use `waitline` when you are:

- building an interactive CLI runtime for the browser,
- wiring up `phpdbg` in a wasm-hosted environment, or
- debugging how `php-wasm` requests and consumes line-oriented input.

If you only use the standard published CLI/debug runtime packages, you usually do not need to think about this extension at all.

## Build Notes

Within the raw custom-build pipeline, this extension is controlled by:

```sh
WITH_WAITLINE=1
```

Important distinction:

- published CLI/debug artifacts generally enable `waitline`
- custom builder defaults still leave `WITH_WAITLINE` at `0` unless you turn it on

Useful build variables:

- `WITH_WAITLINE=1` enables the extension.
- `WAITLINE_BRANCH` overrides the git branch fetched during vendoring.
- `WAITLINE_DEV_PATH` points at a local checkout instead of cloning from GitHub.

`waitline_arginfo.h` is generated from `waitline.stub.php` with the newest
supported PHP branch's `build/gen_stub.php` (currently PHP 8.5). The PHP 8.5
generator is required because the stub preserves PHP 8.5's literal `true`
return types while retaining `bool` for PHP 8.0 through 8.4.

## Constraints

- `waitline` is designed for wasm-hosted PHP, not a normal native CLI install.
- Input is line-oriented. The integration expects complete lines, not arbitrary byte streams.
- The extension is valuable only when the surrounding runtime implements the JavaScript input side correctly.
