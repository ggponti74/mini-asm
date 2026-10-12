; files.asm - TRAP #0 file services: open(4), close(5), lseek(6).
; Runs on the ELF target; the program works in the current directory and
; creates "t.tmp". Exit 0 = pass, N = test N failed (the test number is in D7).
;
; Linux flag values: RDONLY 0, WRONLY 1, RDWR 2, CREAT 64, EXCL 128,
; TRUNC 512, APPEND 1024. lseek whence: 0 start, 1 current, 2 end.

start:
        ; ---- 1: create + write 10 bytes ----------------------------------
        moveq   #1, d7
        lea     fname, a0
        move.l  #577, d1            ; WRONLY | CREAT | TRUNC
        move.l  #420, d2            ; mode 0644
        move.l  #4, d0
        trap    #0
        bmi     fail
        move.l  d0, d5              ; D5 = fd
        move.l  d5, d1
        lea     digits, a0
        move.l  #10, d2
        move.l  #2, d0
        trap    #0
        bmi     fail
        cmp.l   #10, d0
        bne     fail

        ; ---- 2: close returns 0 ------------------------------------------
        moveq   #2, d7
        move.l  d5, d1
        move.l  #5, d0
        trap    #0
        bmi     fail
        bne     fail

        ; ---- 3: open for reading, read it back ---------------------------
        moveq   #3, d7
        lea     fname, a0
        move.l  #0, d1              ; RDONLY
        move.l  #0, d2
        move.l  #4, d0
        trap    #0
        bmi     fail
        move.l  d0, d5
        move.l  d5, d1
        lea     buf, a0
        move.l  #10, d2
        move.l  #3, d0
        trap    #0
        bmi     fail
        cmp.l   #10, d0
        bne     fail
        lea     buf, a1
        move.b  (a1), d0
        cmp.b   #48, d0             ; '0'
        bne     fail

        ; ---- 4: lseek SET to 3, read 2 bytes -> "34" --------------------
        moveq   #4, d7
        move.l  d5, d1
        move.l  #3, d2
        move.l  #0, d3
        move.l  #6, d0
        trap    #0
        bmi     fail
        cmp.l   #3, d0
        bne     fail
        move.l  d5, d1
        lea     buf, a0
        move.l  #2, d2
        move.l  #3, d0
        trap    #0
        cmp.l   #2, d0
        bne     fail
        lea     buf, a1
        move.b  (a1)+, d0
        cmp.b   #51, d0             ; '3'
        bne     fail
        move.b  (a1), d0
        cmp.b   #52, d0             ; '4'
        bne     fail

        ; ---- 5: lseek END with a negative offset -------------------------
        moveq   #5, d7
        move.l  d5, d1
        move.l  #-2, d2
        move.l  #2, d3
        move.l  #6, d0
        trap    #0
        bmi     fail
        cmp.l   #8, d0
        bne     fail
        move.l  d5, d1
        lea     buf, a0
        move.l  #5, d2              ; asks for 5, only 2 are left
        move.l  #3, d0
        trap    #0
        cmp.l   #2, d0
        bne     fail
        lea     buf, a1
        move.b  (a1), d0
        cmp.b   #56, d0             ; '8'
        bne     fail

        ; ---- 6: lseek CUR 0 = current position; read at EOF = 0 ----------
        moveq   #6, d7
        move.l  d5, d1
        move.l  #0, d2
        move.l  #1, d3
        move.l  #6, d0
        trap    #0
        cmp.l   #10, d0
        bne     fail
        move.l  d5, d1
        lea     buf, a0
        move.l  #4, d2
        move.l  #3, d0
        trap    #0
        bmi     fail
        bne     fail                ; 0 bytes at end of file

        ; ---- 7: errors: bad whence, seek before the start ----------------
        moveq   #7, d7
        move.l  d5, d1
        move.l  #0, d2
        move.l  #7, d3
        move.l  #6, d0
        trap    #0
        cmp.l   #-22, d0            ; -EINVAL
        bne     fail
        move.l  d5, d1
        move.l  #-100, d2
        move.l  #0, d3
        move.l  #6, d0
        trap    #0
        cmp.l   #-22, d0            ; -EINVAL
        bne     fail

        ; ---- 8: registers other than D0 survive lseek ------------------
        moveq   #8, d7
        move.l  d5, d1
        move.l  #1, d2
        move.l  #0, d3
        lea     digits, a0
        move.l  #6, d0
        trap    #0
        cmp.l   #1, d0
        bne     fail
        cmp.l   d5, d1              ; D1 = fd, D2 = 1, D3 = 0, A0 = digits
        bne     fail
        cmp.l   #1, d2
        bne     fail
        cmp.l   #0, d3
        bne     fail
        lea     digits, a1
        cmp.l   a0, a1
        bne     fail
        move.l  d5, d1              ; close it: D1 and A0 survive close too
        lea     digits, a0
        move.l  #5, d0
        trap    #0
        cmp.l   d5, d1
        bne     fail
        lea     digits, a1
        cmp.l   a0, a1
        bne     fail

        ; ---- 9: close of a bad fd, open of a missing file ----------------
        moveq   #9, d7
        move.l  #99, d1
        move.l  #5, d0
        trap    #0
        cmp.l   #-9, d0             ; -EBADF
        bne     fail
        lea     nofile, a0
        move.l  #0, d1
        move.l  #0, d2
        move.l  #4, d0
        trap    #0
        cmp.l   #-2, d0             ; -ENOENT
        bne     fail

        ; ---- 10: CREAT|EXCL on an existing file ---------------------------
        moveq   #10, d7
        lea     fname, a0
        move.l  #193, d1            ; WRONLY | CREAT | EXCL
        move.l  #420, d2
        move.l  #4, d0
        trap    #0
        cmp.l   #-17, d0            ; -EEXIST
        bne     fail

        ; ---- 11: APPEND adds to the end, whatever the position -----------
        moveq   #11, d7
        lea     fname, a0
        move.l  #1025, d1           ; WRONLY | APPEND
        move.l  #0, d2
        move.l  #4, d0
        trap    #0
        bmi     fail
        move.l  d0, d5
        move.l  d5, d1
        move.l  #0, d2
        move.l  #0, d3
        move.l  #6, d0              ; seek to 0, the write must still append
        trap    #0
        move.l  d5, d1
        lea     tail, a0
        move.l  #2, d2
        move.l  #2, d0
        trap    #0
        cmp.l   #2, d0
        bne     fail
        move.l  d5, d1
        move.l  #5, d0
        trap    #0
        lea     fname, a0
        move.l  #0, d1
        move.l  #4, d0
        trap    #0
        bmi     fail
        move.l  d0, d5
        move.l  d5, d1
        move.l  #0, d2
        move.l  #2, d3
        move.l  #6, d0              ; file size = position at END
        trap    #0
        cmp.l   #12, d0
        bne     fail
        move.l  d5, d1
        move.l  #5, d0
        trap    #0

        ; ---- 12: TRUNC empties the file ----------------------------------
        moveq   #12, d7
        lea     fname, a0
        move.l  #513, d1            ; WRONLY | TRUNC
        move.l  #0, d2
        move.l  #4, d0
        trap    #0
        bmi     fail
        move.l  d0, d5
        move.l  d5, d1
        move.l  #0, d2
        move.l  #2, d3
        move.l  #6, d0
        trap    #0
        bmi     fail
        bne     fail                ; size 0
        move.l  d5, d1
        move.l  #5, d0
        trap    #0

        moveq   #0, d0
        rts

fail:   move.l  d7, d0
        rts

fname   dc.b    "t.tmp", 0
nofile  dc.b    "no-such-dir/x", 0
digits  dc.b    "0123456789"
tail    dc.b    "AB"
buf     dc.b    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
