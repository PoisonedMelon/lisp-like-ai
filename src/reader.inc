; ---------------------------------------------------------------------------
; slip - reader (source text -> values)
;
;   ( a b c )      form (function call / special form)
;   [ a b c ]      list literal      -> form (list a b c)
;   { k v ... }    hash literal      -> form (hash k v ...)
;   "text\n"       string            42 -7   integers
;   true false nil constants         anything else is a symbol
;   ; or #         comment to end of line
;   , and :        are whitespace, so {"a": 1, "b": 2} reads naturally
; ---------------------------------------------------------------------------

section .bss
src_cur:        resq 1
src_end:        resq 1
src_line:       resq 1
delim_tab:      resb 256
bi_list_obj:    resq 1          ; builtin used for [ ] literals
bi_hash_obj:    resq 1          ; builtin used for { } literals
sym_true:       resq 1
sym_false:      resq 1
sym_nil:        resq 1

section .rodata
delim_chars:    db " ", 9, 10, 13, "()[]{}", '"', ";,:", 0

section .text

init_reader:
        lea     rsi, [rel delim_chars]
        lea     rdi, [rel delim_tab]
.l:     movzx   eax, byte [rsi]
        test    eax, eax
        jz      .d
        mov     byte [rdi+rax], 1
        inc     rsi
        jmp     .l
.d:     ret

; die_line(rdi = message) - parse error with line number
die_line:
        call    die_prefix
        call    sb_cstr
        lea     rdi, [rel s_atline]
        call    sb_cstr
        mov     rdi, [rel src_line]
        call    sb_int
        mov     dil, ')'
        call    sb_putc
        jmp     die_flush

; skip whitespace and comments
skip_ws:
        mov     rsi, [rel src_cur]
        mov     rdx, [rel src_end]
.l:     cmp     rsi, rdx
        jae     .done
        movzx   eax, byte [rsi]
        cmp     al, 10
        je      .nl
        cmp     al, ' '
        je      .sp
        cmp     al, 9
        je      .sp
        cmp     al, 13
        je      .sp
        cmp     al, ','
        je      .sp
        cmp     al, ':'
        je      .sp
        cmp     al, ';'
        je      .cm
        cmp     al, '#'
        je      .cm
.done:  mov     [rel src_cur], rsi
        ret
.nl:    inc     qword [rel src_line]
.sp:    inc     rsi
        jmp     .l
.cm:    inc     rsi
        cmp     rsi, rdx
        jae     .done
        cmp     byte [rsi], 10
        jne     .cm
        jmp     .l

; read_form() -> rax = value, edx = status (0 value, 1 end of input,
; 2 closing bracket with the bracket char in ecx)
read_form:
        STACK_CHECK
        push    rbx
        push    r12
        push    r13
        call    skip_ws
        mov     rsi, [rel src_cur]
        cmp     rsi, [rel src_end]
        jae     .eof
        movzx   eax, byte [rsi]
        cmp     al, '('
        je      .paren
        cmp     al, '['
        je      .brack
        cmp     al, '{'
        je      .brace
        cmp     al, ')'
        je      .close
        cmp     al, ']'
        je      .close
        cmp     al, '}'
        je      .close
        cmp     al, '"'
        je      .string
        jmp     .atom

.eof:   xor     eax, eax
        mov     edx, 1
        jmp     .out
.close: inc     qword [rel src_cur]
        mov     ecx, eax
        xor     eax, eax
        mov     edx, 2
        jmp     .out

.paren: mov     r12d, ')'
        xor     r13d, r13d
        jmp     .seq
.brack: mov     r12d, ']'
        mov     r13, [rel bi_list_obj]
        jmp     .seq
.brace: mov     r12d, '}'
        mov     r13, [rel bi_hash_obj]
.seq:   inc     qword [rel src_cur]
        push    qword [rel src_line]    ; line the form starts on
        xor     ebx, ebx                ; item count (items live on the stack)
        test    r13, r13
        jz      .sl
        push    r13
        inc     rbx
.sl:    call    read_form
        cmp     edx, 1
        je      .unterminated
        cmp     edx, 2
        je      .sclose
        push    rax
        inc     rbx
        jmp     .sl
