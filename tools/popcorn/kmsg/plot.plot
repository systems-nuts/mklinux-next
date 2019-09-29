
SEND

plot 'send.results1_nokvm' u 1 w lp, 'send.results1_perf' u 1 w lp, 'send.results8_nokvm' u 1 w lp, 'send.results8_perf' u 1 w lp



PING PONG

snd is @ 20

plot 'ping_pong.results' u: 1 w lp, '' u 3 w lp t 'isr rcv', '' u 4 w lp t 'bh rcv', '' u 5 w lp t 'bh2 rcv', '' u 6 w lp t 'hnd rcv', '' u (20+$8) w lp t 'isr snd', '' u (20+$9) w lp t 'bh snd', '' u (20+$10) w lp t 'bh2 snd'



plot 'ping_pong.results' u: 1 w lp, '' u ($2+$7+$10) w lp t 'sw only', '' u ($1-$2-$7-$10) w lp t 'hw only' 
