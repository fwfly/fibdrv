reset
set xlabel 'fib num'
set ylabel 'time'
set term png enhanced font 'Verdana,10'
set output 'fib.png'
set xtics 50
set key left 
plot "kernel_res.txt" using 1:2 with linespoints linewidth 2 title "fib", \
 "res_fast_multi.txt" using 1:2 with linespoints linewidth 2 title "fast fib Knuth"
