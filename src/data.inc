; ---------------------------------------------------------------------------
; slip - equality, ordering, hashing, hash tables, printing
; ---------------------------------------------------------------------------

section .text

; equal(rdi = a, rsi = b) -> eax = 1 if structurally equal
equal:
        cmp     rdi, rsi
        je      .yes
        test    dil, 1
        jnz     .no
        test    sil, 1
        jnz     .no
        test    rdi, rdi
        jz      .no
        test    rsi, rsi
        jz      .no
        mov     rax, [rdi]
        cmp     rax, [rsi]
        jne     .no
        cmp     rax, T_STR
        je      .str
        cmp     rax, T_LIST
        je      .list
        cmp     rax, T_HASH
        je      .hash
.no:    xor     eax, eax
        ret
.yes:   mov     eax, 1
        ret
.str:   mov     rcx, [rdi+STR_LEN]
        cmp     rcx, [rsi+STR_LEN]
        jne     .no
        add     rdi, STR_DATA
        add     rsi, STR_DATA
        repe cmpsb
        jne     .no
        jmp     .yes
.list:  STACK_CHECK
        mov     rcx, [rdi+LIST_LEN]
        cmp     rcx, [rsi+LIST_LEN]
        jne     .no
        push    rbx
        push    r12
        push    r13
        push    r14
        sub     rsp, 8
        mov     rbx, [rdi+LIST_ITEMS]
        mov     r12, [rsi+LIST_ITEMS]
        mov     r13, rcx
        xor     r14d, r14d
.ll:    cmp     r14, r13
        jae     .ltrue
        mov     rdi, [rbx+r14*8]
        mov     rsi, [r12+r14*8]
        call    equal
        test    eax, eax
        jz      .lout
        inc     r14
        jmp     .ll
.ltrue: mov     eax, 1
.lout:  add     rsp, 8
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
.hash:  STACK_CHECK
        mov     rcx, [rdi+HASH_COUNT]
        cmp     rcx, [rsi+HASH_COUNT]
        jne     .no
        push    rbx
        push    r12
        push    r13
        push    r14
        sub     rsp, 8
        mov     rbx, rdi
        mov     r12, rsi
        mov     r13, rcx
        xor     r14d, r14d
.hl:    cmp     r14, r13
        jae     .ltrue
        mov     rax, [rbx+HASH_ENTRIES]
        mov     rcx, r14
        shl     rcx, 4
        mov     rsi, [rax+rcx]
        mov     rdi, r12
        call    hash_find
        test    rax, rax
        js      .hno
        mov     rcx, [r12+HASH_ENTRIES]
        shl     rax, 4
        mov     rsi, [rcx+rax+8]
        mov     rax, [rbx+HASH_ENTRIES]
        mov     rcx, r14
        shl     rcx, 4
        mov     rdi, [rax+rcx+8]
        call    equal
        test    eax, eax
        jz      .lout
        inc     r14
        jmp     .hl
.hno:   xor     eax, eax
        jmp     .lout

; compare(rdi = a, rsi = b) -> eax = -1 / 0 / 1 (ints or strings)
compare:
        test    dil, 1
        jz      .notint
        test    sil, 1
        jz      .bad
        xor     eax, eax
        cmp     rdi, rsi
        jl      .lt
        setg    al
        ret
.lt:    mov     eax, -1
        ret
.notint:
        push    rdi
        push    rsi
        call    type_of
        cmp     eax, T_STR
        jne     .bad2
        mov     rdi, rsi
        call    type_of
        cmp     eax, T_STR
        jne     .bad2
        pop     rsi
        pop     rdi
        mov     rcx, [rdi+STR_LEN]
        mov     rdx, [rsi+STR_LEN]
        cmp     rcx, rdx
        cmova   rcx, rdx                ; rcx = min length
        lea     r8, [rdi+STR_DATA]
        lea     r9, [rsi+STR_DATA]
        xor     r10d, r10d
.sl:    cmp     r10, rcx
        jae     .slen
        movzx   eax, byte [r8+r10]
        movzx   r11d, byte [r9+r10]
        cmp     eax, r11d
        jb      .lt
        ja      .gt
        inc     r10
        jmp     .sl
.slen:  mov     rcx, [rdi+STR_LEN]
        xor     eax, eax
        cmp     rcx, rdx
        jb      .lt
        seta    al
        ret
.gt:    mov     eax, 1
        ret
.bad2:  pop     rsi
        pop     rdi
.bad:   BDIEV   "cannot compare values of different/unordered types"

; hash_value(rdi = key) -> rax. Lists and hashes are unhashable.
; Clobbers rax, rcx, rdx.
hash_value:
        mov     rax, rdi
        test    dil, 1
        jnz     .mix
        test    rdi, rdi
        jz      .mix
        mov     rcx, [rdi]
        cmp     rcx, T_STR
        je      .str
        cmp     rcx, T_LIST
        je      .bad
        cmp     rcx, T_HASH
        je      .bad
.mix:   mov     rcx, 0x9E3779B97F4A7C15
        imul    rax, rcx
        mov     rcx, rax
        shr     rcx, 29
        xor     rax, rcx
        ret
.str:   mov     rax, 0xcbf29ce484222325
        mov     rcx, [rdi+STR_LEN]
        push    rsi
        lea     rsi, [rdi+STR_DATA]
        mov     rdx, 0x100000001b3
.sl:    test    rcx, rcx
        jz      .sd
        movzx   r8d, byte [rsi]
        xor     rax, r8
        imul    rax, rdx
        inc     rsi
        dec     rcx
        jmp     .sl
.sd:    pop     rsi
        mov     rcx, rax
        shr     rcx, 29
        xor     rax, rcx
        ret
.bad:   mov     rsi, rdi
        BDIEV   "unhashable key"

; hash_find(rdi = hash, rsi = key) -> rax = entry index or -1
hash_find:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        mov     rbx, rdi
        mov     r12, rsi
        cmp     qword [rbx+HASH_COUNT], 0
        je      .nf
        mov     rdi, r12
        call    hash_value
        mov     r13, [rbx+HASH_CAP]
        dec     r13
        and     rax, r13
        mov     r14, rax
.probe: mov     rcx, [rbx+HASH_INDEX]
        mov     r15, [rcx+r14*8]
        test    r15, r15
        jz      .nf
        dec     r15
        mov     rcx, [rbx+HASH_ENTRIES]
        mov     rdi, r15
        shl     rdi, 4
        mov     rdi, [rcx+rdi]
        mov     rsi, r12
        call    equal
        test    eax, eax
        jnz     .found
        inc     r14
        and     r14, r13
        jmp     .probe
.found: mov     rax, r15
        jmp     .out
.nf:    mov     rax, -1
.out:   pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; hash_build(rdi = entries (key,value pairs, unique keys), rsi = count)
; -> rax = new hash that owns the entries array
hash_build:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        mov     rbx, rdi
        mov     r12, rsi
        mov     r13, 8
        lea     rax, [r12*2]
.cap:   cmp     r13, rax
        jae     .capok
        shl     r13, 1
        jmp     .cap
.capok: lea     rdi, [r13*8]
        call    alloc
        mov     r14, rax                ; index
        mov     edi, HASH_SIZE
        call    alloc
        mov     r15, rax
        mov     qword [r15], T_HASH
        mov     [r15+HASH_COUNT], r12
        mov     [r15+HASH_CAP], r13
        mov     [r15+HASH_ENTRIES], rbx
        mov     [r15+HASH_INDEX], r14
        dec     r13                     ; mask
        xor     ebp, ebp
.ins:   cmp     rbp, r12
        jae     .done
        mov     rdi, rbp
        shl     rdi, 4
        mov     rdi, [rbx+rdi]
        call    hash_value
        and     rax, r13
.pr:    cmp     qword [r14+rax*8], 0
        je      .slot
        inc     rax
        and     rax, r13
        jmp     .pr
