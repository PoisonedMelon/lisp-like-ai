; ---------------------------------------------------------------------------
; slip - core builtins (arithmetic, comparison, strings, lists, hashes)
; ---------------------------------------------------------------------------

section .text

bi_ret:                                 ; shared epilogue for BI_ENTER
        add     rsp, 24
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

ret_nil:
        xor     eax, eax
        jmp     bi_ret
ret_true:
        lea     rax, [rel true_obj]
        jmp     bi_ret
ret_false:
        lea     rax, [rel false_obj]
        jmp     bi_ret

; ---- arithmetic -----------------------------------------------------------

; (+ a b ...) ints are summed, strings and lists are concatenated
bi_add:
        BI_ENTER
        test    r12, r12
        jz      .zero
        mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_INT
        je      .ints
        cmp     eax, T_STR
        je      .strs
        cmp     eax, T_LIST
        je      .lists
        mov     rsi, rdi
        BDIEV   "cannot add"
.zero:  mov     eax, 1
        jmp     bi_ret
.ints:  xor     r13d, r13d
        xor     r14d, r14d
.il:    cmp     r14, r12
        jae     .idone
        NEED_INT [rbx+r14*8]
        add     r13, rax
        inc     r14
        jmp     .il
.idone: mov     rax, r13
        TAGINT  rax
        jmp     bi_ret
.strs:  mov     ebp, T_STR
        jmp     .cat
.lists: mov     ebp, T_LIST
.cat:   xor     r13d, r13d              ; total length
        xor     r14d, r14d
.cl:    cmp     r14, r12
        jae     .calloc
        NEED    [rbx+r14*8], ebp
        add     r13, [rax+8]
        inc     r14
        jmp     .cl
.calloc:
        mov     rdi, r13
        cmp     ebp, T_STR
        jne     .clist
        call    alloc_str
        mov     r15, rax
        lea     rdi, [rax+STR_DATA]
        xor     r14d, r14d
.cs:    cmp     r14, r12
        jae     .cdone
        mov     rax, [rbx+r14*8]
        lea     rsi, [rax+STR_DATA]
        mov     rcx, [rax+STR_LEN]
        rep movsb
        inc     r14
        jmp     .cs
.clist: call    make_list
        mov     r15, rax
        mov     rdi, [rax+LIST_ITEMS]
        xor     r14d, r14d
.clc:   cmp     r14, r12
        jae     .cdone
        mov     rax, [rbx+r14*8]
        mov     rsi, [rax+LIST_ITEMS]
        mov     rcx, [rax+LIST_LEN]
        rep movsq
        inc     r14
        jmp     .clc
.cdone: mov     rax, r15
        jmp     bi_ret

; (- a) negates, (- a b c ...) subtracts
bi_sub:
        BI_ENTER
        NARGS_MIN 1
        NEED_INT [rbx]
        mov     r13, rax
        cmp     r12, 1
        jne     .multi
        neg     r13
        jmp     .done
.multi: mov     r14, 1
.l:     cmp     r14, r12
        jae     .done
        NEED_INT [rbx+r14*8]
        sub     r13, rax
        inc     r14
        jmp     .l
.done:  mov     rax, r13
        TAGINT  rax
        jmp     bi_ret

bi_mul:
        BI_ENTER
        mov     r13d, 1
        xor     r14d, r14d
.l:     cmp     r14, r12
        jae     .done
        NEED_INT [rbx+r14*8]
        imul    r13, rax
        inc     r14
        jmp     .l
.done:  mov     rax, r13
        TAGINT  rax
        jmp     bi_ret

; integer division / modulo with Python (floor) semantics
div_args:                               ; -> r13 = a, rcx = b
        NARGS   2
        NEED_INT [rbx]
        mov     r13, rax
        NEED_INT [rbx+8]
        mov     rcx, rax
        test    rcx, rcx
        jz      .zero
        ret
.zero:  BDIE    "division by zero"

bi_div:
        BI_ENTER
        call    div_args
        mov     rax, r13
        cqo
        idiv    rcx
        test    rdx, rdx
        jz      .ok
        xor     rdx, rcx
        jns     .ok
        dec     rax
.ok:    TAGINT  rax
        jmp     bi_ret

