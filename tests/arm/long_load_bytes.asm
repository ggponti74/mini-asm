; EXIT: 68
; bytes 11 22 33 44 in memory read back as the long $11223344 (low byte $44)
start:  lea     bytes, a1
        move.l  #0, d0
        or.l    (a1), d0
        rts
bytes   dc.b    $11, $22, $33, $44
