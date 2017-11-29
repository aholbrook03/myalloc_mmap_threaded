/* File: myalloc.c
 * By: Andrew Holbrook
 *
 *	All logic for myalloc is located here.
 */

 // Standard/system headers
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/mman.h>

// Custom headers
#include "myalloc.h"

// Keep first element in list of free and used blocks.
static char *_head_free = NULL;
static char *_head_used = NULL;

// Global thread table
static ThreadTable *_thread_tbl = NULL;

// Global mutex, used for threads sharing memory.
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;

/*	is_aligned(p, offset) - Check if p is 8-byte aligned. Store offset to lowest
 *	aligned location.
 *
 *	Returns 0 for memory that is not aligned; not 0 for aligned.
 */
static inline int is_aligned(const void *p, size_t *offset)
{
	*offset = (size_t)((size_t *)p) & 7;
	return !(*offset);
}

/*	add_block(block, list) - add block to list.
 *
 */
static void add_block(char *block, char **list)
{
	Header *h_ptr;

	// List is empty, block will be first element.
	if (!*list) {
		*list = block;
		return;
	}

	// Place block at end of list.
	h_ptr = (Header *)*list;
	while (h_ptr->next)
		h_ptr = (Header *)h_ptr->next;

	h_ptr->next = block;
}

/*	remove_block(block, list) - Remove block from list.
 *
 *	Returns 0 for success; !0 for error.
 */
static int remove_block(char *block, char **list)
{
	Header *h_ptr;

	// Don't continue if list or block are NULL.
	if (!*list || !block)
		return 1;

	h_ptr = (Header *)*list;

	// Removing head of list.
	if (*list == block) {
		if (!h_ptr->next)
			*list = NULL;
		else
			*list = h_ptr->next;

		return 0;
	}

	// Find and remove block from list.
	while (h_ptr->next != block) {

		// Check if end of list. Error if so (could not find block).
		if (!h_ptr->next)
			return 1;

		h_ptr = (Header *)h_ptr->next;
	}

	h_ptr->next = ((Header *)h_ptr->next)->next;

	return 0;
}

/*	split(block, bytes) - Split a block of memory in two.
 *
 *	Returns first block in split.
 */
static void * split(char *block, size_t bytes, char *free)
{
	char *block2;
	char *block_end;
	size_t block_size;
	size_t block2_size;
	Header *h_ptr;
	Header b2header;
	size_t offset;

	h_ptr = (Header *)block;

	// Calculate block's (first block) information.
	block_end = block + sizeof(Header) + h_ptr->size;
	block_size = block_end - block;

	// Calculate block2's (second block) information. Align block2 to 8-byte
	// boundary.
	block2 = block + sizeof(Header) + bytes;
	if (!is_aligned(block2, &offset))
		block2 += 8 - offset;

	block2_size = block_end - block2;

	// If block2 is so small that it wouldn't have any space available, then
	// do not split. (User will get more memory than requested... oh well.)
	if (block2_size <= sizeof(Header))
		return block;

	// Split block.

	// Remove block from free list.
	remove_block(block, &free);

	// Create new headers for block and block2.
	h_ptr->next = NULL;
	h_ptr->size = block_size - block2_size - sizeof(Header);

	b2header.next = NULL;
	b2header.size = block2_size - sizeof(Header);

	memcpy(block2, &b2header, sizeof(Header));

	// Add both blocks to free list.
	add_block(block, &free);
	add_block(block2, &free);

	return block;
}

/*	find_block(bytes) - Find a block that has >= bytes available.
 *
 *	Returns a pointer to that block.
 */