bi_mod:
        BI_ENTER
        call    div_args
        mov     rax, r13
        cqo
        idiv    rcx
        mov     rax, rdx
        test    rdx, rdx
        jz      .ok
        xor     rdx, rcx
        jns     .ok
        add     rax, rcx
.ok:    TAGINT  rax
        jmp     bi_ret

; ---- comparison -----------------------------------------------------------

bi_eq:
        BI_ENTER
        mov     r14, 1
.l:     cmp     r14, r12
        jae     ret_true
        mov     rdi, [rbx]
        mov     rsi, [rbx+r14*8]
        call    equal
        test    eax, eax
        jz      ret_false
        inc     r14
        jmp     .l

bi_ne:
        BI_ENTER
        NARGS   2
        mov     rdi, [rbx]
        mov     rsi, [rbx+8]
        call    equal
        xor     eax, 1
        call    make_bool
        jmp     bi_ret

bi_lt:
        BI_ENTER
        xor     r13d, r13d
        jmp     cmp_chain
bi_gt:
        BI_ENTER
        mov     r13d, 1
        jmp     cmp_chain
bi_le:
        BI_ENTER
        mov     r13d, 2
        jmp     cmp_chain
bi_ge:
        BI_ENTER
        mov     r13d, 3
; r13 = 0 <, 1 >, 2 <=, 3 >=
cmp_chain:
        NARGS_MIN 1
        mov     r14, 1
.l:     cmp     r14, r12
        jae     ret_true
        mov     rdi, [rbx+r14*8-8]
        mov     rsi, [rbx+r14*8]
        call    compare
        cmp     r13d, 1
        jb      .lt
        je      .gt
        cmp     r13d, 2
        je      .le
        cmp     eax, 0
        jl      ret_false
        jmp     .next
.lt:    cmp     eax, 0
        jge     ret_false
        jmp     .next
.gt:    cmp     eax, 0
        jle     ret_false
        jmp     .next
.le:    cmp     eax, 0
        jg      ret_false
.next:  inc     r14
        jmp     .l

bi_not:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    truthy
        xor     eax, 1
        call    make_bool
        jmp     bi_ret

; ---- conversion -----------------------------------------------------------

bi_type:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    type_of
        lea     rcx, [rel type_names]
        mov     rdi, [rcx+rax*8]
        call    make_cstr_str
        jmp     bi_ret

; (str a b ...) -> display form of all arguments concatenated
bi_str:
        BI_ENTER
        mov     r13, [rel sb_len]
        xor     r14d, r14d
.l:     cmp     r14, r12
        jae     .done
        mov     rdi, [rbx+r14*8]
        xor     esi, esi
        call    sb_value
        inc     r14
        jmp     .l
.done:  mov     rdi, r13
        call    sb_take_str
        jmp     bi_ret

; (repr x) -> source-like representation
bi_repr:
        BI_ENTER
        NARGS   1
        mov     r13, [rel sb_len]
        mov     rdi, [rbx]
        mov     esi, 1
        call    sb_value
        mov     rdi, r13
        call    sb_take_str
        jmp     bi_ret

; (int x) -> integer from int, bool or decimal string
bi_int:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_INT
        je      .same
        cmp     eax, T_BOOL
        je      .bool
        cmp     eax, T_STR
        je      .str
        mov     rsi, rdi
        BDIEV   "cannot convert to int"
.same:  mov     rax, rdi
        jmp     bi_ret
.bool:  mov     rax, [rdi+BOOL_VAL]
        TAGINT  rax
        jmp     bi_ret
.str:   lea     rsi, [rdi+STR_DATA]
        mov     rcx, [rdi+STR_LEN]
        add     rcx, rsi                ; end
.ws1:   cmp     rsi, rcx                ; skip leading whitespace
        jae     .bad
        movzx   eax, byte [rsi]
        call    is_space
        jne     .sign
        inc     rsi
        jmp     .ws1
.sign:  xor     r8d, r8d
        cmp     byte [rsi], '-'
        jne     .plus
        mov     r8d, 1
        inc     rsi
        jmp     .first
.plus:  cmp     byte [rsi], '+'
        jne     .first
        inc     rsi
