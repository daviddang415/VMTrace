#define _GNU_SOURCE

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
#include "hashset.h"
#include "hashmap.h"

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

//goals for hash table:
//all caught ptrs go to hash table and array
//check if ptr is in hash table
//check replace ptr

//Aligns pointer to the beginning of page
#define PAGE_BASE(p) ((void *)((intptr_t)p & ~0xfff)) 

//Character array used to write pointer address
static char hex[37] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 
	           'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
		   'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
		   'X', 'Y', 'Z'};

//String that holds pointer address
static char buffer[17];

//Address of main function in benchmark program
static int (*main_orig) (int, char **, char **);

//Original malloc function
extern void *__libc_malloc(size_t size);

//Maintains index that contains the most recent pointer array element added
static int current_index = 0;

//Pointer array that tracks pages used
static void ** ptr_list;

//Value used to direct output into csv file
static int file_addr;

//Size of a page;
static int pagesize;

//Size of ptr_list;
static int SIZE;

//Orginal write for debugging
static typeof(&write) write_orig;

//Lock for multithreaded programs
pthread_mutex_t lock;

static const unsigned int prime_1 = 73;
static const unsigned int prime_2 = 5009;

static struct hashset_st set_struct;
static hashset_t  set = &set_struct;

static int trace_flag = 0;
static pthread_mutex_t trace_flag_lock;

static void handler (int, siginfo_t*, void*);

static void handler2 (int);

static typeof(&handler) orig_sigsegv_handler = NULL;

static typeof(&handler2) orig_sigsegv_handler2 = NULL;

static struct sigaction sa;

static hashmap_s hashmap;

bool add_page(void* address, int permissions, bool isunprotected) { 
   hashmap_entry_s entry; 
   entry.page_num = (page_num_t) address;
   entry.original_perms = permissions;
   entry.unprotected = isunprotected;
   return hashmap_insert(&hashmap, entry);
}

void change_page_info(void* address, int permissions, bool use_permissions, bool isunprotected, bool use_protection) {
	hashmap_entry_s *entry = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(address));
	
	if (entry != NULL) {	
		if (use_permissions == true) {
		  entry->original_perms = permissions;
		}

		if (use_protection == true) {
		  entry->unprotected = isunprotected;
		}
	}
}

bool is_page_unprotected(void* address) {
	hashmap_entry_s *entry = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(address));

	if (entry!= NULL && entry->unprotected == true) {
	   return true;
	}

	return false;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
	//write_orig(1, "wrapper sigaction() has been called\n", 36);
	if (signum == SIGSEGV && &sa != act) {	
	      //write_orig(1, "pick the phone\n", 15);
	      if (act->sa_flags == SA_SIGINFO) {
                orig_sigsegv_handler2 = NULL;
		orig_sigsegv_handler = act->sa_sigaction;
	      } else {
		orig_sigsegv_handler = NULL;
		orig_sigsegv_handler2 = act->sa_handler;
	      }
	} else {
	  //write_orig(1, "mostly cloudly\n", 15);
          typeof(&sigaction) orig = dlsym(RTLD_NEXT, "sigaction");
	  return orig(signum, act, oldact); 	
	}
}

int mprotect(void *addr, size_t len, int prot) {
	//write_orig(1, "wrapper mprotect() has been called\n", 35);	

	//page_permissions = prot;
	
	change_page_info(addr, prot, true, false, false);

	//write_orig(1, "call before dlsym\n", 18);

	typeof(&mprotect) orig = dlsym(RTLD_NEXT, "mprotect");


	//hashset_is_member(set, PAGE_BASE(addr)) == 1
	
        if (is_page_unprotected(addr) == true) {
          write_orig(1, "page is in unprotected list\n", 28);
	  
	  hashmap_entry_s *temp = hashmap_lookup (&hashmap, (page_num_t) PAGE_BASE(addr));
	  
	  if (temp == NULL) {
            write_orig(1, "failure in wrapper mprotect()\n", 30); 
            exit(1);
	  }
	 
	  return orig(addr, len, temp->original_perms);
	
	}
	
	return 0;
}

int internal_mprotect(void *addr, size_t len, int prot) {
	//write_orig(1, "internal mprotect() has been called\n", 36);

	static typeof(&mprotect) orig = NULL;
	
	if (orig == NULL) {
		orig = dlsym(RTLD_NEXT, "mprotect");
	}

	return orig(addr, len, prot);
}

