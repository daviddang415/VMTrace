/**
//Traces memory accesses of a benchmark program by protecting pages used by the program
//When a protected page is accessed, there is a fault, which is caught by the handler which tracks which page was faulted on
//A hashset of unprotected pages is kept to ensure that the program can run correctly and to control slowdown
/Works in conjuction with the parent to walk the length of pointers from system calls and unprotect accordingly to make sure we are not passing a protected pointer into the kernel

//CURRENT BUGS/ISSUES
//Calloc_custom may no longer be needed now that we use trace_flag, needs testing
//Hashset only supports power of 2 sizes
//Hash values haven't been tested to see if they are actually good
*/

/* =============================================================================================================================== */
/**
 * \file manager.c
 * \brief Memory tracer based on mprotect() and signal handling
 */
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* GNU SOURCE */

#define _GNU_SOURCE
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* INCLUDES */

#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <signal.h>
#include "hashset.h"
#include "hashmap.h"

//Left commented since compressed-caching has not been completely implemeneted yet
//#include "compressed-caching.h"

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* MACROS */

/** Aligns pointer to the beginning of page */
#define PAGE_BASE(p) ((void *)((intptr_t)p & ~0xfff))
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* Global Variables */

/** Character array used to write pointer address. */
static char hex[37] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A',
	'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
	'X', 'Y', 'Z'};

/** String that holds pointer address. */
static char buffer[17];

/** Address of main function in benchmark program. */
static int (*main_orig) (int, char **, char **);

/** Original malloc function. */
extern void *__libc_malloc(size_t size);

/** Original write function. */
typeof(&write) write_orig;

/** Integer variable that contains the most recent pointer array element added. */
static int current_index = 0;

/** Pointer array that tracks pages used. */
static void ** ptr_list;

/** Integer variable used to direct output into csv file. */
static int file_addr;

/** Size of a page. */
static int pagesize;

/** Size of array containing unprotected pages. */
static int SIZE;

/** Flag that sets to 1 when the benchmark program is run, makes sure we are not protecting pages before we run the program we want to trace.  */
static int trace_flag = 0;

/** Dummy variable to make sure walk stuct does not get optimized out. */
char *dummy;

/** For sending information between this and the parent syscall catcher. */
static int shared_fd;
static void* shared;

/** Struct for sending data on addresses of functions. **/
struct addr_info {
	void* walker;
	void* brk;
	void* mmap;
	void* munmap;
	void* mprotect;
	void* sigaction;
};

/** Struct for storing data on what to walk. */
struct walker_info {
	void* ptr;
	int length;
};

/** Handler function of input program's signal handler */
static void handler (int, siginfo_t*, void*);

/** Handler function of input program's signal handler */
static void handler2 (int);

/** Variables to hold handler functions  */
static typeof(&handler) orig_sigsegv_handler = NULL;

static typeof(&handler2) orig_sigsegv_handler2 = NULL;

/** Global sigaction struct  */
static struct sigaction sa;

/** Hashmap object used to create a hashmap */
static hashmap_s hashmap;
/*===============================================================================*/



/* =============================================================================================================================== */
/**
 * \brief Add page to the hashmap.
 * \param address A given page.
 * \param permissions Record the page's protection flags.
 * \param isunprotected Record if page is unprotected or protected.
 * \return 'true' if page was added into hashmap; 'false' if attempted add failed.
 */
bool add_page(void* address, int permissions, bool isunprotected) {
	hashmap_entry_s entry;
	entry.page_num = (page_num_t) address;
	entry.original_perms = permissions;
	entry.unprotected = isunprotected;
	return hashmap_insert(&hashmap, entry);
} // add_page ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Set the page's permissions and protection status.
 * \param address A given page.
 * \param permissions The protection flags for the page.
 * \param use_permissions Check if the page needs its permissions updated.
 * \param is_unprotected Is the page now unprotected or protected.
 * \param use_protection Check if the page needs its protection status updated.
 */
