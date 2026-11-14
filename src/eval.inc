; ---------------------------------------------------------------------------
; slip - evaluator
;
; Special forms:
;   (def name value)            bind a global
;   (def name [params] body...) define a named function
;   (fn [params] body...)       anonymous function (closure)
;   (if c1 e1 c2 e2 ... else)   multi-branch conditional
;   (let [n1 v1 n2 v2] body...) local bindings (sequential)
;   (do e1 e2 ...)              evaluate in order, value of the last
;   (and ...) (or ...)          short-circuit logic
;
; Tail positions (if branches, last expr of do/let/fn bodies, last operand of
; and/or) are evaluated in a loop, so tail recursion runs in constant stack.
; ---------------------------------------------------------------------------

section .text

; eval(rdi = expr, rsi = env) -> rax
eval:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        cmp     rsp, [rel stack_limit]
        jb      .overflow
        mov     qword [rsp], 0          ; frame owned by this invocation
        mov     rbx, rdi
        mov     r12, rsi
.top:
        test    bl, 1
        jnz     .self
        test    rbx, rbx
        jz      .self
        mov     rax, [rbx]
        cmp     rax, T_SYM
        je      .sym
        cmp     rax, T_LIST
        je      .form
.self:  mov     rax, rbx
.ret:   add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret
.nil:   xor     eax, eax
        jmp     .ret
.true:  lea     rax, [rel true_obj]
        jmp     .ret
.false: lea     rax, [rel false_obj]
        jmp     .ret

.overflow:
        DIE     "stack overflow (recursion too deep)"

; ---- variable lookup: local frames first, then the symbol's global slot
.sym:   mov     rcx, r12
.frame: test    rcx, rcx
        jz      .global
        mov     rdx, [rcx+ENV_COUNT]
        lea     r8, [rcx+ENV_SLOTS]
.slot:  test    rdx, rdx
        jz      .parent
        cmp     [r8], rbx
        je      .found
        add     r8, 16
        dec     rdx
        jmp     .slot
.parent:
        mov     rcx, [rcx+ENV_PARENT]
        jmp     .frame
.found: mov     rax, [r8+8]
        jmp     .ret
.global:
        mov     rax, [rbx+SYM_VAL]
        cmp     rax, UNBOUND
        jne     .ret
        mov     rsi, rbx
        DIEV    "undefined name"

; ---- forms
.form:  mov     r14, [rbx+LIST_LEN]
        test    r14, r14
        jz      .nil
        mov     r13, [rbx+LIST_ITEMS]
        mov     rax, [r13]
        test    al, 1
        jnz     .apply
        test    rax, rax
        jz      .apply
        cmp     qword [rax], T_SYM
        jne     .apply
        mov     rdx, [rax+SYM_SPECIAL]
        test    rdx, rdx
        jz      .apply
        lea     rcx, [rel special_jt]
        jmp     [rcx+rdx*8]

; r13 = expressions, r14 = count; evaluate all, the last one in tail position
.seq:   test    r14, r14
        jz      .nil
.seql:  cmp     r14, 1
        je      .seqtail
        mov     rdi, [r13]
        mov     rsi, r12
        call    eval
        add     r13, 8
        dec     r14
        jmp     .seql
.seqtail:
        mov     rbx, [r13]
        jmp     .top

; ---- function application
.apply: mov     rdi, [r13]
        mov     rsi, r12
        call    eval
        mov     rbp, rax                ; callee
        dec     r14                     ; argc
        test    al, 1
        jnz     .notfn
        test    rax, rax
        jz      .notfn
        mov     rax, [rbp]
        cmp     rax, T_BUILTIN
        je      .callbi
        cmp     rax, T_FN
        je      .callfn
.notfn: mov     rsi, rbp
        DIEV    "not a function"

.callbi:
        lea     rax, [r14*8+15]
        and     rax, -16
        sub     rsp, rax
        mov     r15, rsp                ; argv
        xor     ebx, ebx
.bil:   cmp     rbx, r14
        jae     .bicall
        mov     rdi, [r13+rbx*8+8]
        mov     rsi, r12
        call    eval
        mov     [r15+rbx*8], rax
        inc     rbx
        jmp     .bil
