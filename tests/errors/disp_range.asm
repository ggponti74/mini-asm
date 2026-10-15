; EXPECT: Error in .*disp_range\.asm at line 3: displacement 40000 does not fit in 16 bits
        lea     buf, a0
        move.l  40000(a0), d1
buf     dc.l    0
