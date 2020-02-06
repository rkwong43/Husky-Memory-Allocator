#ifndef XMALLOC_H
#define XMALLOC_H

#include <stddef.h>

void* xmalloc(size_t bytes);
void  xfree(void* ptr);
void* xrealloc(void* prev, size_t bytes);


/*
 * Represents a free list node with a size and reference to the next node.
 */
typedef struct free_list_node {
    // Size of the memory block the node indicates
    size_t size;
    // Next node in the free list
    struct free_list_node *next;
} free_list_node;

#endif
