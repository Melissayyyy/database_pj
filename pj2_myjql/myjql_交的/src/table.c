#include "table.h"
#include "hash_map.h"
#include <stdio.h>
#include <string.h>

// Initialize the table by initializing the buffer pool and hash table
void table_init(Table *table, const char *data_filename, const char *fsm_filename) {
    init_buffer_pool(data_filename, &table->data_pool);
    hash_table_init(fsm_filename, &table->fsm_pool, PAGE_SIZE / HASH_MAP_DIR_BLOCK_SIZE);
}

// Close the table by closing the buffer pool and hash table
void table_close(Table *table) {
    close_buffer_pool(&table->data_pool);
    hash_table_close(&table->fsm_pool);
}

// Get the total number of blocks in the table
off_t table_get_total_blocks(Table *table) {
    return table->data_pool.file.length / PAGE_SIZE;
}

// Get the total number of items in a specific block
short table_block_get_total_items(Table *table, off_t block_addr) {
    Block *block = (Block*)get_page(&table->data_pool, block_addr);
    short n_items = block->n_items;
    release(&table->data_pool, block_addr);
    return n_items;
}

// Read an item from the table using its RID
void table_read(Table *table, RID rid, ItemPtr dest) {
    off_t block_addr = get_rid_block_addr(rid);
    short idx = get_rid_idx(rid);
    Block *block = (Block*)get_page(&(table->data_pool), block_addr);
    ItemID read_item_id = get_item_id(block, idx);
    ItemPtr read_item_ptr = get_item(block, idx);
    short cpy_size = get_item_id_size(read_item_id);
    release(&(table->data_pool), block_addr);
    memmove(dest, read_item_ptr, cpy_size);
}

// Insert an item into the table and return its RID
RID table_insert(Table *table, ItemPtr src, short size) {
    off_t insert_addr = hash_table_pop_lower_bound(&(table->fsm_pool), size);
    off_t rid_addr;
    short rid_idx;
    RID res_rid;
    if (insert_addr == -1) {
        // If no suitable block is found, create a new block
        rid_addr = table->data_pool.file.length;
        Block block;
        init_block(&block);
        rid_idx = new_item(&block, src, size);
        write_page((const Page*)(&block), &(table->data_pool.file), rid_addr);
        if (block.tail_ptr - block.head_ptr - sizeof(ItemID) > 0) {
            hash_table_insert(&(table->fsm_pool), block.tail_ptr - block.head_ptr - sizeof(ItemID), rid_addr);
        }
    } else {
        // If a suitable block is found, insert the item into it
        Block *block = (Block*)get_page(&(table->data_pool), insert_addr);
        rid_idx = new_item(block, src, size);
        rid_addr = insert_addr;
        int i = 0;
        short avail_size;
        for (; i < block->n_items; ++i) {
            ItemID cur_itemid = get_item_id(block, i);
            if (get_item_id_availability(cur_itemid)) {
                break;
            }
        }
        if (i != block->n_items) {
            avail_size = block->tail_ptr - block->head_ptr;
        } else {
            avail_size = block->tail_ptr - block->head_ptr - sizeof(ItemID);
        }
        release(&(table->data_pool), insert_addr);
        if (avail_size > 0) {
            hash_table_insert(&(table->fsm_pool), avail_size, insert_addr);
        }
    }
    get_rid_block_addr(res_rid) = rid_addr;
    get_rid_idx(res_rid) = rid_idx;
    return res_rid;
}

// Delete an item from the table using its RID
void table_delete(Table *table, RID rid) {
    off_t block_addr = get_rid_block_addr(rid);
    short idx = get_rid_idx(rid);
    Block *block = (Block*)get_page(&(table->data_pool), block_addr);
    int i = 0;
    short avail_size;
    for (; i < block->n_items; ++i) {
        ItemID cur_itemid = get_item_id(block, i);
        if (get_item_id_availability(cur_itemid)) {
            break;
        }
    }
    if (i != block->n_items) {
        avail_size = block->tail_ptr - block->head_ptr;
    } else {
        avail_size = block->tail_ptr - block->head_ptr - sizeof(ItemID);
    }
    // Remove the block from the free space map
    hash_table_pop(&(table->fsm_pool), avail_size, block_addr);
    delete_item(block, idx);
    i = 0;
    for (; i < block->n_items; ++i) {
        ItemID cur_itemid = get_item_id(block, i);
        if (get_item_id_availability(cur_itemid)) {
            break;
        }
    }
    if (i != block->n_items) {
        avail_size = block->tail_ptr - block->head_ptr;
    } else {
        avail_size = block->tail_ptr - block->head_ptr - sizeof(ItemID);
    }
    release(&(table->data_pool), block_addr);
    // Reinsert the block into the free space map
    hash_table_insert(&(table->fsm_pool), avail_size, block_addr);
}

/* void print_table(Table *table, printer_t printer) {
    printf("\n---------------TABLE---------------\n");
    off_t i, total = table_get_total_blocks(table);
    off_t block_addr;
    Block *block;
    for (i = 0; i < total; ++i) {
        block_addr = i * PAGE_SIZE;
        block = (Block*)get_page(&table->data_pool, block_addr);
        printf("[" FORMAT_OFF_T "]\n", block_addr);
        print_block(block, printer);
        release(&table->data_pool, block_addr);
    }
    printf("***********************************\n");
    print_hash_table(&table->fsm_pool);
    printf("-----------------------------------\n\n");
} */

void print_rid(RID rid) {
    printf("RID(" FORMAT_OFF_T ", %d)", get_rid_block_addr(rid), get_rid_idx(rid));
}

/* void analyze_table(Table *table) {
    block_stat_t stat, curr;
    off_t i, total = table_get_total_blocks(table);
    off_t block_addr;
    Block *block;
    stat.empty_item_ids = 0;
    stat.total_item_ids = 0;
    stat.available_space = 0;
    for (i = 0; i < total; ++i) {
        block_addr = i * PAGE_SIZE;
        block = (Block*)get_page(&table->data_pool, block_addr);
        analyze_block(block, &curr);
        release(&table->data_pool, block_addr);
        accumulate_stat_info(&stat, &curr);
    }
    printf("++++++++++ANALYSIS++++++++++\n");
    printf("total blocks: " FORMAT_OFF_T "\n", total);
    total *= PAGE_SIZE;
    printf("total size: " FORMAT_OFF_T "\n", total);
    printf("occupancy: %.4f\n", 1. - 1. * stat.available_space / total);
    printf("ItemID occupancy: %.4f\n", 1. - 1. * stat.empty_item_ids / stat.total_item_ids);
    printf("++++++++++++++++++++++++++++\n\n");
} */