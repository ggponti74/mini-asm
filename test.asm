start:  move	#1, d0
        move	#2, d1
		mulu	d0, d1
		bra     encode
        rts

endcode: move 	#5, d0
        rts
