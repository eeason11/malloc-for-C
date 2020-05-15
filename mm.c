/*
 * This program is a dynamic memory manager that works under 16 byte alignment
 * and heap sizes of 2^64 bytes and smaller. Free blocks in memory are stored
 * in an explicit linked list data structure wherein each free block stores
 * pointers to the next and previous blocks in the list. Allocated blocks are implicitly
 * stored in memory (they are not tracked by a data structure) and are appended
 * to the front of the free list upon being freed (Last In First Out imple-
 * mentation). All blocks have an identical 8 byte header and footer that each store
 * the size of the respective block (including the header and footer space) and
 * whether the block is allocated or not. The free list is not ordered or sorted
 * in any particular manner. Free blocks are always coalesced with adjacent blocks.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"
/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif
/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* W_SIZE is the size of a single header or footer (8 bytes)
 *  D_SIZE is the combined size of a header and footer (16 bytes) */
static const size_t W_SIZE = sizeof(size_t);
static const size_t D_SIZE = 2 * sizeof(size_t);

typedef struct {
    size_t header;
    /* We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allows us to obtain a
     * pointer to the start of the payload. This struct is used to
     * represent blocks in memory, their size, and allocation boolean
     * footer is stored at the end of the payload */
    uint8_t payload[];
} block_t;

typedef struct {
    // Used for setting and getting next and previous pointers of freed blocks
    block_t *next;
    block_t *prev;
} freed_payload;
/* mm_head_first is used as a heap prologue, mm_heap_last an epilogue, mm_free_head
*  the head node of the free list of freed blocks -- Total global var cap: 24 bytes */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;
static block_t *mm_free_head = NULL;

static inline void *incr_pointer(size_t bytes, void *pointer) {
    return (char*)pointer + bytes;
}

static inline void *decr_pointer(size_t bytes, void *pointer) {
    return (char*)pointer - bytes;
}
// Assumed that pointer 1 is greater than pointer 2
static inline size_t pointer_dif(void* pointer1, void* pointer2) {
    return (char*)pointer1 - (char*)pointer2;
}
// Rounds up the inputted size to the next multiple of n
static inline size_t round_up(size_t size, size_t n) {
    return (size + n - 1) / n * n;
}

static inline size_t get_size_from_val(size_t val) {
    return val & ~0xF;
}

static inline size_t get_size(block_t *block) {
    return get_size_from_val(block->header);
}

static inline void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
}
// Assumes header of block has already been set
static inline void set_footer(block_t *block) {
    size_t size = get_size(block);
    size_t* footer = incr_pointer(size - W_SIZE, block);
    *footer = block->header;
}

static inline size_t *get_footer_from_header(size_t *header) {
    size_t header_val = *header;
    return (size_t*)incr_pointer(get_size_from_val(header_val) - W_SIZE, header);
}

static inline bool is_allocated_from_val(size_t val) {
    return val & 0x1;
}

static inline bool is_allocated(block_t *block) {
    return is_allocated_from_val(block->header);
}

static inline void init_heap_first() {
    mm_heap_first = (block_t*) mem_heap_lo();
    mm_heap_first = (block_t*)incr_pointer(W_SIZE, mm_heap_first);
}

