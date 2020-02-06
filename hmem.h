#ifndef HMALLOC_H
#define HMALLOC_H

// Husky Malloc Interface

/*
 * Starter code for class CS3650 at Northeastern. Describes the parameters and functions required for the
 * free-list based Husky Memory Allocator.
 *
 * NOT THREAD-SAFE.
 */
typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;

hm_stats* hgetstats();
void hprintstats();
void* hmalloc(size_t size);
void hfree(void* item);
void* hrealloc(void* prev, size_t bytes);



#endif
