#!/bin/bash

# Antonio Barbalace, Stevens 2019

CUR=0

while read -r LINE ; do
  LINEA=""
  if [ $CUR -eq "0" ] ; then
    #echo $LINE
    CUR=$(($CUR + 1))
    ARRLINE=($LINE)
    MULT=${ARRLINE[5]}
    #echo $MULT "@@@"
    continue
  fi
  for VALUE in $LINE ; do
    if [ "$VALUE" == "-1" ] ; then
      #echo $CUR -- -1
      LINEA="$LINEA -1"
    else
      #values in microseconds
      SCALED=$(python -c "print $VALUE / $MULT * 1000000")
      #echo $CUR -- $VALUE -- $SCALED
      LINEA="$LINEA $SCALED"
    fi
  done
  echo $LINEA
  CUR=$(($CUR + 1))
done
