#!/bin/bash

# Antonio Barbalace, Stevens 2019

# check requirements
command -v lstopo >/dev/null 2>&1 || { echo >&2 "lstopo is required but it's not installed. Exiting."; exit 1; }
command -v taskset >/dev/null 2>&1 || { echo >&2 "taskset is required but it's not installed. Exiting."; exit 1; }
command -v python >/dev/null 2>&1 || { echo >&2 "python is required but it's not installed. Exiting."; exit 1; }
command -v ./lstopo.py >/dev/null 2>&1 || { echo >&2 "lstopo.py is required but it's missing. Exiting."; exit 1; }

# get the CPUs list
TEMP_FILE=`tempfile`
#echo $TEMP_FILE
lstopo -p > $TEMP_FILE
LSTOPO=`./lstopo.py $TEMP_FILE | awk '{print $1}'`
rm $TEMP_FILE

taskset -c $CPU_ORIG echo $CPU_DEST > /proc/ipi_test
      RESULT=`taskset -c $CPU_ORIG cat /proc/ipi_test`