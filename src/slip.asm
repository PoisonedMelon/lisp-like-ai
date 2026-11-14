; ---------------------------------------------------------------------------
; slip - a small, pure functional s-expression language
;
; x86-64 Linux, no libc. Build: make   Usage: ./slip program.slip [args...]
; ---------------------------------------------------------------------------

bits 64
default rel

%include "defs.inc"

section .text
global _start

_start:
        mov     r12, [rsp]              ; argc
        lea     r13, [rsp+8]            ; argv
        call    init_memory
        ; run on a large private stack so deep (non-tail) recursion works
        mov     rdi, 1024*1024*1024
        call    map_region
        lea     rcx, [rax+65536]
        mov     [rel stack_limit], rcx
        lea     rsp, [rax+rdx]
        call    init_reader
        call    init_symbols
        call    init_builtins
        call    init_globals

        ; load the program: argv[1], or stdin when missing / "-"
        cmp     r12, 2
        jb      .stdin
        mov     rdi, [r13+8]
        cmp     word [rdi], '-'         ; "-\0"
        je      .stdin
        mov     rdi, [r13+8]
        xor     esi, esi
        mov     eax, SYS_OPEN
        syscall
        test    rax, rax
        js      .noopen
        mov     edi, eax
        call    read_all_fd
        jmp     .run
.stdin: xor     edi, edi
        call    read_all_fd
.run:   lea     rcx, [rax+STR_DATA]
        mov     [rel src_cur], rcx
        add     rcx, [rax+STR_LEN]
        mov     [rel src_end], rcx
        mov     qword [rel src_line], 1
.loop:  call    read_form
        cmp     edx, 1
        je      .exit
        cmp     edx, 2
        je      .stray
        mov     rdi, rax
        xor     esi, esi
        call    eval
        jmp     .loop
.exit:  mov     eax, SYS_EXIT
        xor     edi, edi
        syscall
.stray: lea     rdi, [rel s_unexpected]
        jmp     die_line
.noopen:
        call    die_prefix
        lea     rdi, [rel s_noprog]
        call    sb_cstr
        mov     rdi, [r13+8]
        call    sb_cstr
        jmp     die_flush

init_symbols:
        push    rbx
        lea     rbx, [rel special_names]
        mov     r8d, 1
.l:     mov     rdi, [rbx]
        test    rdi, rdi
        jz      .d
        push    r8
        call    intern_cstr
        pop     r8
        mov     [rax+SYM_SPECIAL], r8
        inc     r8d
        add     rbx, 8
        jmp     .l
.d:     lea     rdi, [rel s_true]
        call    intern_cstr
        mov     [rel sym_true], rax
        lea     rdi, [rel s_false]
        call    intern_cstr
        mov     [rel sym_false], rax
        lea     rdi, [rel tn_nil]
        call    intern_cstr
        mov     [rel sym_nil], rax
        pop     rbx
        ret

init_builtins:
        push    rbx
        push    r12
        sub     rsp, 8
        lea     rbx, [rel builtin_table]
.l:     mov     rdi, [rbx]
        test    rdi, rdi
        jz      .d
        mov     edi, BI_SIZE
        call    alloc
        mov     r12, rax
        mov     qword [r12], T_BUILTIN
        mov     rax, [rbx+8]
        mov     [r12+BI_CODE], rax
        mov     rdi, [rbx]
        mov     [r12+BI_NAME], rdi
        call    intern_cstr
        mov     [rax+SYM_VAL], r12
        add     rbx, 16
        jmp     .l
.d:     lea     rdi, [rel s_list]
        call    intern_cstr
        mov     rax, [rax+SYM_VAL]
        mov     [rel bi_list_obj], rax
        lea     rdi, [rel s_hash]
        call    intern_cstr
        mov     rax, [rax+SYM_VAL]
        mov     [rel bi_hash_obj], rax
        add     rsp, 8
        pop     r12
        pop     rbx
        ret

; def_global(rdi = name cstr, rsi = value)
def_global:
        push    rsi
        call    intern_cstr
        pop     rsi
        mov     [rax+SYM_VAL], rsi
        ret

; std_file(edi = fd, rsi = display name cstr, rdx = global name cstr)
std_file:
        push    rbx
        push    r12
        push    r13
        mov     ebx, edi
        mov     r13, rdx
        mov     rdi, rsi
        call    make_cstr_str
        mov     edi, ebx
        mov     rsi, rax
        call    make_file
        mov     rdi, r13
        mov     rsi, rax
        call    def_global
        pop     r13
        pop     r12
        pop     rbx
        ret

