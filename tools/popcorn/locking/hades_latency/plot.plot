plot 'results
results1.dat  results12.dat results15.dat results3.dat  results6.dat  results9.dat 
results10.dat results13.dat results16.dat results4.dat  results7.dat 
results11.dat results14.dat results2.dat  results5.dat  results8.dat 
gnuplot> plot 'results
results1.dat  results12.dat results15.dat results3.dat  results6.dat  results9.dat 
results10.dat results13.dat results16.dat results4.dat  results7.dat 
results11.dat results14.dat results2.dat  results5.dat  results8.dat 
gnuplot> plot 'results1.dat' u 1 w lp, 'results4.dat' u 1 w lp, 'results8.dat' u 1 w lp, 'results16.dat' u 1 w lp
gnuplot> plot 'results1.dat' u ($1/2100000) w lp, 'results4.dat' u ($1/2100000) w lp, 'results8.dat' u ($1/21000000) w lp, 'results16.dat' u ($1/2100000) w lp
gnuplot> plot 'results1.dat' u ($1/2100000) w lp, 'results4.dat' u ($1/2100000) w lp, 'results8.dat' u ($1/2100000) w lp, 'results16.dat' u ($1/2100000) w lp
gnuplot> plot 'results1.dat' u ($1/2097580) w lp, 'results4.dat' u ($1/2097580) w lp, 'results8.dat' u ($1/2097580) w 
gnuplot> plot 'results1.dat' u ($1/2097580) w lp, 'results4.dat' u ($1/2097580) w lp, 'results8.dat' u ($1/2097580) w lp, 'results16.dat' u ($1/2097580) w lp
gnuplot> set ylabel 'us'
gnuplot> set xlabel 'samples'
gnuplot> replot
gnuplot> plot 'results1.dat' u ($1/2097580000) w lp, 'results4.dat' u ($1/2097580000) w lp, 'results8.dat' u ($1/2097580000) w lp, 'results16.dat' u ($1/2097580000) w lp
gnuplot> set ylabel 's'
gnuplot> set y log
             ^
         unrecognized option - see 'help set'.

gnuplot> set log y
gnuplot> replot
gnuplot> plot 'results1.dat' u ($1/2097580000) w lp, 'results8.dat' u ($1/2097580000) w lp, 'results16.dat' u ($1/2097580000) w lp, 'results4.dat' u ($1/2097580000) w lp
gnuplot> plot 'results1.dat' u ($1/2097580000) w l, 'results8.dat' u ($1/2097580000) w l, 'results16.dat' u ($1/2097580000) w l, 'results4.dat' u ($1/2097580000) w l
gnuplot> set ylabel 'seconds'
gnuplot> plot 'results1.dat' u ($1/2097580000) w l, 'results8.dat' u ($1/2097580000) w l, 'results16.dat' u ($1/2097580000) w l, 'results4.dat' u ($1/2097580000) w l, 'results12.dat' u ($1/2097580000) w l
gnuplot> plot 'results1.dat' u ($1/2097580000) w l, 'results8.dat' u ($1/2097580000) w l, 'results16.dat' u ($1/2097580000) w l, 'results12.dat' u ($1/2097580000) w l, 'results4.dat' u ($1/2097580000) w l
gnuplot> plot 'results1.dat' u ($1/2097580000) w lp, 'results8.dat' u ($1/2097580000) w lp, 'results16.dat' u ($1/2097580000) w lp, 'results12.dat' u ($1/2097580000) w lp, 'results4.dat' u ($1/2097580000) w lp
gnuplot> plot 'results1.dat' u ($1/2097580000) w lp, 'results8.dat' u ($1/2097580000) w lp, 'results16.dat' u ($1/2097580000) w lp, 'results12.dat' u ($1/2097580000) w lp, 'results4.dat' u ($1/2097580000) w lp, 'results2.dat' u ($1/2097580000) w lp

