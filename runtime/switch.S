/*
 * tf.S - assembly routines for switching trap frames
 */

/*
 * Trap Frame Format
 * WARNING: These values reflect the layout of struct thread_tf. Don't change
 * these values without also updating defs.h.
 */

.file "switch.S"
.text

#if defined(__x86_64__)

/* arguments registers (can be clobbered) */
#define RDI    (0)
#define RSI    (8)
#define RDX    (16)
#define RCX    (24)
#define R8     (32)
#define R9     (40)

/* temporary registers (can be clobbered) */
#define R10    (48)
#define R11    (56)

/* callee-saved registers (can not be clobbered) */
#define RBX    (64)
#define RBP    (72)
#define R12    (80)
#define R13    (88)
#define R14    (96)
#define R15    (104)

/* special-purpose registers */
#define RAX    (112)    /* return code */
#define RIP    (120)    /* instruction pointer */
#define RSP    (128)    /* stack pointer */

/**
 * __jmp_thread - executes a thread from the runtime
 * @tf: the trap frame to restore (%rdi)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does not return.
 */
.align 16
.globl __jmp_thread
.type __jmp_thread, @function
__jmp_thread:
    /* restore callee regs */
    movq    RBX(%rdi), %rbx
    movq    RBP(%rdi), %rbp
    movq    R12(%rdi), %r12
    movq    R13(%rdi), %r13
    movq    R14(%rdi), %r14
    movq    R15(%rdi), %r15

    /* restore ip and stack */
    movq    RSP(%rdi), %rsp
    movq    RIP(%rdi), %rsi

    /* set first argument (in case new thread) */
    movq    RDI(%rdi), %rdi /* ARG0 */

    /* re-enable preemption */
    subl    $1, %fs:preempt_cnt@tpoff
    jz      1f

    /* jump into trap frame */
    jmpq    *%rsi
    nop

1:    /* cold-path, save RIP and park the kthread */
    pushq   %rsi
    pushq   %rdi
    call    preempt
    popq    %rdi
    popq    %rsi
    jmpq    *%rsi

/**
 * __jmp_thread_direct - directly switches from one thread to the next
 * @oldtf: the trap frame to save (%rdi)
 * @newtf: the trap frame to restore (%rsi)
 * @stack_busy: a pointer to the busy stack flag owned by the old thread (%rdx)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does return.
 */
.align 16
.globl __jmp_thread_direct
.type __jmp_thread_direct, @function
__jmp_thread_direct:
    /* save return address and stack pointer */
    movq    (%rsp), %r8
    movq    %r8, RIP(%rdi)
    leaq    8(%rsp), %r8
    movq    %r8, RSP(%rdi)

    /* save callee regs */
    movq    %rbx, RBX(%rdi)
    movq    %rbp, RBP(%rdi)
    movq    %r12, R12(%rdi)
    movq    %r13, R13(%rdi)
    movq    %r14, R14(%rdi)
    movq    %r15, R15(%rdi)

    /* clear the stack busy flag */
    movl    $0,   (%rdx)

    /* restore callee regs */
    movq    RBX(%rsi), %rbx
    movq    RBP(%rsi), %rbp
    movq    R12(%rsi), %r12
    movq    R13(%rsi), %r13
    movq    R14(%rsi), %r14
    movq    R15(%rsi), %r15

    /* restore ip and stack */
    movq    RSP(%rsi), %rsp
    movq    RIP(%rsi), %rcx

    /* set first argument (in case new thread) */
    movq    RDI(%rsi), %rdi /* ARG0 */

    /* re-enable preemption */
    subl    $1, %fs:preempt_cnt@tpoff
    jz      1f

    /* jump into trap frame */
    jmpq    *%rcx
    nop

1:    /* cold-path, save RIP and park the kthread */
    pushq   %rcx
    pushq   %rdi
    call    preempt
    popq    %rdi
    popq    %rcx
    jmpq    *%rcx

/**
 * __jmp_runtime - saves the current trap frame and jumps to a function in the
 *                 runtime
 * @tf: the struct thread_tf to save state (%rdi)
 * @fn: the function pointer to call (%rsi)
 * @stack: the start of the runtime stack (%rdx)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime
.type __jmp_runtime, @function
__jmp_runtime:
    /* save callee regs */
    movq    %rbx,    RBX(%rdi)
    movq    %rbp,    RBP(%rdi)
    movq    %r12,    R12(%rdi)
    movq    %r13,    R13(%rdi)
    movq    %r14,    R14(%rdi)
    movq    %r15,    R15(%rdi)

    /* save ip and stack */
    movq    (%rsp),  %r8
    movq    %r8,     RIP(%rdi)
    leaq    8(%rsp), %r8
    movq    %r8,     RSP(%rdi)

    /* set runtime stack pointer */
    movq    %rdx,    %rsp

    /* jump into runtime code */
    jmpq    *%rsi

/**
 * __jmp_runtime_nosave - jumps to a function in the runtime without saving the
 *              current stack frame
 * @fn: the function pointer to call (%rdi)
 * @stack: the start of the runtime stack (%rsi)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime_nosave
.type __jmp_runtime_nosave, @function
__jmp_runtime_nosave:

    /* set runtime stack pointer */
    movq    %rsi, %rsp
    movq    %rdi, %rsi

    /* jump into runtime code */
    jmpq    *%rsi

#elif defined(__aarch64__)

/* arguments registers (can be clobbered) */
#define R0    (0)

/* callee-saved registers (can not be clobbered) */
#define R19   (8)
#define R20   (16)
#define R21   (24)
#define R22   (32)
#define R23   (40)
#define R24   (48)
#define R25   (56)
#define R26   (64)
#define R27   (72)
#define R28   (80)

