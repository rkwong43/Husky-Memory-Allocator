#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <assert.h>

#include "xmalloc.h"


// Header node for start of a page
typedef struct header {
    // Only contains a size_t field of the number of free chunks
    size_t size;
} header;


// Based off of jemalloc: Uses 2MB (rather than 4KB) chunks
// Page size
static const size_t PAGE_SIZE = 2000000;
// Minimum sized bucket allowed
static const int MIN_SIZE = 16;
// log2(MIN_SIZE)
static int INDEX_DECREMENT = 4;
// Number of buckets
static int BUCKETS = 19;
// Size is BUCKETS
// Each thread gets its own arena
__thread header* bucket[19];
/* bucket[index]-- max byte size -> header 8 bytes -> # chunks
 * 
 * bucket[0]--16 bytes -> header 8 bytes -> floor((PAGE_SIZE - sizeof(header)) / (sizeof(size_t) + 2^(index + 4))) free chunks at start
 * bucket[1]--32 bytes -> header 8 bytes ... 
 * bucket[2]--64 bytes -> header 8 bytes ...
 * bucket[3]--128 bytes -> header 8 bytes ...
 * bucket[4]--256 bytes -> header 8 bytes ...
 * bucket[5]--512 bytes -> header 8 bytes ...
 * bucket[6]--1024 bytes -> header 8 bytes ...
 * bucket[7]--2048 bytes -> header 8 bytes ... 
 * Index calculated through ceil(log2(size))
 * If index >= size_of_array, just mmap it.
 *
 * Header->size stores number of allocated chunks.
 * Each bucket has a page allocated to it, at the beginning of the page is a size_t header of free chunks.
 * Each chunk allocated is preceded by a size_t to give the size.
 * 
 * Malloc gives memory pointed to by (space_taken + sizeof(size_t)) (aka at very end of last allocated memory).
 * 
 * Free just sets the size_t node to 0.
 * 
 * Coalesce by iterating through the page, getting rid of chunks where size_t is 0 by
 * checking adjacent chunks and copying them next to it to move the chunks down the page.
 * Keep track of # of chunks removed and subtract that from the header->size.
 *
 */

/*
 * Maps large allocations of memory.
 * Takes in the number of bytes requested.
 */
