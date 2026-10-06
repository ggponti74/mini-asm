        ; ---- 1: MOVEQ / MOVE / CMP, sizes ------------------------
        moveq   #1, d3
        moveq   #-1, d1
        cmp.l   #-1, d1
        bne     fail
        move.l  #$12345678, d1
        move.b  #$FF, d1            ; only low byte changes
        cmp.l   #$123456FF, d1
        bne     fail
        move.w  #$AAAA, d1          ; only low word changes
        cmp.l   #$1234AAAA, d1
        bne     fail
