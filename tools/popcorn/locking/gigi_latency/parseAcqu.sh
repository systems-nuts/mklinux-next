#!/bin/bash

PREFIX="results"
EXTENSION=".log"

grep Acquisitions *.log | sed 's/:/ /g' | sed "s/$PREFIX//g" | sed "s/$EXTENSION//g" | sort -n > acquisitions.dat

