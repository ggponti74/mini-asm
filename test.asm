start:  lea     string, a0
        lea     string, a1
        move    (a0), d0
        move    (a1), d1
        cmp     d0, d1
        bne     nope
        move    #0, d0
        rts

nope:   move    #5, d0
        rts

a       dc.b    "a", 0
b       dc.b    "b", 0
