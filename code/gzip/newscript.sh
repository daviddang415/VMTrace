# ==============================================================================
# Note: This script is assuming that you ran configure and make for gzip 
# alredy. If you haven't, refer to the README file that is inside this same
# directory.
# ==============================================================================

#! /usr/bin/env bash

printf "Starting compression \n"

#Compresses file but we dont care about it
./gzip-1-2.10/gzip < ./gzipSampleInputs/AMilitaryDictionaryAndGazetteer.txt > \
    /dev/null

printf "Finished crompression \n"
