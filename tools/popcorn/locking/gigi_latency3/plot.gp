set terminal postscript eps enhanced color

load 'config.gp'
us_calibrate=calibrate/1000000

set output "latency.eps"
set title sprintf("%s %s (tsc %f)", machine, notes, calibrate)
set xlabel "threads/CPUs"
set ylabel "latency (us)"
set xrange[-1:145]
set key outside horizontal center top box
set style fill solid
set log y
set grid
plot 'parsed.dat' u :($6/us_calibrate):($1/us_calibrate):($2/us_calibrate):($9/us_calibrate) w candlestick t 'min/5p/95p/max', '' u ($3/us_calibrate) w lp lw 3 t 'avg', '' u ($5/us_calibrate)  w lp t '1p', '' u ($10/us_calibrate) w lp t '99p', '' u ($4/us_calibrate) w lp t 'sd'