void *calloc_custom(int nelem, size_t elsize)
{
	void *p;

	p = __libc_malloc (nelem * elsize);
	if (p == 0) {
		return (p);
	}
	bzero (p, nelem * elsize);
	return (p);
}

void hashset_create()
{
  //makes sure nothing else gets put on the first page of the hash set
  __libc_malloc(sysconf(_SC_PAGE_SIZE));

  set->capacity = (size_t) atoi(getenv("VMT_SIZE"))*4;
  set->mask = set->capacity - 1;
  
  set->items = calloc_custom(set->capacity, sizeof(size_t));
  
  //makes sure nothing else gets put on the last page of the hash set
  __libc_malloc(sysconf(_SC_PAGE_SIZE));
  assert(set-> items != NULL);

  if (set->items == NULL) {
    hashset_destroy(set);
    exit(0);
  }

  set->nitems = 0;
  set->n_deleted_items = 0;
}

static int hashset_add_member(hashset_t set, void *item)
{
  size_t value = (size_t)item >> 12;
    size_t ii;

    if (value == 0 || value == 1) {
        return -1;
    }

    ii = set->mask & (prime_1 * value);

    while (set->items[ii] != 0 && set->items[ii] != 1) {
        if (set->items[ii] == value) {
            return 0;
        } else {
            /* search free slot */
	  ii = set->mask & (ii + prime_2);
        }
    }
    set->nitems++;
    if (set->items[ii] == 1) {
        set->n_deleted_items--;
    }
    set->items[ii] = value;
    return 1;
}


int hashset_remove(hashset_t set, void *item)
{
  size_t value = (size_t)item >> 12;
    size_t ii = set->mask & (prime_1 * value);

    while (set->items[ii] != 0) {
        if (set->items[ii] == value) {
            set->items[ii] = 1;
            set->nitems--;
            set->n_deleted_items++;
            return 1;
        } else {
	  ii = set->mask & (ii + prime_2);
        }
    }
    return 0;
}

int hashset_is_member(hashset_t set, void *item)
{
  assert(set != NULL);
  size_t value = (size_t)item >> 12;
    size_t ii = set->mask & (prime_1 * value);

    while (set->items[ii] != 0) {
        if (set->items[ii] == value) {
            return 1;
        } else {
	  ii = set->mask & (ii + prime_2);
        }
    }
    return 0;
}

//Arguments: pointer array, size of array
//Intializes the elements of the array pointer to NULL values
void initialize_array(void* array[], int size) {
  //Loops through array to set values to NULL
  for (int i = 0; i < size; i++) {
		array[i] = NULL;
	}
}

//Arguements: pointer of buffer, pointer address converted as unsigned integer
//Writes the pointer address as a string in buffer
void convert(char * buffer, uint64_t value) {
	//Performs bit operations to insert proper characters into string buffer
	for (int i = 0; i < 16; i++) {
		uint64_t bits = (value >> (60 - i*4)) & 0xf;
		char c = hex[bits];
		buffer[i] = c;
	}
	buffer[16] = '\n';
}

//Arguements: pointer, pointer array, pointer of index, size of array
//Adds pointer to pointer array and reprotects pages to be replaced in array
void add_ptr_to_list(void* ptr, void* array[], int* index, int size) {
        void* page_based_pointer = PAGE_BASE(ptr);

	//Checks if pointer is already in array
	//write_orig(1, "add_ptr_to_list() is called\n", 28);
	
	
	//hashset_is_member(set, page_based_pointer) == 1
	
	if (is_page_unprotected(page_based_pointer) == true) {
	  write_orig(file_addr, "Seg fault at page already in table", 34);
	  exit(1);
	}

	//write_orig(file_addr, "Pointer has been added to array\n", 32);
	  //Checks if element is not  null
	  if (array[*index] != NULL) {
	    
	    //Replaces element and keeps track of old pointer
	    void* old_ptr = array[*index];
	    
	    //write_orig(file_addr, "Ptr to be unprotected\n", 22);
	    
	    //Protects old pointer element
	    if (internal_mprotect(old_ptr, sysconf(_SC_PAGE_SIZE), PROT_NONE) == -1) {
              //write_orig(file_addr, "mprotect() failed to protect when added to ptr\n", 48);
	      exit(1);
	    }

	    //remove old pointer from hashset
	    hashset_remove(set, old_ptr);

	    //change location of page in hashmap
	    change_page_info(old_ptr, -1, false, false, true);
	  }

	   //Add pointer to array
          array[*index] = page_based_pointer;

	  //Increases index by one
	  *index = *index + 1;

	  hashset_add_member(set, page_based_pointer);
	  
	  change_page_info(page_based_pointer, -1, false, true, true);
	  
	  //Reset index to beginning of array
	  if (*index == size) {
	    //write_orig(file_addr, "index value has been reset to the beginning of list\n", 52); 
		  *index = 0;
	  }
}