.bicall:
        push    qword [rel cur_builtin]
        push    rbp
        mov     [rel cur_builtin], rbp
        mov     rdi, r14
        mov     rsi, r15
        call    [rbp+BI_CODE]
        pop     rbp
        pop     qword [rel cur_builtin]
        lea     rcx, [r14*8+15]
        and     rcx, -16
        add     rsp, rcx
        jmp     .ret

.callfn:
        cmp     r14, [rbp+FN_NPARAMS]
        jne     .arity
        ; evaluate the arguments into a temporary area on the stack
        lea     rax, [r14*8+15]
        and     rax, -16
        sub     rsp, rax
        mov     r15, rsp
        xor     ebx, ebx
.fnl:   cmp     rbx, r14
        jae     .fnframe
        mov     rdi, [r13+rbx*8+8]
        mov     rsi, r12
        call    eval
        mov     [r15+rbx*8], rax
        inc     rbx
        jmp     .fnl
.fnframe:
        ; reuse the frame this invocation created for an earlier call if no
        ; closure captured it (a tail call: nothing else can still see it)
        lea     rax, [r14*8+15]
        and     rax, -16
        mov     rbx, [r15+rax]          ; owned frame slot
        test    rbx, rbx
        jz      .fnnew
        cmp     qword [rbx+ENV_CAPTURED], 0
        jne     .fnnew
        cmp     [rbx+ENV_COUNT], r14
        je      .fnfill
.fnnew: mov     rdi, r14
        shl     rdi, 4
        add     rdi, ENV_SLOTS
        call    alloc
        mov     rbx, rax
        lea     rax, [r14*8+15]
        and     rax, -16
        mov     [r15+rax], rbx
.fnfill:
        mov     rax, [rbp+FN_ENV]
        mov     [rbx+ENV_PARENT], rax
        mov     [rbx+ENV_COUNT], r14
        mov     rsi, [rbp+FN_PARAMS]
        xor     ecx, ecx
.fncp:  cmp     rcx, r14
        jae     .fngo
        mov     rdx, rcx
        shl     rdx, 4
        mov     rax, [rsi+rcx*8]
        mov     [rbx+rdx+ENV_SLOTS], rax
        mov     rax, [r15+rcx*8]
        mov     [rbx+rdx+ENV_SLOTS+8], rax
        inc     rcx
        jmp     .fncp
.fngo:  lea     rax, [r14*8+15]
        and     rax, -16
        add     rsp, rax
        mov     r12, rbx
        mov     r13, [rbp+FN_BODY]
        mov     r14, [rbp+FN_NBODY]
        jmp     .seq
.arity: mov     rsi, rbp
        DIEV    "wrong number of arguments to"

; ---- (if c1 e1 c2 e2 ... [else])
.sf_if: mov     r15, 1
.ifl:   lea     rax, [r15+1]
        cmp     rax, r14
        jae     .ifelse
        mov     rdi, [r13+r15*8]
        mov     rsi, r12
        call    eval
        mov     rdi, rax
        call    truthy
        test    eax, eax
        jnz     .ifthen
        add     r15, 2
        jmp     .ifl
.ifthen:
        mov     rbx, [r13+r15*8+8]
        jmp     .top
.ifelse:
        cmp     r15, r14
        jae     .nil
        mov     rbx, [r13+r15*8]
        jmp     .top

; ---- (do e1 e2 ...)
.sf_do: add     r13, 8
        dec     r14
        jmp     .seq

; ---- (let [name value ...] body...)
.sf_let:
        cmp     r14, 2
        jb      .letbad
        mov     rdi, [r13+8]
        call    form_items
        test    dl, 1
        jnz     .letbad
        mov     rbp, rax                ; binding items
        mov     rbx, rdx
        shr     rbx, 1                  ; number of bindings
        mov     rdi, rbx
        shl     rdi, 4
        add     rdi, ENV_SLOTS
        call    alloc
        mov     r15, rax
        mov     [r15+ENV_PARENT], r12   ; count starts at 0
