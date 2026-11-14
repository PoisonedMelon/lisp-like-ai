; ---------------------------------------------------------------------------
; slip - memory, scratch buffer, errors, strings, lists, symbols
; ---------------------------------------------------------------------------

section .bss
heap_ptr:       resq 1
heap_end:       resq 1
sb_base:        resq 1          ; scratch buffer used for formatting / IO
sb_len:         resq 1
sb_cap:         resq 1
stack_limit:    resq 1
cur_builtin:    resq 1          ; builtin object currently executing (or 0)
symtab:         resq 4096       ; symbol intern hash buckets

section .text

; mmap_noreserve(rdi = size) -> rax = address, or negative errno
mmap_noreserve:
        mov     rsi, rdi
        xor     edi, edi
        mov     edx, 3                  ; PROT_READ | PROT_WRITE
        mov     r10d, 0x4022            ; MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE
        mov     r8, -1
        xor     r9d, r9d
        mov     eax, SYS_MMAP
        syscall
        ret

; map_region(rdi = preferred size) -> rax = base, rdx = size actually mapped.
; Halves the size on failure down to 64 MiB.
map_region:
        push    rbx
        mov     rbx, rdi
.try:
        mov     rdi, rbx
        call    mmap_noreserve
        cmp     rax, -4096
        jb      .ok
        shr     rbx, 1
        cmp     rbx, 64*1024*1024
        jae     .try
        DIE     "out of memory (mmap failed)"
.ok:
        mov     rdx, rbx
        pop     rbx
        ret

; Set up heap and scratch buffer. The machine stack is set up in _start.
init_memory:
        mov     rdi, 64*1024*1024*1024
        call    map_region
        mov     [rel heap_ptr], rax
        add     rax, rdx
        mov     [rel heap_end], rax
        mov     rdi, 4*1024*1024*1024
        call    map_region
        mov     [rel sb_base], rax
        mov     [rel sb_cap], rdx
        mov     qword [rel sb_len], 0
        ret

; alloc(rdi = bytes) -> rax = zeroed, 8-byte aligned memory.
; Clobbers only rax and rdi.
alloc:
        mov     rax, [rel heap_ptr]
        lea     rdi, [rax+rdi+7]
        and     rdi, -8
        cmp     rdi, [rel heap_end]
        ja      .oom
        mov     [rel heap_ptr], rdi
        ret
.oom:
        DIE     "out of memory"

; ---------------------------------------------------------------------------
; scratch buffer (sb). All sb_* helpers preserve every register except rax
; unless stated otherwise. Users must restore sb_len to the value they found
; it at, which makes nested use safe.
; ---------------------------------------------------------------------------

; sb_reserve(rdi = bytes) - make sure there is room for rdi more bytes
sb_reserve:
        mov     rax, [rel sb_len]
        add     rax, rdi
        jc      sb_overflow
        cmp     rax, [rel sb_cap]
        ja      sb_overflow
        ret
sb_overflow:
        DIE     "scratch buffer overflow (value too large)"

; sb_putc(dil = byte)
sb_putc:
        push    rcx
        mov     rax, [rel sb_len]
        cmp     rax, [rel sb_cap]
        jae     sb_overflow
        mov     rcx, [rel sb_base]
        mov     [rcx+rax], dil
        inc     rax
        mov     [rel sb_len], rax
        pop     rcx
        ret

; sb_puts(rsi = ptr, rdx = len)
sb_puts:
        push    rcx
        push    rsi
        push    rdi
        mov     rdi, rdx
        call    sb_reserve
        mov     rdi, [rel sb_base]
        add     rdi, [rel sb_len]
        mov     rcx, rdx
        rep movsb
        add     [rel sb_len], rdx
        pop     rdi
        pop     rsi
        pop     rcx
        ret

; sb_cstr(rdi = NUL terminated string)
sb_cstr:
        push    rsi
        push    rdx
        mov     rsi, rdi
        xor     edx, edx
.len:   cmp     byte [rsi+rdx], 0
        je      .go
        inc     rdx
        jmp     .len
