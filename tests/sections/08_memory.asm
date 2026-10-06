        ; ---- 8: memory (An), (An)+, -(An), LEA -------------------
        moveq   #8, d3
        lea     buf, a0
        move.b  #$11, (a0)+
        move.b  #$22, (a0)+
        move.b  #$33, (a0)+
        move.b  #$44, (a0)+
        move.b  -(a0), d1
        cmp.b   #$44, d1
        bne     fail
        move.b  -(a0), d1           ; $33
        cmp.b   #$33, d1
        bne     fail
        lea     buf, a0
        move.b  (a0), d1
        cmp.b   #$11, d1
        bne     fail
        lea     buf, a1             ; other address register
        move.b  (a1)+, d1
        move.b  (a1), d2
        cmp.b   #$22, d2
        bne     fail