.letl:  mov     rcx, [r15+ENV_COUNT]
        cmp     rcx, rbx
        jae     .letbody
        shl     rcx, 4
        mov     rax, [rbp+rcx]
        test    al, 1
        jnz     .letname
        test    rax, rax
        jz      .letname
        cmp     qword [rax], T_SYM
        jne     .letname
        mov     [r15+rcx+ENV_SLOTS], rax
        mov     rdi, [rbp+rcx+8]
        mov     rsi, r15
        call    eval
        mov     rcx, [r15+ENV_COUNT]
        shl     rcx, 4
        mov     [r15+rcx+ENV_SLOTS+8], rax
        inc     qword [r15+ENV_COUNT]
        jmp     .letl
.letbody:
        mov     r12, r15
        add     r13, 16
        sub     r14, 2
        jmp     .seq
.letbad:
        DIE     "let: expected (let [name value ...] body...)"
.letname:
        mov     rsi, rax
        DIEV    "let: binding name must be a symbol"

; ---- (fn [params] body...)
.sf_fn: cmp     r14, 2
        jb      .fnbad
        mov     rdi, [r13+8]
        lea     rsi, [r13+16]
        lea     rdx, [r14-2]
        mov     rcx, r12
        xor     r8d, r8d
        call    make_closure
        jmp     .ret
.fnbad: DIE     "fn: expected (fn [params] body...)"

; ---- (def name value) / (def name [params] body...)
.sf_def:
        cmp     r14, 3
        jb      .defbad
        mov     rbp, [r13+8]
        test    bpl, 1
        jnz     .defname
        test    rbp, rbp
        jz      .defname
        cmp     qword [rbp], T_SYM
        jne     .defname
        cmp     r14, 3
        ja      .deffn
        mov     rdi, [r13+16]
        mov     rsi, r12
        call    eval
        test    al, 1
        jnz     .defset
        test    rax, rax
        jz      .defset
        cmp     qword [rax], T_FN
        jne     .defset
        cmp     qword [rax+FN_NAME], 0
        jne     .defset
        mov     [rax+FN_NAME], rbp
.defset:
        mov     [rbp+SYM_VAL], rax
        jmp     .ret
.deffn: mov     rdi, [r13+16]
        lea     rsi, [r13+24]
        lea     rdx, [r14-3]
        mov     rcx, r12
        mov     r8, rbp
        call    make_closure
        mov     [rbp+SYM_VAL], rax
        jmp     .ret
.defbad:
        DIE     "def: expected (def name value) or (def name [params] body...)"
.defname:
        mov     rsi, rbp
        DIEV    "def: name must be a symbol"

; ---- (and ...) / (or ...)
.sf_and:
        cmp     r14, 1
        je      .true
        mov     r15, 1
.andl:  lea     rax, [r15+1]
        cmp     rax, r14
        jae     .logtail
        mov     rdi, [r13+r15*8]
        mov     rsi, r12
        call    eval
        mov     rbx, rax
        mov     rdi, rax
        call    truthy
        test    eax, eax
        jz      .logval
        inc     r15
        jmp     .andl
.sf_or: cmp     r14, 1
        je      .false
        mov     r15, 1
.orl:   lea     rax, [r15+1]
        cmp     rax, r14
        jae     .logtail
        mov     rdi, [r13+r15*8]
        mov     rsi, r12
        call    eval
        mov     rbx, rax
        mov     rdi, rax
        call    truthy
        test    eax, eax
        jnz     .logval
        inc     r15
        jmp     .orl
.logval:
        mov     rax, rbx
        jmp     .ret
.logtail:
        mov     rbx, [r13+r15*8]
        jmp     .top

; form_items(rdi = form) -> rax = items ptr, rdx = count. Accepts both
; (a b c) and [a b c] (whose reader form starts with the list builtin).
form_items:
        test    dil, 1
        jnz     .bad
        test    rdi, rdi
        jz      .bad
        cmp     qword [rdi], T_LIST
        jne     .bad
        mov     rax, [rdi+LIST_ITEMS]
        mov     rdx, [rdi+LIST_LEN]
        test    rdx, rdx
        jz      .ok
        mov     rcx, [rax]
        cmp     rcx, [rel bi_list_obj]
        jne     .ok
        add     rax, 8
        dec     rdx