//Arguments: signal, signal information, and pointer to other arguements
//Catches SIGSEV signals, writes the page in to csv file, addes pointer to array, 
//and unprotects page
static void handler(int mysignal, siginfo_t *si, void* arg) {
  //Gets pagesize
  pagesize = sysconf(_SC_PAGE_SIZE);

  //write_orig(1, "in handler\n", 11);

  //Writes address where signal was caught into csv file
  write_orig(file_addr, "Segmentation fault occured at address: ", 39);
  convert(buffer, (uint64_t) PAGE_BASE(si->si_addr));
  write_orig(file_addr, buffer, 17);

  //Adds pointer to pointer array
  if (trace_flag == 1) {
     
     //hashset_is_member(set, PAGE_BASE(si->si_addr)) == 1
     if (is_page_unprotected(si->si_addr) == true) {
	   if(!(orig_sigsegv_handler == NULL && orig_sigsegv_handler2 == NULL)) {
	     if (orig_sigsegv_handler != NULL) {
		     orig_sigsegv_handler(mysignal, si, arg);
	     } else {
                     orig_sigsegv_handler2(mysignal);
	     }
	   }

  /*	   
	   change_page_info(si->si_addr, PROT_WRITE | PROT_READ, true, false, false); 
	   if (internal_mprotect(PAGE_BASE(si->si_addr), pagesize, PROT_WRITE | PROT_READ) == -1) {
		  
	       write_orig(1, "internal mprotect() failed in handler()\n", 40); 
	       exit(1);
	  }
*/
          write_orig(1, "Segmentation Fault due to no signal handler\n", 44);
	  exit(1); 
     } else {
     add_ptr_to_list(PAGE_BASE(si->si_addr), ptr_list, &current_index, SIZE);

     hashmap_entry_s *entry_temp = hashmap_lookup(&hashmap, (page_num_t) PAGE_BASE(si->si_addr));
     if (entry_temp == NULL) {
	write_orig(1, "lookup failure in handler()\n", 28);   
	exit(1);
     }
     
    if (internal_mprotect(PAGE_BASE(si->si_addr), pagesize, entry_temp->original_perms) == -1) {
      write_orig(file_addr, "mprotect() did not sucessfully protect in handler()\n", 52);
      exit(1);
      }
    }
    //trace_flag = 1;
    //pthread_mutex_unlock(&trace_flag_lock);
    
  } else {

    if (internal_mprotect(PAGE_BASE(si->si_addr), pagesize, PROT_WRITE | PROT_READ) == -1) {
      write_orig(file_addr, "mprotect() did not sucessfully protect in handler()\n", 52);
      exit(0);
    }
  }

  //write_orig(file_addr, "Page has been unprotected: ", 26);
  //write_orig(file_addr, buffer, 17);
}

//Arguments: size of desired allocated memory
//Calls original malloc, calculates number of pages to protect
//and protects the newly allocated memory
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

	//typeof(&mprotect) orig = dlsym(RTLD_NEXT, "mprotect");

	while (numpages > 0) {
	  //hashset_is_member(set, new_ptr) == 0
	
	  if (is_page_unprotected(new_ptr) == false) {
	     if (add_page(PAGE_BASE(new_ptr), PROT_READ | PROT_WRITE, false) == false) {
		write_orig(1, "add_page() failed in internal_mprotect()\n", 41); 
	        exit(1);
	     }

             if (internal_mprotect(PAGE_BASE(new_ptr), sysconf(_SC_PAGE_SIZE) , PROT_NONE) == -1) {
	      write_orig(file_addr, "mprotect failed in malloc()\n", 29);
	      exit(1);
	    }
	  }

	  new_ptr = new_ptr + sysconf(_SC_PAGE_SIZE);
	  numpages = numpages - 1;

	}

	//write_orig(file_addr, "Call finished\n", 14); 
	return ptr;
}

void walk_str(const char *str) {
  
  //Walks string, faulting on protected pages
  while (*str != '\0') {
    str = str + 1;
  }
  
}

