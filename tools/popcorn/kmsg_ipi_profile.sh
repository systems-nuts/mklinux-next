#!/bin/bash

# Antonio Barbalace, Stevens 2019

REPS=10

# check requirements
command -v lstopo >/dev/null 2>&1 || { echo >&2 "lstopo is required but it's not installed. Exiting."; exit 1; }
command -v taskset >/dev/null 2>&1 || { echo >&2 "taskset is required but it's not installed. Exiting."; exit 1; }
command -v python >/dev/null 2>&1 || { echo >&2 "python is required but it's not installed. Exiting."; exit 1; }
command -v ./lstopo.py >/dev/null 2>&1 || { echo >&2 "lstopo.py is required but it's missing. Exiting."; exit 1; }
command -v ./calibrate >/dev/null 2>&1 || { echo >&2 "calibrate is required but it's not compiled. Exiting."; exit 1; }
KMSG_IPI_MODULE=`lsmod | grep kmsg_ipi_test`
if [ -z "$KMSG_IPI_MODULE" ] ; then echo >&2 "kmsg_ipi_test.ko module not loaded. Exiting." ; exit 1 ; fi

# get the CPUs list
TEMP_FILE=`tempfile`
#echo $TEMP_FILE
lstopo -p > $TEMP_FILE
LSTOPO=`./lstopo.py $TEMP_FILE | awk '{print $1}'`
rm $TEMP_FILE

# calibrate as well as parse dmesg
DMESG_VALUE=`dmesg | awk '/^\[[0-9.]+\] kmsg_ipi_test cpu_khz [0-9]+ tsc_khz [0-9]+/{print $6}'`
CAL_VALUE=`./calibrate | awk '/calibrate: [0-9.]+ tick\/second/{print $2}'`
echo "TSC @ $DMESG_VALUE kHz (kernel) $CAL_VALUE tick/s (user)"
#TODO select the best multiplier

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
      taskset -c $CPU_ORIG echo $CPU_DEST > /proc/ipi_test
      RESULT=`taskset -c $CPU_ORIG cat /proc/ipi_test`
      LINE1=`echo "$RESULT" | awk '/current [0-9]+ target [0-9]+/{print $0}'`
      if [ -z "$LINE1" ] ; then echo >&2 "/proc/ipi_test returned unformatted output line 1. Exiting.\n $RESULT"; exit 1; fi
      LINE2=`echo "$RESULT" | awk '/sender [0-9]+ [0-9]+ [0-9]+ [0-9]+/{print $0}'`
      if [ -z "$LINE2" ] ; then echo >&2 "/proc/ipi_test returned unformatted output line 2. Exiting.\n $RESULT"; exit 1; fi
      LINE3=`echo "$RESULT" | awk '/inthnd [0-9]+ [0-9]+/{print $0}'`
      if [ -z "$LINE3" ] ; then echo >&2 "/proc/ipi_test returned unformatted output line 3. Exiting.\n $RESULT"; exit 1; fi
      #echo $LINE1 -- $LINE2 -- $LINE3
      
      # check consistency and save the value
      ARRLINE1=($LINE1)
      if [ $CPU_ORIG -ne ${ARRLINE1[1]} ] ; then echo >&2 "/proc/ipi_test current ${ARRLINE1[1]} differs from $CPU_ORIG. Exiting.\n $RESULT"; exit 1; fi
      if [ $CPU_DEST -ne ${ARRLINE1[3]} ] ; then echo >&2 "/proc/ipi_test target ${ARRLINE1[3]} differs from $CPU_DEST. Exiting.\n $RESULT"; exit 1; fi
      ARRLINE2=($LINE2)
      VALUE=${ARRLINE2[4]}
      #echo $VALUE     
      SUM=$(($SUM + $VALUE))
    done
    AVG=$(python -c "print $SUM.00 / $REPS.00")
    #echo $SUM $AVG
    CUR_RESULTS="$CUR_RESULTS $AVG"
  done
  echo $CUR_RESULTS
done

