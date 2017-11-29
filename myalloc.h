/* File: myalloc.h
 * By: Andrew Holbrook
 */

#pragma once
#ifndef __MYALLOC_H__
#define __MYALLOC_H__

#include <pthread.h>

// Create an 8-byte aligned structure for holding metadata.
typedef struct _Header
{
	char *next;		// next block of memory
	size_t size;	// size of current block
} __attribute__ ((aligned(8))) Header;

typedef struct _ThreadTable
{
	long long id;
	char *used;
	char *free;
} ThreadTable;

// Memory management functions.
void * malloc(size_t);
void * calloc(size_t, size_t);
void * realloc(void *, size_t);
void free(void *);
void list_free();
void list_used();
#endif // __MYALLOC_H__