.go:    call    sb_puts
        pop     rdx
        pop     rsi
        ret

; sb_int(rdi = raw signed integer)
sb_int:
        push    rcx
        push    rdx
        push    rsi
        push    r8
        sub     rsp, 40
        mov     rax, rdi
        lea     rsi, [rsp+32]
        xor     r8d, r8d
        test    rax, rax
        jns     .pos
        neg     rax
        mov     r8d, 1
.pos:   mov     ecx, 10
.dig:   xor     edx, edx
        div     rcx
        add     dl, '0'
        dec     rsi
        mov     [rsi], dl
        test    rax, rax
        jnz     .dig
        test    r8d, r8d
        jz      .out
        dec     rsi
        mov     byte [rsi], '-'
.out:   lea     rdx, [rsp+32]
        sub     rdx, rsi
        call    sb_puts
        add     rsp, 40
        pop     r8
        pop     rsi
        pop     rdx
        pop     rcx
        ret

; sb_take_str(rdi = start offset) -> rax = new string with sb[start..len];
; sb_len is reset to start. Clobbers caller-saved registers.
sb_take_str:
        push    rbx
        mov     rbx, rdi
        mov     rdx, [rel sb_len]
        sub     rdx, rbx
        mov     rsi, [rel sb_base]
        add     rsi, rbx
        call    make_str
        mov     [rel sb_len], rbx
        pop     rbx
        ret

; col_push(rdi = value) - append a value to a collection being built in sb
col_push:
        push    rdi
        mov     rdi, 8
        call    sb_reserve
        pop     rdi
        mov     rax, [rel sb_len]
        add     rax, [rel sb_base]
        mov     [rax], rdi
        add     qword [rel sb_len], 8
        ret

; col_list(rdi = start offset) -> rax = list of the values pushed since start
col_list:
        push    rbx
        push    r12
        sub     rsp, 8
        mov     rbx, rdi
        mov     r12, [rel sb_len]
        sub     r12, rbx
        shr     r12, 3
        mov     rdi, r12
        call    make_list
        mov     rdi, [rax+LIST_ITEMS]
        mov     rsi, [rel sb_base]
        add     rsi, rbx
        mov     rcx, r12
        rep movsq
        mov     [rel sb_len], rbx
        add     rsp, 8
        pop     r12
        pop     rbx
        ret

; write_all(edi = fd, rsi = buf, rdx = len) -> rax = bytes written or -errno
write_all:
        push    rbx
        push    r12
        push    r13
        mov     ebx, edi
        mov     r12, rsi
        mov     r13, rdx
        xor     ecx, ecx                ; written so far
.loop:  cmp     rcx, r13
        jae     .done
        push    rcx
        mov     eax, SYS_WRITE
        mov     edi, ebx
        lea     rsi, [r12+rcx]
        mov     rdx, r13
        sub     rdx, rcx
        syscall
        pop     rcx
        cmp     rax, -4                 ; EINTR
        je      .loop
        test    rax, rax
        js      .err
        add     rcx, rax
        jmp     .loop
.done:  mov     rax, rcx
.err:   pop     r13
        pop     r12
        pop     rbx
        ret

; ---------------------------------------------------------------------------
; errors (never return)
; ---------------------------------------------------------------------------

; flush sb[0..len] to stderr and exit(1)
die_flush:
        mov     dil, 10
        call    sb_putc
        mov     edi, 2
        mov     rsi, [rel sb_base]
        mov     rdx, [rel sb_len]
        call    write_all
        mov     eax, SYS_EXIT
        mov     edi, 1
        syscall

die_prefix:                             ; rdi preserved
        push    rdi
        mov     qword [rel sb_len], 0
        lea     rdi, [rel s_error]
        call    sb_cstr
        pop     rdi
        ret

; die(rdi = message)
die:
        call    die_prefix
        call    sb_cstr
        jmp     die_flush

; die_val(rdi = message, rsi = value)
die_val:
        push    rsi
        call    die_prefix
        call    sb_cstr
        lea     rdi, [rel s_colon]
        call    sb_cstr
        pop     rdi
        mov     esi, 1
        call    sb_value
        jmp     die_flush

