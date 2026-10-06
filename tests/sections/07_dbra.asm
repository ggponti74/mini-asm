        ; ---- 7: DBRA loop (sum 1..10) ----------------------------
        moveq   #7, d3
        clr.l   d1
        move.w  #9, d2              ; DBRA runs count+1 times
        moveq   #1, d0
sumlp:  add.l   d0, d1
        addq.l  #1, d0
        dbra    d2, sumlp
        cmp.l   #55, d1
        bne     fail