void* large_allocation(size_t bytes) {
    // Mmap large allocations
    void *result = 0
    if (bytes < PAGE_SIZE) {
        result = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    } else {
        pages_needed = ceil(PAGE_SIZE / bytes)
        result = mmap(NULL, pages_needed * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    }
    assert(result != 0);
    // Puts in a header
    header *ptr = (header *) result;
    ptr->size = bytes;
    return result + sizeof(size_t);
}

/*
 * Coalesces chunks of memory with size = 0
 * Takes in the index of the bucket to coalesce and the size of the bucket contents.
 */
void coalesce(int index, size_t size) {
    header* head =  bucket[index];
    void* start = (char*) head + sizeof(header);
    long removed = 0;
    long bytes = sizeof(header);
    while (bytes <= PAGE_SIZE) {
        size_t chunk = *((size_t*) start);
        // Hits an empty chunk
        if (chunk == 0) {
            // Copies everything after the empty chunk
            int more = 0;
            // Temporary address
            void* tad = start + sizeof(size_t) + size;
            // Checks if there are chunks allocated after the empty one
            for (int k = bytes + (size + sizeof(size_t)); bytes < PAGE_SIZE; bytes += (size + sizeof(size_t))) {
                size_t temp = *((size_t*) tad);
                if (temp == size) {
                    more = 1;
                    break;
                }
                tad += (size + sizeof(size_t));
            }
            if (more) {
                // More chunks to remove
                memcpy(start, ((char *) start) + (size + sizeof(size_t)), PAGE_SIZE - bytes);
                removed++;
                continue;
            } else {
                // Breaks the loop because there are no more allocated chunks in the free page
                break;
            }
            // Continue because we don't want to go past the next chunk that was just copied over
        }
        // Increments the pointer and the # of bytes traversed
        start += (size + sizeof(size_t));
        bytes += (size + sizeof(size_t));
    }
    head->size -= removed;
}

/*
 * Recursively checks the buckets for one of sufficient space.
 * Takes in the index of the bucket and the number of bytes required.
 */
void* check_buckets(int index, size_t bytes) {
    header* head = bucket[index];
    if (!head) {
        // Creates head if it doesn't exist
        head = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        assert(head != 0);
        head->size = 0;
        bucket[index] = head;
    }
    // Size of each chunk in the bucket
    long size = pow(2, index + INDEX_DECREMENT);
    // Total space taken up
    int allocated = head->size * (size + sizeof(size_t));
    // No more space
    if (allocated >= (PAGE_SIZE - sizeof(header))) {
        // Coalesces and checks if there's more space
        coalesce(index, size);
        if ((head->size * (size + sizeof(size_t))) >= (PAGE_SIZE - sizeof(header))) {
            // If no space:
            // Puts it in the next bucket
            if (index + 1 >= BUCKETS) {
                // Exceeds the maximum bucket size, just mmap
                return large_allocation(bytes);
            } else {
                // Checks the next bucket if it has space
                return check_buckets(index + 1, bytes);
            }
        }
    }
    // Gives the chunk after the last allocated chunk
    char* addy = ((char*) head) + sizeof(header) + (head->size * (size + sizeof(size_t)));
    // Puts in the size
    memcpy(addy, &size, sizeof(size_t));
    head->size++;
    return addy + sizeof(size_t);
}

/*
 * Allocates the requested amount of memory and returns a pointer to it.
 * Takes in the number of bytes requested.
 * Returns the memory address to the allocated space.
 */
void * xmalloc(size_t bytes) {
    // If the requested bytes are less than the minimum size allowed for a bucket
    bytes = bytes < MIN_SIZE ? MIN_SIZE : bytes;
    void *result;
    // Index in the buckets to give memory from
    int index = ceil(log2((double) bytes)) - INDEX_DECREMENT;
    // If the size requested doesn't fit in the bucket
    if (index >= BUCKETS) {
        return large_allocation(bytes);
    }
    size_t size = pow(2, index + INDEX_DECREMENT);
    header* head = bucket[index];
    // If the head/page doesn't exist, creates it
    if (!head) {
        // Creates the head
        head = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        assert(head != 0);
        head->size = 1;
        bucket[index] = head;
        memcpy((char*) head + sizeof(header), &size, sizeof(size_t));
        result = (void*) head;
        return (void*) ((char*) result) + sizeof(header) + sizeof(size_t);
    } else {
        // Head exists, so we check for buckets
        return check_buckets(index, bytes);
    }
}

/*
 * Frees the given chunk of memory.
 * Takes in the pointer of the chunk to free.
 */
void xfree(void *ptr)
{
    // Retrieves the size of the chunk
    size_t ptrsize = *(size_t *)(ptr - sizeof(size_t));
    if (ptrsize > PAGE_SIZE) {
        // If it's greater than a page, unmaps the page.
        int rv = munmap(ptr - sizeof(size_t), ptrsize);
        assert(rv != -1);
    } else {
        // Just sets the size of the chunk's size_t field to 0.
        // Coalescing will take care of de-fragmentation.
        size_t size = 0;
        memcpy((char*) ptr - sizeof(size_t), &size, sizeof(size_t));
    }
}

/*
 * Reallocates memory by copying data over from the previous space and returning a new chunk of the requested
 * size.
 * Takes in the previous chunk of data and the size to expand to in bytes.
 * Returns the memory address of the new chunk of memory.
 */
void * xrealloc(void *prev, size_t bytes) {
    free_list_node *node = (free_list_node *)(prev - sizeof(size_t));
    size_t prev_size = *((size_t *)node);
    void *result = xmalloc(bytes);
    memcpy(result, prev, prev_size);
    xfree(prev);
    return result;
}
