        ; ---- 4: carry / overflow flags ---------------------------
        moveq   #4, d3
        move.b  #$80, d1
        add.b   #$80, d1            ; $80+$80 = 0, C=1 V=1 Z=1
        bne     fail
        bcc     fail
        bvc     fail
        move.b  #$7F, d1
        add.b   #1, d1              ; $7F+1 = $80: V=1 N=1 C=0
        bvc     fail
        bcs     fail
        bpl     fail