static inline void init_heap_last() {
    void *last_heap_index = mem_heap_hi();
    mm_heap_last = (block_t*)decr_pointer(D_SIZE - 1, last_heap_index);
}
// Assumes both blocks are freed
static inline void set_next(block_t *block, block_t *next) {
    freed_payload *block_links = (freed_payload*)(block->payload);
    block_links->next = next;
}
// Assumes both blocks are freed
static inline void set_prev(block_t *block, block_t *prev) {
    freed_payload *block_links = (freed_payload*)(block->payload);
    block_links->prev = prev;
}
// Assumes block is freed
static inline block_t *get_next(block_t *block) {
    freed_payload *block_links = (freed_payload*)(block->payload);
    return block_links->next;
}
// Assumes block is freed
static inline block_t *get_prev(block_t *block) {
    freed_payload *block_links = (freed_payload*)(block->payload);
    return block_links->prev;
}
// Appends block to front of linked list of freed blocks
static void block_append(block_t *block) {
    set_next(block, mm_free_head);
    if (mm_free_head != NULL) {
        set_prev(mm_free_head, block);
    }
    set_prev(block, NULL);
    mm_free_head = block;
}
// Removes block from linked list of freed blocks; assumes the block is freed
static void block_remove(block_t *block) {
    if (get_next(mm_free_head) == NULL) {
        assert(block == mm_free_head);
        mm_free_head = NULL;
    }
    else {
        block_t *next = get_next(block);
        block_t *prev = get_prev(block);
        if (block == mm_free_head) {
           set_prev(next, NULL);
           mm_free_head = next;
        }
        else {
            set_next(prev, next);
            if (next) {
                set_prev(next, prev);
            }
        }
    }
}
// Called when a new trace starts - pads heap and (re)initializes globals
int mm_init(void) {
    mm_free_head = NULL;
    if (!(mem_sbrk((long)(2 * D_SIZE + W_SIZE)))) {
        return -1;
    }
    init_heap_first();
    init_heap_last();
    return 0;
}
// Expands the heap and returns a new block of the inputted size
static block_t *create_space(size_t size) {
    mem_sbrk((long)(size));
    block_t *block = mm_heap_last;
    mm_heap_last = (block_t*)incr_pointer(size, mm_heap_last);
    set_header(block, size, true);
    set_footer(block);
    return block;
}
// Splits the inputted block into a block to be allocated and a new free block
static block_t *split(block_t *block, size_t size) {
    block_remove(block);
    size_t old_size = get_size(block);
    set_header(block, size, true);
    set_footer(block);
    block_t *split_free = (block_t*)(incr_pointer(size, block));
    set_header(split_free, old_size - size, false);
    set_footer(split_free);
    block_append(split_free);
    return block;
}
// Traverses the free list to find a block valid for allocation
static block_t *find_fit(size_t size) {
    block_t *curr = mm_free_head;
    while (curr != NULL) {
        size_t curr_size = get_size(curr);
        if (curr_size >= 2 * D_SIZE + size) {
                return split(curr, size);
        }
        else if (curr_size >= size) {
            block_remove(curr);
            set_header(curr, get_size(curr), true);
            set_footer(curr);
            return curr;
        }
        curr = get_next(curr);
    }
    return NULL;
}
// Returns 16-byte aligned pointer to an allocated space in memory of the inputted size
void *malloc(size_t size) {
    if (!mm_heap_first) {
        mm_init();
    }
    if (size == 0) {
        return NULL;
    }
    size_t adj_size = D_SIZE + round_up(size, D_SIZE);
    block_t *block = find_fit(adj_size);
    if (block == NULL) {
        block = create_space(adj_size);
    }
    return incr_pointer(W_SIZE, block);
}

static block_t *coalesce_left(block_t *block) {
    size_t *left_footer_pt = decr_pointer(W_SIZE, block);
    if ((void*)left_footer_pt != incr_pointer(W_SIZE, mm_heap_first)) {
        size_t left_footer = *left_footer_pt;
        size_t jump_dist = get_size_from_val(left_footer);
        block_t *left_block = decr_pointer(jump_dist, block);
        if (!(is_allocated(left_block))) {
            block_remove(block);
            block_remove(left_block);
            size_t new_size = get_size(block) + get_size(left_block);
            set_header(left_block, new_size, false);
            set_footer(left_block);
            block_append(left_block);
            block = left_block;
        }
    }
    return block;
} 
// Combines the inputted free block with potential adjacent free blocks
static void coalesce(block_t *block) {
    block = coalesce_left(block);
    size_t block_size = get_size(block);
    block_t *right_block = (block_t*)incr_pointer(block_size, block);
    if (right_block != mm_heap_last && !(is_allocated(right_block))) {
        coalesce_left(right_block);
    }
}

