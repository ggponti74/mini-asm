; ---- 23: .b / .w results leave the rest of the register alone ---
        ; (ADD/SUB/AND/OR/EOR/CLR on a register; bits 8-15 are the ones a
        ; careless byte write destroys)
        moveq   #23, d3
        moveq   #1, d2
        move.l  #$12348000, d1
        add.b   d2, d1              ; low byte 00 + 01
        cmp.l   #$12348001, d1
        bne     fail
        move.l  #$12348000, d1
        sub.b   d2, d1              ; 00 - 01 = FF, no borrow into bit 8
        cmp.l   #$123480FF, d1
        bne     fail
        moveq   #24, d3
        move.l  #$1234ABCD, d1
        move.l  #$0000000F, d2
        and.b   d2, d1
        cmp.l   #$1234AB0D, d1
        bne     fail
        move.l  #$12348000, d1
        moveq   #1, d2
        or.b    d2, d1
        cmp.l   #$12348001, d1
        bne     fail
        move.l  #$1234ABCD, d1
        move.l  #$000000FF, d2
        eor.b   d2, d1
        cmp.l   #$1234AB32, d1
        bne     fail
        move.l  #$1234ABCD, d1
        clr.b   d1
        cmp.l   #$1234AB00, d1
        bne     fail
        moveq   #25, d3
        move.l  #$1234FFFF, d1
        moveq   #1, d2
        add.w   d2, d1              ; FFFF + 1 = 0000, carry stays out of the upper word
        cmp.l   #$12340000, d1
        bne     fail
        sub.w   d2, d1
        cmp.l   #$1234FFFF, d1
        bne     fail
        move.l  #$1234ABCD, d1
        move.l  #$00000FF0, d2
        and.w   d2, d1
        cmp.l   #$12340BC0, d1
        bne     fail
        clr.w   d1
        cmp.l   #$12340000, d1
        bne     fail