.sclose:
        cmp     ecx, r12d
        jne     .mismatch
        mov     rdi, rbx
        call    make_list
        mov     rcx, [rax+LIST_ITEMS]
        mov     rdx, rbx
.fill:  test    rdx, rdx
        jz      .filled
        dec     rdx
        pop     r8
        mov     [rcx+rdx*8], r8
        jmp     .fill
.filled:
        add     rsp, 8                  ; start line
        xor     edx, edx
        jmp     .out
.unterminated:
        mov     rax, [rsp+rbx*8]
        mov     [rel src_line], rax
        lea     rdi, [rel s_unterminated]
        jmp     die_line
.mismatch:
        lea     rdi, [rel s_mismatch]
        jmp     die_line

.string:
        inc     rsi
        mov     r13, [rel src_line]     ; line the string starts on
        mov     r12, [rel sb_len]
        mov     rdx, [rel src_end]
.strl:  cmp     rsi, rdx
        jae     .strunterm
        movzx   edi, byte [rsi]
        inc     rsi
        cmp     dil, '"'
        je      .strend
        cmp     dil, '\'
        je      .esc
        cmp     dil, 10
        jne     .strput
        inc     qword [rel src_line]
.strput:
        call    sb_putc
        jmp     .strl
.esc:   cmp     rsi, rdx
        jae     .strunterm
        movzx   edi, byte [rsi]
        inc     rsi
        cmp     dil, 'n'
        je      .en
        cmp     dil, 't'
        je      .et
        cmp     dil, 'r'
        je      .er
        cmp     dil, '0'
        je      .e0
        cmp     dil, 'e'
        je      .ee
        jmp     .strput                 ; \" \\ and anything else: literal
.en:    mov     edi, 10
        jmp     .strput
.et:    mov     edi, 9
        jmp     .strput
.er:    mov     edi, 13
        jmp     .strput
.e0:    xor     edi, edi
        jmp     .strput
.ee:    mov     edi, 27
        jmp     .strput
.strend:
        mov     [rel src_cur], rsi
        mov     rdi, r12
        call    sb_take_str
        xor     edx, edx
        jmp     .out
.strunterm:
        mov     [rel src_line], r13
        lea     rdi, [rel s_unterm_str]
        jmp     die_line

.atom:  ; token = [rsi, r12)
        mov     r12, rsi
        mov     rdx, [rel src_end]
        lea     rcx, [rel delim_tab]
.tok:   cmp     r12, rdx
        jae     .tokend
        movzx   eax, byte [r12]
        cmp     byte [rcx+rax], 0
        jne     .tokend
        inc     r12
        jmp     .tok
.tokend:
        mov     [rel src_cur], r12
        mov     rdx, r12
        sub     rdx, rsi                ; token length (>= 1)
        ; integer?  -?[0-9]+
        mov     rcx, rsi
        xor     r13d, r13d              ; negative flag
        cmp     byte [rcx], '-'
        jne     .digits
        cmp     rdx, 1
        je      .symbol
        mov     r13d, 1
        inc     rcx
.digits:
        xor     eax, eax
.dl:    cmp     rcx, r12
        jae     .num
        movzx   r8d, byte [rcx]
        sub     r8d, '0'
        cmp     r8d, 9
        ja      .symbol
        imul    rax, rax, 10
        add     rax, r8
        inc     rcx
        jmp     .dl
.num:   test    r13d, r13d
        jz      .pos
        neg     rax
.pos:   TAGINT  rax
        xor     edx, edx
        jmp     .out
.symbol:
        call    intern
        cmp     rax, [rel sym_true]
        je      .true
        cmp     rax, [rel sym_false]
        je      .false
        cmp     rax, [rel sym_nil]
        je      .nil
        xor     edx, edx
        jmp     .out
.true:  lea     rax, [rel true_obj]
        xor     edx, edx
        jmp     .out
.false: lea     rax, [rel false_obj]
        xor     edx, edx
        jmp     .out
.nil:   xor     eax, eax
        xor     edx, edx
.out:   pop     r13
        pop     r12
        pop     rbx
        ret

section .rodata
s_atline:       db " (line ", 0
s_unterminated: db "unclosed bracket", 0
s_mismatch:     db "mismatched closing bracket", 0
s_unterm_str:   db "unterminated string", 0
s_unexpected:   db "unexpected closing bracket", 0

section .text
