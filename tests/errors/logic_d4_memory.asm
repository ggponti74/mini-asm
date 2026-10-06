; EXPECT: Error in .*logic_d4_memory\.asm at line 3.*D4 maps to ESP.*AND/OR/EOR
        lea     buf, a0
        and.l   d4, (a0)
buf     dc.l    0
