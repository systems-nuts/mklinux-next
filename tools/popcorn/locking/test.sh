#!/bin/bash

# Antonio Barbalace, Stevens 2019

# check requirements
command -v lstopo >/dev/null 2>&1 || { echo >&2 "lstopo is required but it's not installed. Exiting."; exit 1; }
command -v taskset >/dev/null 2>&1 || { echo >&2 "taskset is required but it's not installed. Exiting."; exit 1; }
command -v python >/dev/null 2>&1 || { echo >&2 "python is required but it's not installed. Exiting."; exit 1; }
command -v ../lstopo.py >/dev/null 2>&1 || { echo >&2 "lstopo.py is required but it's missing. Exiting."; exit 1; }

#configuration
PREFIX="results"
EXTENSION=".log"

# get the CPUs list
TEMP_FILE=`tempfile`
#echo $TEMP_FILE
lstopo -p > $TEMP_FILE
LSTOPO=`../lstopo.py $TEMP_FILE | awk '{print $1}'`
rm $TEMP_FILE

#new implementation using lstopo -p
CPUS=""
for CPU in $LSTOPO ; do
  [[ ! -z "$CPUS" ]] && CPUS="$CPUS,$CPU" || CPUS="$CPU"
  NUMCPUS="${CPUS//[^,]}"
  NUMCPUS=${#NUMCPUS}
  echo "running with $NUMCPUS CPUs added CPU $CPU"
  taskset -c $CPUS ./test > $PREFIX$NUMCPUS$EXTENSION
done

exit

NUMCPUS=`cat /proc/cpuinfo | grep "processor" | wc -l`
#this is the previous implementation
for CPUS in `seq 1 $NUMCPUS` ; do
  MASK=`python -c "print (hex(2 ** $CPUS -1))" | sed 's/L//g'`
  TEST_MASK=`python -c "value=((2 ** $CPUS -1))
count=0
while (value):
  count+=value&1
  value >>=1
print(count)"`
  echo "running $CPUS with $MASK and $TEST_MASK"
  taskset $MASK ./test > $PREFIX$CPUS$EXTENSION
done