.slot:  lea     rcx, [rbp+1]
        mov     [r14+rax*8], rcx
        inc     rbp
        jmp     .ins
.done:  mov     rax, r15
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

; hash_empty() -> rax
hash_empty:
        xor     edi, edi
        xor     esi, esi
        jmp     hash_build

; hash_put(rdi = hash, rsi = key, rdx = value) -> rax = new hash
hash_put:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        mov     rbx, rdi
        mov     r12, rsi
        mov     r13, rdx
        call    hash_find
        mov     r14, rax                ; existing index or -1
        mov     rbp, [rbx+HASH_COUNT]   ; new count
        test    r14, r14
        jns     .have
        inc     rbp
.have:  mov     rdi, rbp
        shl     rdi, 4
        call    alloc
        mov     r15, rax
        mov     rdi, r15
        mov     rsi, [rbx+HASH_ENTRIES]
        mov     rcx, [rbx+HASH_COUNT]
        shl     rcx, 1
        rep movsq
        test    r14, r14
        js      .append
        shl     r14, 4
        mov     [r15+r14+8], r13
        jmp     .build
.append:
        mov     rcx, rbp
        dec     rcx
        shl     rcx, 4
        mov     [r15+rcx], r12
        mov     [r15+rcx+8], r13
.build: mov     rdi, r15
        mov     rsi, rbp
        call    hash_build
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

; hash_del(rdi = hash, rsi = key) -> rax = hash without key
hash_del:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        mov     rbx, rdi
        call    hash_find
        test    rax, rax
        js      .same
        mov     r12, rax                ; index to drop
        mov     r13, [rbx+HASH_COUNT]
        dec     r13                     ; new count
        mov     rdi, r13
        shl     rdi, 4
        call    alloc
        mov     r14, rax
        mov     rdi, r14
        mov     rsi, [rbx+HASH_ENTRIES]
        mov     rcx, r12
        shl     rcx, 1
        rep movsq                       ; entries before the index
        add     rsi, 16                 ; skip dropped entry
        mov     rcx, r13
        sub     rcx, r12
        shl     rcx, 1
        rep movsq                       ; entries after the index
        mov     rdi, r14
        mov     rsi, r13
        call    hash_build
        jmp     .out
.same:  mov     rax, rbx
.out:   pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; ---------------------------------------------------------------------------
; sb_value(rdi = value, esi = 0 display / 1 repr) - append text to sb.
; display shows strings raw; repr quotes them (containers always use repr
; for their elements).
; ---------------------------------------------------------------------------
sb_value:
        STACK_CHECK
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        mov     rbx, rdi
        mov     r12d, esi
        test    bl, 1
        jz      .notint
        sar     rdi, 1
        call    sb_int
        jmp     .out
.notint:
        test    rbx, rbx
        jnz     .obj
        lea     rdi, [rel tn_nil]
        call    sb_cstr
        jmp     .out
.obj:   mov     rax, [rbx]
        cmp     rax, T_STR
        je      .str
        cmp     rax, T_LIST
        je      .list
        cmp     rax, T_HASH
        je      .hash
        cmp     rax, T_BOOL
        je      .bool
        cmp     rax, T_SYM
        je      .sym
        cmp     rax, T_FN
        je      .fn
        cmp     rax, T_BUILTIN
        je      .bi
        cmp     rax, T_FILE
        je      .file
        lea     rdi, [rel s_unknown]
        call    sb_cstr
        jmp     .out
.str:   lea     rsi, [rbx+STR_DATA]
        mov     rdx, [rbx+STR_LEN]
        test    r12d, r12d
        jnz     .quoted
        call    sb_puts
        jmp     .out
.quoted:
        mov     dil, '"'
        call    sb_putc
        xor     r13d, r13d
.ql:    cmp     r13, [rbx+STR_LEN]
        jae     .qend
        movzx   edi, byte [rbx+STR_DATA+r13]
        inc     r13
        cmp     dil, 10
        je      .qn
        cmp     dil, 9
        je      .qt
        cmp     dil, 13
        je      .qr
        cmp     dil, '"'
        je      .qesc
        cmp     dil, '\'
        je      .qesc
        call    sb_putc
        jmp     .ql
