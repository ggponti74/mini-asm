; EXPECT: Error in .*unknown_mnemonic\.asm at line 3, column 1: Unknown mnemonic
start:  moveq   #0, d0
        bogus   d0
        rts