.ok:    ret
.bad:   mov     rsi, rdi
        DIEV    "expected a list of names"

; make_closure(rdi = params form, rsi = body ptr, rdx = body count,
;              rcx = env, r8 = name symbol or 0) -> rax
make_closure:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        mov     r12, rsi
        mov     r13, rdx
        mov     r14, rcx
        mov     r15, r8
        ; mark the captured environment chain so tail calls never reuse it
        mov     rax, rcx
.mark:  test    rax, rax
        jz      .marked
        cmp     qword [rax+ENV_CAPTURED], 0
        jne     .marked
        mov     qword [rax+ENV_CAPTURED], 1
        mov     rax, [rax+ENV_PARENT]
        jmp     .mark
.marked:
        call    form_items
        mov     rbx, rax
        xor     ecx, ecx
.chk:   cmp     rcx, rdx
        jae     .ok
        mov     rax, [rbx+rcx*8]
        test    al, 1
        jnz     .bad
        test    rax, rax
        jz      .bad
        cmp     qword [rax], T_SYM
        jne     .bad
        inc     rcx
        jmp     .chk
.bad:   mov     rsi, rax
        DIEV    "fn: parameter must be a name"
.ok:    push    rdx
        mov     edi, FN_SIZE
        call    alloc
        pop     rdx
        mov     qword [rax], T_FN
        mov     [rax+FN_NPARAMS], rdx
        mov     [rax+FN_PARAMS], rbx
        mov     [rax+FN_BODY], r12
        mov     [rax+FN_NBODY], r13
        mov     [rax+FN_ENV], r14
        mov     [rax+FN_NAME], r15
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; apply_value(rdi = function, rsi = argc, rdx = argv) -> rax
; Used by builtins (map, filter, reduce, ...) to call back into user code.
apply_value:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        mov     rbp, rdi
        mov     r14, rsi
        mov     r13, rdx
        test    bpl, 1
        jnz     .notfn
        test    rbp, rbp
        jz      .notfn
        mov     rax, [rbp]
        cmp     rax, T_BUILTIN
        je      .bi
        cmp     rax, T_FN
        je      .fn
.notfn: mov     rsi, rbp
        DIEV    "not a function"
.bi:    push    qword [rel cur_builtin]
        push    rbp
        mov     [rel cur_builtin], rbp
        mov     rdi, r14
        mov     rsi, r13
        call    [rbp+BI_CODE]
        pop     rbp
        pop     qword [rel cur_builtin]
        jmp     .out
.fn:    cmp     r14, [rbp+FN_NPARAMS]
        jne     .arity
        mov     rdi, r14
        shl     rdi, 4
        add     rdi, ENV_SLOTS
        call    alloc
        mov     r12, rax
        mov     rcx, [rbp+FN_ENV]
        mov     [r12+ENV_PARENT], rcx
        mov     [r12+ENV_COUNT], r14
        mov     rsi, [rbp+FN_PARAMS]
        xor     ecx, ecx
.cp:    cmp     rcx, r14
        jae     .body
        mov     rdx, rcx
        shl     rdx, 4
        mov     rax, [rsi+rcx*8]
        mov     [r12+rdx+ENV_SLOTS], rax
        mov     rax, [r13+rcx*8]
        mov     [r12+rdx+ENV_SLOTS+8], rax
        inc     rcx
        jmp     .cp
.body:  mov     r13, [rbp+FN_BODY]
        mov     r14, [rbp+FN_NBODY]
        xor     eax, eax
.bl:    test    r14, r14
        jz      .out
        mov     rdi, [r13]
        mov     rsi, r12
        call    eval
        add     r13, 8
        dec     r14
        jmp     .bl
.out:   add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret
.arity: mov     rsi, rbp
        DIEV    "wrong number of arguments to"

section .data
align 8
special_jt:     dq 0, eval.sf_if, eval.sf_def, eval.sf_fn, eval.sf_let
                dq eval.sf_do, eval.sf_and, eval.sf_or

section .text
