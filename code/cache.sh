#!/bin/bash

printf "Beginning script\n"

gcc -ggdb little_loop.c -o little_loop
gcc -ggdb timing_loop.c -o timing_loop
gcc -ggdb cache_libc.c -o cache_libc.so -fPIC -shared -ldl -lpapi
export LD_PRELOAD=$PWD/cache_libc.so
export Cache_Name="cache.csv"

./little_loop
./timing_loop

unset LD_PRELOAD

printf "Script completed\n"
