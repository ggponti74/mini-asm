; EXPECT: Error in .*inc/inner_error\.asm at line 2, column 1: Unknown mnemonic
start:  moveq   #0, d0
        include "inc/inner_error.asm"
