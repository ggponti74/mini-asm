; common.asm - shared tail of every test program: failure handler,
; helper subroutine and data. Included last, after the sections.

fail:   move.l  d3, d0              ; exit code = number of the failed test
        rts

dbl:    add.l   d1, d1              ; helper for the call tests
        rts

        include "data.asm"
