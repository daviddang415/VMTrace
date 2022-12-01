#!/bin/bash

printf "Beginning script\n"

gcc -ggdb little_loop.c -o little_loop
#gcc -ggdb manager.c compressed-caching.c hashmap.c -o manager.so -fPIC -shared -ldl
gcc -ggdb safeio.c manager.c hashmap.c -o manager.so -fPIC -shared -ldl
gcc -ggdb safeio.c catcher.c -o catcher
#gcc -ggdb thread_test.c -o thread_test -lpthread
export VMT_TRACENAME="foo.csv"
export VMT_SIZE="1024"

./catcher ./little_loop

printf "Script completed\n"