void change_page_info(void *address, int permissions, bool use_permissions, bool isunprotected, bool use_protection) {
	hashmap_entry_s *entry = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(address));

	if (entry != NULL) {
		if (use_permissions == true) {
			entry->original_perms = permissions;
		}

		if (use_protection == true) {
			entry->unprotected = isunprotected;
		}
	}
} // change_page_info ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Use the hashmap to check if a page is unprotected.
 * \param address A given page.
 * \return 'true' if page exists in hashmap and unprotected; 'false' if page does
 *         not in hashmap or protected.
 */
bool is_page_unprotected(void* address) {
	hashmap_entry_s *entry = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(address));

	if (entry!= NULL && entry->unprotected == true) {
		return true;
	}

	return false;
} // is_page_unprotected ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Wrapper sigaction records a benchmark program's signal handler for
 *        later use in manager's handler and calls original handler.
 * \param signum Specifies signal type.
 * \param act Sigaction structure that contains sigaction handler.
 * \param oldact Sigaction structure that contains previous sigaction handler.
 * \return '0' if call was sucessful; '-1' if error occured during call.
 */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
	if (signum == SIGSEGV && &sa != act) {
		if (act->sa_flags == SA_SIGINFO) {
			orig_sigsegv_handler2 = NULL;
			orig_sigsegv_handler = act->sa_sigaction;
		} else {
			orig_sigsegv_handler = NULL;
			orig_sigsegv_handler2 = act->sa_handler;
		}
	} else {
		typeof(&sigaction) orig = dlsym(RTLD_NEXT, "sigaction");
		return orig(signum, act, oldact);
	}
} // sigaction ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Wrapper mprotect that takes in a page's original permissions and calls
 *        original mprotect () only if page is currently unprotected.
 * \param addr Starting page-aligned address of the memory region being protected.
 * \param len Length of the address range.
 * \param prot Desired memory protection of mapping.
 * \return '0' if call was sucessful; '-1' if error occured during call.
 */
int mprotect(void *addr, size_t len, int prot) {
	change_page_info(addr, prot, true, false, false);
	typeof(&mprotect) orig = dlsym(RTLD_NEXT, "mprotect");
	if (is_page_unprotected(addr) == true) {
		write(1, "page is in unprotected list\n", 28);
		hashmap_entry_s *temp = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(addr));
		if (temp == NULL) {
			//write_orig(1, "failure in wraper mprotect()\n", 30);
			exit(1);
		}

		return orig(addr, len, temp->original_perms);
	}

	return 0;
} // mprotect ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Standard mprotect call used exclusively in manager.
 * \param addr Starting page-aligned address of the memory region being protected.
 * \param len Length of the address range.
 * \param prot Desired memory protection of mapping.
 * \return '0' if call was sucessful; '-1' if error occured during call.
 */
int internal_mprotect(void *addr, size_t len, int prot) {
	static typeof(&mprotect) orig = NULL;

	if (orig == NULL) {
		orig = dlsym(RTLD_NEXT, "mprotect");
	}

	return orig(addr, len, prot);
} // internal_mprotect ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Intialize a new array that holds 'NULL' values
 * \param array[] Array whose elements are pointers
 * \param size Number of elements in array
 */
void initialize_array(void* array[], int size) {
	//Loops through array to set values to NULL
	for (int i = 0; i < size; i++) {
		array[i] = NULL;
	}
} // initialize_array ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Write a pointer address as a string
 * \param buffer String that holds converted pointer address
 * \param value  Pointer address casted as an integer
 */
void convert(char * buffer, uint64_t value) {
	//Performs bit operations to insert proper characters into string buffer
	for (int i = 0; i < 16; i++) {
		uint64_t bits = (value >> (60 - i*4)) & 0xf;
		char c = hex[bits];
		buffer[i] = c;
	}
	buffer[16] = '\n';
} // convert ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Add page to array and reprotect page when it will be replaced in array.
 * \param ptr Page.
 * \param array Array whose elements are pages.
 * \param index Pointer to index that keeps track of latest added page in array.
 * \param size Size of array.
 */
