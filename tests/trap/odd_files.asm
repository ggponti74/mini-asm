; odd_size.asm - TRAP #0 still reaches the dispatcher when the program's size
; is not a multiple of 4 (the ARM writer pads the code, so the dispatcher
; address has to include the padding). Writes 3 bytes to stdout.
; Exit 0 = pass, N = test N failed.
start:
        moveq   #1, d7
        move.l  #1, d1              ; fd 1 = stdout
        lea     msg, a0
        move.l  #3, d2
        move.l  #2, d0              ; service 2 = write
        trap    #0
        cmp.l   #3, d0
        bne     fail
        moveq   #0, d0
        rts
fail:   move.l  d7, d0
        rts
msg     dc.b    "ok", 10
        dc.b    0
        dc.b    0
        dc.b    0