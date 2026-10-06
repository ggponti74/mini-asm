; EXIT: 255
; addq.w on memory holding $01FE (bytes 01 FE) must give $01FF
start:  lea     wdata, a1
        addq.w  #1, (a1)
        move.l  #0, d0
        or.w    (a1), d0
        rts
wdata   dc.w    $01FE
