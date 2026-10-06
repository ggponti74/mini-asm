        ; ---- 11: CMPA / CMPI / MOVEA / NOP ----------------------
        moveq   #11, d3
        nop
        lea     buf, a0
        lea     buf, a1
        cmpa.l  a0, a1
        bne     fail
        move.l  #5, a0
        cmpa.l  #5, a0
        bne     fail
        cmpi.w  #7, d3              ; d3 = 11 here, so NOT equal
        beq     fail
