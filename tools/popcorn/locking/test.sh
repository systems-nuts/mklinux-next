#!/bin/bash

# Antonio Barbalace, Stevens 2019

# check requirements
command -v lstopo >/dev/null 2>&1 || { echo >&2 "lstopo is required but it's not installed. Exiting."; exit 1; }
command -v taskset >/dev/null 2>&1 || { echo >&2 "taskset is required but it's not installed. Exiting."; exit 1; }
command -v python >/dev/null 2>&1 || { echo >&2 "python is required but it's not installed. Exiting."; exit 1; }
command -v ../lstopo.py >/dev/null 2>&1 || { echo >&2 "lstopo.py is required but it's missing. Exiting."; exit 1; }

for CPUS in `seq 1 144` ; do
  MASK=`python -c "print (hex(2 ** $CPUS -1))" | sed 's/L//g'`
  echo "running $CPUS with $MASK"
#  taskset $MASK ./test > results$CPUS.dat
done
