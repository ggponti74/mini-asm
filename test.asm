start:  MOVE #0, D0
        RTS
data:   dc.b 1, 2, $FF, -1
        DC.W $1234, 513
ptr     dc.l start, data, later
later:  dc.l 0x10
