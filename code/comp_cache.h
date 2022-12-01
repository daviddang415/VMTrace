// =============================================================================
/*******************************************************************************
 * Header file for the compressed caching functionality of the VMTRACE
 * directory.
 *
 * @author Luka Duranovic <luk.duranovic@gmail.com>
 *                        <lduranovic22@amherst.edu>
 * @date   Monday, 26 July, 2021
 ******************************************************************************/
// =============================================================================



// =============================================================================
#ifndef COMPRESSED_CACHING_H
#define COMPRESSED_CACHING_H
// =============================================================================



// =============================================================================
// TYPES AND STRUCTURES

/**
 * A single LRU queue node. It will keep track of a page number and of two
 * pointers (since we want a doubly-linked list).
 */
typedef struct q_node {

  // Pointers to the previous and next nodes in the queue.
  struct q_node *next, *prev;

  // The actual page number that is stored in this node.
  intptr_t page_num;

  // The position of the node in the LRU queue. */
  int index;

} q_node; // struct q_node



/**
 * The LRU queue struct. It will keep track of how many page numbers are
 * currently in the queue, and it will have a predefined maximal capacity.
 */
typedef struct lru_queue {

  // The number of page numbers in the queue.
  unsigned long count;

  // The head of the queue.
  q_node *head;

} lru_queue; // struct lru_queue
// =============================================================================



// =============================================================================
// FUNCTION DECLARATIONS

/* Initialize everything needed for compressed caching to work. */
void cc_init ();

/* Initializes the LRU queue. */
void create_queue ();

/* Attempts to find the given page number in the queue. */
q_node *find (intptr_t);

/* Adds a page number to the queue. */
void cc_add (intptr_t);

/* Adds a completely new page number to the queue. */
void add_new (intptr_t);

/* Moves a page number that is in the queue to the front of the queue.  */
void move_to_front(q_node *);

/* Remove a given page number from the queue. */
void cc_remove (intptr_t);

/* Check whether the queue has been initialized. */
bool is_init ();

/* Checks whether the queue is empty. */
bool is_empty ();

/* Prints the contents of the queue at the present time. */
void cc_print_queue ();

/* Initializes a new node given a page number that is to be stored in it. */
q_node *create_node (intptr_t, q_node *, q_node *);

/* Adds a node into the queue given the pointers for the `next` and `prev`
 * nodes. */
q_node *connect_node (q_node *, q_node *, q_node *);

/* Removes the node given a pointer to it. */
q_node *disconnect_node (q_node *);
// =============================================================================



// =============================================================================
#endif // compressed-caching.h
// =============================================================================
