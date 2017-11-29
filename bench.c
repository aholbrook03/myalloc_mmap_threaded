/* File: myalloc.c
 * By: Andrew Holbrook
 *
 *	Test program for myalloc_threaded
 */

#define NUM_ALLOC 10000
#define MAX_ALLOC 1024 * 1024 * 1024
#define NUM_THREADS 10

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "myalloc.h"

typedef struct _thread_data
{
	unsigned int id;
	unsigned int flag;
} thread_data;

void bench()
{
	unsigned char *mem;
	unsigned int i;
	unsigned int j;
	size_t bytes;
	unsigned int bad = 0;
	for (i = 0; i < rand() % NUM_ALLOC + 1; ++i) {
		bytes = rand() % MAX_ALLOC + 1;

		mem = (unsigned char *)malloc(bytes);
		if (!mem) {
			++bad;
			continue;
		}

		mem = (unsigned char *)realloc(mem, bytes + 1000);
		if (!mem) {
			++bad;
			continue;
		}

		for (j = 0; j < bytes; ++j) {
			mem[j] = j;
		}

		free(mem);
	}
}

int main(int argc, char **argv)
{
	unsigned int i;
	pthread_t threads[NUM_THREADS];
	unsigned int num_threads = 0;
	clock_t start;
	clock_t end;
	float dtime;

	start = clock();

	srand(time(NULL));

	for (i = 0; i < NUM_THREADS; ++i) {
		if (pthread_create(&threads[i], NULL, (void *)bench, NULL)) {
			break;
		}

		++num_threads;
	}

	for (i = 0; i < num_threads; ++i) {
		pthread_join(threads[i], NULL);
	}

	end = clock();

	dtime = (float)(end - start) / CLOCKS_PER_SEC;

	printf("%f seconds\n", dtime);

	return 0;
}
