; ---- 26: AND / OR / EOR with an immediate (ANDI, ORI, EORI) ----
        moveq   #26, d3
        move.l  #$FF00FF00, d1
        andi.l  #$0FF00FF0, d1
        cmp.l   #$0F000F00, d1
        bne     fail
        ori.l   #$000000F0, d1
        cmp.l   #$0F000FF0, d1
        bne     fail
        eori.l  #$0F000FF0, d1
        bne     fail                ; EORI sets Z when the result is 0
        move.l  #$80000000, d1
        ori.l   #1, d1
        bpl     fail                ; N from the result
        moveq   #27, d3
        move.l  #$1234ABCD, d1      ; .b and .w only touch the low part
        andi.b  #$0F, d1
        cmp.l   #$1234AB0D, d1
        bne     fail
        ori.w   #$8000, d1
        cmp.l   #$1234AB0D, d1      ; bit 15 of the word is already set (AB0D)
        bne     fail
        eori.w  #$FFFF, d1
        cmp.l   #$123454F2, d1
        bne     fail
        moveq   #28, d3
        move.l  #$00F000F0, d1      ; the plain AND/OR/EOR mnemonics take an immediate too
        and.l   #$0000FFFF, d1
        cmp.l   #$000000F0, d1
        bne     fail
        or.l    #$0F000000, d1
        cmp.l   #$0F0000F0, d1
        bne     fail
        eor.l   #$0F0000F0, d1
        bne     fail
        moveq   #29, d3
        lea     buf, a0             ; memory destination: big-endian layout
        move.l  #$11223344, (a0)
        andi.b  #$F0, (a0)
        lea     buf, a1
        addq.l  #2, a1
        andi.w  #$FF0F, (a1)        ; bytes 2-3: 33 44 -> 33 04
        move.l  (a0), d1
        cmp.l   #$10223304, d1
        bne     fail
        ori.l   #$000F0F00, (a0)
        move.l  (a0), d1
        cmp.l   #$102F3F04, d1
        bne     fail
        eori.b  #$FF, (a0)
        move.l  (a0), d1
        cmp.l   #$EF2F3F04, d1
        bne     fail
        moveq   #30, d3
        move.l  #100, d1            ; ADDI / SUBI
        addi.l  #23, d1
        cmp.l   #123, d1
        bne     fail
        subi.l  #124, d1
        bpl     fail                ; -1 -> N
        cmp.l   #-1, d1
        bne     fail
        move.l  #$1234FFFF, d1
        addi.w  #1, d1
        bne     fail                ; word result 0 -> Z
        cmp.l   #$12340000, d1
        bne     fail