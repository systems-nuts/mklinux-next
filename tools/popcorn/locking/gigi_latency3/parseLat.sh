#!/bin/bash

PREFIX="results"
EXTENSION=".log"
NEWEXTENSION=".dat"

IDS=`ls *$EXTENSION | sed "s/$PREFIX//g" | sed "s/$EXTENSION//g" | sort -n`

for FILEID in $IDS 
do
  # remove headers and zeros
  cat $PREFIX$FILEID$EXTENSION | awk '/^[0-9]+$/ {if ($1 != 0) print $0;}' > $PREFIX$FILEID$NEWEXTENSION 
  
  # analize the data
  R -q -e "r1 <- read.table('$PREFIX$FILEID$NEWEXTENSION', skip=0); r1 <- as.numeric(unlist(r1)); r1min <- min(r1); r1max <-max(r1); r1avg <- mean(r1); r1sd <- sd(r1); r1per <- quantile(r1, c(.01, .05, .10, .90, .95, .99)); cat(c(r1min, r1max, r1avg, r1sd, r1per));" | awk '/[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+[ \t]+[0-9]+[.0-9]+>/{print $0}' | sed 's/>//g'
done

# TODO create the config file for the gnuplot
