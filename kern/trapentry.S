/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
.text;                                 \
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps;                 \
.data;                                      \
    .long name;               



/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
.text;                                 \
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps;                           \
.data;                                      \
    .long name;                        

.data
    .globl trap_table
    trap_table:

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(trap_div    , T_DIVIDE )
TRAPHANDLER_NOEC(trap_dbg    , T_DEBUG  )
TRAPHANDLER_NOEC(trap_nmi    , T_NMI    )
TRAPHANDLER_NOEC(trap_brkpt  , T_BRKPT  )
TRAPHANDLER_NOEC(trap_oflow  , T_OFLOW  )
TRAPHANDLER_NOEC(trap_bound  , T_BOUND  )
TRAPHANDLER_NOEC(trap_illop  , T_ILLOP  )
TRAPHANDLER_NOEC(trap_dev    , T_DEVICE )
TRAPHANDLER     (trap_dblflt , T_DBLFLT )
TRAPHANDLER     (trap_coproc , T_COPROC )
TRAPHANDLER     (trap_tss    , T_TSS    )
TRAPHANDLER     (trap_segnp  , T_SEGNP  )
TRAPHANDLER     (trap_stk    , T_STACK  )
TRAPHANDLER     (trap_gpflt  , T_GPFLT  )
TRAPHANDLER     (trap_pgflt  , T_PGFLT  )
TRAPHANDLER     (trap_res    , T_RES    )
TRAPHANDLER_NOEC(trap_fperr  , T_FPERR  )
TRAPHANDLER_NOEC(trap_align  , T_ALIGN  )
TRAPHANDLER_NOEC(trap_mchk   , T_MCHK   )
TRAPHANDLER_NOEC(trap_simderr, T_SIMDERR)

/* 
 *  reserved traps, i'm setting them, to make setup cleaner in trap.c
 */
TRAPHANDLER_NOEC(trap_20, 20)
TRAPHANDLER_NOEC(trap_21, 21)
TRAPHANDLER_NOEC(trap_22, 22)
TRAPHANDLER_NOEC(trap_23, 23)
TRAPHANDLER_NOEC(trap_24, 24)
TRAPHANDLER_NOEC(trap_25, 25)
TRAPHANDLER_NOEC(trap_26, 26)
TRAPHANDLER_NOEC(trap_27, 27)
TRAPHANDLER_NOEC(trap_28, 28)
TRAPHANDLER_NOEC(trap_29, 29)
TRAPHANDLER_NOEC(trap_30, 30)
TRAPHANDLER_NOEC(trap_31, 31)

/*
 * IRQ interrupts
 */

TRAPHANDLER_NOEC(trap_irq_0 , IRQ_OFFSET +  0)
TRAPHANDLER_NOEC(trap_irq_1 , IRQ_OFFSET +  1)
TRAPHANDLER_NOEC(trap_irq_2 , IRQ_OFFSET +  2)
TRAPHANDLER_NOEC(trap_irq_3 , IRQ_OFFSET +  3)
TRAPHANDLER_NOEC(trap_irq_4 , IRQ_OFFSET +  4)
TRAPHANDLER_NOEC(trap_irq_5 , IRQ_OFFSET +  5)
TRAPHANDLER_NOEC(trap_irq_6 , IRQ_OFFSET +  6)
TRAPHANDLER_NOEC(trap_irq_7 , IRQ_OFFSET +  7)
TRAPHANDLER_NOEC(trap_irq_8 , IRQ_OFFSET +  8)
TRAPHANDLER_NOEC(trap_irq_9 , IRQ_OFFSET +  9)
TRAPHANDLER_NOEC(trap_irq_10, IRQ_OFFSET + 10)
TRAPHANDLER_NOEC(trap_irq_11, IRQ_OFFSET + 11)
TRAPHANDLER_NOEC(trap_irq_12, IRQ_OFFSET + 12)
TRAPHANDLER_NOEC(trap_irq_13, IRQ_OFFSET + 13)
TRAPHANDLER_NOEC(trap_irq_14, IRQ_OFFSET + 14)
TRAPHANDLER_NOEC(trap_irq_15, IRQ_OFFSET + 15)

/*
 * Syscall trap
 */

TRAPHANDLER_NOEC(trap_syscall, T_SYSCALL)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
    pushw $0
    pushw %ds
    pushw $0
    pushw %es
    pushal
    push %esp
    movw $GD_KD, %ax
    movw %ax, %ds
    movw %ax, %es
    call trap
