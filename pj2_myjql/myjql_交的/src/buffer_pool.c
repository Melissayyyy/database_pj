#include "buffer_pool.h"
#include "file_io.h"

#include <stdio.h>
#include <stdlib.h>

//long long test_cnt = 0;

void init_buffer_pool(const char *filename, BufferPool *pool) {
    FileIOResult fior = open_file(&(pool->file),filename);
    if(fior != FILE_IO_SUCCESS){
        // printf("ERROR: file unsuccessful\n");       
        return;
    } 
    for(int i=0;i<CACHE_PAGE;++i){
        pool->addrs[i] = -1;    // initialize: it means there is a empty page in buffer pool
        pool->cnt[i] = 0;       // initialize: not used
        pool->ref[i] = 0;
    }
}

void close_buffer_pool(BufferPool *pool) {
    for(int i=0;i<CACHE_PAGE;++i){
        if(pool->addrs[i] == -1) continue;
        FileIOResult fior = write_page(&(pool->pages[i]),&(pool->file),pool->addrs[i]);
        if(fior != FILE_IO_SUCCESS){
            printf("file io fail: result = %d",fior);
            return;
        } 
    }
    close_file(&(pool->file));
    //free(pool);
}

// Retrieves a page from the buffer pool, using the LRU (Least Recently Used) policy
Page *get_page(BufferPool *pool, off_t addr) {
    // printf("get_page:offset: %d ", addr);

    int pos = CACHE_PAGE;  // Initialize position to an invalid value
    size_t maxcnt = 0;     // Track the maximum count

    // Iterate through all cache pages
    for (int i = 0; i < CACHE_PAGE; ++i) {
        ++pool->cnt[i];  // Increment the count for LRU
        if (pool->addrs[i] == addr) {
            pos = i;  // If the address is found, set the position
        } 
    }

    if (pos != CACHE_PAGE) {
        // Page is found in the buffer pool
        pool->cnt[pos] = 0;  // Reset the count for this page
        ++pool->ref[pos];    // Increment the reference count
        return &(pool->pages[pos]);
    } else {
        // Page is not found in the buffer pool
        for (int j = 0; j < CACHE_PAGE; ++j) {
            // Find the least recently used page that is not currently referenced
            if (maxcnt < pool->cnt[j] && pool->ref[j] == 0) {
                maxcnt = pool->cnt[j];
                pos = j;
            }
        }
        FileIOResult fior;
        if (pool->addrs[pos] != -1) {
            // Write the current page back to the file if it is valid
            fior = write_page(&(pool->pages[pos]), &(pool->file), pool->addrs[pos]);
            if (fior != FILE_IO_SUCCESS) {
                printf("file io fail: result = %d", fior);
                return NULL;
            }
        }
        // Read the new page from the file into the buffer pool
        fior = read_page(&(pool->pages[pos]), &(pool->file), addr);
        pool->addrs[pos] = addr;  // Update the address
        pool->cnt[pos] = 0;       // Reset the count
        pool->ref[pos] = 1;       // Set the reference count to 1
        return &(pool->pages[pos]);
    }
}

// Releases a page from the buffer pool by decrementing its reference count
void release(BufferPool *pool, off_t addr) {
    for (int i = 0; i < CACHE_PAGE; ++i) {
        if (pool->addrs[i] == addr) {
            --pool->ref[i];  // Decrement the reference count
            return;
        }
    }
    // Print an error message if the address is not found
    printf("release not found addr! %lld", addr);
}

/* void print_buffer_pool(BufferPool *pool) {
} */

/* void validate_buffer_pool(BufferPool *pool) {
} */
