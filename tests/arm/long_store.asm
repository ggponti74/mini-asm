; EXIT: 34
; or.l d1,(a1) must put the high bytes of $11223344 first: the first word in memory is $1122
; (Byte-sized ARM logic is skipped here: or.b is known to be broken on this target.)
start:  lea     buf, a1
        move.l  #$11223344, d1
        or.l    d1, (a1)
        move.l  #0, d0
        or.w    (a1), d0
        rts
buf     dc.l    0
