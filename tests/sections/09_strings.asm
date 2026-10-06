        ; ---- 9: string walk (strlen) -----------------------------
        moveq   #9, d3
        lea     msg, a0
        clr.l   d1
sl1:    tst.b   (a0)+
        beq     sl2
        addq.l  #1, d1
        bra     sl1
sl2:    cmp.l   #5, d1              ; "Hello"
        bne     fail
