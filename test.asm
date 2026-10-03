start:
	cmp     #1, d1
	BEQ     good
        move #5, d0
bad:    bra good
        rts

good:   move 	#0, d0
        rts