.first: cmp     rsi, rcx
        jae     .bad
        movzx   eax, byte [rsi]
        sub     eax, '0'
        cmp     eax, 9
        ja      .bad
        xor     edx, edx
.dl:    cmp     rsi, rcx
        jae     .end
        movzx   eax, byte [rsi]
        sub     eax, '0'
        cmp     eax, 9
        ja      .tail
        imul    rdx, rdx, 10
        add     rdx, rax
        inc     rsi
        jmp     .dl
.tail:  movzx   eax, byte [rsi]         ; only trailing whitespace allowed
        call    is_space
        jne     .bad
        inc     rsi
        cmp     rsi, rcx
        jb      .tail
.end:   mov     rax, rdx
        test    r8d, r8d
        jz      .pos
        neg     rax
.pos:   TAGINT  rax
        jmp     bi_ret
.bad:   mov     rsi, [rbx]
        BDIEV   "invalid integer"

; is_space(al) -> ZF set if whitespace
is_space:
        cmp     al, ' '
        je      .y
        cmp     al, 9
        je      .y
        cmp     al, 10
        je      .y
        cmp     al, 13
        je      .y
        cmp     al, 11
        je      .y
        cmp     al, 12
.y:     ret

; (len x) for str, list, hash
bi_len:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_STR
        je      .ok
        cmp     eax, T_LIST
        je      .ok
        cmp     eax, T_HASH
        je      .ok
        mov     rsi, rdi
        BDIEV   "value has no length"
.ok:    mov     rax, [rdi+8]
        TAGINT  rax
        jmp     bi_ret

; ---- lists ----------------------------------------------------------------

bi_list:
        BI_ENTER
        mov     rdi, r12
        call    make_list
        mov     rdi, [rax+LIST_ITEMS]
        mov     rsi, rbx
        mov     rcx, r12
        rep movsq
        jmp     bi_ret

bi_first:
        BI_ENTER
        NARGS   1
        NEED    [rbx], T_LIST
        cmp     qword [rax+LIST_LEN], 0
        je      ret_nil
        mov     rax, [rax+LIST_ITEMS]
        mov     rax, [rax]
        jmp     bi_ret

bi_last:
        BI_ENTER
        NARGS   1
        NEED    [rbx], T_LIST
        mov     rcx, [rax+LIST_LEN]
        test    rcx, rcx
        jz      ret_nil
        mov     rax, [rax+LIST_ITEMS]
        mov     rax, [rax+rcx*8-8]
        jmp     bi_ret

; (rest xs) -> everything but the first element (shares storage, O(1))
bi_rest:
        BI_ENTER
        NARGS   1
        NEED    [rbx], T_LIST
        cmp     qword [rax+LIST_LEN], 1
        jb      bi_ret                  ; empty stays empty
        mov     r13, rax
        mov     edi, LIST_HDR
        call    alloc
        mov     qword [rax], T_LIST
        mov     rcx, [r13+LIST_LEN]
        dec     rcx
        mov     [rax+LIST_LEN], rcx
        mov     rcx, [r13+LIST_ITEMS]
        add     rcx, 8
        mov     [rax+LIST_ITEMS], rcx
        jmp     bi_ret

; normalise_index(rax = index, rcx = len) -> rax in [0, len) or CF set
; (negative indexes count from the end)
normalise_index:
        test    rax, rax
        jns     .chk
        add     rax, rcx
        js      .out
.chk:   cmp     rax, rcx
        jae     .out
        clc
        ret
.out:   stc
        ret

; (get coll key [default]) - list/str by index, hash by key
bi_get:
        BI_ENTER
        NARGS_RANGE 2, 3
        xor     r15d, r15d              ; default
        cmp     r12, 3
        jne     .nodef
        mov     r15, [rbx+16]
.nodef: mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_LIST
        je      .list
        cmp     eax, T_STR
        je      .str
        cmp     eax, T_HASH
        je      .hash
        cmp     eax, T_NIL
        je      .def
        mov     rsi, rdi
        BDIEV   "cannot index"
.list:  NEED_INT [rbx+8]
        mov     rdi, [rbx]
        mov     rcx, [rdi+LIST_LEN]
        call    normalise_index
        jc      .def
        mov     rcx, [rdi+LIST_ITEMS]
        mov     rax, [rcx+rax*8]
        jmp     bi_ret
