reset
set xlabel 'fib num'
set ylabel 'time'
set term png enhanced font 'Verdana,10'
set output 'fib.png'
set xtics 100
set key left 
plot "kernel_res.txt" using 1:2 with linespoints linewidth 2 title "kernel"