bdie_name:
        call    die_prefix
        push    rdi
        mov     rax, [rel cur_builtin]
        test    rax, rax
        jz      .none
        mov     rdi, [rax+BI_NAME]
        call    sb_cstr
        lea     rdi, [rel s_colon]
        call    sb_cstr
.none:  pop     rdi
        ret

; bdie(rdi = message) - prefixed with the running builtin's name
bdie:
        call    bdie_name
        call    sb_cstr
        jmp     die_flush

; bdie_val(rdi = message, rsi = value)
bdie_val:
        push    rsi
        call    bdie_name
        call    sb_cstr
        lea     rdi, [rel s_colon]
        call    sb_cstr
        pop     rdi
        mov     esi, 1
        call    sb_value
        jmp     die_flush

bad_arity:
        BDIE    "wrong number of arguments"

; ---------------------------------------------------------------------------
; type helpers
; ---------------------------------------------------------------------------

; type_of(rdi = value) -> eax = T_* (preserves everything else)
type_of:
        test    dil, 1
        jnz     .int
        test    rdi, rdi
        jz      .nil
        mov     rax, [rdi]
        ret
.int:   mov     eax, T_INT
        ret
.nil:   mov     eax, T_NIL
        ret

; need_type(rdi = value, esi = type) -> rax = value, dies on mismatch.
; Preserves everything else.
need_type:
        call    type_of
        cmp     eax, esi
        jne     .bad
        mov     rax, rdi
        ret
.bad:   push    rdi
        call    bdie_name
        lea     rdi, [rel s_expected]
        call    sb_cstr
        lea     rax, [rel type_names]
        mov     rdi, [rax+rsi*8]
        call    sb_cstr
        lea     rdi, [rel s_got]
        call    sb_cstr
        pop     rdi
        mov     esi, 1
        call    sb_value
        jmp     die_flush

; truthy(rdi = value) -> eax = 0/1. Falsy: nil, false, 0, "", [], {}.
; Preserves everything else.
truthy:
        test    dil, 1
        jz      .ptr
        xor     eax, eax
        cmp     rdi, 1
        setne   al
        ret
.ptr:   test    rdi, rdi
        jz      .no
        mov     rax, [rdi]
        cmp     rax, T_BOOL
        je      .field
        cmp     rax, T_STR
        je      .field
        cmp     rax, T_LIST
        je      .field
        cmp     rax, T_HASH
        je      .field
        mov     eax, 1
        ret
.field: cmp     qword [rdi+8], 0
        setne   al
        movzx   eax, al
        ret
.no:    xor     eax, eax
        ret

; make_bool(eax = 0/1) -> rax = false/true object
make_bool:
        test    eax, eax
        lea     rax, [rel true_obj]
        jnz     .t
        lea     rax, [rel false_obj]
.t:     ret

; ---------------------------------------------------------------------------
; strings and lists
; ---------------------------------------------------------------------------

; alloc_str(rdi = len) -> rax = string with uninitialised (zero) bytes
; Clobbers rax, rdi.
alloc_str:
        push    rdi
        add     rdi, STR_DATA + 1
        call    alloc
        pop     rdi
        mov     qword [rax], T_STR
        mov     [rax+STR_LEN], rdi
        ret

; make_str(rsi = ptr, rdx = len) -> rax. Clobbers rax, rcx, rsi, rdi.
make_str:
        mov     rdi, rdx
        call    alloc_str
        lea     rdi, [rax+STR_DATA]
        mov     rcx, rdx
        rep movsb
        ret

; make_cstr_str(rdi = NUL terminated) -> rax string
make_cstr_str:
        mov     rsi, rdi
        xor     edx, edx
.len:   cmp     byte [rsi+rdx], 0
        je      make_str
        inc     rdx
        jmp     .len