.str:   NEED_INT [rbx+8]
        mov     rdi, [rbx]
        mov     rcx, [rdi+STR_LEN]
        call    normalise_index
        jc      .def
        lea     rsi, [rdi+STR_DATA+rax]
        mov     edx, 1
        call    make_str
        jmp     bi_ret
.hash:  mov     rsi, [rbx+8]
        call    hash_find
        test    rax, rax
        js      .def
        mov     rcx, [rbx]
        mov     rcx, [rcx+HASH_ENTRIES]
        shl     rax, 4
        mov     rax, [rcx+rax+8]
        jmp     bi_ret
.def:   mov     rax, r15
        jmp     bi_ret

; (push xs a b ...) -> new list with the values appended
bi_push:
        BI_ENTER
        NARGS_MIN 1
        NEED    [rbx], T_LIST
        mov     r13, rax
        mov     rdi, [r13+LIST_LEN]
        add     rdi, r12
        dec     rdi
        call    make_list
        mov     r14, rax
        mov     rdi, [rax+LIST_ITEMS]
        mov     rsi, [r13+LIST_ITEMS]
        mov     rcx, [r13+LIST_LEN]
        rep movsq
        lea     rsi, [rbx+8]
        mov     rcx, r12
        dec     rcx
        rep movsq
        mov     rax, r14
        jmp     bi_ret

; clamp_index(rax = index, rcx = len) -> rax clamped to [0, len]
clamp_index:
        test    rax, rax
        jns     .pos
        add     rax, rcx
        jns     .pos
        xor     eax, eax
.pos:   cmp     rax, rcx
        jbe     .ok
        mov     rax, rcx
.ok:    ret

; (slice coll start [end]) - Python-style slicing for lists and strings
bi_slice:
        BI_ENTER
        NARGS_RANGE 2, 3
        mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_LIST
        je      .ok
        cmp     eax, T_STR
        je      .ok
        mov     rsi, rdi
        BDIEV   "cannot slice"
.ok:    mov     ebp, eax
        mov     r15, [rdi+8]            ; len
        NEED_INT [rbx+8]
        mov     rcx, r15
        call    clamp_index
        mov     r13, rax                ; start
        mov     r14, r15                ; end
        cmp     r12, 3
        jne     .noend
        mov     rdi, [rbx+16]
        test    rdi, rdi                ; nil end means "to the end"
        jz      .noend
        NEED_INT rdi
        mov     rcx, r15
        call    clamp_index
        mov     r14, rax
.noend: cmp     r14, r13
        jae     .range
        mov     r14, r13
.range: mov     rdx, r14
        sub     rdx, r13                ; length
        mov     rdi, [rbx]
        cmp     ebp, T_STR
        jne     .list
        lea     rsi, [rdi+STR_DATA+r13]
        call    make_str
        jmp     bi_ret
.list:  mov     r15, rdx
        mov     edi, LIST_HDR
        call    alloc
        mov     qword [rax], T_LIST
        mov     [rax+LIST_LEN], r15
        mov     rdi, [rbx]
        mov     rcx, [rdi+LIST_ITEMS]
        lea     rcx, [rcx+r13*8]
        mov     [rax+LIST_ITEMS], rcx
        jmp     bi_ret

; (range end) (range start end) (range start end step)
bi_range:
        BI_ENTER
        NARGS_RANGE 1, 3
        xor     r13d, r13d              ; start
        mov     r15d, 1                 ; step
        cmp     r12, 1
        jne     .two
        NEED_INT [rbx]
        mov     r14, rax                ; end
        jmp     .go
.two:   NEED_INT [rbx]
        mov     r13, rax
        NEED_INT [rbx+8]
        mov     r14, rax
        cmp     r12, 3
        jne     .go
        NEED_INT [rbx+16]
        mov     r15, rax
        test    r15, r15
        jnz     .go
        BDIE    "step must not be zero"
.go:    ; count = max(0, ceil((end - start) / step))
        mov     rax, r14
        sub     rax, r13
        mov     rcx, r15
        test    rcx, rcx
        jns     .pstep
        neg     rax
        neg     rcx
.pstep: test    rax, rax
        jle     .empty
        add     rax, rcx
        dec     rax
        xor     edx, edx
        div     rcx
        jmp     .build
