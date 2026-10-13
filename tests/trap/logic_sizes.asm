; EXIT: 0
; The .b/.w ADD/SUB/AND/OR/EOR/CLR must leave the other bits of the register alone
; (ARM had a byte-merge bug that moved bits 8-15 into 24-31), and the immediate
; forms must work on ARM too. The checks are the shared test sections.
start:
        include "../sections/15_logic_sizes.asm"
        include "../sections/16_logic_immediate.asm"
        moveq   #0, d0
        rts
        include "../common.asm"