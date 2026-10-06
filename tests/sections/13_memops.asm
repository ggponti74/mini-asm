        ; ---- 16: memory to memory, word and long -------------------
        moveq   #16, d3
        lea     ldata, a0
        lea     buf2, a1
        move.l  (a0), (a1)
        bpl     fail                ; N from the plain value
        move.b  (a1), d1
        cmp.b   #$AA, d1
        bne     fail
        lea     wdata, a0
        lea     buf2, a1
        move.w  (a0)+, (a1)+
        move.w  (a1), d1            ; (a1) now at buf2+2: old bytes CC DD
        cmp.w   #$CCDD, d1
        bne     fail
        lea     wdata, a2
        addq.l  #2, a2
        cmpa.l  a2, a0              ; a0 advanced by 2
        bne     fail
        lea     buf2, a2
        move.w  (a2), d1
        cmp.w   #$1234, d1          ; copied in the same byte order
        bne     fail

        ; ---- 17: AND / OR / EOR with memory, .w and .l -------------
        moveq   #17, d3
        lea     buf, a0
        move.l  #$F0F0F0F0, (a0)
        move.l  #$0FF00FF0, d1
        and.l   (a0), d1            ; memory source -> $00F000F0
        bmi     fail
        beq     fail
        cmp.l   #$00F000F0, d1
        bne     fail
        eor.l   d1, (a0)            ; memory destination -> $F000F000
        bpl     fail
        move.l  (a0), d2
        cmp.l   #$F000F000, d2
        bne     fail
        move.l  #$00000F0F, d1
        or.l    d1, (a0)            ; -> $F000FF0F
        bpl     fail
        move.l  (a0), d2
        cmp.l   #$F000FF0F, d2
        bne     fail
        move.w  #$8001, (a0)
        move.l  #$000000FF, d1
        and.w   d1, (a0)            ; $8001 & $00FF = $0001
        bmi     fail
        beq     fail
        move.b  (a0)+, d2           ; first byte 00
        bne     fail
        move.b  (a0), d2
        cmp.b   #$01, d2
        bne     fail
        lea     buf, a0
        move.l  #$AAAA0010, d1
        or.w    (a0), d1            ; low word only: $0010 | $0001
        cmp.l   #$AAAA0011, d1
        bne     fail

        ; ---- 18: ADDQ / SUBQ to memory, .w and .l ------------------
        moveq   #18, d3
        lea     buf, a0
        move.w  #$00FF, (a0)
        addq.w  #1, (a0)            ; $0100
        bcs     fail
        bmi     fail
        beq     fail
        move.b  (a0), d1
        cmp.b   #$01, d1            ; carry rippled into the high byte
        bne     fail
        move.w  #$FFFF, (a0)
        addq.w  #1, (a0)            ; wraps to 0: Z=1, C=1
        bne     fail
        bcc     fail
        subq.w  #1, (a0)            ; borrows back to $FFFF: C=1, N=1
        bcc     fail
        bpl     fail
        move.l  #$000000FF, (a0)
        addq.l  #1, (a0)
        move.l  (a0), d1
        cmp.l   #$00000100, d1
        bne     fail
        move.l  #$00010000, (a0)
        subq.l  #1, (a0)
        move.l  (a0), d1
        cmp.l   #$0000FFFF, d1
        bne     fail
        lea     buf2, a0
        move.l  #$11111111, (a0)
        addq.l  #2, a0
        addq.w  #1, -(a0)           ; pre-decrement: touches exactly 2 bytes
        move.l  (a0), d1
        cmp.l   #$11121111, d1
        bne     fail

        ; ---- 19: TST.w / TST.l of memory (sign is in the first byte)
        moveq   #19, d3
        lea     buf, a0
        move.w  #$0080, (a0)        ; bytes 00 80: positive, non-zero
        tst.w   (a0)
        bmi     fail
        beq     fail
        move.w  #$8000, (a0)        ; bytes 80 00: negative
        tst.w   (a0)
        bpl     fail
        beq     fail
        move.w  #$0100, (a0)        ; bytes 01 00: non-zero
        tst.w   (a0)
        beq     fail
        move.l  #0, (a0)
        tst.l   (a0)
        bne     fail
        move.l  #$80000000, (a0)
        tst.l   (a0)
        bpl     fail
        move.l  #$00800000, (a0)
        tst.l   (a0)
        bmi     fail
        beq     fail

        ; ---- 20: A1-A7 pointers and MOVEA from memory --------------
        moveq   #20, d3
        lea     buf2, a1
        move.l  #$01020304, (a1)+
        move.w  #$0506, (a1)+
        move.w  -(a1), d1
        cmp.w   #$0506, d1
        bne     fail
        move.l  -(a1), d1
        cmp.l   #$01020304, d1
        bne     fail
        lea     wneg, a0
        move.w  (a0), a2            ; MOVEA.W sign-extends
        cmpa.l  #$FFFF8001, a2
        bne     fail
        lea     ldata, a0
        move.l  (a0), a2
        cmpa.l  #$AABBCCDD, a2
        bne     fail
        moveq   #0, d1              ; Z=1
        move.w  (a0), a2            ; MOVEA changes no flags
        bne     fail
        moveq   #1, d1              ; Z=0
        move.l  (a0), a2
        beq     fail

        ; ---- 21: other data registers as the register operand ------
        moveq   #21, d3
        lea     buf, a0
        move.l  #$A1B2C3D4, d7
        move.l  d7, (a0)
        move.b  (a0), d1
        cmp.b   #$A1, d1
        bne     fail
        move.w  d7, (a0)            ; low word C3D4 -> bytes C3 D4
        move.b  (a0), d1
        cmp.b   #$C3, d1
        bne     fail
        move.l  d4, (a0)            ; D4 (ESP) as a source
        move.l  (a0), d1
        cmp.l   d4, d1
        bne     fail
        move.w  d4, (a0)
        move.w  (a0), d1
        cmp.w   d4, d1
        bne     fail
        move.l  #$00FF00FF, d7
        move.l  #$0F0F0F0F, (a0)
        and.l   d7, (a0)            ; -> $000F000F
        move.l  (a0), d1
        cmp.l   #$000F000F, d1
        bne     fail