.empty: xor     eax, eax
.build: mov     rbp, rax
        mov     rdi, rax
        call    make_list
        mov     rdi, [rax+LIST_ITEMS]
        xor     ecx, ecx
        mov     rdx, r13
.fill:  cmp     rcx, rbp
        jae     bi_ret
        lea     rsi, [rdx*2+1]
        mov     [rdi+rcx*8], rsi
        add     rdx, r15
        inc     rcx
        jmp     .fill

; (map f xs)
bi_map:
        BI_ENTER
        NARGS   2
        NEED    [rbx+8], T_LIST
        mov     r13, rax
        mov     rdi, [r13+LIST_LEN]
        call    make_list
        mov     r15, rax
        xor     r14d, r14d
.l:     cmp     r14, [r13+LIST_LEN]
        jae     .done
        mov     rdi, [rbx]
        mov     esi, 1
        mov     rdx, [r13+LIST_ITEMS]
        lea     rdx, [rdx+r14*8]
        call    apply_value
        mov     rcx, [r15+LIST_ITEMS]
        mov     [rcx+r14*8], rax
        inc     r14
        jmp     .l
.done:  mov     rax, r15
        jmp     bi_ret

; (filter f xs)
bi_filter:
        BI_ENTER
        NARGS   2
        NEED    [rbx+8], T_LIST
        mov     r13, rax
        mov     r15, [rel sb_len]
        xor     r14d, r14d
.l:     cmp     r14, [r13+LIST_LEN]
        jae     .done
        mov     rdi, [rbx]
        mov     esi, 1
        mov     rdx, [r13+LIST_ITEMS]
        lea     rdx, [rdx+r14*8]
        call    apply_value
        mov     rdi, rax
        call    truthy
        test    eax, eax
        jz      .skip
        mov     rdi, [r13+LIST_ITEMS]
        mov     rdi, [rdi+r14*8]
        call    col_push
.skip:  inc     r14
        jmp     .l
.done:  mov     rdi, r15
        call    col_list
        jmp     bi_ret

; (reduce f xs [initial])
bi_reduce:
        BI_ENTER
        NARGS_RANGE 2, 3
        NEED    [rbx+8], T_LIST
        mov     r13, rax
        xor     r14d, r14d
        cmp     r12, 3
        jne     .noinit
        mov     rax, [rbx+16]
        jmp     .go
.noinit:
        cmp     qword [r13+LIST_LEN], 0
        je      ret_nil
        mov     rax, [r13+LIST_ITEMS]
        mov     rax, [rax]
        inc     r14
.go:    mov     [rsp], rax              ; accumulator
        cmp     r14, [r13+LIST_LEN]
        jae     .done
        mov     rcx, [r13+LIST_ITEMS]
        mov     rcx, [rcx+r14*8]
        mov     [rsp+8], rcx
        mov     rdi, [rbx]
        mov     esi, 2
        mov     rdx, rsp
        call    apply_value
        inc     r14
        jmp     .go
.done:  mov     rax, [rsp]
        jmp     bi_ret

; (reverse xs) for lists and strings
bi_reverse:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    type_of
        cmp     eax, T_STR
        je      .str
        NEED    rdi, T_LIST
        mov     r13, rax
        mov     rdi, [r13+LIST_LEN]
        call    make_list
        mov     rcx, [r13+LIST_LEN]
        mov     rsi, [r13+LIST_ITEMS]
        mov     rdi, [rax+LIST_ITEMS]
        xor     edx, edx
.l:     test    rcx, rcx
        jz      bi_ret
        dec     rcx
        mov     r8, [rsi+rcx*8]
        mov     [rdi+rdx*8], r8
        inc     rdx
        jmp     .l
.str:   mov     r13, rdi
        mov     rdi, [r13+STR_LEN]
        call    alloc_str
        mov     rcx, [r13+STR_LEN]
        xor     edx, edx
.sl:    test    rcx, rcx
        jz      bi_ret
        dec     rcx
        mov     r8b, [r13+STR_DATA+rcx]
        mov     [rax+STR_DATA+rdx], r8b
        inc     rdx
        jmp     .sl

