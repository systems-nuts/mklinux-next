
set terminal postscript eps size 5in,5in enhanced color font 'Helvetica,20'
set output "kmsg_ipi_profile.eps"

set yrange[0:143]
set xrange[0:143]
set cbrange [-1:101]
set cblabel "useconds"

set title "IPI latency between CPU-threads (usec)"

set view map
splot 'kmsg_ipi_prof.dat' matrix with image

