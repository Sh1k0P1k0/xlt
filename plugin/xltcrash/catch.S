# xltcrash - the exception handler (registered as the kernel's default one).
#
# The way in follows PSPLINK's default exception handler (psplinkusb,
# psplink/exception_asm.S, BSD licence, (c) 2005 James F.), which works on the
# 6.xx firmwares:
#
# - ExceptionManager has already saved v0 and v1 (COP0 control registers 4 and 5),
#   and the crash's EPC, Status and Cause (control registers 0, 2 and 3). The live
#   COP0 registers hold its own state by now, so those are what get saved.
# - The thread is moved into kernel mode as if it had made a system call: on its
#   kernel stack (control register 15) when it crashed in user mode, with a
#   syscall frame the thread manager links into the thread (control register 16),
#   k1 = 0, interrupts still off. Then a plain call into xltcrash_trap(slot); no
#   eret. (The first version returned with eret into C on a stack of its own, with
#   no syscall frame: the PSP froze and switched itself off at the first thread
#   switch.)
#
# One slot per crash (xltcrash.h). No slot left: on to the handler that was there
# before (s_prev), as if this one weren't here. xltcrash_trap never returns.

#include "xltcrash.h"

    .set noreorder
    .set noat

    .text
    .global xltcrash_handler
    .ent    xltcrash_handler
xltcrash_handler:
    nop                                 # the first instruction must be a nop
    nop

    lui     $v0, %hi(s_next)
    lw      $v1, %lo(s_next)($v0)
    addiu   $v1, $v1, -SLOTS
    bgez    $v1, 9f                     # all used
    addiu   $v1, $v1, SLOTS             # (delay slot: the index again)
    addiu   $v1, $v1, 1
    sw      $v1, %lo(s_next)($v0)
    addiu   $v1, $v1, -1
    sll     $v1, $v1, SLOT_SHIFT
    lui     $v0, %hi(s_slots)
    addiu   $v0, $v0, %lo(s_slots)
    addu    $v0, $v0, $v1               # v0 = the slot

    sw      $zero, S_R+0*4($v0)
    sw      $1,  S_R+1*4($v0)
    cfc0    $1, $4
    sw      $1,  S_R+2*4($v0)           # v0
    cfc0    $1, $5
    sw      $1,  S_R+3*4($v0)           # v1
    sw      $4,  S_R+4*4($v0)
    sw      $5,  S_R+5*4($v0)
    sw      $6,  S_R+6*4($v0)
    sw      $7,  S_R+7*4($v0)
    sw      $8,  S_R+8*4($v0)
    sw      $9,  S_R+9*4($v0)
    sw      $10, S_R+10*4($v0)
    sw      $11, S_R+11*4($v0)
    sw      $12, S_R+12*4($v0)
    sw      $13, S_R+13*4($v0)
    sw      $14, S_R+14*4($v0)
    sw      $15, S_R+15*4($v0)
    sw      $16, S_R+16*4($v0)
    sw      $17, S_R+17*4($v0)
    sw      $18, S_R+18*4($v0)
    sw      $19, S_R+19*4($v0)
    sw      $20, S_R+20*4($v0)
    sw      $21, S_R+21*4($v0)
    sw      $22, S_R+22*4($v0)
    sw      $23, S_R+23*4($v0)
    sw      $24, S_R+24*4($v0)
    sw      $25, S_R+25*4($v0)
    sw      $26, S_R+26*4($v0)
    sw      $27, S_R+27*4($v0)
    sw      $28, S_R+28*4($v0)
    sw      $29, S_R+29*4($v0)
    sw      $30, S_R+30*4($v0)
    sw      $31, S_R+31*4($v0)
    mfhi    $v1
    sw      $v1, S_HI($v0)
    mflo    $v1
    sw      $v1, S_LO($v0)
    mfc0    $v1, $8
    sw      $v1, S_BADVADDR($v0)
    cfc0    $v1, $3                     # the crash's Cause
    sw      $v1, S_CAUSE($v0)
    cfc0    $v1, $0                     # its EPC
    sw      $v1, S_EPC($v0)
    cfc0    $v1, $2                     # its Status
    sw      $v1, S_STATUS($v0)
    li      $1, 1
    sw      $1, S_STATE($v0)

    # into kernel mode, as PSPLINK does: crashed in user mode (KSU bits set), the
    # thread's kernel stack; in kernel mode, the stack it was on
    cfc0    $t0, $2
    cfc0    $t3, $15
    andi    $t2, $t0, 0x18
    movz    $t3, $sp, $t2
    addiu   $t3, $t3, -32               # a syscall frame for the thread manager
    sw      $t0, 0($t3)
    cfc0    $t0, $0
    sw      $t0, 4($t3)
    sw      $sp, 8($t3)
    sw      $ra, 0xC($t3)
    sw      $k1, 0x10($t3)
    sw      $zero, 0x14($t3)
    sw      $zero, 0x18($t3)
    sw      $zero, 0x1C($t3)
    cfc0    $t0, $16                    # linked into the thread, if it has a place for it
    beqz    $t0, 5f
    move    $sp, $t3
    lw      $t1, 0($t0)
    sw      $t1, 0x1C($sp)
    sw      $sp, 0($t0)
5:
    cfc0    $t0, $2
    li      $t1, 0xFFFFFFE4             # IE, EXL and KSU cleared: kernel mode, interrupts off
    and     $t0, $t0, $t1
    nop
    nop
    mtc0    $t0, $12
    nop
    move    $k1, $zero
    jal     xltcrash_trap               # never returns
    move    $a0, $v0
8:  b       8b
    nop

9:  # no slot: the previous handler, with everything still as it found it
    lui     $v0, %hi(s_prev)
    lw      $v0, %lo(s_prev)($v0)
    beqz    $v0, 8b
    nop
    jr      $v0
    nop
    .end    xltcrash_handler
