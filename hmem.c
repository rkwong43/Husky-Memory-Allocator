
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "hmem.h"
#include <string.h>


/*
 * Represents functions that create a free-list-managed Husky Memory allocator.
 * Functionality includes returning the length of the free list, stats on how much was malloc'd and freed,
 * malloc, and free.
 */

/*
 * Free list node, has a size and a pointer towards the next node.
 */
typedef struct free_list_node {
    // Size of the memory block the node represents
    size_t size;
    // Next node in the list
    struct free_list_node *next;
} free_list_node;

// Sets the size of a page to 4KB
const size_t PAGE_SIZE = 4096;
// Initializes the given statistics struct to values of 0
static hm_stats stats;
// Initializes the free list
static free_list_node *freelist;

// Iterates through the free list and returns the total number of nodes.
long free_list_length() {
    free_list_node *temp = freelist;
    long num_nodes = 0;
    for (; temp; temp = temp->next) {
        num_nodes++;
    }
    return num_nodes;
}

/*
 * Returns the memory address of the stats object keeping track of the state of the Husky Allocator.
 */
hm_stats* hgetstats() {
    stats.free_length = free_list_length();
    return &stats;
}

/*
 * Prints the current stats of the Husky Memory Allocator.
 * Prints out:
 * Number of pages mapped.
 * Number of pages unmapped.
 * Number of chunks allocated.
 * Number of chunks freed.
 * Number of nodes of the free list.
 */
void hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

/*
 * Given code that determines how many pages needed for larger allocations.
 * Takes in two size_t's, divides the first by the second, and takes the ceiling.
 */
static size_t div_up(size_t xx, size_t yy) {
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;
    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

/*
 * Adds a free list node to the free list.
 * Takes in a free list node.
 */
void add(free_list_node *node) {
    // If the free list is empty, then sets the given node to the free list.
    if (!freelist) {
        freelist = node;
        node->next = 0;
        return;
    }
    // Keeps track of the previous node
    free_list_node *saved = 0;
    for (free_list_node *temp = freelist; temp; saved = temp, temp = temp->next) {
        // At the end of the free list
        if (!temp->next) {
            // If the node is the largest in size
            if ((void *)node > (void *)temp) {
                temp->next = node;
                node->next = 0;
                return;
            } else {
                // If the node is the smallest in size
                freelist = node;
                node->next = temp;
                return;
            }
        } else {
            // Node is between two other free list nodes
            if ((void *)saved < (void *)node) {
                if ((void *)temp > (void *)node && saved) {
                    saved->next = node;
                    node->next = temp;
                    return;
                }
            }
        }
    }
}

/*
 * Allocates a memory chunk of the given size.
 * Takes in a size_t and returns a memory pointer to the data block.
 */
void* hmalloc(size_t size)
{
    stats.chunks_allocated += 1;
    // Accounts for the header of the size of block to free later
    size += sizeof(size_t);
    // The resulting memory address to return
    void *result = 0;
    // When the given size is less than a page
    if (size < PAGE_SIZE) {
        // Searches free list for block that can accommodate the data
        for (free_list_node *temp = freelist; temp; temp = temp->next) {
            // Free list node has block of sufficient size
            if (temp->size >= size) {
                result = temp;
                // Previous node is 0
                free_list_node *saved = 0;
                // Remove temp from the free list nodes
                for (free_list_node *node = freelist; node; saved = node, node = node->next) {
                    if (node == temp) {
                        // First node encountered
                        if (!saved) {
                            // Set the free list to be whatever the next node is
                            freelist = temp->next;
                            break;
                        } else {
                            // Changes the previous node's next to the current node's next,
                            // skipping over the node to be erased
                            saved->next = node->next;
                            break;
                        }
                    }
                }
                // If the block is bigger than the request,
                // and the leftover is big enough to store a free list cell, return the extra to the free list.
                if (temp->size - size > sizeof(free_list_node)) {
                    // Offset by the size of malloc'd data
                    free_list_node *newNode = (free_list_node *)((void *)temp + size);
                    // Change the size respectively
                    newNode->size = temp->size - size;
                    // Returns the node back to the free list
                    add(newNode);
                }
                // Adds a header of the size of block allocated
                free_list_node *ptr = (free_list_node *)result;
                // Set the size of return node to current size
                ptr->size = size;
                // Return the address offset by the header
                return result + sizeof(size_t);
            }
        }
        // If this is reached, there is no free list node block capable of storing the requested memory in the page
        // Creates a new page
        result = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        // Checks if the syscall fails or not
        assert(result != 0);
        // Updates stats because another page was allocated
        stats.pages_mapped += 1;
        // Adds a node if there is enough space
        if (PAGE_SIZE - size > sizeof(free_list_node))
        {
            free_list_node *newNode = (free_list_node *)((void *)result + size);
            newNode->size = PAGE_SIZE - size;
            add(newNode);
        }
        // Sets the header
        free_list_node *ptr = (free_list_node *)result;
        ptr->size = size;
        return result + sizeof(size_t);

    } else {
        // Case where the requested memory is larger than a page
        // Maps a new page
        result = mmap(NULL, div_up(size, PAGE_SIZE) * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        assert(result != 0);
        stats.pages_mapped += div_up(size, PAGE_SIZE);
        // Set the size of the result pointer and places it at the beginning of the page
        free_list_node *ptr = (free_list_node *)result;
        ptr->size = size;
        return result + sizeof(size_t);
    }
}

/*
 * Frees the memory block associated with the given memory address pointer.
 *
 * Takes in a memory address pointer of the data that needs to be freed.
 */
void hfree(void *item) {
    stats.chunks_freed += 1;
    // Creates a new free list node at the location of the block
    free_list_node *newNode = (free_list_node *)(item - sizeof(size_t));
    // Case where the block is less than a page:
    if (*(size_t *)(item - sizeof(size_t)) < PAGE_SIZE) {
        // Set the size by pointer arithmetic
        newNode->size = *(size_t *)(item - sizeof(size_t));
        // Add the new node to the free list
        add(newNode);
        // First free list node
        free_list_node *saved = 0;
        // Handles coalescing by checking if there is another node to merge with
        for (free_list_node *temp = freelist; temp; saved = temp, temp = temp->next) {
            // First node
            if (!saved) {
                // If the block is adjacent to another free list node
                if ((void *)temp + temp->size == (void *)temp->next) {
                    temp->size = temp->size + temp->next->size;
                    temp->next = temp->next->next;
                }
            } else {
                if ((void *)saved + saved->size == (void *)temp) {
                    saved->size = saved->size + temp->size;
                    saved->next = temp->next;
                }
            }
        }
    } else {
        // Case the block size larger than page size
        size_t pages = div_up(*(size_t *)(item - sizeof(size_t)), PAGE_SIZE);
        // Only difference here is we unmap the data and skip the freelist entirely
        munmap(item - sizeof(size_t), pages * PAGE_SIZE);
        stats.pages_unmapped += pages;
    }
}

/*
 * Reallocates the block of memory representing the given memory address to the given number of bytes.
 *
 * Takes in the previous memory address pointer and the size_t of bytes to expand to.
 * Returns the new pointer to the block of memory.
 */
void* hrealloc(void* prev, size_t bytes) {
    // Subtracts the header
    free_list_node* node = (free_list_node*) (prev - sizeof(size_t));
    // Previous size
    size_t prev_size = *((size_t*) node);
    void* result = hmalloc(bytes);
    // Copies the data at the previous pointer to the new given block
    memcpy(result, prev, prev_size);
    hfree(prev);
    return result;
}