; str_to_cstr(rdi = str) -> rax = freshly allocated NUL terminated copy
str_to_cstr:
        push    rbx
        mov     rbx, rdi
        mov     rdi, [rbx+STR_LEN]
        inc     rdi
        call    alloc
        mov     rdi, rax
        lea     rsi, [rbx+STR_DATA]
        mov     rcx, [rbx+STR_LEN]
        rep movsb
        pop     rbx
        ret

; str_eq_cstr(rdi = str, rsi = cstr) -> eax = 1 if equal
str_eq_cstr:
        mov     rcx, [rdi+STR_LEN]
        lea     rdx, [rdi+STR_DATA]
        xor     eax, eax
.l:     test    rcx, rcx
        jz      .end
        mov     r8b, [rsi]
        cmp     r8b, [rdx]
        jne     .no
        inc     rsi
        inc     rdx
        dec     rcx
        jmp     .l
.end:   cmp     byte [rsi], 0
        jne     .no
        mov     eax, 1
.no:    ret

; make_list(rdi = n) -> rax = list with n nil items. Clobbers rax, rdi.
make_list:
        push    rdi
        lea     rdi, [rdi*8+LIST_HDR]
        call    alloc
        pop     rdi
        mov     qword [rax], T_LIST
        mov     [rax+LIST_LEN], rdi
        lea     rdi, [rax+LIST_HDR]
        mov     [rax+LIST_ITEMS], rdi
        ret

; ---------------------------------------------------------------------------
; symbols
; ---------------------------------------------------------------------------

; intern(rsi = ptr, rdx = len) -> rax = unique symbol
intern:
        push    rbx
        push    r12
        push    r13
        mov     r12, rsi
        mov     r13, rdx
        ; FNV-1a
        mov     eax, 2166136261
        xor     ecx, ecx
.h:     cmp     rcx, r13
        jae     .hd
        movzx   edx, byte [r12+rcx]
        xor     eax, edx
        imul    eax, eax, 16777619
        inc     rcx
        jmp     .h
.hd:    and     eax, 4095
        lea     rbx, [rel symtab]
        lea     rbx, [rbx+rax*8]        ; rbx = bucket address
        mov     rax, [rbx]
.find:  test    rax, rax
        jz      .new
        cmp     [rax+SYM_LEN], r13
        jne     .next
        lea     rsi, [rax+SYM_DATA]
        mov     rdi, r12
        mov     rcx, r13
        repe cmpsb
        je      .done
.next:  mov     rax, [rax+SYM_NEXT]
        jmp     .find
.new:   lea     rdi, [r13+SYM_DATA+1]
        call    alloc
        mov     qword [rax], T_SYM
        mov     [rax+SYM_LEN], r13
        mov     qword [rax+SYM_VAL], UNBOUND
        mov     rcx, [rbx]
        mov     [rax+SYM_NEXT], rcx
        mov     [rbx], rax
        lea     rdi, [rax+SYM_DATA]
        mov     rsi, r12
        mov     rcx, r13
        rep movsb
.done:  pop     r13
        pop     r12
        pop     rbx
        ret

; intern_cstr(rdi = NUL terminated) -> rax
intern_cstr:
        mov     rsi, rdi
        xor     edx, edx
.len:   cmp     byte [rsi+rdx], 0
        je      intern
        inc     rdx
        jmp     .len

section .data
align 8
true_obj:       dq T_BOOL, 1
false_obj:      dq T_BOOL, 0

section .rodata
s_error:        db "error: ", 0
s_colon:        db ": ", 0
s_expected:     db "expected ", 0
s_got:          db ", got ", 0
tn_none:        db "?", 0
tn_str:         db "str", 0
tn_sym:         db "symbol", 0
tn_list:        db "list", 0
tn_hash:        db "hash", 0
tn_fn:          db "fn", 0
tn_builtin:     db "fn", 0
tn_file:        db "file", 0
tn_bool:        db "bool", 0
tn_int:         db "int", 0
tn_nil:         db "nil", 0

section .data
align 8
type_names:     dq tn_none, tn_str, tn_sym, tn_list, tn_hash, tn_fn
                dq tn_builtin, tn_file, tn_bool, tn_int, tn_nil

section .text