.qesc:  mov     r14d, edi
        mov     dil, '\'
        call    sb_putc
        mov     edi, r14d
        call    sb_putc
        jmp     .ql
.qn:    mov     r14d, 'n'
        jmp     .qesc2
.qt:    mov     r14d, 't'
        jmp     .qesc2
.qr:    mov     r14d, 'r'
.qesc2: mov     dil, '\'
        call    sb_putc
        mov     edi, r14d
        call    sb_putc
        jmp     .ql
.qend:  mov     dil, '"'
        call    sb_putc
        jmp     .out
.list:  mov     dil, '['
        call    sb_putc
        xor     r13d, r13d
.ll:    cmp     r13, [rbx+LIST_LEN]
        jae     .lend
        test    r13, r13
        jz      .lfirst
        lea     rdi, [rel s_comma]
        call    sb_cstr
.lfirst:
        mov     rax, [rbx+LIST_ITEMS]
        mov     rdi, [rax+r13*8]
        mov     esi, 1
        call    sb_value
        inc     r13
        jmp     .ll
.lend:  mov     dil, ']'
        call    sb_putc
        jmp     .out
.hash:  mov     dil, '{'
        call    sb_putc
        xor     r13d, r13d
.hl:    cmp     r13, [rbx+HASH_COUNT]
        jae     .hend
        test    r13, r13
        jz      .hfirst
        lea     rdi, [rel s_comma]
        call    sb_cstr
.hfirst:
        mov     r14, r13
        shl     r14, 4
        add     r14, [rbx+HASH_ENTRIES]
        mov     rdi, [r14]
        mov     esi, 1
        call    sb_value
        lea     rdi, [rel s_colon]
        call    sb_cstr
        mov     rdi, [r14+8]
        mov     esi, 1
        call    sb_value
        inc     r13
        jmp     .hl
.hend:  mov     dil, '}'
        call    sb_putc
        jmp     .out
.bool:  lea     rdi, [rel s_true]
        cmp     qword [rbx+BOOL_VAL], 0
        jne     .bput
        lea     rdi, [rel s_false]
.bput:  call    sb_cstr
        jmp     .out
.sym:   lea     rsi, [rbx+SYM_DATA]
        mov     rdx, [rbx+SYM_LEN]
        call    sb_puts
        jmp     .out
.fn:    lea     rdi, [rel s_fnopen]
        call    sb_cstr
        mov     r13, [rbx+FN_NAME]
        test    r13, r13
        jz      .fnanon
        mov     dil, ' '
        call    sb_putc
        lea     rsi, [r13+SYM_DATA]
        mov     rdx, [r13+SYM_LEN]
        call    sb_puts
.fnanon:
        mov     dil, '>'
        call    sb_putc
        jmp     .out
.bi:    lea     rdi, [rel s_biopen]
        call    sb_cstr
        mov     rdi, [rbx+BI_NAME]
        call    sb_cstr
        mov     dil, '>'
        call    sb_putc
        jmp     .out
.file:  lea     rdi, [rel s_fileopen]
        call    sb_cstr
        mov     rax, [rbx+FILE_NAME]
        lea     rsi, [rax+STR_DATA]
        mov     rdx, [rax+STR_LEN]
        call    sb_puts
        cmp     qword [rbx+FILE_FD], 0
        jge     .fopen
        lea     rdi, [rel s_closed]
        call    sb_cstr
.fopen: mov     dil, '>'
        call    sb_putc
.out:   pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

section .rodata
s_true:         db "true", 0
s_false:        db "false", 0
s_comma:        db ", ", 0
s_fnopen:       db "<fn", 0
s_biopen:       db "<builtin ", 0
s_fileopen:     db "<file ", 0
s_closed:       db " (closed)", 0
s_unknown:      db "<?>", 0

section .text
