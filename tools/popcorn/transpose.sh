
cat interrupts.log  |while read line ; do paste tmp1 <(echo $line | tr -s ' '\n) > tmp2; cp tmp2 tmp1;done ; cat tmp1
