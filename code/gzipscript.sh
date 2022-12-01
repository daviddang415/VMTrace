#! /usr/bin/env bash

printf "Starting compression \n"

gcc -ggdb new_libc.c -o new_libc.so -fPIC -shared -ldl
export LD_PRELOAD=$PWD/new_libc.so

#Compresses file but we dont care about it
./gzip/gzip-1-2.10/gzip < ./gzip/gzipSampleInputs/AMilitaryDictionaryAndGazetteer.txt  > /dev/null

unset LD_PRELOAD

printf "Finished compression \n"
