; EXIT: 221
; or.l (a1),d0 from dc.l $AABBCCDD must give $AABBCCDD (low byte $DD), not $DDCCBBAA
start:  lea     ldata, a1
        move.l  #0, d0
        or.l    (a1), d0
        rts
ldata   dc.l    $AABBCCDD
