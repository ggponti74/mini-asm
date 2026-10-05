; selftest.asm - self-checking opcode test for mini-asm
; Exit code 0 = everything passed, N = test number N failed.
; D3 holds the current test number; every failed check jumps to "fail".

start:
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

        ; ---- 2: ADD / SUB / ADDQ / SUBQ / reg-reg ----------------
        moveq   #2, d3
        move.l  #100, d1
        add.l   #23, d1
        sub.l   #3, d1
        addq.l  #5, d1
        subq.l  #1, d1
        cmp.l   #124, d1
        bne     fail
        move.l  d1, d2
        add.l   d2, d1              ; 248
        sub.l   d2, d1              ; back to 124
        cmp.l   d2, d1
        bne     fail

        ; ---- 3: AND / OR / EOR / CLR / TST -----------------------
        moveq   #3, d3
        move.l  #$FF00FF00, d1
        move.l  #$0FF00FF0, d2
        and.l   d2, d1
        cmp.l   #$0F000F00, d1
        bne     fail
        move.l  #$000000F0, d2
        or.l    d2, d1
        cmp.l   #$0F000FF0, d1
        bne     fail
        move.l  #$0F000FF0, d2
        eor.l   d2, d1
        bne     fail                ; EOR sets Z when result is 0
        clr.l   d1
        tst.l   d1
        bne     fail
        move.l  #$80000000, d1
        tst.l   d1
        bpl     fail                ; negative -> N set
        bmi     ok3
        bra     fail
ok3:

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

        ; ---- 5: signed vs unsigned branches ----------------------
        moveq   #5, d3
        move.l  #-1, d1             ; $FFFFFFFF
        cmp.l   #1, d1
        bls     fail                ; unsigned: $FFFFFFFF > 1
        bhi     ok5a
        bra     fail
ok5a:   bge     fail                ; signed: -1 < 1
        blt     ok5b
        bra     fail
ok5b:   bgt     fail
        ble     ok5c
        bra     fail
ok5c:   cmp.l   d1, d1              ; equal
        bne     fail
        blo     fail
        bhs     ok5d                ; (BCC alias)
        bra     fail
ok5d:   bgt     fail
        bge     ok5e
        bra     fail
ok5e:

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

        ; ---- 7: DBRA loop (sum 1..10) ----------------------------
        moveq   #7, d3
        clr.l   d1
        move.w  #9, d2              ; DBRA runs count+1 times
        moveq   #1, d0
sumlp:  add.l   d0, d1
        addq.l  #1, d0
        dbra    d2, sumlp
        cmp.l   #55, d1
        bne     fail

        ; ---- 8: memory (An), (An)+, -(An), LEA -------------------
        moveq   #8, d3
        lea     buf, a0
        move.b  #$11, (a0)+
        move.b  #$22, (a0)+
        move.b  #$33, (a0)+
        move.b  #$44, (a0)+
        move.b  -(a0), d1
        cmp.b   #$44, d1
        bne     fail
        move.b  -(a0), d1           ; $33
        cmp.b   #$33, d1
        bne     fail
        lea     buf, a0
        move.b  (a0), d1
        cmp.b   #$11, d1
        bne     fail
        lea     buf, a1             ; other address register
        move.b  (a1)+, d1
        move.b  (a1), d2
        cmp.b   #$22, d2
        bne     fail

        ; ---- 9: string walk (strlen) -----------------------------
        moveq   #9, d3
        lea     msg, a0
        clr.l   d1
sl1:    tst.b   (a0)+
        beq     sl2
        addq.l  #1, d1
        bra     sl1
sl2:    cmp.l   #5, d1              ; "Hello"
        bne     fail

        ; ---- 10: BSR / RTS, JSR (An), JMP ------------------------
        moveq   #10, d3
        move.l  #21, d1
        bsr     dbl
        cmp.l   #42, d1
        bne     fail
        lea     dbl, a1
        jsr     (a1)
        cmp.l   #84, d1
        bne     fail
        jmp     skip
        bra     fail                ; must be skipped
skip:

        ; ---- 11: CMPA / CMPI / MOVEA / NOP ----------------------
        moveq   #11, d3
        nop
        lea     buf, a0
        lea     buf, a1
        cmpa.l  a0, a1
        bne     fail
        move.l  #5, a0
        cmpa.l  #5, a0
        bne     fail
        cmpi.w  #7, d3              ; d3 = 11 here, so NOT equal
        beq     fail

        ; ---- 12: word/long byte order in memory (68K = big-endian) -
        ; Known to fail until the endianness question is decided.
        moveq   #12, d3
        lea     buf, a0
        move.w  #$3344, (a0)
        move.b  (a0), d1
        cmp.b   #$33, d1            ; high byte must sit at the lower address
        bne     fail
        moveq   #13, d3
        lea     wdata, a0
        move.b  (a0), d1            ; dc.w $1234 -> $12 first
        cmp.b   #$12, d1
        bne     fail

        ; ---- all passed ------------------------------------------
        moveq   #0, d0
        rts

fail:   move.l  d3, d0
        rts

dbl:    add.l   d1, d1
        rts

INCLUDE "test_data.asm"