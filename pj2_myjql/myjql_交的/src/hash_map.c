#include "hash_map.h"

#include <stdio.h>
#include <string.h>

void hash_table_init(const char *filename, BufferPool *pool, off_t n_directory_blocks) {
    init_buffer_pool(filename, pool);
    if (pool->file.length != 0) // not a new file. already initialized.
        return; 
    // new file. need to initialize
    Page *ctrblock_page = get_page(pool, 0);
    HashMapControlBlock *ctrblock = (HashMapControlBlock*) ctrblock_page;
    ctrblock->n_directory_blocks = n_directory_blocks;
    ctrblock->free_block_head = PAGE_SIZE * (n_directory_blocks + 1);
    ctrblock->max_size = 8;
    write_page(ctrblock_page, &pool->file, 0);
    release(pool, 0);

    // Initialize directory blocks
    for (int i = 0; i < n_directory_blocks; ++i) {
        HashMapDirectoryBlock dirblock;
        memset(dirblock.directory, (off_t) -1, sizeof(dirblock.directory));
        write_page((const Page*) &dirblock, &pool->file, PAGE_SIZE * (i + 1));
    }

    // Initialize map blocks
    for (int i = 0; i < ctrblock->max_size; ++i) {
        HashMapBlock mapblock;
        mapblock.n_items = 0;
        mapblock.next = (i != ctrblock->max_size - 1) ? ctrblock->free_block_head + PAGE_SIZE * (i + 1) : -1;
        write_page((const Page*) &mapblock, &pool->file, ctrblock->free_block_head + PAGE_SIZE * i);
    }

    printf("Hash table initialized with %lld directory blocks and %lld map blocks.\n", n_directory_blocks, ctrblock->max_size);
}

void hash_table_close(BufferPool *pool) {
    close_buffer_pool(pool);
    printf("Hash table closed.\n");
}

void hash_table_insert(BufferPool *pool, short size, off_t addr) {
    // lock HashMapControlBlock
    //  find position
    // if dir no ptr: create a new link list
        // can add into some "unfree" block
            // for convenience: inquire space from file
            // insert before the first HashMapBlock
    // no "unfree" list
        // need to make a new "unfree" block
        // there is free block existing: take the first one
            // no free block: use some space from the bottom of the file
            // lastpage is the starter of the empty space



    if (size < 0 || size >= 128) {
        printf("Invalid size (%d) when inserting.\n", size);
        return;
    }

    HashMapControlBlock *ctrblock_ptr = (HashMapControlBlock*) get_page(pool, 0);
    Page *target_page = get_page(pool, PAGE_SIZE * ((size / HASH_MAP_DIR_BLOCK_SIZE) + 1));
    off_t *ptr = &(((HashMapDirectoryBlock*) target_page)->directory[size % HASH_MAP_DIR_BLOCK_SIZE]);

    if (*ptr == -1) {
        // No existing block for this size, create a new map block
        off_t blank_mapblock_ptr = ctrblock_ptr->free_block_head;
        if (blank_mapblock_ptr != -1) {
            HashMapBlock *blank_mapblock = (HashMapBlock*) get_page(pool, blank_mapblock_ptr);
            ctrblock_ptr->free_block_head = blank_mapblock->next;
            blank_mapblock->n_items = 1;
            blank_mapblock->next = -1;
            blank_mapblock->table[0] = addr;
            release(pool, blank_mapblock_ptr);
            *ptr = blank_mapblock_ptr;
            printf("Inserted new map block at address %lld for size %d.\n", blank_mapblock_ptr, size);
        } else {
            // No free blocks available, allocate a new one
            HashMapBlock newmapblock;
            newmapblock.n_items = 1;
            newmapblock.next = -1;
            newmapblock.table[0] = addr;
            off_t new_block_addr = PAGE_SIZE * (1 + ctrblock_ptr->n_directory_blocks + ctrblock_ptr->max_size);
            write_page((const Page*) &newmapblock, &pool->file, new_block_addr);
            *ptr = new_block_addr;
            ++ctrblock_ptr->max_size;
            printf("Allocated new map block at address %lld for size %d.\n", new_block_addr, size);
        }
    } else {
        // Existing block found, insert the item
        off_t temp_ptr = *ptr;
        while (temp_ptr != -1) {
            HashMapBlock *mapblock = (HashMapBlock*) get_page(pool, temp_ptr);
            if (mapblock->n_items >= HASH_MAP_BLOCK_SIZE) {
                off_t pre = temp_ptr;
                temp_ptr = mapblock->next;
                release(pool, pre);
            } else {
                mapblock->table[mapblock->n_items] = addr;
                ++mapblock->n_items;
                release(pool, temp_ptr);
                printf("Inserted item at address %lld into map block %lld.\n", addr, temp_ptr);
                return;
            }
        }

        // If no suitable block is found, create a new one
        HashMapBlock newmapblock;
        newmapblock.n_items = 1;
        newmapblock.next = *ptr;
        newmapblock.table[0] = addr;
        off_t new_block_addr = PAGE_SIZE * (1 + ctrblock_ptr->n_directory_blocks + ctrblock_ptr->max_size);
        write_page((const Page*) &newmapblock, &pool->file, new_block_addr);
        *ptr = new_block_addr;
        ++ctrblock_ptr->max_size;
        printf("Allocated and inserted new map block at address %lld for size %d.\n", new_block_addr, size);
    }

    release(pool, 0);
    release(pool, PAGE_SIZE * ((size / HASH_MAP_DIR_BLOCK_SIZE) + 1));
}

