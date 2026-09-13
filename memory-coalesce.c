#include "memory.h"
#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>

#define HEAP_SIZE 2048
#define ALIGNMENT 16

typedef struct header {
    size_t size;
    struct header * prev;
    struct header * next;
    int in_use;
} m_header;

m_header* freelist = NULL;

void print_freelist() {
    m_header* curr = freelist;
    while(curr != NULL) {
        printf("[%p: size:%lu prev:%p next:%p use:%d]\n", curr, curr->size, curr->prev, curr->next, curr->in_use);
        curr = curr->next;
    }
    printf("\n --- \n");
}

// rounds a request up to the next multiple of ALIGNMENT, so every block we hand
// out keeps the one after it aligned too
size_t align_up(size_t size) {
    return (size + ALIGNMENT - 1) & ~((size_t)ALIGNMENT - 1);
}

// splits block in two if the leftover can hold a header plus a full aligned
// chunk of data, leaving block sized exactly for the request
void split_block(m_header * block, size_t size) {
    if (block->size < size + sizeof(m_header) + ALIGNMENT) {
        return;
    }

    m_header * rest = (m_header *)((char *)(block + 1) + size);
    rest->size = block->size - size - sizeof(m_header);
    rest->in_use = 0;
    rest->prev = block;
    rest->next = block->next;

    if (rest->next != NULL) {
        rest->next->prev = rest;
    }

    block->size = size;
    block->next = rest;
}

// merges block with its neighbours when they are also free. list order matches
// memory order, so a neighbour's header and data are absorbed as one span.
void coalesce(m_header * block) {
    m_header * next = block->next;
    if (next != NULL && !next->in_use) {
        block->size += sizeof(m_header) + next->size;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    m_header * prev = block->prev;
    if (prev != NULL && !prev->in_use) {
        prev->size += sizeof(m_header) + block->size;
        prev->next = block->next;
        if (prev->next != NULL) {
            prev->next->prev = prev;
        }
    }
}

void * new_malloc(size_t size) {
    if(freelist == NULL) {
        printf("MMAP\n");
         // Use mmap to get anonymous, private memory
        freelist = mmap(NULL,                    // Desired start address (NULL lets OS choose)
                      2048,                  // Length of the mapping (rounded up to page size)
                      PROT_READ | PROT_WRITE,  // Memory protection: readable and writable
                      MAP_PRIVATE | MAP_ANONYMOUS, // Visibility: private to the process, not file-backed
                      -1,                      // File descriptor: -1 for anonymous mapping
                      0);                      // Offset: 0 for anonymous mapping

        if (freelist == MAP_FAILED) {
            printf("map failed\n");
            return NULL;
        }

        // the whole mapping starts life as one free block
        freelist->size = HEAP_SIZE - sizeof(m_header);
        freelist->prev = NULL;
        freelist->next = NULL;
        freelist->in_use = 0;
    }

    if (size == 0) {
        return NULL;
    }

    size = align_up(size);

    // first fit: take the first free block large enough to hold the request
    m_header * curr = freelist;
    while (curr != NULL) {
        if (!curr->in_use && curr->size >= size) {
            split_block(curr, size);
            curr->in_use = 1;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    return NULL;
}

void new_free(void * ptr) {
    if (ptr == NULL) {
        return;
    }

    // the header sits immediately before the data we handed out
    m_header * block = (m_header *)ptr - 1;
    block->in_use = 0;
    coalesce(block);
}