void free(void *ptr) {
    if (!mm_heap_first) {
        mm_init();
    }
    if (ptr == NULL) {
        return;
    }
    block_t *to_free = (block_t*)decr_pointer(W_SIZE, ptr);
    set_header(to_free, get_size(to_free), false);
    set_footer(to_free);
    block_append(to_free);
    coalesce(to_free);
}
/* Changes the size of the block by mallocing a new block, copying its data, 
 * and freeing the old block */
void *realloc(void *old_ptr, size_t size) {
    if (size == 0) {
        free(old_ptr);
        return NULL;
    }
    if (!old_ptr) {
        return malloc(size);
    }
    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }
    block_t *block = (block_t*)decr_pointer(W_SIZE, old_ptr);
    size_t old_size = get_size(block) - D_SIZE;
    if (size < old_size) {
        old_size = size;
    }
    memcpy(new_ptr, old_ptr, old_size);
    free(old_ptr);
    return new_ptr;
}
// Allocates a block and sets it to zero
void *calloc(size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *new_ptr = malloc(bytes);
    /* If malloc() fails, skip zeroing out the memory. */
    if (new_ptr) {
        memset(new_ptr, 0, bytes);
    }
    return new_ptr;
}
// Prints runtime errors in the heap's implementation and the line at which they occur
void mm_checkheap(int verbose) {
    void *heap_lo = mem_heap_lo();
    void *heap_hi = mem_heap_hi();
    if (mm_heap_first == NULL) {
        printf("Error: prologue is null. Line %d", verbose);
    }
    else if ((void*)mm_heap_first != heap_lo + W_SIZE) {
        printf("Error: prologue has been moved. Line %d", verbose);
    }
    if (mm_heap_last == NULL) {
        printf("Error: epilogue is null. Line %d", verbose);
    }
    else if ((void*)mm_heap_last != decr_pointer(D_SIZE - 1 ,heap_hi)) {
        printf("Error: epilogue has been moved. Line %d", verbose);
    }
    block_t *curr = incr_pointer(D_SIZE, mm_heap_first);
    block_t *prev = NULL;
    int64_t num_free_check = 0;
    while (curr != mm_heap_last) {
        if (prev != NULL) {
            if (!is_allocated(curr)) {
                num_free_check++;
                if (!is_allocated(prev)) {
                    printf("Error: failure to coalesce. Line %d", verbose);
                }
            }
        }
        size_t size = get_size(curr);
        if (size % D_SIZE != 0) {
            printf("Error: block is not aligned. Line %d", verbose);
        }
        if ((void*)curr < heap_lo || (void*)curr > heap_hi) {
            printf("Error: block is outside of heap boundary. Line %d", verbose);
        }
        size_t *footer = get_footer_from_header((size_t*)curr);
        if (curr->header != *footer) {
            printf("Error: a footer is not equivalent to its header. Line %d", verbose);
        }
        if (size < 2 * D_SIZE) {
            printf("Error: size of block is below minimum size. Line %d", verbose);
        }
        if (pointer_dif(curr, mm_heap_first) % D_SIZE != 0) {
            printf("Error: block address not aligned. Line %d", verbose);
        }
        prev = curr;
        curr = (block_t*)incr_pointer(size, curr);
    }
    if (mm_free_head != NULL) {
        curr = mm_free_head;
        prev = NULL;
        while (curr != NULL) {
            if (get_prev(curr) != prev) {
                printf("Error: prev of curr not matched with next of prev. Line %d", verbose);
            }
            if ((void*)curr < heap_lo || (void*)curr > heap_hi) {
                printf("Error: free block outside of heap boundaries. Line %d", verbose);
            }
            num_free_check--;
            prev = curr;
            curr = get_next(curr);
        }
    }
    if (num_free_check < 0) {
        printf("Error: free list storing more blocks than are freed. Line %d", verbose);
    }
    else if (num_free_check > 0) {
        printf("Error: not all free blocks are being stored in list. Line %d", verbose);
    }
}