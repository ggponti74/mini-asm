; EXIT: 68
; a store must leave the register unchanged (swap, store, swap back)
start:  lea     buf, a1
        move.l  #$11223344, d1
        or.l    d1, (a1)
        move.l  #0, d0
        or.l    d1, d0              ; d0 = $11223344, low byte $44
        rts
buf     dc.l    0