//Arguments: pathname to file, desired operation using file
//Unprotects the pathname and calls original fopen to open file
FILE *fopen(const char *restrict pathname, const char *restrict mode) {
  //write_orig(file_addr, "New fopen() has been called\n", 28);
   
  walk_str(pathname);

  //write_orig(file_addr, "fopen() call finished\n", 22);
  
  //Calls standard fopen()
  typeof(&fopen) orig = dlsym(RTLD_NEXT, "fopen");

  return orig(pathname, mode);
}

//Arguments: pathanme to file, desire operation to use file, 
//and desired operation when file is opened
//Unprotects pathname and calls original open() to open file  
int open(const char *pathname, int flags, ...) {

   //write_orig(file_addr, "New open() has been called\n", 27);

   //Deals with the arguments for open
   mode_t mode = S_IRWXU;
   
   if (flags & (O_CREAT|O_TMPFILE))
    {
      va_list ap;
      va_start(ap, flags);
      mode = va_arg(ap, mode_t);
      va_end(ap);
    }

   walk_str(pathname);

   //write_orig(file_addr, "open() call finished\n", 21);

   //Calls standard open()
   typeof(&open) orig = dlsym(RTLD_NEXT, "open");

   return orig(pathname, flags, mode);
}

int openat(int dirfd, const char *pathname, int flags, ...) {

  //write_orig(file_addr, "New openat() has been called\n", 29);

  //Deals with the arguments for open
  mode_t mode = S_IRWXU;
  
  if (flags & (O_CREAT|O_TMPFILE))
    {
      va_list ap;
      va_start(ap, flags);
      mode = va_arg(ap, mode_t);
      va_end(ap);
    }

   walk_str(pathname);

   //write_orig(file_addr, "openat() call finished\n", 23);

   //Calls standard openat()
   typeof(&openat) orig = dlsym(RTLD_NEXT, "openat");

   return orig(dirfd, pathname, flags, mode);
}

size_t fread(void *pathname, size_t size, size_t nitems, FILE *stream) {

   //write_orig(file_addr, "New fread() has been called\n", 28);

   walk_str(pathname);

   //write_orig(file_addr, "fread() call finished\n", 22);

   //Calls standard fread()
   typeof(&fread) orig = dlsym(RTLD_NEXT, "fread");

   return orig(pathname, size, nitems, stream);
}

ssize_t read(int fd, void *buf, size_t count) {

   //write_orig(file_addr, "New read() has been called\n", 27);

   walk_str(buf);

   //write_orig(file_addr, "read() call finished\n", 21);

   //Calls standard open()
   typeof(&read) orig = dlsym(RTLD_NEXT, "read");

   return orig(fd, buf, count);
}

size_t fwrite(const void *pathname, size_t size, size_t nitems, FILE *stream) {

   //write_orig(file_addr, "New fwrite() has been called\n", 27);

   walk_str(pathname);

   //write_orig(file_addr, "fwrite() call finished\n", 23);

   //Calls standard open()
   typeof(&fwrite) orig = dlsym(RTLD_NEXT, "fwrite");

   return orig(pathname, size, nitems, stream);
}

ssize_t write(int fd, const void *buf, size_t count) {

   //write_orig(file_addr, "New write() has been called\n", 28);

   walk_str(buf);

   //write_orig(file_addr, "write() call finished\n", 22);

   //Calls standard open()
   typeof(&write) orig = dlsym(RTLD_NEXT, "write");

   return orig(fd, buf, count);
}

//Initializes signal catcher, creates csv file using name of benchmark program,
//opens csv file, intialize pointer array, and call main() on benchmark program
int main_hook(int argc, char **argv, char **envp)
{
  write_orig = dlsym(RTLD_NEXT, "write");
  //write_orig(1, "Start\n", 6);

  //Initializes signal catcher
  //struct sigaction sa;

  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = handler;
  sigaction(SIGSEGV, &sa, NULL);

  //Get size of hash and array
  SIZE = atoi(getenv("VMT_SIZE"));
  void* ptr_arr[SIZE];
  ptr_list = ptr_arr;

  //Initializes hashset
  hashset_create();

  //Get the file name
  char *FILENAME = getenv("VMT_TRACENAME");

  //Create the output file
  file_addr = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
	
  //Initializes array
  initialize_array(ptr_list, SIZE);

  //Initializes hashmap
  hashmap_create(&hashmap); 

  trace_flag = 1;

  //Calls main() in benchmark program
  int ret = main_orig(argc, argv, envp);

  write_orig(file_addr, "End\n", 4);
  close(file_addr);
  
  return ret;
}

//Replaces __libc_start_main to use call on main_hook
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
}
