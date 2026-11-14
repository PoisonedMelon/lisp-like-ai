; ---------------------------------------------------------------------------
; slip - shared definitions
; ---------------------------------------------------------------------------
;
; Value representation (64-bit word):
;   xxxx...xxx1   tagged integer (63-bit signed, value = word >> 1)
;   0             nil
;   2             UNBOUND marker (only ever stored in a symbol's global slot)
;   other         pointer to a heap object; first qword is the type tag
;
; Heap objects are 8-byte aligned and never freed (bump allocation).

%define T_STR      1
%define T_SYM      2
%define T_LIST     3
%define T_HASH     4
%define T_FN       5
%define T_BUILTIN  6
%define T_FILE     7
%define T_BOOL     8
%define T_INT      9          ; pseudo type (tagged immediate)
%define T_NIL      10         ; pseudo type (0)

%define NIL        0
%define UNBOUND    2

; str:   [T_STR][len][bytes...]
%define STR_LEN     8
%define STR_DATA    16
; sym:   [T_SYM][len][global value][intern chain][special id][bytes...]
%define SYM_LEN     8
%define SYM_VAL     16
%define SYM_NEXT    24
%define SYM_SPECIAL 32
%define SYM_DATA    40
; list:  [T_LIST][len][items ptr]   (items usually inline right after header)
%define LIST_LEN    8
%define LIST_ITEMS  16
%define LIST_HDR    24
; hash:  [T_HASH][count][cap][entries ptr][index ptr]
;        entries: count * (key, value) in insertion order
;        index:   cap slots, each 0 (empty) or entry number + 1
%define HASH_COUNT   8
%define HASH_CAP     16
%define HASH_ENTRIES 24
%define HASH_INDEX   32
%define HASH_SIZE    40
; fn:    [T_FN][nparams][params ptr][body ptr][nbody][env][name sym]
%define FN_NPARAMS  8
%define FN_PARAMS   16
%define FN_BODY     24
%define FN_NBODY    32
%define FN_ENV      40
%define FN_NAME     48
%define FN_SIZE     56
; builtin: [T_BUILTIN][code ptr][name cstr]
%define BI_CODE     8
%define BI_NAME     16
%define BI_SIZE     24
; file:  [T_FILE][fd (-1 once closed)][name str]
%define FILE_FD     8
%define FILE_NAME   16
%define FILE_SIZE   24
; bool:  [T_BOOL][0|1]
%define BOOL_VAL    8
; environment frame: [parent][count][captured][sym0][val0][sym1][val1]...
; `captured` is set once a closure may reference the frame; uncaptured
; frames are reused by tail calls so loops run in constant memory.
%define ENV_PARENT   0
%define ENV_COUNT    8
%define ENV_CAPTURED 16
%define ENV_SLOTS    24

; special form ids (stored in SYM_SPECIAL)
%define SF_IF   1
%define SF_DEF  2
%define SF_FN   3
%define SF_LET  4
%define SF_DO   5
%define SF_AND  6
%define SF_OR   7

; Linux x86-64 syscalls
%define SYS_READ   0
%define SYS_WRITE  1
%define SYS_OPEN   2
%define SYS_CLOSE  3
%define SYS_LSEEK  8
%define SYS_MMAP   9
%define SYS_EXIT   60

%define O_RDONLY   0
%define O_WRONLY   1
%define O_RDWR     2
%define O_CREAT    0x40
%define O_TRUNC    0x200
%define O_APPEND   0x400

; die with "stack overflow" when the native stack is nearly exhausted
%macro STACK_CHECK 0
        cmp     rsp, [rel stack_limit]
        jb      stack_overflow
%endmacro

; reg := tagged(reg)
%macro TAGINT 1
        lea     %1, [%1*2+1]
%endmacro

; Fatal error helpers. DIE/DIEV report plain errors, BDIE/BDIEV prefix the
; message with the name of the builtin that is currently running.
%macro DIE 1
        [section .rodata]
%%msg:  db      %1, 0
        __SECT__
        lea     rdi, [rel %%msg]
        jmp     die
%endmacro

; value to show must be in rsi
%macro DIEV 1
        [section .rodata]
%%msg:  db      %1, 0
        __SECT__
        lea     rdi, [rel %%msg]
        jmp     die_val
%endmacro

%macro BDIE 1
        [section .rodata]
%%msg:  db      %1, 0
        __SECT__
        lea     rdi, [rel %%msg]
        jmp     bdie
%endmacro

%macro BDIEV 1
        [section .rodata]
%%msg:  db      %1, 0
        __SECT__
        lea     rdi, [rel %%msg]
        jmp     bdie_val
%endmacro

; Builtins are called with rdi = argc, rsi = argv (array of values) and
; return the result in rax. BI_ENTER saves all callee-saved registers,
; reserves three local qwords at [rsp], [rsp+8], [rsp+16] and loads
; r12 = argc, rbx = argv. Leave through `jmp bi_ret` with the result in rax.
%macro BI_ENTER 0
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 24
        mov     r12, rdi
        mov     rbx, rsi
%endmacro

%macro NARGS 1
        cmp     r12, %1
        jne     bad_arity
%endmacro

%macro NARGS_MIN 1
        cmp     r12, %1
        jb      bad_arity
%endmacro

%macro NARGS_RANGE 2
        cmp     r12, %1
        jb      bad_arity
        cmp     r12, %2
        ja      bad_arity
%endmacro

; NEED value, type  -> dies unless value has the type; rax = value
%macro NEED 2
        mov     rdi, %1
        mov     esi, %2
        call    need_type
%endmacro

; NEED_INT value -> rax = untagged integer
%macro NEED_INT 1
        mov     rdi, %1
        mov     esi, T_INT
        call    need_type
        sar     rax, 1
%endmacro
