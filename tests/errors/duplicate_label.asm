; EXPECT: Error in .*duplicate_label\.asm at line 4: label 'x' already defined in .*inc/dup_label\.asm at line 1
start:  moveq   #0, d0
        include "inc/dup_label.asm"
x:      nop
