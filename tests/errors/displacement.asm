; EXIT: 0
; d(An) addressing on every target (the checks are shared with the test sections).
start:
        include "../sections/17_displacement.asm"
        moveq   #0, d0
        rts
        include "../common.asm"