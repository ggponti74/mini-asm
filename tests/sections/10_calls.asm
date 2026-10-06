        ; ---- 10: BSR / RTS, JSR (An), JMP ------------------------
        moveq   #10, d3
        move.l  #21, d1
        bsr     dbl
        cmp.l   #42, d1
        bne     fail
        lea     dbl, a1
        jsr     (a1)
        cmp.l   #84, d1
        bne     fail
        jmp     skip
        bra     fail                ; must be skipped
skip:
