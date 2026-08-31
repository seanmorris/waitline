--TEST--
waitline exposes the readline-compatible API
--EXTENSIONS--
waitline
--FILE--
<?php

$functions = [
    'readline',
    'readline_info',
    'readline_add_history',
    'readline_clear_history',
    'readline_list_history',
    'readline_read_history',
    'readline_write_history',
    'readline_completion_function',
    'readline_callback_handler_install',
    'readline_callback_read_char',
    'readline_callback_handler_remove',
    'readline_redisplay',
    'readline_on_new_line',
];

var_dump(READLINE_LIB);
var_dump(array_map('function_exists', $functions));

var_dump(readline_clear_history());
var_dump(readline_add_history('first'));
var_dump(readline_add_history(''));
var_dump(readline_list_history());

$historyFile = tempnam(sys_get_temp_dir(), 'waitline.');
var_dump(readline_write_history($historyFile));
var_dump(readline_clear_history());
var_dump(readline_read_history($historyFile));
var_dump(readline_list_history());
unlink($historyFile);

var_dump(readline_info('line_buffer', 'buffer'));
var_dump(readline_info('line_buffer'));
var_dump(readline_completion_function(static fn (): array => []));
var_dump(readline_callback_handler_install('', static function (): void {}));
var_dump(readline_callback_handler_remove());
var_dump(readline_callback_handler_remove());

?>
--EXPECT--
string(8) "waitline"
array(13) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(true)
  [3]=>
  bool(true)
  [4]=>
  bool(true)
  [5]=>
  bool(true)
  [6]=>
  bool(true)
  [7]=>
  bool(true)
  [8]=>
  bool(true)
  [9]=>
  bool(true)
  [10]=>
  bool(true)
  [11]=>
  bool(true)
  [12]=>
  bool(true)
}
bool(true)
bool(true)
bool(true)
array(2) {
  [0]=>
  string(5) "first"
  [1]=>
  string(0) ""
}
bool(true)
bool(true)
bool(true)
array(2) {
  [0]=>
  string(5) "first"
  [1]=>
  string(0) ""
}
string(0) ""
string(6) "buffer"
bool(true)
bool(true)
bool(true)
bool(false)
