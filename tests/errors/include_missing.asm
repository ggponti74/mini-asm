; EXPECT: Error in .*include_missing\.asm at line 2, column [0-9]+: cannot open INCLUDE '.*does_not_exist\.asm'
        include "does_not_exist.asm"
