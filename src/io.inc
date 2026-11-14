; ---------------------------------------------------------------------------
; slip - file IO builtins
;
;   (open path [mode])         -> file, or nil if it cannot be opened
;                                 modes: "r" "w" "a" "r+" "w+" "a+"
;   (read f-or-path [n])       -> str  (whole rest, or up to n bytes; "" at EOF)
;   (readline f)               -> str including "\n"; "" at EOF
;   (lines f-or-path)          -> list of lines without "\n"
;   (write f-or-path x ...)    -> number of bytes written
;   (seek f offset [whence])   -> new position; whence "start" "current" "end"
;   (tell f)                   -> current position
;   (close f)                  -> nil
;   (print x ...)              -> writes to stdout, space separated + "\n"
;   (exit [code])
; ---------------------------------------------------------------------------

section .text

; make_file(edi = fd, rsi = name str) -> rax
make_file:
        push    rdi
        push    rsi
        mov     edi, FILE_SIZE
        call    alloc
        pop     rsi
        pop     rdi
        mov     qword [rax], T_FILE
        movsxd  rdi, edi
        mov     [rax+FILE_FD], rdi
        mov     [rax+FILE_NAME], rsi
        ret

; file_fd(rdi = value) -> rax = fd of an open file (dies otherwise)
file_fd:
        mov     esi, T_FILE
        call    need_type
        mov     rax, [rdi+FILE_FD]
        test    rax, rax
        js      .closed
        ret
.closed:
        mov     rsi, rdi
        BDIEV   "file is closed"

; open_path(rdi = path str, esi = flags) -> rax = fd or -errno
open_path:
        push    rbx
        mov     ebx, esi
        call    str_to_cstr
        mov     rdi, rax
        mov     esi, ebx
        mov     edx, 0o644
        mov     eax, SYS_OPEN
        syscall
        pop     rbx
        ret

; open_or_die(rdi = path str, esi = flags) -> rax = fd
open_or_die:
        push    rdi
        call    open_path
        pop     rsi
        test    rax, rax
        js      .fail
        ret
.fail:  BDIEV   "cannot open file"

sys_close:                              ; edi = fd
        mov     eax, SYS_CLOSE
        syscall
        ret

io_fail:
        BDIE    "I/O error"

; read_all_fd(edi = fd) -> rax = str with everything up to EOF
read_all_fd:
        push    rbx
        push    r12
        sub     rsp, 8
        mov     ebx, edi
        mov     r12, [rel sb_len]
.l:     mov     edi, 65536
        call    sb_reserve
        mov     eax, SYS_READ
        mov     edi, ebx
        mov     rsi, [rel sb_base]
        add     rsi, [rel sb_len]
        mov     edx, 65536
        syscall
        cmp     rax, -4                 ; EINTR
        je      .l
        test    rax, rax
        js      io_fail
        jz      .done
        add     [rel sb_len], rax
        jmp     .l
.done:  mov     rdi, r12
        call    sb_take_str
        add     rsp, 8
        pop     r12
        pop     rbx
        ret

; read_n_fd(edi = fd, rsi = n) -> rax = str with up to n bytes
read_n_fd:
        push    rbx
        push    r12
        push    r13
        mov     ebx, edi
        mov     r13, rsi                ; remaining
        mov     r12, [rel sb_len]
        mov     rdi, rsi
        call    sb_reserve
.l:     test    r13, r13
        jz      .done
        mov     eax, SYS_READ
        mov     edi, ebx
        mov     rsi, [rel sb_base]
        add     rsi, [rel sb_len]
        mov     rdx, r13
        syscall
        cmp     rax, -4
        je      .l
        test    rax, rax
        js      io_fail
        jz      .done
        add     [rel sb_len], rax
        sub     r13, rax
        jmp     .l
.done:  mov     rdi, r12
        call    sb_take_str
        pop     r13
        pop     r12
        pop     rbx
        ret

; readline_fd(edi = fd) -> rax = next line including "\n" ("" at EOF).
; Seekable files are read in chunks and the file position is moved back to
; just after the newline; pipes and terminals are read byte by byte.
readline_fd:
        push    rbx
        push    r12
        push    r13
        mov     ebx, edi
        mov     r12, [rel sb_len]
        mov     eax, SYS_LSEEK
        xor     esi, esi
        mov     edx, 1                  ; SEEK_CUR
        syscall
        test    rax, rax
        js      .bytes
