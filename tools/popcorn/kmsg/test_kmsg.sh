#!/bin/bash

# Antonio Barbalace, Stevens 2019

### currently this script only run the ping-pong test
#sudo taskset 1 ./kmsg -c 5 -t 1 -n 32

REPS=10
REPS_N=32

# check requirements
command -v lstopo >/dev/null 2>&1 || { echo >&2 "lstopo is required but it's not installed. Exiting."; exit 1; }
command -v taskset >/dev/null 2>&1 || { echo >&2 "taskset is required but it's not installed. Exiting."; exit 1; }
command -v python >/dev/null 2>&1 || { echo >&2 "python is required but it's not installed. Exiting."; exit 1; }

# the following two are for ipi but useful here as well
#command -v ./lstopo.py >/dev/null 2>&1 || { echo >&2 "lstopo.py is required but it's missing. Exiting."; exit 1; }
#command -v ./calibrate >/dev/null 2>&1 || { echo >&2 "calibrate is required but it's not compiled. Exiting."; exit 1; }

# check if required modules are in
KMSG_MODULE=`lsmod | grep "^kmsg"`
if [ -z "$KMSG_IPI_MODULE" ] ; then echo >&2 "kmsg.ko module not loaded. Exiting." ; exit 1 ; fi
KMSG_TEST_MODULE=`lsmod | grep "^pcn_kmsg_test"`
if [ -z "$KMSG_IPI_MODULE" ] ; then echo >&2 "pcn_kmsg_test.ko module not loaded. Exiting." ; exit 1 ; fi

# get the CPUs list
TEMP_FILE=`tempfile`
#echo $TEMP_FILE
lstopo -p > $TEMP_FILE
LSTOPO=`./lstopo.py $TEMP_FILE | awk '{print $1}'`
rm $TEMP_FILE

#calibrate TODO ???

# main test loop
for CPU_ORIG in $LSTOPO
do
  CUR_RESULTS=""
  for CPU_DEST in $LSTOPO
  do
    # if self continue (but need to add the number)
    if [ $CPU_ORIG -eq $CPU_DEST ] ; then CUR_RESULTS="$CUR_RESULTS -1" ; continue ; fi
    
    # if not self
    #echo $CPU_ORIG "->" $CPU_DEST
    SUM="0"
    for REP in `seq 1 $REPS` ;
    do
      # run a single instance of the test and gather numbers, check for errors
      MASK_ORIG=$(python -c "print 2**$CPU_ORIG")
      RESULT=`taskset -c $MASK_ORIG ./kmsg -c $CPU_DEST -t 1 -n $REPS_N`
      echo "$RESULT"
      
      exit 1 
      
      udo taskset 1 ./test_kmsg -c 12 -t 1 -n 32 | awk 'BEGIN{VAL=2693534579.670722*0.000001} {print ($2 -$1)/VAL, ($8 -$1)/VAL, ($4 -$3)/VAL, ($5 -$3)/VAL, ($6 -$3)/VAL, ($7 -$3)/VAL}' 
      # TODO in us
      /// updated version
      echo "CPU" "RTT" "snd(snd)" "int(rcv)" "bh(rcv)" "bh1(rcv)" "hnd(rcv)" "snd(rcv)" "int(rcv)" "bh(rcv)" "bh1(rcv)" ; for CURC in `seq 2 63` ; do  RESULT=`sudo taskset 1 ./test_kmsg -c $CURC -t 1 -n 1` ; echo "$RESULT" | awk 'BEGIN{VAL=2693534579.670722*0.000001;} {print ($8 -$1)/VAL, ($2 -$1)/VAL, ($4 -$3)/VAL, ($5 -$3)/VAL, ($6 -$3)/VAL, ($7 -$3)/VAL, ($9 -$3)/VAL, ($11 -$10)/VAL, ($12 -$11)/VAL, ($13 -$11)/VAL}'; done
      // just another version
      FILE=ping_pong.results16 ; echo "CPU" "RTT" "snd(snd)" "int(rcv)" "bh(rcv)" "bh1(rcv)" "hnd(rcv)" "snd(rcv)" "int(rcv)" "bh(rcv)" "bh1(rcv)" > $FILE; for CURC in `seq 2 143` ; do  RESULT=`sudo taskset 1 ./test_kmsg -c $CURC -t 1 -n 16` ; printf "$RESULT" | awk 'BEGIN{VAL=2693534579.670722*0.000001;} {print ($8 -$1)/VAL, ($2 -$1)/VAL, ($4 -$3)/VAL, ($5 -$3)/VAL, ($6 -$3)/VAL, ($7 -$3)/VAL, ($9 -$3)/VAL, ($11 -$10)/VAL, ($12 -$11)/VAL, ($13 -$11)/VAL}'; done >> $FILE
      #multiple per test
      echo "CPU" "RTT" "snd(snd)" "int(rcv)" "bh(rcv)" "bh1(rcv)" "hnd(rcv)" "snd(rcv)" "int(rcv)" "bh(rcv)" "bh1(rcv)" ; for CURC in `seq 2 8` ; do  RESULT=`sudo taskset 1 ./test_kmsg -c $CURC -t 1 -n 8` ; printf "$RESULT" | awk 'BEGIN{VAL=2693534579.670722*0.000001;} {print ($8 -$1)/VAL, ($2 -$1)/VAL, ($4 -$3)/VAL, ($5 -$3)/VAL, ($6 -$3)/VAL, ($7 -$3)/VAL, ($9 -$3)/VAL, ($11 -$10)/VAL, ($12 -$11)/VAL, ($13 -$11)/VAL}'; done
      
      
      // send time
      for CURC in `seq 2 63` ; do  sudo taskset 1 ./test_kmsg -c $CURC -t 0 -n 1 | awk 'BEGIN{VAL=2693534579.670722*0.000001} {print ($4+0.0)/VAL}' ; done
      //// NEW version
      for CURC in `seq 2 143` ; do  RESULT=`sudo taskset 1 ./test_kmsg -c $CURC -t 0 -n 1` ; echo $CURC $RESULT | awk 'BEGIN{VAL=2693534579.670722*0.000001} {print $1, ($5+0.0)/VAL}' ; done
      // as above
      FILE=send.results8_nokvm ; echo "snd(snd)" > $FILE ; for CURC in `seq 2 143` ; do  RESULT=`sudo taskset 1 ./test_kmsg -c $CURC -t 0 -n 8` ; echo "$RESULT" | awk 'BEGIN{VAL=2693534579.670722*0.000001} {print ($4+0.0)/VAL}' ; done >> $FILE

      
    done
    
    #TODO TODO TODO TODO
    
    AVG=$(python -c "print $SUM.00 / $REPS.00")
    #echo $SUM $AVG
    CUR_RESULTS="$CUR_RESULTS $AVG"
  done
  echo $CUR_RESULTS
done
