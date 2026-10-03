start:  move	#3, d0
        move	#2, d1
	mulu	d0, d1
	bra     end
        rts

end:    move 	d1, d0
        rts
