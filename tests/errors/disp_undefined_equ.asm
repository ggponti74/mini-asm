; EXPECT: Error in .*disp_undefined_equ\.asm at line 3: undefined EQU constant .NOPE.
        lea     buf, a0
        move.l  NOPE(a0), d1
buf     dc.l    0