void add_ptr_to_list(void* ptr, void* array[], int* index, int size) {
	void* page_based_pointer = PAGE_BASE(ptr);
	//Checks if pointer is already in array
	/*
		 if (hashset_is_member(set, page_based_pointer) == 1) {
		 write(file_addr, "Seg fault at page already in table", 34);
		 exit(0);
		 }
		 */

	if (is_page_unprotected(page_based_pointer) == true) {
		write_orig(1, "Seg fault at page already in table", 34);
		exit(0);
	}

	//Checks if element is not null
	if (array[*index] != NULL) {

		//Replaces element and keeps track of old pointer
		void* old_ptr = array[*index];

		//Protects old pointer element
		if (internal_mprotect(old_ptr, sysconf(_SC_PAGE_SIZE), PROT_NONE) == -1) {
			write(file_addr, "mprotect() failed to protect when added to ptr\n", 48);
			exit(1);
		}

		//change location of page in hashmap
		change_page_info(old_ptr, -1, false, false, true);

		// Adds the pointer to the compressed caching mechanism.
		// cc_add((intptr_t) old_ptr);

	}

	//Add pointer to array
	array[*index] = page_based_pointer;

	//Increases index by one
	*index = *index + 1;

	//change location of page in hashmap
	change_page_info(page_based_pointer, -1, false, true, true);

	//Reset index to beginning of array
	if (*index == size) {
		*index = 0;
	}


} // add_ptr_to_list ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Signal handler that catches 'SIGSEGV' signals to unprotect/protect pages.
 * \param mysignal Number of signal that caused invocation of handler.
 * \param si Pointer to 'siginfo_t', a struct that contains more info about signal.
 * \param arg Pointer to 'ucontext_t' struct that contains signal context info.
 */
static void handler(int mysignal, siginfo_t *si, void* arg) {

	//Gets pagesize
	pagesize = sysconf(_SC_PAGE_SIZE);

	//Writes address where signal was caught into csv file
	//write(file_addr, "Segmentation fault occured at address: ", 39);
	convert(buffer,(uint64_t) PAGE_BASE(si->si_addr));
	write_orig(file_addr, buffer, 17);
  write_orig(file_addr, "one\n", 4);

	//Adds pointer to pointer array
	if (trace_flag == 1) {
		//Code to call input program's signal handler if it exists
		if (is_page_unprotected(si->si_addr) == true) {
			if (!(orig_sigsegv_handler == NULL && orig_sigsegv_handler2 == NULL)) {
				if (orig_sigsegv_handler != NULL) {
					orig_sigsegv_handler(mysignal, si, arg);
				} else {
					orig_sigsegv_handler2(mysignal);
				}
			}

			write_orig(1, "Segmentation Fault due to no signal handler\n", 44);
			exit(1);
		} else {

			add_ptr_to_list(PAGE_BASE(si->si_addr), ptr_list, &current_index, SIZE);
			hashmap_entry_s *entry_temp = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(si->si_addr));
			if (entry_temp == NULL) {
				write(1, "lookup failure in handler()\n", 28);
				exit(0);
			}


			//Unprotects page
			if (internal_mprotect(PAGE_BASE(si->si_addr), pagesize, entry_temp->original_perms) == -1) {
				write(file_addr, "mprotect() did not sucessfully protect in handler()\n", 52);
				exit(0);
			}

			// Removes page from the compressed cache.
			// cc_remove((intptr_t) PAGE_BASE(si->si_addr));
		}
	} else {

		if (internal_mprotect(PAGE_BASE(si->si_addr), pagesize, PROT_WRITE | PROT_READ) == -1) {
			write(file_addr, "mprotect() did not sucessfully protect in handler()\n", 52);
			exit(0);
		}
	}

} // handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Allocate memory with original malloc call and protect allocated space.
 * \param size Amount of memory requested.
 */
