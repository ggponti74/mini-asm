diff --git a/lib/std.asm b/lib/std.asm
new file mode 100644
index 0000000..462fb48
--- /dev/null
+++ b/lib/std.asm
@@ -0,0 +1,124 @@
+; std.asm - small runtime library for mini-asm programs, written in plain 68K
+; on top of the TRAP #0 services (see src/core.c). Use it with
+;
+;         include "lib/std.asm"        ; path is relative to the including file
+;
+; It may sit anywhere in your program: it jumps over itself, so it never
+; runs by accident. Include it once. Call the routines with BSR.
+;
+; Routine         In                          Out / what it clobbers
+; --------------  --------------------------  ------------------------------------
+; strlen          A0 = NUL-terminated string  D0 = length (without the NUL)
+;                                             clobbers A1; A0 is kept
+; strcpy          A0 = source, A1 = dest      D0 = length copied (without the NUL)
+;                                             the NUL is copied too; A0 and A1 are
+;                                             left just past it
+; print           A0 = NUL-terminated string  writes it to stdout
+;                                             D0 = bytes written, or negative error
+;                                             clobbers D1, D2, A1; A0 is kept
+; println         A0 = NUL-terminated string  like print, then a newline
+; utoa            D0 = unsigned 32-bit value  writes decimal digits + NUL at A0
+;                 A0 = buffer (11 bytes)      D0 = number of digits
+;                                             clobbers D1, D2, A1, A2; A0 is kept
+; print_dec       D0 = unsigned 32-bit value  prints it in decimal to stdout
+;                                             D0 = bytes written, or negative error
+;                                             clobbers D1, D2, A0, A1, A2
+;
+; Everything else (D3, D5, D7, A3-A7) is preserved. D4 is ESP and D6 shares
+; ESI with A0 on the x86 target, so these routines never touch D4 or D6.
+; The routines are not re-entrant (print_dec uses one static buffer) and
+; print does not retry a short write.
+;
+; These names are global labels; an internal label starts with _std_.
+
+        bra     _std_end
+
+; ---- strlen ----------------------------------------------------------
+strlen:
+        move.l  a0, a1
+        clr.l   d0
+_std_strlen1:
+        tst.b   (a1)+
+        beq     _std_strlen2
+        addq.l  #1, d0
+        bra     _std_strlen1
+_std_strlen2:
+        rts
+
+; ---- strcpy ----------------------------------------------------------
+strcpy:
+        clr.l   d0
+_std_strcpy1:
+        move.b  (a0)+, (a1)+        ; sets Z when the NUL has been copied
+        beq     _std_strcpy2
+        addq.l  #1, d0
+        bra     _std_strcpy1
+_std_strcpy2:
+        rts
+
+; ---- print / println -------------------------------------------------
+print:
+        bsr     strlen              ; D0 = length, A0 kept
+        move.l  d0, d2              ; D2 = length
+        moveq   #2, d0              ; service 2 = write
+        moveq   #1, d1              ; fd 1 = stdout
+        trap    #0                  ; A0 = buffer
+        rts
+
+println:
+        bsr     print
+        tst.l   d0
+        bmi     _std_println1       ; the string failed: skip the newline
+        lea     _std_nl, a0
+        bsr     print
+_std_println1:
+        rts
+
+; ---- utoa ------------------------------------------------------------
+; Unsigned decimal by repeated subtraction of 10^9 ... 10^0: there is no
+; shift or SWAP here, and DIVU.W only has a 16-bit quotient, so this is
+; the simple way to cover the whole 32-bit range.
+utoa:
+        move.l  a0, a1              ; A1 = write pointer, A0 stays at the start
+        lea     _std_pow10, a2      ; A2 walks the table of powers of ten
+        move.l  d0, d1              ; D1 = what is left to convert
+_std_utoa1:
+        move.l  (a2)+, d2           ; D2 = current power of ten
+        moveq   #0, d0              ; D0 = this digit
+_std_utoa2:
+        cmp.l   d2, d1
+        blo     _std_utoa3          ; value < power: digit is done
+        sub.l   d2, d1
+        addq.l  #1, d0
+        bra     _std_utoa2
+_std_utoa3:
+        tst.l   d0
+        bne     _std_utoa4          ; non-zero digit: write it
+        cmpa.l  a0, a1
+        bne     _std_utoa4          ; digits already started: zeros count
+        cmp.l   #1, d2
+        bne     _std_utoa1          ; leading zero: skip (but 10^0 always prints)
+_std_utoa4:
+        add.b   #48, d0             ; '0' + digit
+        move.b  d0, (a1)+
+        cmp.l   #1, d2
+        bne     _std_utoa1          ; more powers to go
+        clr.b   (a1)                ; terminating NUL
+        move.l  a1, d0
+        move.l  a0, d1
+        sub.l   d1, d0              ; D0 = number of digits
+        rts
+
+; ---- print_dec -------------------------------------------------------
+print_dec:
+        lea     _std_numbuf, a0
+        bsr     utoa
+        bra     print               ; tail call: print returns to our caller
+
+; ---- data ------------------------------------------------------------
+_std_nl:     dc.b    10, 0
+_std_numbuf: dc.b    0,0,0,0,0,0,0,0,0,0,0,0
+_std_pow10:  dc.l    1000000000, 100000000, 10000000, 1000000, 100000
+             dc.l    10000, 1000, 100, 10, 1
+
+_std_end:
diff --git a/tests/trap/std_print.asm b/tests/trap/std_print.asm
new file mode 100644
index 0000000..088ed42
--- /dev/null
+++ b/tests/trap/std_print.asm
@@ -0,0 +1,29 @@
+; EXIT: 0
+; STDOUT: Hello 0 7 10 100 1234567890 4294967295 end
+; lib/std.asm: print, print_dec, println (and the whole 32-bit unsigned range)
+        include "../../lib/std.asm"
+start:  lea     hello, a0
+        bsr     print
+        moveq   #0, d0
+        bsr     num
+        moveq   #7, d0
+        bsr     num
+        moveq   #10, d0
+        bsr     num
+        moveq   #100, d0
+        bsr     num
+        move.l  #1234567890, d0
+        bsr     num
+        move.l  #$FFFFFFFF, d0
+        bsr     num
+        lea     endmsg, a0
+        bsr     println
+        moveq   #0, d1              ; exit(0)
+        moveq   #1, d0
+        trap    #0
+num:    bsr     print_dec           ; value in D0
+        lea     space, a0
+        bra     print               ; tail call
+hello:  dc.b    "Hello ", 0
+space:  dc.b    " ", 0
+endmsg: dc.b    "end", 0
diff --git a/tests/trap/std_strings.asm b/tests/trap/std_strings.asm
new file mode 100644
index 0000000..14ffb7a
--- /dev/null
+++ b/tests/trap/std_strings.asm
@@ -0,0 +1,164 @@
+; EXIT: 0
+; STDOUT: Hello12345
+; lib/std.asm: strlen, strcpy, utoa, and the return values and preserved
+; registers of print / print_dec. On a failure the exit status is the number
+; of the check (kept in D3).
+        include "../../lib/std.asm"
+start:  move.l  #$DEADBEEF, d5      ; these must survive every library call
+        move.l  #$CAFEF00D, d7
+        lea     tbuf, a3
+
+        ; ---- 1: strlen("") --------------------------------------
+        moveq   #1, d3
+        lea     empty, a0
+        bsr     strlen
+        tst.l   d0
+        bne     fail
+        ; ---- 2: strlen("Hello"), A0 is kept ----------------------
+        moveq   #2, d3
+        lea     hello, a0
+        bsr     strlen
+        cmp.l   #5, d0
+        bne     fail
+        lea     hello, a2
+        cmpa.l  a2, a0
+        bne     fail
+        ; ---- 3: strcpy: length, NUL copied, pointers end past it --
+        moveq   #3, d3
+        lea     hello, a0
+        lea     dbuf, a1
+        bsr     strcpy
+        cmp.l   #5, d0
+        bne     fail
+        lea     hello, a2
+        addq.l  #6, a2
+        cmpa.l  a2, a0
+        bne     fail
+        lea     dbuf, a2
+        addq.l  #6, a2
+        cmpa.l  a2, a1
+        bne     fail
+        lea     dbuf, a0
+        bsr     strlen
+        cmp.l   #5, d0
+        bne     fail
+        ; ---- 4: strcpy of "" overwrites junk with just the NUL ----
+        moveq   #4, d3
+        lea     empty, a0
+        lea     dbuf, a1
+        bsr     strcpy
+        tst.l   d0
+        bne     fail
+        lea     dbuf, a0
+        bsr     strlen
+        tst.l   d0
+        bne     fail
+        ; ---- 5..11: utoa ------------------------------------------
+        moveq   #5, d3
+        moveq   #0, d0
+        lea     s0, a4
+        bsr     chk_utoa
+        moveq   #6, d3
+        moveq   #7, d0
+        lea     s7, a4
+        bsr     chk_utoa
+        moveq   #7, d3
+        moveq   #10, d0
+        lea     s10, a4
+        bsr     chk_utoa
+        moveq   #8, d3
+        moveq   #100, d0
+        lea     s100, a4
+        bsr     chk_utoa
+        moveq   #9, d3
+        move.l  #1000, d0
+        lea     s1000, a4
+        bsr     chk_utoa
+        moveq   #10, d3
+        move.l  #1000000000, d0
+        lea     s1e9, a4
+        bsr     chk_utoa
+        moveq   #11, d3
+        move.l  #$FFFFFFFF, d0
+        lea     smax, a4
+        bsr     chk_utoa
+        ; ---- 12: print: bytes written, A0 kept; "" writes 0 -------
+        moveq   #12, d3
+        lea     hello, a0
+        bsr     print
+        cmp.l   #5, d0
+        bne     fail
+        lea     hello, a2
+        cmpa.l  a2, a0
+        bne     fail
+        moveq   #13, d3
+        lea     empty, a0
+        bsr     print
+        tst.l   d0
+        bne     fail
+        ; ---- 14: print_dec returns the byte count ------------------
+        moveq   #14, d3
+        move.l  #12345, d0
+        bsr     print_dec
+        cmp.l   #5, d0
+        bne     fail
+        ; ---- 15: D3, D5, D7 and A3 survived everything -----------
+        moveq   #15, d3
+        cmp.l   #$DEADBEEF, d5
+        bne     fail
+        cmp.l   #$CAFEF00D, d7
+        bne     fail
+        lea     tbuf, a2
+        cmpa.l  a2, a3
+        bne     fail
+        moveq   #0, d1              ; exit(0)
+        moveq   #1, d0
+        trap    #0
+fail:   move.l  d3, d1              ; exit(check number)
+        moveq   #1, d0
+        trap    #0
+
+; utoa D0 into nbuf and compare with the string at A4. A0 must come back
+; pointing at the buffer, and the reported length must match.
+chk_utoa:
+        lea     nbuf, a0
+        bsr     utoa
+        move.l  d0, d1              ; D1 = reported digit count
+        lea     nbuf, a2
+        cmpa.l  a2, a0
+        bne     fail
+        move.l  a4, a0
+        bsr     strlen              ; D0 = expected digit count
+        cmp.l   d1, d0
+        bne     fail
+        lea     nbuf, a0
+        move.l  a4, a1
+        bsr     streq
+        tst.l   d0
+        bne     fail
+        rts
+
+; streq: A0, A1 = strings -> D0 = 0 when equal
+streq:  move.b  (a0)+, d2
+        move.b  (a1)+, d1
+        cmp.b   d1, d2
+        bne     streq2
+        tst.b   d2
+        bne     streq
+        moveq   #0, d0
+        rts
+streq2: moveq   #1, d0
+        rts
+
+empty:  dc.b    0
+hello:  dc.b    "Hello", 0
+s0:     dc.b    "0", 0
+s7:     dc.b    "7", 0
+s10:    dc.b    "10", 0
+s100:   dc.b    "100", 0
+s1000:  dc.b    "1000", 0
+s1e9:   dc.b    "1000000000", 0
+smax:   dc.b    "4294967295", 0
+dbuf:   dc.b    $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
+nbuf:   dc.b    $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
+tbuf:   dc.b    0
