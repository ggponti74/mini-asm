; std.asm - small runtime library for mini-asm programs, written in plain 68K
; on top of the TRAP #0 services (see src/core.c). Use it with
;
;         include "lib/std.asm"        ; path is relative to the including file
;
; It may sit anywhere in your program: it jumps over itself, so it never
; runs by accident. Include it once. Call the routines with BSR.
;
; Routine         In                          Out / what it clobbers
; --------------  --------------------------  ------------------------------------
; strlen          A0 = NUL-terminated string  D0 = length (without the NUL)
;                                             clobbers A1; A0 is kept
; strcpy          A0 = source, A1 = dest      D0 = length copied (without the NUL)
;                                             the NUL is copied too; A0 and A1 are
;                                             left just past it
; print           A0 = NUL-terminated string  writes it to stdout
;                                             D0 = bytes written, or negative error
;                                             clobbers D1, D2, A1; A0 is kept
; println         A0 = NUL-terminated string  like print, then a newline
; utoa            D0 = unsigned 32-bit value  writes decimal digits + NUL at A0
;                 A0 = buffer (11 bytes)      D0 = number of digits
;                                             clobbers D1, D2, A1, A2; A0 is kept
; print_dec       D0 = unsigned 32-bit value  prints it in decimal to stdout
;                                             D0 = bytes written, or negative error
;                                             clobbers D1, D2, A0, A1, A2
;
; Everything else (D3, D5, D7, A3-A7) is preserved. D4 is ESP and D6 shares
; ESI with A0 on the x86 target, so these routines never touch D4 or D6.
; The routines are not re-entrant (print_dec uses one static buffer) and
; print does not retry a short write.
;
; These names are global labels; an internal label starts with _std_.

        bra     _std_end

; ---- strlen ----------------------------------------------------------
strlen:
        move.l  a0, a1
        clr.l   d0
_std_strlen1:
        tst.b   (a1)+
        beq     _std_strlen2
        addq.l  #1, d0
        bra     _std_strlen1
_std_strlen2:
        rts

; ---- strcpy ----------------------------------------------------------
strcpy:
        clr.l   d0
_std_strcpy1:
        move.b  (a0)+, (a1)+        ; sets Z when the NUL has been copied
        beq     _std_strcpy2
        addq.l  #1, d0
        bra     _std_strcpy1
_std_strcpy2:
        rts

; ---- print / println -------------------------------------------------
print:
        bsr     strlen              ; D0 = length, A0 kept
        move.l  d0, d2              ; D2 = length
        moveq   #2, d0              ; service 2 = write
        moveq   #1, d1              ; fd 1 = stdout
        trap    #0                  ; A0 = buffer
        rts

println:
        bsr     print
        tst.l   d0
        bmi     _std_println1       ; the string failed: skip the newline
        lea     _std_nl, a0
        bsr     print
_std_println1:
        rts

; ---- utoa ------------------------------------------------------------
; Unsigned decimal by repeated subtraction of 10^9 ... 10^0: there is no
; shift or SWAP here, and DIVU.W only has a 16-bit quotient, so this is
; the simple way to cover the whole 32-bit range.
utoa:
        move.l  a0, a1              ; A1 = write pointer, A0 stays at the start
        lea     _std_pow10, a2      ; A2 walks the table of powers of ten
        move.l  d0, d1              ; D1 = what is left to convert
_std_utoa1:
        move.l  (a2)+, d2           ; D2 = current power of ten
        moveq   #0, d0              ; D0 = this digit
_std_utoa2:
        cmp.l   d2, d1
        blo     _std_utoa3          ; value < power: digit is done
        sub.l   d2, d1
        addq.l  #1, d0
        bra     _std_utoa2
_std_utoa3:
        tst.l   d0
        bne     _std_utoa4          ; non-zero digit: write it
        cmpa.l  a0, a1
        bne     _std_utoa4          ; digits already started: zeros count
        cmp.l   #1, d2
        bne     _std_utoa1          ; leading zero: skip (but 10^0 always prints)
_std_utoa4:
        add.b   #48, d0             ; '0' + digit
        move.b  d0, (a1)+
        cmp.l   #1, d2
        bne     _std_utoa1          ; more powers to go
        clr.b   (a1)                ; terminating NUL
        move.l  a1, d0
        move.l  a0, d1
        sub.l   d1, d0              ; D0 = number of digits
        rts

; ---- print_dec -------------------------------------------------------
print_dec:
        lea     _std_numbuf, a0
        bsr     utoa
        bra     print               ; tail call: print returns to our caller

; ---- data ------------------------------------------------------------
; Keep this block a multiple of 4 bytes: on the ARM target code must stay
; word aligned, and _std_end (and everything after it) follows this data.
_std_pow10:  dc.l    1000000000, 100000000, 10000000, 1000000, 100000
             dc.l    10000, 1000, 100, 10, 1
_std_numbuf: dc.b    0,0,0,0,0,0,0,0,0,0,0,0
_std_nl:     dc.b    10, 0, 0, 0    ; newline, NUL, and 2 bytes of padding

_std_end:
