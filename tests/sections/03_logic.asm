        ; ---- 3: AND / OR / EOR / CLR / TST -----------------------
        moveq   #3, d3
        move.l  #$FF00FF00, d1
        move.l  #$0FF00FF0, d2
        and.l   d2, d1
        cmp.l   #$0F000F00, d1
        bne     fail
        move.l  #$000000F0, d2
        or.l    d2, d1
        cmp.l   #$0F000FF0, d1
        bne     fail
        move.l  #$0F000FF0, d2
        eor.l   d2, d1
        bne     fail                ; EOR sets Z when result is 0
        clr.l   d1
        tst.l   d1
        bne     fail
        move.l  #$80000000, d1
        tst.l   d1
        bpl     fail                ; negative -> N set
        bmi     ok3
        bra     fail
ok3:
