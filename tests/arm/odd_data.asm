; EXIT: 7
; Data that isn't a multiple of 4 bytes at the end of the program used to
; misalign the epilogue (Bus error).
start:  move.l  #7, d0
        rts
msg     dc.b    "Hello", 0
