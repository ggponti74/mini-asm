; main.asm - self-checking opcode test for mini-asm
; Exit code 0 = everything passed, N = test number N failed.
; D3 holds the current test number; every failed check jumps to "fail".
; Each section can also be run on its own: see tests/run.sh.

start:
        include "sections/01_moves.asm"
        include "sections/02_addsub.asm"
        include "sections/03_logic.asm"
        include "sections/04_flags.asm"
        include "sections/05_branches.asm"
        include "sections/06_muldiv.asm"
        include "sections/07_dbra.asm"
        include "sections/08_memory.asm"
        include "sections/09_strings.asm"
        include "sections/10_calls.asm"
        include "sections/11_cmp_misc.asm"
        include "sections/12_endian.asm"
        include "sections/13_memops.asm"
        include "sections/14_spilled_an.asm"

        ; ---- all passed ------------------------------------------
        moveq   #0, d0
        rts

        include "common.asm"
