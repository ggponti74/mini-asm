        ; ---- 2: ADD / SUB / ADDQ / SUBQ / reg-reg ----------------
        moveq   #2, d3
        move.l  #100, d1
        add.l   #23, d1
        sub.l   #3, d1
        addq.l  #5, d1
        subq.l  #1, d1
        cmp.l   #124, d1
        bne     fail
        move.l  d1, d2
        add.l   d2, d1              ; 248
        sub.l   d2, d1              ; back to 124
        cmp.l   d2, d1
        bne     fail
