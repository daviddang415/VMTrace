// =============================================================================
/*******************************************************************************
 * NEEDS MORE WORK.
 * Write the description here.
 *
 * @author Luka Duranovic <luk.duranovic@gmail.com>
 *                        <lduranovic22@amherst.edu>
 * @date   Thursday 29 July 2021
 ******************************************************************************/
// =============================================================================



// =============================================================================
// INCLUDES

#include <errno.h>   // errno used by perror()
#include <stdbool.h> // true & false
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   // memset
#include <sys/mman.h> // mmap() & munmap()

#include "safeio.h"   // DEBUG() & ERROR()
#include "vmt_mman.h"
// =============================================================================



// =============================================================================
// MACROS AND CONSTANTS

/**
 * Macros to easily calculate the number of bytes for larger scales (e.g., kilo,
 * mega, gigabytes).
 */
#define KB(size)  ((size_t)size * 1024)
#define MB(size)  (KB(size) * 1024)
#define GB(size)  (MB(size) * 1024)

/** The virtual address space reserved for the heap. */
#define HEAP_SIZE MB(64)

/** The size of a single page on this system. */
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

/** Whether to emit debugging message. */
#define VMT_DEBUG true

/**
 * The minimum size at which we mmap() a region instead of allocating in the
 * private heap itself.
 */
#define LARGE_BLOCK_THRESHOLD (PAGE_SIZE / 2)

/** Given a pointer to a header, obtain a `void*` pointer to the block itself. */
#define HEADER_TO_BLOCK(hp) ((void*)((intptr_t)hp + sizeof(header_s)))

/** Given a pointer to a block, obtain a `header_s*` pointer to its header. */
#define BLOCK_TO_HEADER(bp) ((header_s*)((intptr_t)bp - sizeof(header_s)))

/** The size of a native word/pointer (bytes). */
#define WORD_SIZE sizeof(void*)

/** The size of a double word (bytes). */
#define DWORD_SIZE (2 * WORD_SIZE)

/**
 * The padding for a given block size needed to expand the block to an integral
 * number of double-words (for alignment).
 */
#define PAD(size) ((DWORD_SIZE - (size % DWORD_SIZE)) % DWORD_SIZE)

/**
 *  The padding for a given block size needed to expand the block to an integral
 *  number of pages (for alignment).
 */
#define PAGE_PAD(size) ((PAGE_SIZE - (size % PAGE_SIZE)) % PAGE_SIZE)

/** The flag value for marking a block _allocated_ in its header. */
#define HEADER_ALLOCATED 1

/**
 * The flag value for marking a block _large_ (and thus separately mapped) in
 * its header.
 */
#define HEADER_LARGE     2

/** Get whether a block has a given flag set. */
#define GET_FLAG(hp,flag) (hp->flags & flag)

/** Set a given flag in a block's header. */
#define SET_FLAG(hp,flag) (hp->flags |= flag)

/** Clear a given flag in a block's header. */
#define CLEAR_FLAG(hp,flag) (hp->flags &= ~flag)
// =============================================================================



// =============================================================================
// TYPES AND STRUCTURES

/** The header for each allocated object. */
typedef struct header {

  /** The usable size of the block (exclusive of the header itself). */
  size_t         size;

  /** Pointer to the next header in the list. */
  struct header* next;

  /** Pointer to the previous header in the list. */
  struct header* prev;

  /** Flags that track characteristics of the block? */
  unsigned int   flags;

} header_s;
// =============================================================================



// =============================================================================
// GLOBALS

/** The address of the next available byte in the heap region. */
static intptr_t free_addr  = 0;

/** The beginning of the heap. */
static intptr_t start_addr = 0;

/** The end of the heap. */
static intptr_t end_addr   = 0;

/** The head of the free list. */
static header_s* free_list_head      = NULL;

/** The head of the allocated list. */
static header_s* allocated_list_head = NULL;
// =============================================================================



// =============================================================================
/**
 * \brief  Allocate a block by mapping its own private region.
 * \param  size The number of bytes to allocate.
 * \return A pointer to the mapped space; `NULL` if the mapping fails.
 */
static void* ff_map_region (size_t size)
{

  // Allocate virtual address space in which the heap will reside. Make it
  // un-shared and not backed by any file (_anonymous_ space).
  void* region_ptr = mmap(NULL,
			  HEAP_SIZE,
			  PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS,
			  -1,
			  0);
  
  return (region_ptr == MAP_FAILED) ? NULL : region_ptr;
  
} // ff_map_region ()
// =============================================================================



