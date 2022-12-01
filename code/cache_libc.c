#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <papi.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

//custom error sender
void die (char* msg) {

  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
  
}

//the real main() address
static int (*main_orig)(int, char **, char **);

//fake main() that gets called by __libc_start_main()
int main_hook(int argc, char **argv, char **envp)
{

  
  //initializes PAPI
    int event_set = PAPI_NULL;
    long_long values[10];

    if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
      die("Failed initialization");
    }

    if (PAPI_create_eventset(&event_set) != PAPI_OK) {
      die("Failed to create event set");
    }

    //Adds performance counters
     
    if (PAPI_add_event(event_set, PAPI_L1_TCM) != PAPI_OK) {
      die("Failed to add L1 total cache misses event");
    }

    if (PAPI_add_event(event_set, PAPI_L2_TCM) != PAPI_OK) {
      die("Failed to add L2 total cache misses event");
    }

    if (PAPI_add_event(event_set, PAPI_L3_TCM) != PAPI_OK) {
      die("Failed to add L3 total cache misses event");
    }
    
    if (PAPI_add_event(event_set, PAPI_TOT_INS) != PAPI_OK) {
      die("Failed to add total instuctions event");
    }

    
    //start counting events
    if (PAPI_num_events(event_set) != 0 && PAPI_start(event_set) != PAPI_OK) {
      die("Failed to start counting, make sure at least one environmental variable is set");
    }

    //runs the program
    int ret = main_orig(argc, argv, envp);

    //stops the counting
    
    if (PAPI_num_events(event_set) != 0 && PAPI_stop(event_set, values) != PAPI_OK) {
      die("Failed to stop counting");
    }
    
    //get the file name to output data to
    char *FILENAME = getenv("Cache_Name");

    //create the output file
    FILE *fp = fopen(FILENAME, "w+");
    
    fprintf(fp, "L1 cache misses: %lld\n", values[0]);

    fprintf(fp, "L2 cache misses: %lld\n", values[1]);

    fprintf(fp, "L3 cache misses: %lld\n", values[2]);

    //total memory accesses is aproximated from total instructions plus an aproxiamtion that 1/4 of instructions are load/store instructions, minus the L1 misses

    long_long mem = 0;
    mem += values[3] + (int).25*values[3] - values[0];

    fprintf(fp, "Total memory accesses: %lld\n", mem);

    fclose(fp);
    
    return ret;
}

int __libc_start_main(
    int (*main)(int, char **, char **),
    int argc,
    char **argv,
    int (*init)(int, char **, char **),
    void (*fini)(void),
    void (*rtld_fini)(void),
    void *stack_end)
{
    /* Save the REAL main function address */
    main_orig = main;

    /* Find the REAL __libc_start_main() */
    typeof(&__libc_start_main) orig = dlsym(RTLD_NEXT, "__libc_start_main");

    /* ... and call it with our custom main function */
    return orig(main_hook, argc, argv, init, fini, rtld_fini, stack_end);
} 
