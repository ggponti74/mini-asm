; ---- 31: d(An) with A0 (a real register on x86) ----------------
        ; darea is 600 bytes, dend the label right behind it
        moveq   #31, d3
        lea     darea, a0
        move.l  #$11223344, 0(a0)       ; bytes 11 22 33 44
        move.l  #$55667788, 4(a0)       ; bytes 55 66 77 88
        move.l  4(a0), d1
        cmp.l   #$55667788, d1
        bne     fail
        move.l  #$FFFF0000, d1
        move.w  2(a0), d1               ; the word at +2 is 33 44
        cmp.l   #$FFFF3344, d1
        bne     fail
        move.b  5(a0), d1               ; the byte at +5 is 66
        cmp.l   #$FFFF3366, d1
        bne     fail
        move.b  #$AB, 1(a0)             ; store a byte in the middle of the long
        move.l  0(a0), d1
        cmp.l   #$11AB3344, d1
        bne     fail
        move.w  #$BEEF, 6(a0)
        move.l  4(a0), d1
        cmp.l   #$5566BEEF, d1
        bne     fail
        ; ---- 32: the same with A3 (a stand-in register on x86) ---------
        moveq   #32, d3
        lea     darea, a3
        move.l  #$11223344, 0(a3)
        move.l  #$55667788, 4(a3)
        move.l  4(a3), d1
        cmp.l   #$55667788, d1
        bne     fail
        move.l  #$FFFF0000, d1
        move.w  2(a3), d1
        cmp.l   #$FFFF3344, d1
        bne     fail
        move.b  5(a3), d1
        cmp.l   #$FFFF3366, d1
        bne     fail
        move.b  #$AB, 1(a3)
        move.l  0(a3), d1
        cmp.l   #$11AB3344, d1
        bne     fail
        ; ---- 33: large and negative displacements ----------------------
        moveq   #33, d3
        lea     darea, a0
        move.l  #$CAFEBABE, 596(a0)     ; the last long of the area
        move.l  #$0BADF00D, 300(a0)
        lea     dend, a0
        move.l  -4(a0), d1
        cmp.l   #$CAFEBABE, d1
        bne     fail
        move.l  -300(a0), d1
        cmp.l   #$0BADF00D, d1
        bne     fail
        lea     dend, a3
        move.l  -300(a3), d1
        cmp.l   #$0BADF00D, d1
        bne     fail
        move.l  #$13572468, -128(a3)    ; the disp8 / disp32 encoding boundary
        move.l  #$24681357, -132(a3)    ; (-129 and below need a 32-bit displacement)
        move.l  -128(a3), d1
        cmp.l   #$13572468, d1
        bne     fail
        move.l  -132(a3), d1
        cmp.l   #$24681357, d1
        bne     fail
        ; ---- 34: flags of a store through d(An) ------------------------
        moveq   #34, d3
        lea     darea, a0
        move.l  #0, 8(a0)
        bne     fail                    ; Z
        move.l  #$80000000, 8(a0)
        bpl     fail                    ; N
        move.l  8(a0), d1
        cmp.l   #$80000000, d1
        bne     fail
        ; ---- 35: LEA d(An),Am ------------------------------------------
        moveq   #35, d3
        lea     darea, a0
        lea     12(a0), a1              ; a1 = darea + 12
        move.l  #$01020304, (a1)
        move.l  12(a0), d1
        cmp.l   #$01020304, d1
        bne     fail
        lea     -8(a1), a2              ; a2 = darea + 4
        move.l  #$0A0B0C0D, (a2)
        move.l  4(a0), d1
        cmp.l   #$0A0B0C0D, d1
        bne     fail
        lea     100(a0), a0             ; An = An + d
        move.l  #$55AA55AA, (a0)
        lea     darea, a3
        move.l  100(a3), d1
        cmp.l   #$55AA55AA, d1
        bne     fail
        ; ---- 36: other instructions with d(An) -------------------------
        moveq   #36, d3
        lea     darea, a0
        move.l  #$F0F0F0F0, 0(a0)
        move.l  #$FFFF00FF, d1
        and.l   0(a0), d1               ; source d(An)
        cmp.l   #$F0F000F0, d1
        bne     fail
        or.b    3(a0), d1
        cmp.l   #$F0F000F0, d1
        bne     fail
        eor.l   d1, 0(a0)               ; destination d(An)
        move.l  0(a0), d1
        cmp.l   #$0000F000, d1          ; F0F0F0F0 eor F0F000F0
        bne     fail
        move.l  #$12345678, 4(a0)
        clr.w   6(a0)
        move.l  4(a0), d1
        cmp.l   #$12340000, d1
        bne     fail
        not.b   4(a0)
        move.l  4(a0), d1
        cmp.l   #$ED340000, d1
        bne     fail
        move.l  #1, 8(a0)
        neg.l   8(a0)
        move.l  8(a0), d1
        cmp.l   #-1, d1
        bne     fail
        addq.l  #1, 8(a0)               ; -1 + 1 = 0
        bne     fail
        subq.w  #1, 10(a0)
        move.l  8(a0), d1
        cmp.l   #$0000FFFF, d1
        bne     fail
        tst.b   8(a0)                   ; the byte at +8 is 0
        bne     fail
        btst    #0, 11(a0)              ; bit 0 of byte +11 (FF) is set
        beq     fail
        bclr    #0, 11(a0)
        move.l  8(a0), d1
        cmp.l   #$0000FFFE, d1
        bne     fail
        move.l  #$40000000, 12(a0)
        lsl.w   12(a0)                  ; word 4000 -> 8000
        move.l  12(a0), d1
        cmp.l   #$80000000, d1
        bne     fail
        ; ---- 37: An is also the source or the target -------------------
        moveq   #37, d3
        lea     darea, a0
        move.l  a0, 16(a0)              ; stores the old a0
        move.l  16(a0), a0              ; loads through the old a0, overwrites a0
        lea     darea, a1
        move.l  a0, d1
        move.l  a1, d2
        cmp.l   d2, d1
        bne     fail
        ; ---- 38: displacement from an EQU, hex, explicit sign ----------
DOFF    equ     20
        moveq   #38, d3
        lea     darea, a0
        move.l  #$13131313, DOFF(a0)
        move.l  #$24242424, $18(a0)     ; $18 = 24
        move.l  #$35353535, +28(a0)
        lea     dend, a1
        move.l  20(a0), d1
        cmp.l   #$13131313, d1
        bne     fail
        move.l  24(a0), d1
        cmp.l   #$24242424, d1
        bne     fail
        move.l  28(a0), d1
        cmp.l   #$35353535, d1
        bne     fail
        move.l  DOFF(a0), d1
        cmp.l   #$13131313, d1
        bne     fail