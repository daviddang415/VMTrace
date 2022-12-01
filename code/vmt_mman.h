// =============================================================================
/*******************************************************************************
 * The header file for the simple memory allocator which our VMTrace code will
 * be using in order to avoid polluting the heap with allocations that need to
 * be made in order to store the data structures that are maintaing the whole
 * tracing infrastructure. Those data structures include the LRU queue (which
 * keeps track of the protected pages), the hot set (which keeps track of the
 * unprotected pages) and of (probably various) hash maps.
 *
 * @author Luka Duranovic <luk.duranovic@gmail.com>
 *                        <lduranovic22@amherst.edu>
 * @date   Thursday 29 July 2021
 ******************************************************************************/
// =============================================================================



// =============================================================================
// INCLUDES

#ifndef VMT_MMAN_H
#define VMT_MMAN_H

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h> // For `sysconf()`
// =============================================================================



// =============================================================================
// MACROS AND CONSTANTS

/*
 * By defining these macros, the specific choice of allocator can be changed by
 * redefining these.  For example, we could use the standard allocator (e.g.,
 * `#define vmt_malloc malloc`), or we could add alternative allocators, such as
 * a best fit, a segregated fit, or a binary buddy allocator, and select which
 * one to use here.
 *
 * Our default choice is _first fit_, since it is most simple, and likely to be
 * fast and effective (we think) given the uses required by VMTrace.
 */
#define vmt_malloc  ff_malloc
#define vmt_free    ff_free
#define vmt_calloc  ff_calloc
#define vmt_realloc ff_realloc
// =============================================================================



// =============================================================================
// DATA MEMBERS

// NEEDS MORE WORK:
// Do we need to have any data members here at all??
// Should the process that is including this header file know anything about
// the internals of the allocator? -- Probably not...
// =============================================================================



// =============================================================================
// FUNCTION DECLARATIONS

/**
 * \brief Initializes the allocator.
 */
void vmt_init ();

/**
 * \brief  Allocates the requested a block of memory in a private heap.
 * \param  size The minimum number of bytes to allocate.
 * \return a pointer to the allocated block, or `NULL` if the allocation fails.
 */
void* vmt_malloc (size_t size);

/**
 * \brief Deallocates the given block.
 * \param ptr The address of the block to deallocate.
 */
void vmt_free (void* ptr);
// =============================================================================



// =============================================================================
#endif // vmt_mman.h
// =============================================================================
