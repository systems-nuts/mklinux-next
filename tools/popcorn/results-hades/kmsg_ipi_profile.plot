
set terminal postscript eps size 5in,5in enhanced color font 'Helvetica,20'
set output "kmsg_ipi_profile.eps"

set yrange[-0.5:15.5]
set xrange[-0.5:15.5]
set cbrange [-1:41]
set cblabel "useconds"

#set palette defined ( 0 "#000090", 1 "#000fff", 2 "#0090ff", 3 "#0fffee", 4 "#90ff70", 5 "#ffee00", 6 "#ff7000", 7 "#ee0000", 8 "#7f0000")


set title "IPI latency between CPU-threads (usec)"

set view map
splot 'kmsg_ipi_prof.dat' matrix with image

