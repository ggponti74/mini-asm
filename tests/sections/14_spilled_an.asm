        ; ---- 22: ADDQ / SUBQ on A1-A7 (kept in the register block) -
        moveq   #22, d3
        lea     buf, a2
        lea     buf, a3
        addq.l  #3, a2
        cmpa.l  a3, a2
        beq     fail                ; a2 moved
        subq.l  #3, a2
        cmpa.l  a3, a2
        bne     fail                ; and moved back
        addq.w  #4, a3              ; .w on An works on the whole register
        subq.w  #4, a3
        cmpa.l  a2, a3
        bne     fail
