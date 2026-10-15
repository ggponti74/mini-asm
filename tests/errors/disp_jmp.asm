; EXPECT: Error in .*disp_jmp\.asm at line 3: d\(An\) isn.t supported with this instruction yet
        lea     buf, a0
        jmp     4(a0)
buf     dc.l    0