; (in x coll) - list membership, substring, or hash key
bi_in:
        BI_ENTER
        NARGS   2
        mov     rdi, [rbx+8]
        call    type_of
        cmp     eax, T_LIST
        je      .list
        cmp     eax, T_HASH
        je      .hash
        cmp     eax, T_STR
        je      .str
        mov     rsi, rdi
        BDIEV   "cannot search in"
.list:  mov     r13, rdi
        xor     r14d, r14d
.ll:    cmp     r14, [r13+LIST_LEN]
        jae     ret_false
        mov     rdi, [rbx]
        mov     rsi, [r13+LIST_ITEMS]
        mov     rsi, [rsi+r14*8]
        call    equal
        test    eax, eax
        jnz     ret_true
        inc     r14
        jmp     .ll
.hash:  mov     rsi, [rbx]
        call    hash_find
        test    rax, rax
        js      ret_false
        jmp     ret_true
.str:   NEED    [rbx], T_STR
        mov     rsi, rax                ; needle
        mov     rdi, [rbx+8]            ; haystack
        call    str_find
        test    rax, rax
        js      ret_false
        jmp     ret_true

; str_find(rdi = haystack str, rsi = needle str) -> rax = offset or -1
str_find:
        push    rbx
        mov     r8, [rdi+STR_LEN]
        mov     r9, [rsi+STR_LEN]
        cmp     r9, r8
        ja      .nf
        sub     r8, r9                  ; last start position
        xor     r10d, r10d
.l:     cmp     r10, r8
        ja      .nf
        xor     ecx, ecx
.c:     cmp     rcx, r9
        jae     .found
        lea     rax, [r10+rcx]
        mov     bl, [rdi+STR_DATA+rax]
        cmp     bl, [rsi+STR_DATA+rcx]
        jne     .next
        inc     rcx
        jmp     .c
.next:  inc     r10
        jmp     .l
.found: mov     rax, r10
        pop     rbx
        ret
.nf:    mov     rax, -1
        pop     rbx
        ret

; ---- hashes ---------------------------------------------------------------

; (hash k1 v1 k2 v2 ...)
bi_hash:
        BI_ENTER
        test    r12, 1
        jnz     .odd
        call    hash_empty
        mov     r13, rax
        xor     r14d, r14d
.l:     cmp     r14, r12
        jae     .done
        mov     rdi, r13
        mov     rsi, [rbx+r14*8]
        mov     rdx, [rbx+r14*8+8]
        call    hash_put
        mov     r13, rax
        add     r14, 2
        jmp     .l
.done:  mov     rax, r13
        jmp     bi_ret
.odd:   BDIE    "expected key/value pairs"

; (put h k v [k v ...]) -> new hash
bi_put:
        BI_ENTER
        NARGS_MIN 3
        test    r12, 1
        jz      bi_hash.odd
        NEED    [rbx], T_HASH
        mov     r13, rax
        mov     r14, 1
.l:     cmp     r14, r12
        jae     .done
        mov     rdi, r13
        mov     rsi, [rbx+r14*8]
        mov     rdx, [rbx+r14*8+8]
        call    hash_put
        mov     r13, rax
        add     r14, 2
        jmp     .l
.done:  mov     rax, r13
        jmp     bi_ret

; (del h k ...) -> new hash without the keys
bi_del:
        BI_ENTER
        NARGS_MIN 1
        NEED    [rbx], T_HASH
        mov     r13, rax
        mov     r14, 1
.l:     cmp     r14, r12
        jae     .done
        mov     rdi, r13
        mov     rsi, [rbx+r14*8]
        call    hash_del
        mov     r13, rax
        inc     r14
        jmp     .l
.done:  mov     rax, r13
        jmp     bi_ret

; (keys h) / (values h) in insertion order
bi_keys:
        BI_ENTER
        xor     ebp, ebp
        jmp     hash_column
bi_values:
        BI_ENTER
        mov     ebp, 8
hash_column:
        NARGS   1
        NEED    [rbx], T_HASH
        mov     r13, rax
        mov     rdi, [r13+HASH_COUNT]
        call    make_list
        mov     rdi, [rax+LIST_ITEMS]
        mov     rsi, [r13+HASH_ENTRIES]
        add     rsi, rbp
        mov     rcx, [r13+HASH_COUNT]
