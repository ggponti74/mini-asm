; EXIT: 52
; or.w (a1),d0 from dc.w $1234 must give $1234 (low byte $34), not $3412
start:  lea     wdata, a1
        move.l  #0, d0
        or.w    (a1), d0
        rts
wdata   dc.w    $1234