init_globals:
        push    rbx
        push    r14
        sub     rsp, 8
        xor     edi, edi
        lea     rsi, [rel s_stdin_d]
        lea     rdx, [rel s_stdin]
        call    std_file
        mov     edi, 1
        lea     rsi, [rel s_stdout_d]
        lea     rdx, [rel s_stdout]
        call    std_file
        mov     edi, 2
        lea     rsi, [rel s_stderr_d]
        lea     rdx, [rel s_stderr]
        call    std_file
        ; args: program arguments after the script path
        mov     r14, [rel sb_len]
        mov     rbx, 2
.a:     cmp     rbx, r12
        jae     .ad
        mov     rdi, [r13+rbx*8]
        call    make_cstr_str
        mov     rdi, rax
        call    col_push
        inc     rbx
        jmp     .a
.ad:    mov     rdi, r14
        call    col_list
        mov     rsi, rax
        lea     rdi, [rel s_args]
        call    def_global
        add     rsp, 8
        pop     r14
        pop     rbx
        ret

%include "runtime.inc"
%include "data.inc"
%include "reader.inc"
%include "eval.inc"
%include "builtins.inc"
%include "io.inc"

section .rodata
s_noprog:       db "cannot open program file: ", 0
s_list:         db "list", 0
s_hash:         db "hash", 0
s_args:         db "args", 0
s_stdin:        db "stdin", 0
s_stdout:       db "stdout", 0
s_stderr:       db "stderr", 0
s_stdin_d:      db "<stdin>", 0
s_stdout_d:     db "<stdout>", 0
s_stderr_d:     db "<stderr>", 0
sf_if:          db "if", 0
sf_def:         db "def", 0
sf_fn:          db "fn", 0
sf_let:         db "let", 0
sf_do:          db "do", 0
sf_and:         db "and", 0
sf_or:          db "or", 0

section .data
align 8
; order must match the SF_* ids in defs.inc
special_names:  dq sf_if, sf_def, sf_fn, sf_let, sf_do, sf_and, sf_or, 0

%macro BUILTIN 2
        dq      %%name, %2
        [section .rodata]
%%name: db      %1, 0
        __SECT__
%endmacro

builtin_table:
        BUILTIN "+", bi_add
        BUILTIN "-", bi_sub
        BUILTIN "*", bi_mul
        BUILTIN "/", bi_div
        BUILTIN "%", bi_mod
        BUILTIN "=", bi_eq
        BUILTIN "!=", bi_ne
        BUILTIN "<", bi_lt
        BUILTIN ">", bi_gt
        BUILTIN "<=", bi_le
        BUILTIN ">=", bi_ge
        BUILTIN "not", bi_not
        BUILTIN "type", bi_type
        BUILTIN "str", bi_str
        BUILTIN "repr", bi_repr
        BUILTIN "int", bi_int
        BUILTIN "len", bi_len
        BUILTIN "list", bi_list
        BUILTIN "first", bi_first
        BUILTIN "last", bi_last
        BUILTIN "rest", bi_rest
        BUILTIN "get", bi_get
        BUILTIN "push", bi_push
        BUILTIN "slice", bi_slice
        BUILTIN "range", bi_range
        BUILTIN "map", bi_map
        BUILTIN "filter", bi_filter
        BUILTIN "reduce", bi_reduce
        BUILTIN "reverse", bi_reverse
        BUILTIN "in", bi_in
        BUILTIN "hash", bi_hash
        BUILTIN "put", bi_put
        BUILTIN "del", bi_del
        BUILTIN "keys", bi_keys
        BUILTIN "values", bi_values
        BUILTIN "split", bi_split
        BUILTIN "join", bi_join
        BUILTIN "strip", bi_strip
        BUILTIN "open", bi_open
        BUILTIN "close", bi_close
        BUILTIN "read", bi_read
        BUILTIN "readline", bi_readline
        BUILTIN "lines", bi_lines
        BUILTIN "write", bi_write
        BUILTIN "seek", bi_seek
        BUILTIN "tell", bi_tell
        BUILTIN "print", bi_print
        BUILTIN "exit", bi_exit
        dq      0, 0
