#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include<sys/mman.h>



int main()
{
  static char* buffer;
	int page_size;
	int num_allocated_pages;
	
	page_size = sysconf(_SC_PAGE_SIZE);
        
	num_allocated_pages = 20;

	buffer = malloc(num_allocated_pages*page_size);
	char* old = buffer;

	//buffer = memalign(page_size, num_allocated_pages * page_size);
        //mprotect(buffer, page_size, PROT_READ | PROT_WRITE);
        //mprotect(buffer + page_size, page_size, PROT_READ | PROT_WRITE);

	
	for (int i=0;i<4;i++) {
	  int* temp = (int*) (buffer);
	for (int g = 0; g < num_allocated_pages; g++){
		*temp = g;
		buffer = buffer + page_size;
		temp = (int*) buffer;
	}
	buffer = old;
	}
/*
	buffer = buffer - num_allocated_pages * page_size;

	for (int i = 2; i <  num_allocated_pages; i++) {
		mprotect(buffer + i * page_size, page_size, PROT_NONE);	
	}

	printf("Reprotect pages and repeating loop to test array\n");

	for (int j = num_allocated_pages; j < num_allocated_pages + 2; j++) {
	  printf("integer value: %d\n", j);
	  *temp = j;
	  buffer = buffer + page_size;
	  temp = (int*) buffer;
	}
*/
//	printf("Loop completed\n");

	return 0;
}
