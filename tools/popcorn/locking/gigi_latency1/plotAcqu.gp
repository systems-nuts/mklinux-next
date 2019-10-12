set terminal postscript eps enhanced color

load 'config.gp'

set output "acquisitions.eps"
set title sprintf("%s %s", machine, notes)
set xlabel "threads/CPUs"
set ylabel "acquisitions (during 10s)"
set xrange [-1:maxcpus]
unset key
set log y
set grid
plot 'acquisitions.dat' u 3 w lp lw 3



