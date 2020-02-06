# Husky Allocator


_Done for CS3650, Computer Systems._

Represents a thread-safe memory allocator that is optimally more efficient than
system allocation in some cases described below.

Includes functionality such as:
* Retrieving statistics of how many pages and chunks were freed or allocated.
* Allocating memory same as malloc() calls.
* Reallocating memory same as realloc() calls.
* Freeing memory same as free() calls.

The functions within par_malloc.c use arenas for memory management and is thread-safe
by giving an arena to each thread.

The functions within hmem.c use a free-list to manage memory. Not necessarily thread-safe.

The functions used to test run time are in the ivec_main.c and list_main.c
files. Both test the Collatz Conjecture, and were provided as starter code.
Both hold up to 1,000,000 items in their respective data structure used.

Results from testing:

Job | Measured Time
-------|---------------
list-hmem | 10+ minutes
ivec-hmem | 10+ minutes
list-malloc() | 27.17s
ivec-malloc() | 7.51s
list-par_malloc | 35.71s
ivec-par_malloc | 5.38s

Specifications of local machine:
* CPU:
    * Model name: Intel(R) Core(TM) i7-8850H CPU @ 2.60GHz
    * Core(s) per socket:  6
    * OS: Ubuntu 18.04.3 LTS
    * RAM: 16GB

Without as many system calls or locks as malloc() in hmem, the bucket
and arena system proved to be much more efficient. However, calls to
memcpy() may have slowed the arena-based Husky Allocator down, as well
as handling data traversal and defragmentation.