void* malloc (size_t size) {

	//Calls original malloc
	void* ptr = __libc_malloc(size);

	if (trace_flag == 0) {
		return ptr;
	}

	//Determines number of pages to mprotect()
	int base = (intptr_t) ptr;
	int limit = base + size -1;
	int numpages = (limit>>12) - (base>>12) + 1;

	//Checks if page is in list of unprotected, if not protect
	void* new_ptr = PAGE_BASE(ptr);
	while (numpages > 0) {
		//Add page to hashmap (will integrate into code later)
		if (is_page_unprotected(new_ptr) == false) {
			if (add_page(PAGE_BASE(new_ptr), PROT_READ | PROT_WRITE, false) == false) {
				write_orig(1, "add_page() failed in malloc\n", 28);
				exit(0);
			}
		}


		if (internal_mprotect(PAGE_BASE(new_ptr), sysconf(_SC_PAGE_SIZE) , PROT_NONE) == -1) {
			write(file_addr, "mprotect failed in malloc()\n", 29);
			exit(0);
		}



		// Adds the newly-protected page to the compressed cache.
		// cc_add((intptr_t) new_ptr);

		new_ptr = new_ptr + sysconf(_SC_PAGE_SIZE);
		numpages = numpages - 1;

	}

	return ptr;
} // malloc ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Called by parent system catcher, reads in data from the shared file on
 *        which addresses to walk.
 */
void walk_struct() {

	//walks struct, faulting on protected pages
	//if a string, walk to end
	//if struct, walk the specified length

	struct walker_info* wi_ptr = (struct walker_info*) shared;
	for(int i = 0; i < 6; i++) {
		int size = wi_ptr[i].length;
		if(size == 0) {
			break;
		}
		if (wi_ptr[i].ptr != NULL) {
			//string
			if (size == -1) {
				char *ptr = wi_ptr[i].ptr;
				while (*ptr != '\0') {
					ptr = ptr + 1;
				}
				//struct
			} else {
				void *ptr = wi_ptr[i].ptr;
				while(size > 0) {
					dummy = ptr;
					ptr = ptr + 1;
					size = size - 1;
				}
			}
		}
	}
	//send signal to parent that walking is done
	kill(getppid(), SIGUSR2);
} // walk_struct ()
/* =============================================================================================================================== */



//these functions are to be filled out later to handle virtualization
//check documentation for more info
/* =============================================================================================================================== */
/**
 * \brief Brk () wrapper that uses 'mprotect ()' to protect added data segment.
 * \param old_ptr Address that specifies end of data segment.
 * \param new_ptr Address that specifies new end of data segment.
 */
void brk_handler(void *old_ptr, void *new_ptr) {
	kill(getppid(), SIGUSR1);
} // brk_handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Mmap () wrapper that uses 'mprotect ()' to protect allocated memory.
 * \param ptr Starting address for new mapping.
 * \param size The length of mapping.
 * \param prot Desired memory protection of mapping.
 */
void mmap_handler(void *ptr, size_t size, int prot) {
	kill(getppid(), SIGUSR1);
} // mmap_handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Munmap () wrapper uses 'mprotect ()' to unprotect space before freeing.
 * \param ptr  Starting page-aligned address of the memory region being removed.
 * \param size Length of the address range.
 */
void munmap_handler(void *ptr, size_t size) {
	kill(getppid(), SIGUSR1);
} // munmap_handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Mprotect () wrapper records original protection permissions when called.
 * \param ptr Starting page-aligned address of the memory region being protected.
 * \param size Length of the address range.
 * \param prot Desired memory protection of mapping.
 */
