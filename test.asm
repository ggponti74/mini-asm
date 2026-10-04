start:  move    #a, d0
        move    #b, d1
        cmp     d0, d1
        bne     nope
        move    #0, d0
        rts

nope:   move    #5, d0
        rts

a       equ     1
b       equ     2
