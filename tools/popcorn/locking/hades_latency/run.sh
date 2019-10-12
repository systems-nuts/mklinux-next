#!/bin/bash

./parseAcqu.sh > acquisitions.dat
gnuplot plotAcqu.gp

./parseLat.sh > latency.dat
gnuplot plotLat.gp
./clean.sh