/* special-purpose registers */
#define R29   (88)    /* frame register */
#define R30   (96)    /* link register */
#define IP    (104)   /* instruction pointer */
#define SP    (112)   /* stack pointer */

/**
 * __jmp_thread - executes a thread from the runtime
 * @tf: the trap frame to restore (x0)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does not return.
 */
.align 16
.globl __jmp_thread
.type __jmp_thread, @function
__jmp_thread:
    /* restore callee regs */
    ldp     x19, x20,  [x0, R19]
    ldp     x21, x22,  [x0, R21]
    ldp     x23, x24,  [x0, R23]
    ldp     x25, x26,  [x0, R25]
    ldp     x27, x28,  [x0, R27]

    /* restore LR and FR */
    ldp     x29, x30,  [x0, R29]

    /* fetch IP and SP */
    ldp     x11, x10,  [x0, IP]
    /* restore SP */
    mov     sp,  x10

    /* set first argument (in case new thread) */
    ldr     x0,  [x0,  R0] /* ARG0 */

    /* re-enable preemption */
    mrs     x9,  TPIDR_EL0
#if defined(TLS_LOCAL_EXEC)
    ldr     w10, [x9,  #:tprel_lo12: preempt_cnt]
    subs    w10, w10,  #1
    str     w10, [x9,  #:tprel_lo12: preempt_cnt]
#else
    adrp    x10, _GLOBAL_OFFSET_TABLE_
    ldr     x10, [x10,  #:gottprel_lo12: preempt_cnt]
    add     x9,  x9,    x10
    ldr     w10, [x9]
    subs    w10, w10,   #1
    str     w10, [x9]
#endif
    b.eq    7f

    /* jump into trap frame */
    br      x11
    nop

7:    /* cold-path, save RIP and park the kthread */
    stp     x11, x0,  [sp, #-16]!
    bl      preempt
    ldp     x11, x0,  [sp], #16
    br      x11

/**
 * __jmp_thread_direct - directly switches from one thread to the next
 * @oldtf: the trap frame to save (x0)
 * @newtf: the trap frame to restore (x1)
 * @stack_busy: a pointer to the busy stack flag owned by the old thread (x2)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does return.
 */
.align 16
.globl __jmp_thread_direct
.type __jmp_thread_direct, @function
__jmp_thread_direct:
    /* save the frame pointer */
    str     x29, [x0, R29]
    /* save return address (link register) and stack */
    mov     x29, sp
    stp     x30, x29,  [x0, IP]

    /* save callee regs */
    stp     x19, x20,  [x0, R19]
    stp     x21, x22,  [x0, R21]
    stp     x23, x24,  [x0, R23]
    stp     x25, x26,  [x0, R25]
    stp     x27, x28,  [x0, R27]

    /* clear the stack busy flag */
    str     xzr, [x2]

    /* restore callee regs */
    ldp     x19, x20,  [x1, R19]
    ldp     x21, x22,  [x1, R21]
    ldp     x23, x24,  [x1, R23]
    ldp     x25, x26,  [x1, R25]
    ldp     x27, x28,  [x1, R27]

    /* restore LR and FR */
    ldp     x29, x30,  [x1, R29]

    /* fetch IP and SP */
    ldp     x11, x10,  [x1, IP]
    /* restore SP */
    mov     sp,  x10

    /* set first argument (in case new thread) */
    ldr     x0,  [x1,  R0] /* ARG0 */

    /* re-enable preemption */
    mrs     x9,  TPIDR_EL0
#if defined(TLS_LOCAL_EXEC)
    ldr     w10, [x9,  #:tprel_lo12: preempt_cnt]
    subs    w10, w10,  #1
    str     w10, [x9,  #:tprel_lo12: preempt_cnt]
#else
    adrp    x10, _GLOBAL_OFFSET_TABLE_
    ldr     x10, [x10,  #:gottprel_lo12: preempt_cnt]
    add     x9,  x9,    x10
    ldr     w10, [x9]
    subs    w10, w10,   #1
    str     w10, [x9]
#endif
    b.eq    7f

    /* jump into trap frame */
    br      x11
    nop

7:    /* cold-path, save RIP and park the kthread */
    stp     x11,  x0,  [sp, #-16]!
    bl      preempt
    ldp     x11,  x0,  [sp], #8
    br      x11

/**
 * __jmp_runtime - saves the current trap frame and jumps to a function in the
 *                 runtime
 * @tf: the struct thread_tf to save state (x0)
 * @fn: the function pointer to call (x1)
 * @stack: the start of the runtime stack (x2)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime
.type __jmp_runtime, @function
__jmp_runtime:
    /* save callee regs */
    stp     x19, x20,  [x0, R19]
    stp     x21, x22,  [x0, R21]
    stp     x23, x24,  [x0, R23]
    stp     x25, x26,  [x0, R25]
    stp     x27, x28,  [x0, R27]

    /* save the frame pointer */
    str     x29, [x0, R29]
    /* save return address (link register) and stack */
    mov     x29, sp
    stp     x30, x29,  [x0, IP]

    /* set runtime stack pointer */
    mov     sp,  x2

    /* jump into runtime code */
    br      x1

/**
 * __jmp_runtime_nosave - jumps to a function in the runtime without saving the
 *              current stack frame
 * @fn: the function pointer to call (x0)
 * @stack: the start of the runtime stack (x1)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime_nosave
.type __jmp_runtime_nosave, @function
__jmp_runtime_nosave:

    /* set runtime stack pointer */
    mov     sp,  x1

    /* jump into runtime code */
    br      x0

#endif
