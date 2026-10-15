; EXPECT: Error in .*disp_same_register\.asm at line 3: d\(An\) can.t be combined with \(An\)\+ or -\(An\) of the same register
        lea     buf, a3
        move.l  (a3)+, 4(a3)
buf     dc.l    0,0