// =============================================================================
static void ff_unmap_region (void* ptr, size_t size)
{

  if (munmap(ptr, size) == -1) {
    perror("Failed munmap() of region");
  }
  
}
// =============================================================================



// =============================================================================
/**
 * \brief Initialize the allocator by mmap'ing a private heap to manage.
 */
void ff_init ()
{

  // Only do anything if there is no heap region (i.e., first time called).
  if (start_addr == 0) {

    // Map a space to serve as the private heap to be managed.  Failure here is
    // fatal.
    void* heap = ff_map_region(HEAP_SIZE);
    if (heap == MAP_FAILED) {
      ERROR("Could not map heap region");
    }

    // Hold onto the boundaries of the heap as a whole.
    start_addr = (intptr_t)heap;
    end_addr   = start_addr + HEAP_SIZE;
    free_addr  = start_addr;

  }

} // ff_init ()
// =============================================================================



// =============================================================================
/**
 * Allocate and return `size` bytes of heap space.  Specifically, search the
 * free list, choosing the _best fit_.  If no such block is available, expand
 * into the heap region via _pointer bumping_.
 *
 * \param size The number of bytes to allocate.
 * \return A pointer to the allocated block, if successful; `NULL` if unsuccessful.
 */
void* ff_malloc (size_t size)
{

  ff_init();

  // Cannot allocate an empty block.
  if (size == 0) {
    return NULL;
  }

  // If this block is large enough, just mmap() its own space, separate from the
  // private heap.
  if (size >= LARGE_BLOCK_THRESHOLD) {

    // Store a header in the region that indicates its size.  The size must be
    // an integral number of pages.  SK: Yes, it could be a smaller header, but
    // using a standard header is nicely uniform.
    size_t    header_size = sizeof(header_s) + PAD(sizeof(header_s));
    size_t    block_size  = size + header_size;
    size_t    total_size  = block_size + PAGE_PAD(block_size);
    header_s* header_ptr  = ff_map_region(total_size);
    void*     region      = (void*)(header_ptr + 1);

    // Note that for large blocks, we store the total size in the header so that
    // we can properly unmap the space when it is freed.
    header_ptr->size      = total_size;
    header_ptr->next      = NULL;
    header_ptr->prev      = NULL;
    SET_FLAG(header_ptr, HEADER_ALLOCATED);
    SET_FLAG(header_ptr, HEADER_LARGE);
    return region;
    
  }
  
  // Search the free list for the first acceptable block.
  header_s* current = free_list_head;
  while (current != NULL && current->size < size) {
    current = current->next;
  }

  // Was some sufficiently large block found?
  void* new_block_ptr = NULL;
  if (current != NULL) {

    // Yes. Remove it from the free list.
    if (current->prev == NULL) {
      free_list_head      = current->next;
    } else {
      current->prev->next = current->next;
    }
    if (current->next != NULL) {
      current->next->prev = current->prev;
    }
    current->next         = NULL;
    current->prev         = NULL;

    // Return the block itself.
    SET_FLAG(current, HEADER_ALLOCATED);
    CLEAR_FLAG(current, HEADER_LARGE);
    new_block_ptr         = HEADER_TO_BLOCK(current);
    
  } else {

    // No.  Grow the heap.  The new object will go at the beginning of the
    // expanded area.  Include padding around the block.
    size_t header_pad    = PAD(sizeof(header_s));
    size_t block_pad     = PAD(size);
    size_t block_size    = size + block_pad;
    size_t total_size    = header_pad + sizeof(header_s) + block_size;
    header_s* header_ptr = (header_s*)(free_addr + header_pad);
    new_block_ptr = HEADER_TO_BLOCK(header_ptr);

    // Set up the header.
    header_ptr->next      = NULL;
    header_ptr->prev      = NULL;
    header_ptr->size      = block_size;
    SET_FLAG(header_ptr, HEADER_ALLOCATED);
    CLEAR_FLAG(header_ptr, HEADER_LARGE);
    
    // Move the free address forward, past the new block.  Make sure we are
    // still within the heap space.
    intptr_t new_free_addr = (intptr_t)new_block_ptr + total_size;
    if (new_free_addr > end_addr) {

      // Not enough space!  Return failure.
      return NULL;

    } else {

      // Enough space, so commit the movement forward of the occupied heap.
      free_addr = new_free_addr;

    }

  }

  // dd the block to the allocated list.
  header_s* new_header_ptr = BLOCK_TO_HEADER(new_block_ptr);
  new_header_ptr->next = allocated_list_head;
  allocated_list_head  = new_header_ptr;
  new_header_ptr->prev = NULL;
  if (new_header_ptr->next != NULL) {
    new_header_ptr->next->prev = new_header_ptr;
  }
  
  return new_block_ptr;

} // ff_malloc ()
// =============================================================================