void mprotect_handler(void *ptr, size_t size, int prot) {
	kill(getppid(), SIGUSR1);
} // mprotect_handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Sigaction () wrapper that records benchmark program's signal handler.
 * \param signum Specifies signal type.
 * \param sigaction act Sigaction structure that contains sigaction handler.
 */
void sigaction_handler(int signum, struct sigaction act) {
	kill(getppid(), SIGUSR1);
} // sigaction_handler ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Write () wrapper that walks string to unprotect it to use for debugging.
 * \param fd File descriptor to use write in.
 * \param buf Starting address of string.
 * \param count Number of bytes from buffer.
 * \return '0' if string was successfully written; '-1' if write () call failed.
 */
ssize_t write(int fd, const void *buf, size_t count) {
	//throws a warning but is fine
	char *ptr = (char*) buf;
	size_t size = count;
	while (size > 0) {
		dummy = ptr;
		ptr = ptr + 1;
		size = size - 1;
	}

	//Calls standard open()
	typeof(&write) orig = dlsym(RTLD_NEXT, "write");

	return orig(fd, buf, count);
} // write ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Initialize signal catcher, create csv file using name of benchmark
 *        program opens csv file, initialize pointer array, and call main()
 *        on benchmark program.
 * \param argc Number of command line arguements.
 * \param argv Pointer to list of individual command line argument strings.
 * \param envp Array of pointers to environment variables.
 * \return Return value of main ().
 */
int main_hook(int argc, char **argv, char **envp)
{
	//Sets up custom write for handler
	write_orig = dlsym(RTLD_NEXT, "write");

	//Opens and writes to a shared file to send address of walk and handler functions to parent
	shared_fd = open("shared.data", O_RDWR, S_IRWXU);
	shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);

	//The following segment of commented code seems to cause seg faults and write () calls to not print so    it's left out of manager for now.
	/*
		 struct addr_info* info = (struct addr_info*) shared;
		 info->walker = walk_struct;
		 info->brk = brk_handler;
		 info->mmap = mmap_handler;
		 info->munmap = munmap_handler;
		 info->mprotect = mprotect_handler;
		 info->sigaction = sigaction_handler;

		 kill(getppid(), SIGUSR1);
		 */

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sigaction(SIGSEGV, &sa, NULL);

	//Get size of hash and array
	SIZE = atoi(getenv("VMT_SIZE"));
	void* ptr_arr[SIZE];
	ptr_list = ptr_arr;

	// Initializes compressed cache.
	// cc_init();

	//Get the file name
	char *FILENAME = getenv("VMT_TRACENAME");

	// Create the output file
	file_addr = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);

	//Intialize array
	initialize_array(ptr_list, SIZE);

	//Tells malloc to start protecting pages
	trace_flag = 1;

	//Initalize hashmap
	hashmap_create(&hashmap);

	//Calls main() in benchmark program
	int ret = main_orig(argc, argv, envp);

	write(file_addr, "End\n", 4);
	close(file_addr);

	return ret;
} // main_hook ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Replace __libc_start_main () to call main_hook ().
 * \param main Main () of benchmark program called with __libc_start_main ().
 * \param argc Argc off of the stack.
 * \param argv Argv off of the stack.
 * \param init Constructor of program.
 * \param fini Destructor of program.
 * \param rtld_fini Destructor of dynamic linker.
 * \param stack_end Aligned stack pointer.
 * \return Return value of main ().
 */
	int __libc_start_main(
			int (*main) (int, char **, char **),
			int argc,
			char **argv,
			int (*init) (int, char **, char **),
			void (*fini) (void),
			void (*rtld_fini) (void),
			void *stack_end)
{
	main_orig = main;

	//Calls orignal __libc_start_main
	typeof(&__libc_start_main) orig = dlsym(RTLD_NEXT, "__libc_start_main");

	//Calls on main_hook
	return orig(main_hook, argc, argv , init,  fini, rtld_fini, stack_end);
} // __libc_start_main ()
/* =============================================================================================================================== */