off_t hash_table_pop_lower_bound(BufferPool *pool, short size) {
    if (size < 0 || size >= 128) {
        printf("Invalid size (%d) when popping lower bound.\n", size);
        return -1;
    }

    HashMapControlBlock *ctrblock_ptr = (HashMapControlBlock*) get_page(pool, 0);
    short target_size = size;
    off_t start_offset = PAGE_SIZE * ((size / HASH_MAP_DIR_BLOCK_SIZE) + 1);

    for (int i = 0; i < ctrblock_ptr->n_directory_blocks; ++i) {
        HashMapDirectoryBlock *dirblock_ptr = (HashMapDirectoryBlock*) get_page(pool, start_offset + i * PAGE_SIZE);
        int j = (i == 0) ? size % HASH_MAP_DIR_BLOCK_SIZE : 0;
        off_t ptr;

        for (; j < HASH_MAP_DIR_BLOCK_SIZE; ++j) {
            ptr = dirblock_ptr->directory[j];
            if (ptr != -1) {
                HashMapBlock *mapblock = (HashMapBlock*) get_page(pool, ptr);
                off_t res = mapblock->table[mapblock->n_items - 1];
                release(pool, ptr);
                release(pool, start_offset + i * PAGE_SIZE);
                release(pool, 0);
                hash_table_pop(pool, target_size, res);
                printf("Popped lower bound: size %d, address %lld.\n", target_size, res);
                return res;
            }
            ++target_size;
            if (target_size >= 128) {
                release(pool, start_offset + i * PAGE_SIZE);
                release(pool, 0);
                return -1;
            }
        }

        release(pool, start_offset + i * PAGE_SIZE);
    }

    release(pool, 0);
    return -1;
}

void hash_table_pop(BufferPool *pool, short size, off_t addr) {
    // delete "addr" in the hash_table(pool)
    // size is the offset of "addr" block (in the buffer pool)
    if (size < 0 || size >= 128) {
        printf("Invalid size (%d) when popping.\n", size);
        return;
    }

    HashMapControlBlock *ctrblock_ptr = (HashMapControlBlock*) get_page(pool, 0);
    HashMapDirectoryBlock *target_dirblock_ptr = (HashMapDirectoryBlock*) get_page(pool, PAGE_SIZE * ((size / HASH_MAP_DIR_BLOCK_SIZE) + 1));
    off_t *offset_ptr = &(target_dirblock_ptr->directory[size % HASH_MAP_DIR_BLOCK_SIZE]);
    off_t *i = offset_ptr;

    while (*i != -1) {
        HashMapBlock *mapblock = (HashMapBlock*) get_page(pool, *i);
        short if_pop_success = 0;

        for (int j = 0; j < mapblock->n_items; ++j) {
            if (mapblock->table[j] == addr) {
                if (mapblock->n_items == 1) {
                    off_t temp_next = mapblock->next;
                    mapblock->next = ctrblock_ptr->free_block_head;
                    mapblock->n_items = 0;
                    ctrblock_ptr->free_block_head = *i;
                    release(pool, *i);
                    *i = temp_next;
                } else {
                    if (mapblock->n_items - 1 - j != 0)
                        memmove(mapblock->table + j, mapblock->table + j + 1, (mapblock->n_items - 1 - j) * sizeof(off_t));
                    --mapblock->n_items;
                    release(pool, *i);
                }
                if_pop_success = 1;
                break;
            }
        }

        if (if_pop_success) {
            printf("Popped address %lld for size %d.\n", addr, size);
            break;
        } else {
            release(pool, *i);
        }

        i = &(mapblock->next);
    }

    release(pool, PAGE_SIZE * ((size / HASH_MAP_DIR_BLOCK_SIZE) + 1));
    release(pool, 0);
}

/* void print_hash_table(BufferPool *pool) {
    HashMapControlBlock *ctrl = (HashMapControlBlock*)get_page(pool, 0);
    HashMapDirectoryBlock *dir_block;
    off_t block_addr, next_addr;
    HashMapBlock *block;
    int i, j;
    printf("----------HASH TABLE----------\n");
    for (i = 0; i < ctrl->max_size; ++i) {
        dir_block = (HashMapDirectoryBlock*)get_page(pool, (i / HASH_MAP_DIR_BLOCK_SIZE + 1) * PAGE_SIZE);
        if (dir_block->directory[i % HASH_MAP_DIR_BLOCK_SIZE] != 0) {
            printf("%d:", i);
            block_addr = dir_block->directory[i % HASH_MAP_DIR_BLOCK_SIZE];
            while (block_addr != 0) {
                block = (HashMapBlock*)get_page(pool, block_addr);
                printf("  [" FORMAT_OFF_T "]", block_addr);
                printf("{");
                for (j = 0; j < block->n_items; ++j) {
                    if (j != 0) {
                        printf(", ");
                    }
                    printf(FORMAT_OFF_T, block->table[j]);
                }
                printf("}");
                next_addr = block->next;
                release(pool, block_addr);
                block_addr = next_addr;
            }
            printf("\n");
        }
        release(pool, (i / HASH_MAP_DIR_BLOCK_SIZE + 1) * PAGE_SIZE);
    }
    release(pool, 0);
    printf("------------------------------\n");
} */