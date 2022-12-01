// =============================================================================
/*******************************************************************************
 * A module that offers a compressed caching mechanism for protected pages
 * coming from `manager.c`.  The mechanism will be reading in page numbers of
 * protected pages and putting them in a simple LRU queue.
 *
 * @author Luka Duranovic <luk.duranovic@gmail.com>
 *                        <lduranovic22@amherst.edu>
 * @date   Monday, 26 July, 2021
 ******************************************************************************/
// =============================================================================



// =============================================================================
// INCLUDES

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/mman.h> // for `mmap()` and `munmap()`
#include "compressed-caching.h"
// =============================================================================



// =============================================================================
// MACROS AND CONSTANTS

/* The initial capacity of the LRU queue. */
#define INITIAL_CAPACITY     1024
// =============================================================================



// =============================================================================
// DATA MEMBERS

/* The LRU queue that will be storing all the page numbers. */
static lru_queue *queue;
// =============================================================================



// =============================================================================
/**
 * Initializes the compressed caching module. Creates the LRU queue that will
 * keep track of protected pages.
 */
void cc_init ()
{

  write(1, "cc_init() is called.\n", 21);
  create_queue();

} // void comp_cache_init ()
// =============================================================================



// =============================================================================
/**
 * Creates a new LRU queue which will keep track of the page numbers of
 * protected pages.
 *
 * @return          A pointer to the newly created LRU queue.
 */
