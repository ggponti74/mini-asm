        ; ---- 6: MULU / MULS / DIVU / DIVS ------------------------
        moveq   #6, d3
        move.l  #300, d1
        mulu    #100, d1            ; 30000
        cmp.l   #30000, d1
        bne     fail
        move.l  #30, d1
        muls    #-3, d1             ; -90
        cmp.l   #-90, d1
        bne     fail
        move.l  #100, d1
        divu    #7, d1              ; quotient 14, remainder 2
        cmp.l   #$0002000E, d1
        bne     fail
        move.l  #-100, d1
        divs    #7, d1              ; quotient -14, remainder -2
        cmp.l   #$FFFEFFF2, d1
        bne     fail
