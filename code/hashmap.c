/* =============================================================================================================================== */
/**
 * \file hashmap.c
 * \author Scott Kaplan <sfkaplan@amherst.edu>
 * \date 2021-Jul-20
 * \brief A open addressing hashmap implementation.
 */
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* INCLUDES */

#include <assert.h>
#include <stdbool.h>  // true
#include <stdint.h>   // For uint32_t and uint64_t
#include <stdio.h>    // For fprintf()
#include <strings.h>  // For bzero()
#include <stdlib.h>   // For exit()
#include <sys/mman.h> // For mmap()/munmap()

#include "hashmap.h"
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/* MACROS and CONSTANTS */

/** The initial capacity of the hash map's array. */
#define INITIAL_CAPACITY 1024

/** Load factor threshold. */
#define LOAD_FACTOR_THRESHOLD 0.5
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Allocate and initialize a new hash map.
 * \param hashmap The hashmap structure that serves as the entry point to the hashmap itself.
 */
void hashmap_create (hashmap_s* hashmap) {

  hashmap->capacity = INITIAL_CAPACITY;
  hashmap->storage  = mmap(NULL, hashmap->capacity * sizeof(hashmap_entry_s), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  hashmap->elements = 0;
  if (hashmap->storage == NULL) {
    fprintf(stderr, "ERROR: hashmap_create(): mmap failed\n");
    exit(1);
  }

} // hashmap_create ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Double the capacity of a hash map.
 * \param hashmap The hashmap whose capacity will be doubled.
 */
void hashmap_expand (hashmap_s* hashmap) {

  // Allocate a new, double-sized mmap region.
  hashmap_s old_hashmap = *hashmap;
  hashmap->capacity *= 2;
  hashmap->storage = mmap(NULL, hashmap->capacity * sizeof(hashmap_entry_s), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (hashmap->storage == NULL) {
    fprintf(stderr, "ERROR: hashmap_expand(): mmap failed\n");
    exit(1);
  }

  // For each element in the old hash map, insert it in the new one.
  for (int i = 0; i < old_hashmap.capacity; ++i) {
    if (old_hashmap.storage[i].page_num != 0) {
      hashmap_insert(hashmap, old_hashmap.storage[i]);
    }
  }

  // Unmap the old hash map storage space.
  if (munmap(old_hashmap.storage, old_hashmap.capacity * sizeof(hashmap_entry_s)) == -1) {
    perror("ERROR: hashmap_expand(): munmap failed\n");
    exit(1);
  }
  
} // hashmap_expand ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief Print a representation of the complete contents of the hash map.
 * \param hashmap The hashmap whose contents to print.
 */ 
void hashmap_dump (hashmap_s* hashmap) {                                                

  printf("hashmap_dump:n");
  for (int i = 0; i < hashmap->capacity; ++i) {                            
    printf("\tmap[%4d] = { page_num = 0x%16lx", i, hashmap->storage[i].page_num); 
    if (hashmap->storage[i].page_num != 0) {                               
      printf(", original_perms = %d, unprotected = %B", hashmap->storage[i].original_perms, hashmap->storage[i].unprotected);
    }
    printf(" }\n");
  }
  
} // hashmap_dump ()
/* =============================================================================================================================== */
  


/* =============================================================================================================================== */
/**
 * \brief  Hash a given page number into a given hash map.
 * \param  hashmap  The hash map into which a location is hashed.
 * \param  page_num The page number to hash into a location.
 * \return The index into the given hash map's storage at which the page number hashes.
 */
int hashmap_hash (hashmap_s* hashmap, page_num_t page_num) {

  // SK: This is maybe an insufficiently distributed hash, given that page numbers do cluster.
  return page_num % hashmap->capacity;
  
} // hashmap_hash ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief  Find the location in the hash map where the given page number is or should be.
 * \param  hashmap  The hash map to search.
 * \param  page_num The page number for which to search.
 * \return A pointer to the entry that contains the page number, if found; a pointer to the empty entry that should contain the page
 *         number, if not found.
 */
hashmap_entry_s* hashmap_find (hashmap_s* hashmap, page_num_t page_num) {

  // Hash into the map.
  int              start_index = hashmap_hash(hashmap, page_num);        // The hashed index for this page number.
  hashmap_entry_s* start_ptr   = &(hashmap->storage[start_index]);       // Take a pointer into the array for that index.
  hashmap_entry_s* storage_end = hashmap->storage + hashmap->capacity;   // Hold onto the end of the array, for wrap-around during
                                                                         // linear probing.

  // Search the entries (linear probing).
  hashmap_entry_s* entry_ptr = start_ptr;
  while (entry_ptr->page_num != page_num && entry_ptr->page_num != 0) {  // If we haven't found it or hit an empty entry...
    if (++entry_ptr == storage_end) entry_ptr = hashmap->storage;        // Advance to the next, wrapping around if needed.
    assert(entry_ptr != start_ptr);                                      // We should never return to the start; expansion should
                                                                         // prevent it.
  }

  return entry_ptr;

} // hashmap_find ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief  Find a given page number's entry in the hash map.
 * \param  hashmap  The hashmap on which to perform the lookup.
 * \param  page_num The page number for which to search.
 * \return A pointer to the entry if the page number is found; `NULL` otherwise.
 */
hashmap_entry_s* hashmap_lookup (hashmap_s* hashmap, page_num_t page_num) {

  // If we found it, return this entry; otherwise return NULL to indicate failure.
  hashmap_entry_s* entry_ptr = hashmap_find(hashmap, page_num);
  return (entry_ptr->page_num == page_num) ? entry_ptr : NULL;
  
} // hashmap_lookup ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief  Insert a new page's information into the hash map.
 * \param  hashmap The hash map into which to insert the new entry.
 * \param  entry   The page number and associated information to insert.
 * \return `true` if the page was inserted; `false` if it had already been present.
 */
bool hashmap_insert (hashmap_s* hashmap, hashmap_entry_s entry) {

  // Find where the page should go.
  hashmap_entry_s* entry_ptr = hashmap_find(hashmap, entry.page_num);

  // If the slot is empty (the page isn't already in the table)...
  bool empty_slot = (entry_ptr->page_num == 0);
  if (empty_slot) {

    // ...insert it.
    *entry_ptr = entry;
    ++hashmap->elements;

    // If insertion pushes the load factor past its threshold, then expand.
    if ((double)hashmap->elements / (double)hashmap->capacity > LOAD_FACTOR_THRESHOLD) {
      hashmap_expand(hashmap);
    }
    
  }

  // Return whether the entry was inserted.
  return empty_slot;

} // hashmap_insert ()
/* =============================================================================================================================== */



/* =============================================================================================================================== */
/**
 * \brief  Remove a page's from the hash map.
 * \param  hashmap  The hash map from which to remove an entry.
 * \param  page_num The page number whose entry to remove.
 * \return whether the page was found and removed.
 * \see    https://en.wikipedia.org/w/index.php?title=Hash_table&oldid=95275577
 */
bool hashmap_remove (hashmap_s* hashmap, page_num_t page_num) {

  // Find where the page should be.
  hashmap_entry_s* entry_ptr = hashmap_find(hashmap, page_num);
  bool             full_slot = (entry_ptr->page_num == page_num);

  // If it's there...
  if (full_slot) {

    // Start (i and j) at the position that holds the given page.
    int i = entry_ptr - hashmap->storage;
    int j = i;

    while (true) {

      // Linearly probe to the next spot that could hold a page that maps to the same location as the one our page occupied.
      j = (j + 1) % hashmap->capacity;

      // If this is an empty slot, we're done.
      if (hashmap->storage[j].page_num == 0) break;

      // If the page that is in this position (j) hashes to a location that is outside of the range between i and j...
      int k = hashmap_hash(hashmap, hashmap->storage[j].page_num);
      if ( (j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j)) ) {

	// Move the j-th entry into the i-th spot (that is, slide it closer to where it hashes to).
	hashmap->storage[i] = hashmap->storage[j];

	// Bump i up to j, closing the interval between them.
	i = j;
      }
    }

    // The i-th position, after this loop, is left at the last spot whose contents were bumped down, closer to their hashed
    // position.  Mark this entry as empty now.
    hashmap->storage[i].page_num = 0;
    
  }

  return full_slot;
 
} // hashmap_remove ()
/* =============================================================================================================================== */