// =============================================================================
/**
 * Deallocate a given block on the private heap.  Add the given block (if any)
 * to the free list.
 *
 * \param ptr A pointer to the block to be deallocated.
 */
void ff_free (void* ptr) {

  // This function is allowed to be passed a `NULL` pointer.  Do nothing.
  if (ptr == NULL) {
    return;
  }

  // Walk back to the header.
  header_s* header_ptr = BLOCK_TO_HEADER(ptr);

  // Sanity check: Is this block already marked as free?
  if (!GET_FLAG(header_ptr, HEADER_ALLOCATED)) {
    ERROR("Double-free: ", (intptr_t)header_ptr);
  }

  // Is this a large block that was allocated in its own mapped space?
  if (GET_FLAG(header_ptr, HEADER_LARGE)) {
    ff_unmap_region(header_ptr, header_ptr->size);
    return;
  }

  // Remove it from the allocated list.
  if (header_ptr->prev == NULL) {
    allocated_list_head    = header_ptr->next;
  } else {
    header_ptr->prev->next = header_ptr->next;
  }
  if (header_ptr->next != NULL) {
    header_ptr->next->prev = header_ptr->prev;
  }
  header_ptr->next = NULL;
  header_ptr->prev = NULL;
  
  // Insert it at the head of the free list.
  header_ptr->next = free_list_head;
  free_list_head   = header_ptr;
  header_ptr->prev = NULL;
  if (header_ptr->next != NULL) {
    header_ptr->next->prev = header_ptr;
  }
  CLEAR_FLAG(header_ptr, HEADER_ALLOCATED);

} // ff_free()
// =============================================================================



// ==============================================================================
/**
 * Allocate a block of `nmemb * size` bytes on the heap, zeroing its contents.
 *
 * \param nmemb The number of elements in the new block.
 * \param size  The size, in bytes, of each of the `nmemb` elements.
 * \return      A pointer to the newly allocated and zeroed block, if successful;
 *              `NULL` if unsuccessful.
 */
void* ff_calloc (size_t nmemb, size_t size)
{

  // Allocate a block of the requested size.
  size_t block_size    = nmemb * size;
  void*  new_block_ptr = ff_malloc(block_size);

  // If the allocation succeeded, clear the entire block.
  if (new_block_ptr != NULL) {
    memset(new_block_ptr, 0, block_size);
  }

  return new_block_ptr;
  
} // ff_calloc ()
// ==============================================================================



// ==============================================================================
/**
 * Update the given block at `ptr` to take on the given `size`.  Here, if `size`
 * fits within the given block, then the block is returned unchanged.  If the
 * `size` is an increase for the block, then a new and larger block is
 * allocated, and the data from the old block is copied, the old block freed,
 * and the new block returned.
 *
 * \param ptr  The block to be assigned a new size.
 * \param size The new size that the block should assume.
 * \return     A pointer to the resultant block, which may be `ptr` itself, or
 *             may be a newly allocated block.
 */
void* realloc (void* ptr, size_t size) {

  // Special case: If there is no original block, then just allocate the new one
  // of the given size.
  if (ptr == NULL) {
    return ff_malloc(size);
  }

  // Special case: If the new size is 0, that's tantamount to freeing the block.
  if (size == 0) {
    ff_free(ptr);
    return NULL;
  }

  // Get the current block size from its header.
  header_s* header_ptr = BLOCK_TO_HEADER(ptr);

  // If the new size isn't an increase, then just return the original block as-is.
  if (size <= header_ptr->size) {
    return ptr;
  }

  // The new size is an increase.  Allocate the new, larger block, copy the
  // contents of the old into it, and free the old.
  void* new_block_ptr = malloc(size);
  if (new_block_ptr != NULL) {
    memcpy(new_block_ptr, ptr, header_ptr->size);
    ff_free(ptr);
  }
    
  return new_block_ptr;
  
} // ff_realloc()
// ==============================================================================
