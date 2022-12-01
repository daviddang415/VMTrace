# VMTRACE Tracing Mechanism

### How to use VMTRACE:

**VMTRACE** uses two tools, `manager.c` and `catcher.c` to trace the memory
accesses of a program.

#### Manager

The manager works by wrapping all `malloc` calls by the traced program,
protecting every page that is allocated. When a protected page is referenced,
a fault occurs, which is caught by the handler in the manager. The handler
records what page was faulted on, thus tracking the memory access, and
unprotects the page. A number of pages are kept unprotected to ensure that the
program can run, and to minimize slowdown. When a page is unprotected, the page
at the end of an approximated LRU for unprotected page is protected.

#### Catcher

The *catcher* works in conjuction with the *manager* to catch and handle system
calls from the traced program. Since the *manager* is protecting pages, it is
possible that a pointer to a protected space will be passed to the kernel. To
prevent this from crashing the program, the *catcher* uses `ptrace` to control
the manager and traced program. Every time the program does a system call, the
*catcher* halts it, and reads what kind of system call it is. If the system
call passes pointers, the catcher nullifies the system call, and tells the
*manager* to walk the length of all the pointers. This walking could cause
a fault if it happens on any protected pages, which will then call the handler
in *manager* and unprotect. When the *manager* is done walking, it signals the
*catcher*, which restores the original system call and executes it.

*To see the full interaction between the catcher and the manager, look at
Catcher_and_Manager.pdf file.*

### Running VMTRACE

To run **VMTRACE**, run the *catcher* with the first argument being the program
you want to trace, and subsequent arguments being the arguments for the traced
program. Make sure to set the enviornment variables `VMT_TRACENAME` for the
file you want to export the traces to, and `VMT_SIZE` for the amount of
unprotected pages kept at any time.

For an example on how to run **VMTRACE**, look at `script.sh`.

### Curent issues

* **VMTRACE** does not work on **multithreaded programs**.

    As `ptrace` can only attach to one thread at a time, only the main thread
    has its system calls handled. Some prototype multithread handling is
    present in `ptrace.c`, which can catch new threads being created, but does
    not currently trace those. In the event that multithreading is added, the
    shared memory needs to be change a bit for each thread to be able to
    communicate seperately.

* **Exit handlers** not working.

    Catcher is supposed to track the exit of brk, mmap, munmap, mprotect, and
    sigaction calls for virtualization. Supposed to set instruction pointer to
    send to functions in the manage similar to the how the walker works.
    Someone needs to make sure the passing of instuction pointers is working.
    Also someone needs to fill out what the exit handlers are actually supposed
    to do.

* **Misc bugs**:

    The tool has not been thoroughly tested, so there may be some other bug
    hanging around.
