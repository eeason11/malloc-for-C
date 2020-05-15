# malloc-for-C
Implemented a dynamic memory allocator in C capable of the standard malloc, free, realloc, and calloc operations.
All code I wrote is contained in the file mm.c. All other files developed by Caltech Computing Systems course TAs.

### Full Description:
This program is a dynamic memory manager that works under 16 byte alignment and heap sizes of 2^64 bytes and smaller. 
Free blocks in memory are stored in an explicit linked list data structure wherein each free block stores pointers to 
the next and previous blocks in the list. Allocated blocks are implicitly stored in memory (they are not tracked by a 
data structure) and are appended to the front of the free list upon being freed (Last In First Out implementation). All 
blocks have an identical 8 byte header and footer that each store the size of the respective block (including the header 
and footer space) and whether the block is allocated or not. The free list is not ordered or sorted in any particular 
manner. Free blocks are always coalesced with adjacent blocks.
