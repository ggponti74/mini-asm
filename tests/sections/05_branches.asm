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