.chunk: mov     edi, 4096
        call    sb_reserve
        mov     eax, SYS_READ
        mov     edi, ebx
        mov     rsi, [rel sb_base]
        add     rsi, [rel sb_len]
        mov     edx, 4096
        syscall
        cmp     rax, -4
        je      .chunk
        test    rax, rax
        js      io_fail
        jz      .done
        mov     r13, rax                ; bytes read
        mov     rdi, [rel sb_base]
        add     rdi, [rel sb_len]
        mov     rdx, rdi                ; chunk start
        mov     rcx, rax
        mov     al, 10
        repne scasb
        jne     .nonl
        sub     rdi, rdx                ; bytes up to and including "\n"
        add     [rel sb_len], rdi
        sub     rdi, r13                ; -(unused bytes)
        jz      .done
        mov     rsi, rdi
        mov     eax, SYS_LSEEK
        mov     edi, ebx
        mov     edx, 1
        syscall
        jmp     .done
.nonl:  add     [rel sb_len], r13
        jmp     .chunk
.bytes: mov     edi, 1
        call    sb_reserve
        mov     eax, SYS_READ
        mov     edi, ebx
        mov     rsi, [rel sb_base]
        add     rsi, [rel sb_len]
        mov     r13, rsi
        mov     edx, 1
        syscall
        cmp     rax, -4
        je      .bytes
        test    rax, rax
        js      io_fail
        jz      .done
        inc     qword [rel sb_len]
        cmp     byte [r13], 10
        jne     .bytes
.done:  mov     rdi, r12
        call    sb_take_str
        pop     r13
        pop     r12
        pop     rbx
        ret

; target_fd(rdi = file or path, esi = flags for paths)
;   -> rax = fd, rdx = 1 if it was opened from a path (caller closes)
target_fd:
        push    rdi
        call    type_of
        pop     rdi
        cmp     eax, T_STR
        je      .path
        call    file_fd
        xor     edx, edx
        ret
.path:  call    open_or_die
        mov     edx, 1
        ret

; (open path [mode])
bi_open:
        BI_ENTER
        NARGS_RANGE 1, 2
        NEED    [rbx], T_STR
        mov     r13d, O_RDONLY
        cmp     r12, 2
        jne     .go
        NEED    [rbx+8], T_STR
        lea     r14, [rel open_modes]
.m:     mov     rsi, [r14]
        test    rsi, rsi
        jz      .badmode
        mov     rdi, [rbx+8]
        call    str_eq_cstr
        test    eax, eax
        jnz     .found
        add     r14, 16
        jmp     .m
.found: mov     r13, [r14+8]
.go:    mov     rdi, [rbx]
        mov     esi, r13d
        call    open_path
        test    rax, rax
        js      ret_nil
        mov     edi, eax
        mov     rsi, [rbx]
        call    make_file
        jmp     bi_ret
.badmode:
        mov     rsi, [rbx+8]
        BDIEV   'invalid mode (use "r" "w" "a" "r+" "w+" or "a+")'

; (close f)
bi_close:
        BI_ENTER
        NARGS   1
        NEED    [rbx], T_FILE
        mov     rdi, [rax+FILE_FD]
        test    rdi, rdi
        js      ret_nil
        mov     qword [rax+FILE_FD], -1
        call    sys_close
        jmp     ret_nil

; (read f-or-path [n])
bi_read:
        BI_ENTER
        NARGS_RANGE 1, 2
        mov     rdi, [rbx]
        mov     esi, O_RDONLY
        call    target_fd
        mov     r13, rax                ; fd
        mov     r14, rdx                ; close afterwards?
        cmp     r12, 2
        je      .n
        mov     edi, r13d
        call    read_all_fd
        jmp     .out
.n:     NEED_INT [rbx+8]
        test    rax, rax
        js      .neg
        mov     edi, r13d
        mov     rsi, rax
        call    read_n_fd
.out:   test    r14, r14
        jz      bi_ret
        mov     r15, rax
        mov     edi, r13d
        call    sys_close
        mov     rax, r15
        jmp     bi_ret
.neg:   BDIE    "byte count must not be negative"

; (readline f)
bi_readline:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    file_fd
        mov     edi, eax
        call    readline_fd
        jmp     bi_ret

; (lines f-or-path) -> list of lines without their "\n"
bi_lines:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        mov     esi, O_RDONLY
        call    target_fd
        mov     r13, rax
        mov     r14, rdx
        mov     edi, r13d
        call    read_all_fd
        mov     r15, rax
        test    r14, r14
        jz      .split
        mov     edi, r13d
        call    sys_close
.split: mov     rdi, r15
        lea     rsi, [rel newline_str]
        call    split_str
        mov     rcx, [rax+LIST_LEN]     ; drop the empty piece after a final "\n"
        mov     rdx, [rax+LIST_ITEMS]
        mov     rdx, [rdx+rcx*8-8]
        cmp     qword [rdx+STR_LEN], 0
        jne     bi_ret
        dec     qword [rax+LIST_LEN]
        jmp     bi_ret

; (write f-or-path x ...) -> bytes written
bi_write:
        BI_ENTER
        NARGS_MIN 1
        mov     rdi, [rbx]
        mov     esi, O_WRONLY | O_CREAT | O_TRUNC
        call    target_fd
        mov     r13, rax
        mov     r14, rdx
        mov     r15, [rel sb_len]
        mov     rbp, 1
.l:     cmp     rbp, r12
        jae     .flush
        mov     rdi, [rbx+rbp*8]
        xor     esi, esi
        call    sb_value
        inc     rbp
        jmp     .l
.flush: mov     edi, r13d
        mov     rsi, [rel sb_base]
        add     rsi, r15
        mov     rdx, [rel sb_len]
        sub     rdx, r15
        call    write_all
        mov     [rel sb_len], r15
        test    rax, rax
        js      io_fail
        mov     rbp, rax
        test    r14, r14
        jz      .done
        mov     edi, r13d
        call    sys_close
.done:  mov     rax, rbp
        TAGINT  rax
        jmp     bi_ret

; (print x ...) -> nil
bi_print:
        BI_ENTER
        mov     r15, [rel sb_len]
        xor     r14d, r14d
.l:     cmp     r14, r12
        jae     .flush
        test    r14, r14
        jz      .first
        mov     dil, ' '
        call    sb_putc
.first: mov     rdi, [rbx+r14*8]
        xor     esi, esi
        call    sb_value
        inc     r14
        jmp     .l
.flush: mov     dil, 10
        call    sb_putc
        mov     edi, 1
        mov     rsi, [rel sb_base]
        add     rsi, r15
        mov     rdx, [rel sb_len]
        sub     rdx, r15
        call    write_all
        mov     [rel sb_len], r15
        test    rax, rax
        js      io_fail
        jmp     ret_nil

; (seek f offset [whence])
bi_seek:
        BI_ENTER
        NARGS_RANGE 2, 3
        mov     rdi, [rbx]
        call    file_fd
        mov     r13, rax
        NEED_INT [rbx+8]
        mov     r14, rax
        xor     r15d, r15d              ; SEEK_SET
        cmp     r12, 3
        jne     .go
        NEED    [rbx+16], T_STR
        lea     rbp, [rel whence_names]
.w:     mov     rsi, [rbp]
        test    rsi, rsi
        jz      .bad
        mov     rdi, [rbx+16]
        call    str_eq_cstr
        test    eax, eax
        jnz     .found
        add     rbp, 16
        jmp     .w
.found: mov     r15, [rbp+8]
.go:    mov     eax, SYS_LSEEK
        mov     edi, r13d
        mov     rsi, r14
        mov     edx, r15d
        syscall
        test    rax, rax
        js      .fail
        TAGINT  rax
        jmp     bi_ret
.fail:  BDIE    "seek failed"
.bad:   mov     rsi, [rbx+16]
        BDIEV   'invalid whence (use "start" "current" or "end")'

; (tell f)
bi_tell:
        BI_ENTER
        NARGS   1
        mov     rdi, [rbx]
        call    file_fd
        mov     edi, eax
        xor     esi, esi
        mov     edx, 1
        mov     eax, SYS_LSEEK
        syscall
        test    rax, rax
        js      bi_seek.fail
        TAGINT  rax
        jmp     bi_ret

; (exit [code])
bi_exit:
        BI_ENTER
        NARGS_RANGE 0, 1
        xor     edi, edi
        test    r12, r12
        jz      .go
        NEED_INT [rbx]
        mov     edi, eax
.go:    mov     eax, SYS_EXIT
        syscall

section .rodata
s_r:            db "r", 0
s_w:            db "w", 0
s_a:            db "a", 0
s_rp:           db "r+", 0
s_wp:           db "w+", 0
s_ap:           db "a+", 0
s_start:        db "start", 0
s_current:      db "current", 0
s_end:          db "end", 0

section .data
align 8
newline_str:    dq T_STR, 1
                db 10, 0, 0, 0, 0, 0, 0, 0
open_modes:     dq s_r,  O_RDONLY
                dq s_w,  O_WRONLY | O_CREAT | O_TRUNC
                dq s_a,  O_WRONLY | O_CREAT | O_APPEND
                dq s_rp, O_RDWR
                dq s_wp, O_RDWR | O_CREAT | O_TRUNC
                dq s_ap, O_RDWR | O_CREAT | O_APPEND
                dq 0, 0
whence_names:   dq s_start, 0
                dq s_current, 1
                dq s_end, 2
                dq 0, 0

section .text