.l:     test    rcx, rcx
        jz      bi_ret
        mov     rdx, [rsi]
        mov     [rdi], rdx
        add     rsi, 16
        add     rdi, 8
        dec     rcx
        jmp     .l

; ---- strings --------------------------------------------------------------

; split_str(rdi = str, rsi = separator str or 0 for whitespace) -> list
split_str:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        mov     rbx, rdi
        mov     rbp, rsi
        mov     r15, [rel sb_len]
        lea     r12, [rbx+STR_DATA]     ; cursor
        mov     r13, r12
        add     r13, [rbx+STR_LEN]      ; end
        test    rbp, rbp
        jz      .ws
        cmp     qword [rbp+STR_LEN], 0
        je      .ws
.sep:   mov     r14, r12                ; piece start
.scan:  mov     rax, r13
        sub     rax, r12
        cmp     rax, [rbp+STR_LEN]
        jb      .last
        mov     rcx, [rbp+STR_LEN]
        mov     rsi, r12
        lea     rdi, [rbp+STR_DATA]
        repe cmpsb
        je      .match
        inc     r12
        jmp     .scan
.match: mov     rsi, r14
        mov     rdx, r12
        sub     rdx, r14
        call    make_str
        mov     rdi, rax
        call    col_push
        add     r12, [rbp+STR_LEN]
        jmp     .sep
.last:  mov     rsi, r14
        mov     rdx, r13
        sub     rdx, r14
        call    make_str
        mov     rdi, rax
        call    col_push
        jmp     .done
.ws:    cmp     r12, r13                ; skip whitespace
        jae     .done
        movzx   eax, byte [r12]
        call    is_space
        jne     .word
        inc     r12
        jmp     .ws
.word:  mov     r14, r12
.wl:    cmp     r12, r13
        jae     .wend
        movzx   eax, byte [r12]
        call    is_space
        je      .wend
        inc     r12
        jmp     .wl
.wend:  mov     rsi, r14
        mov     rdx, r12
        sub     rdx, r14
        call    make_str
        mov     rdi, rax
        call    col_push
        jmp     .ws
.done:  mov     rdi, r15
        call    col_list
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

; (split s [sep]) - without sep splits on runs of whitespace
bi_split:
        BI_ENTER
        NARGS_RANGE 1, 2
        NEED    [rbx], T_STR
        xor     esi, esi
        cmp     r12, 2
        jne     .go
        NEED    [rbx+8], T_STR
        mov     rsi, rax
.go:    mov     rdi, [rbx]
        call    split_str
        jmp     bi_ret

; (join xs [sep]) - display form of each element, separated by sep
bi_join:
        BI_ENTER
        NARGS_RANGE 1, 2
        NEED    [rbx], T_LIST
        mov     r13, rax
        xor     ebp, ebp
        cmp     r12, 2
        jne     .go
        NEED    [rbx+8], T_STR
        mov     rbp, rax
.go:    mov     r15, [rel sb_len]
        xor     r14d, r14d
.l:     cmp     r14, [r13+LIST_LEN]
        jae     .done
        test    r14, r14
        jz      .nosep
        test    rbp, rbp
        jz      .nosep
        lea     rsi, [rbp+STR_DATA]
        mov     rdx, [rbp+STR_LEN]
        call    sb_puts
.nosep: mov     rdi, [r13+LIST_ITEMS]
        mov     rdi, [rdi+r14*8]
        xor     esi, esi
        call    sb_value
        inc     r14
        jmp     .l
.done:  mov     rdi, r15
        call    sb_take_str
        jmp     bi_ret

; (strip s) - remove leading and trailing whitespace
bi_strip:
        BI_ENTER
        NARGS   1
        NEED    [rbx], T_STR
        lea     rsi, [rax+STR_DATA]
        mov     rdx, rsi
        add     rdx, [rax+STR_LEN]
.l:     cmp     rsi, rdx
        jae     .make
        movzx   eax, byte [rsi]
        call    is_space
        jne     .r
        inc     rsi
        jmp     .l
.r:     movzx   eax, byte [rdx-1]
        call    is_space
        jne     .make
        dec     rdx
        jmp     .r
.make:  sub     rdx, rsi
        call    make_str
        jmp     bi_ret
