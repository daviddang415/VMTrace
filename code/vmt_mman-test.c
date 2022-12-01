#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "vmt_mman.h"



typedef struct link {
  int          value;
  struct link* next;
} link_s;



typedef struct list {
  link_s* head;
  size_t  length;
} list_s;



list_s* create () {

  list_s* l = vmt_malloc(sizeof(list_s));
  assert(l != NULL);

  l->head = vmt_malloc(sizeof(link_s));
  assert(l->head != NULL);
  l->head->value = -1; // Sentinel.
  l->head->next  = NULL;

  return l;
  
} // create ()



void insert (list_s* list, int value) {

  link_s* sentinel = list->head;
  link_s* link     = vmt_malloc(sizeof(link_s));
  assert(link != NULL);
  link->value    = value;
  link->next     = sentinel->next;
  sentinel->next = link;
  list->length  += 1;

  printf("insert:\tvalue = %d\tlink = %p\n", value, link);
  
} // insert ()



int delete (list_s* list, int index) {

  assert(0 <= index && index < list->length);

  // Walk to the link previous to the desired index.
  link_s* current  = list->head;
  int     position = -1;
  while (position < index - 1) {
    current   = current->next;
    position += 1;
  }

  link_s* target = current->next;
  int     value  = target->value;
  current->next  = target->next;
  printf("delete:\tvalue = %d\tlink = %p\n", value, target);
  vmt_free(target);
  list->length -= 1;

  return value;
  
} // delete ()



void usage_and_exit (char* invocation)
{

  fprintf(stderr, "USAGE: %s <# ops> [ <seed> ]\n", invocation);
  exit(1);
  
} // usage_and_exit ()



int main (int argc, char** argv)
{

  // Parse command line args.  Set the random seed, if requested.
  if (! (2 <= argc && argc <= 3) ) usage_and_exit(argv[0]);
  int ops = atoi(argv[1]);
  if (ops <= 0) usage_and_exit(argv[0]);
  if (argc == 3) {
    int seed = atoi(argv[2]);
    if (seed == 0) usage_and_exit(argv[0]);
    srandom(seed);
  }

  list_s* l   = create();
  long    sum = 0;
  for (int op = 0; op < ops; ++op) {

    // Pick a function, insert or delete.
    int value;
    int function = random() % 100;
    if (function < 75) {

      // Insert a randomly chosen value.
      value = random();
      insert(l, random());
      
    } else {

      // Delete a randomly chosen entry.  If the list is empty, do
      // nothing.
      if (l->length > 0) {
	value = delete(l, random() % l->length);
      }

    }
    
    sum += value;

  }

  printf("Final sum:\t%ld\n", sum);

  char* big_buffer = vmt_malloc(5000);
  assert(big_buffer != NULL);
  printf("big_buffer = %p\n", big_buffer);
  vmt_free(big_buffer);
  
  return 0;
   
} // main ()
