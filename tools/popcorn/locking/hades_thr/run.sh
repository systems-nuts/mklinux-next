#!/bin/bash

./parseAcqu.sh > acquisitions.dat
gnuplot plotAcqu.gp