static void * find_block(size_t bytes, char *free)
{
	Header *h_ptr;
	size_t numbytes;
	size_t pagesize;
	unsigned char *mem;

	// Look through each free block. If found, split. (The block may need to be
	// split if it's much larger than bytes.)
	h_ptr = (Header *)free;
	while (h_ptr) {
		if (h_ptr->size >= bytes)
			return split((char *)h_ptr, bytes, free);

		h_ptr = (Header *)h_ptr->next;
	}

	// Suitable block not found... create one!

	// Calculate how many pages will be needed to store new block.
	pagesize = sysconf(_SC_PAGESIZE);
	numbytes = (bytes + sizeof(Header) + pagesize - 1) & ~(pagesize - 1);

	// Use mmap to allocate new pages.
	mem = (unsigned char *)mmap(NULL, numbytes, PROT_READ | PROT_WRITE,
								MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (mem == MAP_FAILED) {
		return NULL;
	}

	// Setup block's header.
	h_ptr = (Header *)mem;
	h_ptr->next = NULL;
	h_ptr->size = numbytes - sizeof(Header);

	// Add block to free list.
	add_block((char *)h_ptr, &free);

	// Split and return new block.
	return (Header *)split((char *)h_ptr, bytes, free);
}

void list_free()
{
	Header *h_ptr = (Header *)_head_free;
	size_t size;
	size_t location;
	size_t next;

	printf("Free:\n");
	while (h_ptr) {
		location = (size_t)h_ptr;
		next = (size_t)h_ptr->next;
		size = h_ptr->size;
		printf("\tlocation: %d\n\tnext: %d\n\tsize: %d\n\n", (int)location,
			(int)next, (int)size);

		h_ptr = (Header *)h_ptr->next;
	}
}

void list_used()
{
	Header *h_ptr = (Header *)_head_used;
	size_t size;
	size_t location;
	size_t next;

	printf("Used:\n");
	while (h_ptr) {
		location = (size_t)h_ptr;
		next = (size_t)h_ptr->next;
		size = h_ptr->size;
		printf("\tlocation: %d\n\tnext: %d\n\tsize: %d\n\n", (int)location,
			(int)next, (int)size);

		h_ptr = (Header *)h_ptr->next;
	}
}

/*	malloc(bytes) - Allocate block of bytes.
 *
 *	Returns a pointer to allocated block of bytes.
 */
void * malloc(size_t bytes)
{
	Header *h_ptr;
	unsigned int i;
	size_t num = sysconf(_SC_PAGESIZE) / sizeof(ThreadTable);

	// check that the thread table has been created.
	if (!_thread_tbl) {
		if (pthread_mutex_lock(&_mutex)) {
			return NULL;
		}

		// Check thread table again in case a thread is waiting on the lock
		// before it can be changed.
		if (!_thread_tbl) {
			_thread_tbl = (ThreadTable *)mmap(NULL, sysconf(_SC_PAGESIZE),
											  PROT_READ | PROT_WRITE,
											  MAP_PRIVATE | MAP_ANONYMOUS,
											  -1, 0);
			if (_thread_tbl == MAP_FAILED) {
				pthread_mutex_unlock(&_mutex);
				return NULL;
			}

			// init thread table values.
			for (i = 1; i < num; ++i) {
				_thread_tbl[i].id = -1;
				_thread_tbl[i].free = NULL;
				_thread_tbl[i].used = NULL;
			}

			_thread_tbl[0].id = pthread_self();
		}

		pthread_mutex_unlock(&_mutex);
	}

	for (i = 0; i < num; ++i) {
		if (_thread_tbl[i].id == pthread_self()) {
			break;
		} else if (_thread_tbl[i].id == -1) {
			if (pthread_mutex_lock(&_mutex)) {
				return NULL;
			}

			_thread_tbl[i].id = pthread_self();

			pthread_mutex_unlock(&_mutex);
			break;
		}
	}

	if (i == num) {
		return NULL;
	}

	// Get header of suitable block.
	h_ptr = find_block(bytes, _thread_tbl[i].free);
	if (!h_ptr) {
		return NULL;
	}

	// Remove block from free list.
	remove_block((char *)h_ptr, &_thread_tbl[i].free);

	// Add block to used list. (Setting next to NULL is just a precaution.)
	h_ptr->next = NULL;
	add_block((char *)h_ptr, &_thread_tbl[i].used);

	return ((char *)h_ptr) + sizeof(Header);
}

/*	calloc(bytes) - Allocate block of bytes. Set bytes to value.
 *
 *	Returns a pointer to allocated block of bytes.
 */
void * calloc(size_t bytes, size_t value)
{
	char *ptr;

	ptr = (char *)malloc(bytes);
	if (!ptr) {
		return NULL;
	}

	memset(ptr, 0, bytes);

	return ptr;
}

/*	realloc(ptr, bytes) - Resize allocated block (ptr) to bytes.
 *
 *	Returns a pointer to allocated block of bytes.
 */
void * realloc(void *ptr, size_t bytes)
{
	char *new_mem;
	Header *h_ptr;

	h_ptr = (Header *)(ptr - sizeof(Header));
	new_mem = (char *)malloc(bytes);
	if (!new_mem) {
		return NULL;
	}

	memcpy(new_mem, ptr, bytes);

	free(ptr);

	return new_mem;
}

/*	free() - Free allocated block of bytes.
 *
 */
void free(void *ptr)
{
	Header *h_ptr;
	size_t num = sysconf(_SC_PAGESIZE) / sizeof(ThreadTable);
	unsigned int i;

	if (!ptr) {
		return;
	}

	h_ptr = (Header *)(ptr - sizeof(Header));

	for (i = 0; i < num; ++i) {
		if (_thread_tbl[i].id == pthread_self()) {
			break;
		}
	}

	if (i == num) {
		return;
	}

	remove_block((char *)h_ptr, &_thread_tbl[i].used);

	h_ptr->next = NULL;
	add_block((char *)h_ptr, &_thread_tbl[i].free);
}