void create_queue ()
{

  queue->capacity = INITIAL_CAPACITY;
  // Now using `mmap` here...
  queue           = mmap(NULL, queue->capacity * sizeof(qnode),
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  queue->count    = 0;
  queue->head     = NULL;

  if (queue->capacity == NULL) {
    write(1, "mmap faield inside 'cc_init()'!\n", 32);
    exit(1);
  }

} // lru_queue *create_queue ()
// =============================================================================



// =============================================================================
/**
 * Double the capacity of the queue.
 *
 * @param queue The pointer to the queue whose space will be doubled.
 */
void expand_queue ()
{

  // Allocate a new, double-sized, `mmap()` region.
  lru_queue *old_queue = queue; 
  queue->capacity *= 2;
  queue->storage = mmap(NULL, queue->capacity * sizeof(qnode),
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (queue->storage == NULL) {
    write(1, "mmap failed inside 'expand_queue()'!\n", 37);
    exit(1);
  }

  // For each element in the old LRU queue, insert it in the new one.
  

}
// =============================================================================



// =============================================================================
/**
 * Attempt to find the given node in the LRU queue.
 *
 * @param page_number The page number to look for.
 * @return            The node if it is in the queue. `NULL` otherwise.
 */
q_node *find (intptr_t page_number)
{

  if (!is_init()) {
    char *message = "find() called and queue isn't initialized.\n";
    write(1, message, 43);
    exit(1);
  }

  q_node *p = queue->head;
  int counter = 0;

  while (p != NULL && p->page_num != page_number) {
    p = p->next;
    counter++;
  }

  if (p != NULL) {
    p->index = counter;
    return p;
  }

  return NULL;

} // int find (intptr_t)
// =============================================================================



// =============================================================================
/**
 * Adds a page number to the LRU queue.
 *
 * @param page_num The page number that needs to be added.
 */
void cc_add (intptr_t page_num)
{

  // DEBUG: Show me that `cc_add()` is getting called.
  char *debug = "'cc_add()' is called.\n";
  write(1, debug, 22);

  if (!is_init()) {
    char *message = "cc_add() is called and queue isn't initialized.\n";
    write (1, message, 47);
    exit(1);
  }

  q_node *node = find(page_num);

  if (node == NULL)
    add_new(page_num);
  else
    move_to_front(node);

} // void add (intptr_t)
// =============================================================================



// =============================================================================
/**
 * Adds a completely new page number to the queue.
 *
 * @param page_num The page number to add.
 */
void add_new (intptr_t page_num)
{

  if (queue->head == NULL) {
    queue->head = create_node(page_num, NULL, NULL);
  } else {
    queue->head = create_node(page_num, NULL, queue->head);
  }

} // void add_new (intptr_t)
// =============================================================================



// =============================================================================
/**
 * Moves a page number that is already in the queue to the front of the queue.
 *
 * @param node The `q_node` that holds the page number in question.
 */
void move_to_front (q_node *node)
{

  if (node == NULL) {
    exit(1);
  }

  disconnect_node(node);
  queue->head = connect_node(node, NULL, queue->head);

} // void move_to_front (q_node *)
// =============================================================================



// =============================================================================
/**
 * Removes a page number from the compressed cache.
 *
 * @param page_num The page number that is to be removed.
 */
void cc_remove (intptr_t page_num)
{

  // DEBUG: Tell me that a page is getting removed.
  char *message = "Calling 'cc_remove()'\n";
  write(1, message, 22);

  q_node *node = find(page_num); 

  if (node == NULL) {
    exit(1);
  }

  disconnect_node(node);

} // void cc_remove (intptr_t)
// =============================================================================



// =============================================================================
/**
 * Checks whether the queue has been initialized.
 *
 * @return `true` if the queue has been initialized. `false` otherwise.
 */
bool is_init ()
{

  return queue != NULL;

} // bool is_init ()
// =============================================================================



// =============================================================================
/**
 * Checks whether the LRU queue is empty or not. It is assumed that this method
 * will be called on an initialized queue.
 *
 * @return `true` if the queue is empty. `false` otherwise.
 */
bool is_empty ()
{

  if (!is_init()) {
    exit(1);
  }

  return queue->head == NULL;

} // bool is_empty ()
// =============================================================================



// =============================================================================
/**
 * Prints the contents of the queue.
 */
void cc_print_queue ()
{

  if (!is_init()) {
    exit(1);
  }

  q_node *p = queue->head;
  
  while (p != NULL) {
    if (p->next != NULL)
      printf("%p -> ", p->page_num);
    else
      printf("%p", p->page_num);
    p = p->next;
  }
  printf("\n\n");

} // void cc_print_queue ()
// =============================================================================



// =============================================================================
/**
 * Creates a new LRU queue node, which stores the given page number.
 *
 * @param page_num The page number that is to be stored.
 * @param prev     A pointer to the previous node.
 * @param next     A pointer to the next node.
 * @return         A pointer to the newly created node.
 */
q_node *create_node (intptr_t page_num, q_node *prev, q_node *next)
{

  q_node *node = (q_node *)__libc_malloc(sizeof(q_node));
  node->page_num = page_num;
  node->prev = prev;
  node->next = next;

  // If necessary, adjusts the pointers for the other two nodes.
  if (prev != NULL)
    prev->next = node;
  if (next != NULL)
    next->prev = node;

  return node;

} // q_node *create_node (intptr_t, q_node *, q_node *)
// =============================================================================



// =============================================================================
/**
 * Connect the given node to the other two.
 *
 * @param node The node that is to be added.
 * @param prev A pointer to the node that needs to be come before this one.
 * @param next A pointer to the node that needs to come after this one.
 */
q_node *connect_node (q_node *node, q_node *prev, q_node *next)
{

  if (node == NULL) {
    printf("ERROR: Trying to connect a NULL node.\n");
    exit(1);
  }

  node->prev = prev;
  node->next = next;

  if (prev != NULL)
    prev->next = node;
  if (next != NULL)
    next->prev = node;

  return node;

} // void connect_node (q_node *, q_node *, q_node *)
// =============================================================================



// =============================================================================
/**
 * Removes a node from the queue in constant time, by disconnecting the
 * pointers.
 *
 * @param node A pointer to the node that is to be removed from the queue.
 */
q_node *disconnect_node (q_node *node)
{

  // Keep track of the pointers that come with the node.
  q_node *prev = node->prev;
  q_node *next = node->next;

  // Remove the node from the list.
  if (prev != NULL)
    prev->next = next;
  if (next != NULL)
    next->prev = prev;

  // Adjust the pointers of this node.
  node->prev = NULL;
  node->next = NULL;

  return node;

} // void disconnect_node (q_node *)
// =============================================================================



// =============================================================================
/**
 * Run some simple tests.
 */
void main (int argc, char **argv)
{

  // Hopefully initialize the queue.
  cc_init(); 

  // Sanity check: Make sure that the queue is empty now...
  printf("%d\n", is_empty());

  // Let's try printing the queue??
  cc_print_queue();

  // Add two page numbers to the queue.
  intptr_t pn1 = 0xab1;
  intptr_t pn2 = 0xab2;
  intptr_t pn3 = 0xab3;
  intptr_t pn4 = 0xab4;
  intptr_t pn5 = 0xab5;
  intptr_t pn6 = 0xab6;
  intptr_t pn7 = 0xab7;
  cc_add(pn1);
  cc_add(pn2);
  cc_add(pn3);
  cc_add(pn4);
  cc_add(pn5);
  cc_add(pn6);
  cc_add(pn7);

  // Show me what the queue is like now...
  cc_print_queue();

  // Add some already-seen page numbers now.
  cc_add(pn1);
  cc_add(pn3);
  cc_add(pn5);
  cc_add(pn7);

  // Again show me what is happening on the screen.
  cc_print_queue();

  // Remove some things from the queue.
  cc_remove(pn1);
  // Show me what the queue is like now.
  cc_print_queue();

  // Add some more new page numbers.
  intptr_t pn8 = 0xab8;
  intptr_t pn9 = 0xab9;
  cc_add(pn8);
  cc_add(pn9);

  // Show me what the situation is now...
  cc_print_queue();

} // void main (int argc, char **argv);
// =============================================================================
