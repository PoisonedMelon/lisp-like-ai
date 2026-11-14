# slip

**slip** is a small, pure functional language with s-expression syntax and
Python-flavoured data types: integers, strings, lists and hashes. There is no
OO, no mutation and none of the classic Lisp baggage (no `car`/`cdr`/`cons`,
no quoting, no macros).

The interpreter is written entirely in x86-64 NASM assembly. It talks to Linux
directly through syscalls (no libc), is a single ~50 KB static binary, and runs
tail calls in constant stack *and* memory.

```lisp
(def greet [name] (str "Hello, " name "!"))
(print (greet "world"))                         ; Hello, world!

(def scores {"ann": 3, "bob": 5})
(print (put scores "cy" 7))                     ; {"ann": 3, "bob": 5, "cy": 7}
(print (map (fn [x] (* x x)) [1 2 3]))          ; [1, 4, 9]

(write "out.txt" "some text\n")                 ; create/replace a file
(print (lines "out.txt"))                       ; ["some text"]
```

## Building and running

Requires `nasm` and `ld` (binutils) on x86-64 Linux.

```sh
make                         # builds ./slip
./slip program.slip a b c    # run a program; `args` is ["a", "b", "c"]
./slip < program.slip        # or read the program from stdin
make test                    # run the test suite in tests/
```

See `examples/` for small programs (`hello.slip`, `wordcount.slip`,
`copy.slip`).

## Syntax

| Syntax            | Meaning                                              |
|-------------------|------------------------------------------------------|
| `(f a b)`         | call `f` with `a` and `b`                            |
| `[1 2 3]`         | list                                                 |
| `{"k": 1, "j": 2}`| hash (`:` and `,` are optional - they are whitespace)|
| `"text\n"`        | string; escapes `\n \t \r \0 \e \" \\`               |
| `42` `-7`         | integers (63-bit)                                    |
| `true false nil`  | constants                                            |
| `; ...` `# ...`   | comments to end of line                              |

Truthiness is Python-like: `nil`, `false`, `0`, `""`, `[]` and `{}` are falsy;
everything else is truthy.

## Special forms

```lisp
(def name value)              ; define a global
(def name [params] body...)   ; define a function
(fn [params] body...)         ; anonymous function / closure
(if c1 e1 c2 e2 ... else)     ; multi-branch if (if / elif / else)
(let [a 1 b (+ a 1)] body...) ; local bindings, evaluated in order
(do e1 e2 ...)                ; sequence, returns the last value
(and ...) (or ...)            ; short-circuit, return the deciding value
```

All data is immutable: functions like `push`, `put` and `del` return new
values. Loops are written with recursion; calls in tail position do not grow
the stack.

## Builtins

| Category    | Functions |
|-------------|-----------|
| arithmetic  | `+` (also concatenates strings and lists) `-` `*` `/` (floor division) `%` |
| comparison  | `=` `!=` `<` `>` `<=` `>=` (ints and strings, chainable) `not` |
| conversion  | `str` (display + concatenate) `repr` `int` `type` `len` |
| lists       | `list` `first` `last` `rest` `get` `push` `slice` `range` `reverse` `in` |
| functional  | `map` `filter` `reduce` (`(reduce f xs [init])`) |
| hashes      | `hash` `get` `put` `del` `keys` `values` `in` |
| strings     | `get` `slice` `split` `join` `strip` `reverse` `in` |
| IO          | `open` `close` `read` `readline` `lines` `write` `seek` `tell` `print` `exit` |

`get` takes an optional default: `(get h "missing" 0)`. Negative indexes count
from the end, as in Python: `(get xs -1)`, `(slice "hello" -3)`.

## File IO

Files are ordinary values. Anything that reads or writes a whole file also
accepts a path directly, so there is no need for open/close boilerplate in the
common case.

```lisp
(def f (open "data.txt" "w"))   ; modes: "r" (default) "w" "a" "r+" "w+" "a+"
(write f "line " 1 "\n")        ; writes the display form of each value,
                                ; returns the number of bytes written
(close f)

(read "data.txt")               ; whole file as a string
(lines "data.txt")              ; list of lines without "\n"
(write "data.txt" "replaced")   ; create/truncate and write by path

(def f (open "data.txt" "r+"))
(readline f)                    ; next line including "\n", "" at end of file
(read f 4)                      ; up to 4 bytes, "" at end of file
(read f)                        ; everything that is left
(seek f 0)                      ; move to an absolute position, returns it
(seek f -3 "end")               ; whence: "start" (default) "current" "end"
(tell f)                        ; current position
(close f)

(print "a" 1 [2])               ; shortcut for writing to stdout:
                                ; values separated by spaces, then "\n"
(write stdout "no newline")     ; stdin, stdout and stderr are files too
(open "missing.txt")            ; nil when a file cannot be opened
```

Since `""` is falsy, reading loops read naturally:

```lisp
(def show [f]
  (let [line (readline f)]
    (if line (do (write stdout line) (show f)))))
```

## Errors

Errors print a message to stderr and exit with status 1, e.g.

```
error: +: expected int, got "two"
error: unclosed bracket (line 12)
```

## Implementation notes

* `src/slip.asm` - entry point, globals and the builtin table
* `src/reader.inc` - parser, `src/eval.inc` - evaluator,
  `src/builtins.inc` / `src/io.inc` - builtins, `src/data.inc` - equality,
  hashing, hash tables and printing, `src/runtime.inc` - memory and helpers
* Integers are tagged immediates, so arithmetic never allocates. Hashes are
  open-addressing hash tables that keep insertion order.
* Memory comes from large lazily-committed `mmap` regions and is bump
  allocated. There is no garbage collector yet: values created by a program
  live until it exits (call frames of tail calls are reused, so plain loops
  run in constant memory).
