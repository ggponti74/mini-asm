        ; ---- 12: stores put the high byte first (68K = big-endian) --
        moveq   #12, d3
        lea     buf, a0
        move.w  #$3344, (a0)
        move.b  (a0), d1
        cmp.b   #$33, d1            ; high byte at the lower address
        bne     fail
        lea     buf, a0
        move.l  #$11223344, (a0)
        move.b  (a0)+, d1
        cmp.b   #$11, d1
        bne     fail
        move.b  (a0)+, d1
        cmp.b   #$22, d1
        bne     fail
        move.b  (a0)+, d1
        cmp.b   #$33, d1
        bne     fail
        move.b  (a0)+, d1
        cmp.b   #$44, d1
        bne     fail

        ; ---- 13: dc.w / dc.l data is big-endian --------------------
        moveq   #13, d3
        lea     wdata, a0
        move.b  (a0), d1            ; dc.w $1234 -> $12 first
        cmp.b   #$12, d1
        bne     fail
        lea     ldata, a0
        move.b  (a0)+, d1
        cmp.b   #$AA, d1
        bne     fail
        move.b  (a0)+, d1
        cmp.b   #$BB, d1
        bne     fail
        move.b  (a0)+, d1
        cmp.b   #$CC, d1
        bne     fail
        move.b  (a0), d1
        cmp.b   #$DD, d1
        bne     fail

        ; ---- 14: word / long loads give the plain value + N,Z ------
        moveq   #14, d3
        lea     ldata, a0
        move.l  (a0), d1
        bpl     fail                ; $AABBCCDD is negative
        beq     fail
        cmp.l   #$AABBCCDD, d1
        bne     fail
        move.l  #$FFFFFFFF, d1
        lea     wdata, a0
        move.w  (a0), d1            ; only the low word changes
        bmi     fail                ; $1234 is positive
        cmp.l   #$FFFF1234, d1
        bne     fail
        lea     wneg, a0
        move.w  (a0), d1
        bpl     fail                ; $8001 is negative as a word
        cmp.w   #$8001, d1
        bne     fail

        ; ---- 15: register / immediate stores, flags ----------------
        moveq   #15, d3
        lea     buf, a0
        move.l  #$80000001, d1
        move.l  d1, (a0)
        bpl     fail                ; N from the plain value
        move.b  (a0), d2
        cmp.b   #$80, d2
        bne     fail
        move.w  #$7FFF, d1          ; d1 = $80007FFF
        move.w  d1, (a0)
        bmi     fail                ; word $7FFF is positive
        move.b  (a0), d2
        cmp.b   #$7F, d2
        bne     fail
        move.w  #$8000, (a0)
        bpl     fail
        move.l  #0, (a0)
        bne     fail
        move.l  (a0), d1
        bne     fail
