; data.asm - shared test data. dc.w / dc.l are laid out big-endian, as on a 68K.
wdata   dc.w    $1234               ; bytes 12 34
wneg    dc.w    $8001               ; bytes 80 01
ldata   dc.l    $AABBCCDD           ; bytes AA BB CC DD
msg     dc.b    "Hello", 0
buf     dc.b    0,0,0,0,0,0,0,0
buf2    dc.b    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
