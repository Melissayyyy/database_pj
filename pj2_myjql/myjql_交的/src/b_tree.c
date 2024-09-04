#include "b_tree.h"
#include "buffer_pool.h"

#include <stdio.h>
#include <string.h>

void b_tree_init(const char *filename, BufferPool *pool) {
    init_buffer_pool(filename, pool);
    /* TODO: add code here */
    if(pool->file.length != 0) // if file is not empty, no need to initialize
        return;
    BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
    controlblock->root_node = PAGE_SIZE;
    controlblock->free_node_head = 2 * PAGE_SIZE;
    controlblock->max_size = 16;
    write_page((const Page*)controlblock, &pool->file, 0);
    BNode *root = (BNode*)get_page(pool,PAGE_SIZE);
    root->n = 0;
    root->next = -1;
    root->leaf = '1';
    write_page((const Page*)root, &pool->file, PAGE_SIZE);
    //  printf("Root initialized. Root node page offset: %lu\n", PAGE_SIZE);
    release(pool,PAGE_SIZE);
    // Initialize free list of nodes
    for(int i=0;i<controlblock->max_size;++i){
        BNode *freeblock = (BNode*)get_page(pool,controlblock->free_node_head + PAGE_SIZE*i);
        freeblock->leaf = 'f';
        if(i != controlblock->max_size-1){
            freeblock->next = controlblock->free_node_head + PAGE_SIZE*(i+1);
        }
        else{
            freeblock->next = -1;
        }
        write_page((const Page*)freeblock, &pool->file, controlblock->free_node_head + PAGE_SIZE*i);
        // printf("Free block %d initialized at offset %lu\n", i, controlblock->free_node_head + PAGE_SIZE * i);
        release(pool,controlblock->free_node_head + PAGE_SIZE*i);
        //write_page((const Page*)&freeblock,&pool->file,controlblock->free_node_head + PAGE_SIZE*i);
    }
    release(pool,0);
}

void b_tree_close(BufferPool *pool) {
    close_buffer_pool(pool);
}

// Recursive search function to find the appropriate node for a given key
off_t inside_search(BufferPool *pool, void *key, size_t size, off_t node_off, b_tree_ptr_row_cmp_t cmp){
    BNode *nodeptr = (BNode*)get_page(pool,node_off);
    printf("Searching node at offset %ld\n", node_off);
    if(nodeptr->leaf == '1'){
        release(pool,node_off);
        printf("Leaf node found for key at offset %ld\n", node_off);
        return node_off;
    } 
    else{
        if(cmp(key, size, nodeptr->row_ptr[0]) < 0){
            off_t temp = nodeptr->child[0];
            release(pool,node_off);
            return inside_search(pool,key,size,temp,cmp);
        }
        else if(cmp(key, size, nodeptr->row_ptr[nodeptr->n-1]) >= 0){
            off_t temp = nodeptr->child[nodeptr->n];
            release(pool,node_off);
            return inside_search(pool,key,size,temp,cmp);
        }
        else{
            for(int i=0;i<nodeptr->n-1;++i){
                if(cmp(key, size, nodeptr->row_ptr[i]) >= 0 && cmp(key, size, nodeptr->row_ptr[i+1]) < 0){
                    off_t temp = nodeptr->child[i+1];
                    release(pool,node_off);
                    return inside_search(pool,key,size,temp,cmp);
                }
            }
            printf("Key not found in any intervals of non-leaf node at offset %ld\n", node_off);
            return -1; // error
        }
    }
}

// Public API to search for a record identifier (RID) using a key
RID b_tree_search(BufferPool *pool, void *key, size_t size, b_tree_ptr_row_cmp_t cmp) {
    BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
    off_t root_off = controlblock->root_node;
    release(pool,0);
    off_t find_node_off = inside_search(pool,key,size,root_off,cmp);
    BNode *find_node = (BNode*)get_page(pool,find_node_off);
    int i = 0;  
    for(;i<find_node->n;++i){
        if(cmp(key,size,find_node->row_ptr[i]) == 0) break;
    }
    if(i<find_node->n){ //find the rid of the search value
        RID temp = find_node->row_ptr[i];
        printf("Key found with RID block address: %d and index: %d\n", get_rid_block_addr(temp), get_rid_idx(temp));
        release(pool,find_node_off);
        return temp;
    }
    else{ //do not find return rid(-1,0);
        RID res;
        printf("Key not found. Returning invalid RID.\n");
        release(pool,find_node_off);
        get_rid_block_addr(res) = -1;
        get_rid_idx(res) = 0;
        return res;
    }
}

typedef struct {
    RID *newkey_rid;
    off_t *node_off;
} newchildentry_t;

void inside_insert(BufferPool *pool, off_t node_off, RID rid, newchildentry_t newchildentry, b_tree_row_row_cmp_t cmp, b_tree_insert_nonleaf_handler_t insert_handler){
    // Retrieve the node from the buffer pool at the specified offset
    BNode *nodeptr = (BNode*)get_page(pool,node_off);
    // printf("Entering node at offset %ld for deletion\n", node_off);
    if(nodeptr->leaf == '1'){ //leaf node
        if(nodeptr->n < 2 * DEGREE){ //has space
            int i = 0;
            for(;i<nodeptr->n;++i){
                if(cmp(rid, nodeptr->row_ptr[i]) < 0){
                    printf("Position found at index %d for new entry\n", i);
                    break;
                }
            }
            if(i != nodeptr->n){
                memmove(nodeptr->row_ptr + i + 1, nodeptr->row_ptr + i, (nodeptr->n - i)*sizeof(RID));
            }
            nodeptr->row_ptr[i] = rid;
            ++nodeptr->n;
            get_rid_block_addr(*(newchildentry.newkey_rid)) = -1;
        }
        else{ //split
            BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
            off_t free_block_off = controlblock->free_node_head;
            BNode *L2;
            if(free_block_off != -1){
                L2 = (BNode*)get_page(pool,free_block_off);
                controlblock->free_node_head = L2->next;
            }
            else{
                free_block_off = PAGE_SIZE*(controlblock->max_size + 2);
                L2 = (BNode*)get_page(pool,free_block_off);
                write_page((const Page*)L2,&pool->file,free_block_off);
                ++controlblock->max_size;
            }
            
            L2->next = nodeptr->next;
            nodeptr->next = free_block_off;
            L2->leaf = '1';
            L2->n = DEGREE + 1;
            int i = 0;
            for(;i<nodeptr->n;++i){
                if(cmp(rid, nodeptr->row_ptr[i]) < 0){
                    break;
                }
            }
            if(i < DEGREE){
                memcpy(L2->row_ptr, nodeptr->row_ptr + DEGREE - 1, (DEGREE+1)*sizeof(RID));
                memmove(nodeptr->row_ptr + i + 1, nodeptr->row_ptr + i, (DEGREE - i - 1)*sizeof(RID));
                nodeptr->row_ptr[i] = rid;
            }
            else{
                memcpy(L2->row_ptr, nodeptr->row_ptr + DEGREE, (i - DEGREE)*sizeof(RID));
                memcpy(L2->row_ptr + i - DEGREE + 1, nodeptr->row_ptr + i, (2*DEGREE-i)*sizeof(RID));
                L2->row_ptr[i - DEGREE] = rid;
            }
            nodeptr->n = DEGREE;
            printf("Split complete. New node created at offset %ld.\n", free_block_off);


            *(newchildentry.newkey_rid) = L2->row_ptr[0];
            *(newchildentry.node_off) = nodeptr->next;
            release(pool,nodeptr->next); //release L2

            // root split
            if(node_off == controlblock->root_node){
                BNode *newroot;
                off_t free_block_off1 = controlblock->free_node_head;
                if(free_block_off1 != -1){
                    newroot = (BNode*)get_page(pool,free_block_off1);
                    controlblock->free_node_head = newroot->next;
                }
                else{
                    free_block_off1 = PAGE_SIZE*(controlblock->max_size + 2);
                    newroot = (BNode*)get_page(pool,free_block_off1);
                    write_page((const Page*)newroot,&pool->file,free_block_off1);
                    ++controlblock->max_size;
                }
                controlblock->root_node = free_block_off1;
                newroot->n = 1;
                newroot->leaf = '0';
                newroot->next = -1;
                newroot->row_ptr[0] = insert_handler(*(newchildentry.newkey_rid));
                newroot->child[0] = node_off;
                newroot->child[1] = *(newchildentry.node_off);

                release(pool,controlblock->root_node); // release new root node
            }
            release(pool,0); // release control block
        }
        release(pool,node_off); // release current node
        return;
    }
    else{ //non-leaf node
        printf("Non-leaf node processing\n");
        // find the correct child to descend to
        if(cmp(rid, nodeptr->row_ptr[0]) < 0){
            off_t temp = nodeptr->child[0];
            release(pool,node_off);
            inside_insert(pool,temp,rid,newchildentry,cmp,insert_handler);
        }
        else if(cmp(rid, nodeptr->row_ptr[nodeptr->n-1]) >= 0){
            off_t temp = nodeptr->child[nodeptr->n];
            release(pool,node_off);
            inside_insert(pool,temp,rid,newchildentry,cmp,insert_handler);
        }
        else{
            for(int i=0;i<nodeptr->n-1;++i){
                if(cmp(rid, nodeptr->row_ptr[i]) >= 0 && cmp(rid, nodeptr->row_ptr[i+1]) < 0){
                    off_t temp = nodeptr->child[i+1];
                    release(pool,node_off);
                    inside_insert(pool,temp,rid,newchildentry,cmp,insert_handler);
                    break;
                }
            }
        }
        // Check if a new key was successfully inserted and needs to be handled at this level
        if(get_rid_block_addr(*(newchildentry.newkey_rid)) == -1){
            return;
        }
        else{
            printf("INFO-- jandling insertion ripple effect at non-leaf level.\n");
            // Reaccess the node as it might have changed during recursive calls
            nodeptr = (BNode*)get_page(pool,node_off);
            // Check if there is space to insert the new key
            if(nodeptr->n < 2 * DEGREE){ //has space
                int i = 0;
                for(;i<nodeptr->n;++i){
                    if(cmp(*(newchildentry.newkey_rid), nodeptr->row_ptr[i]) < 0){
                        break;
                    }
                }
                if(i != nodeptr->n){
                    memmove(nodeptr->child + i + 2, nodeptr->child + i + 1, (nodeptr->n - i)*sizeof(off_t));
                    memmove(nodeptr->row_ptr + i + 1, nodeptr->row_ptr + i, (nodeptr->n - i)*sizeof(RID));
                }
                nodeptr->child[i+1] = *(newchildentry.node_off);
                nodeptr->row_ptr[i] = insert_handler(*(newchildentry.newkey_rid));
                ++nodeptr->n;

                get_rid_block_addr(*(newchildentry.newkey_rid)) = -1;
            }
            else{ //split
                printf("INFO-- non-leaf node full, proceeding to split.\n");
                BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
                off_t free_block_off = controlblock->free_node_head;
                BNode *N2;
                if(free_block_off != -1){
                    N2 = (BNode*)get_page(pool,free_block_off);
                    controlblock->free_node_head = N2->next;
                }
                else{
                    free_block_off = PAGE_SIZE*(controlblock->max_size + 2);
                    N2 = (BNode*)get_page(pool,free_block_off);
                    write_page((const Page*)N2,&pool->file,free_block_off);
                    ++controlblock->max_size;
                } 
                N2->next = nodeptr->next;
                nodeptr->next = free_block_off;
                N2->leaf = '0';
                N2->n = DEGREE;
                int i = 0;
                for(;i<nodeptr->n;++i){
                    if(cmp(*(newchildentry.newkey_rid), nodeptr->row_ptr[i]) < 0){
                        break;
                    }
                }
                if(i == DEGREE){
                    // Case when the new key is exactly at split point
                    memcpy(N2->child + 1, nodeptr->child + DEGREE + 1, DEGREE*sizeof(off_t));
                    memcpy(N2->row_ptr, nodeptr->row_ptr + DEGREE, DEGREE*sizeof(RID));
                    N2->child[0] = *(newchildentry.node_off);
                    nodeptr->n = DEGREE;
                    *(newchildentry.node_off) = nodeptr->next;
                    //newchildentry.newkey_rid unchanged
                }
                else if(i < DEGREE){
                    // New key goes in the left block
                    memcpy(N2->child, nodeptr->child + DEGREE, (DEGREE+1)*sizeof(off_t));
                    memcpy(N2->row_ptr,nodeptr->row_ptr + DEGREE, DEGREE*sizeof(RID));
                    memmove(nodeptr->row_ptr + i + 1, nodeptr->row_ptr + i, (DEGREE - i)*sizeof(RID));
                    memmove(nodeptr->child + i + 2, nodeptr->child + i + 1, (DEGREE - i - 1)*sizeof(off_t));
                    nodeptr->child[i+1] = *(newchildentry.node_off);
                    nodeptr->row_ptr[i] = insert_handler(*(newchildentry.newkey_rid));
                    *(newchildentry.newkey_rid) = nodeptr->row_ptr[DEGREE];
                    *(newchildentry.node_off) = nodeptr->next;
                    nodeptr->n = DEGREE;
                }
                else{ //i > DEGREE
                    for(int j=0;j<DEGREE;++j){
                        if(j + DEGREE + 1 < i) N2->row_ptr[j] = nodeptr->row_ptr[j + DEGREE + 1];
                        else if(j + DEGREE + 1 == i) N2->row_ptr[j] = insert_handler(*(newchildentry.newkey_rid));
                        else N2->row_ptr[j] = nodeptr->row_ptr[j + DEGREE];
                    }
                    for(int j=0;j<DEGREE + 1;++j){
                        if(j + DEGREE < i) N2->child[j] = nodeptr->child[j + DEGREE + 1];
                        else if(j + DEGREE == i) N2->child[j] = *(newchildentry.node_off);
                        else N2->child[j] = nodeptr->child[j + DEGREE];
                    }
                    *(newchildentry.newkey_rid) = nodeptr->row_ptr[DEGREE];
                    *(newchildentry.node_off) = nodeptr->next;
                    nodeptr->n = DEGREE;
                }
                printf("INFO-- non-leaf node split completed, new node at offset %ld.\n", free_block_off);
                release(pool,nodeptr->next); //release N2
                
                // handle the root split if necessary
                if(node_off == controlblock->root_node){
                    BNode *newroot;
                    off_t free_block_off1 = controlblock->free_node_head;
                    if(free_block_off1 != -1){
                        newroot = (BNode*)get_page(pool,free_block_off1);
                        controlblock->free_node_head = newroot->next;
                    }
                    else{
                        free_block_off1 = PAGE_SIZE*(controlblock->max_size + 2);
                        newroot = (BNode*)get_page(pool,free_block_off1);
                        write_page((const Page*)newroot,&pool->file,free_block_off1);
                        ++controlblock->max_size;
                    }
                    controlblock->root_node = free_block_off1;
                    newroot->n = 1;
                    newroot->leaf = '0';
                    newroot->next = -1;
                    newroot->row_ptr[0] = insert_handler(*(newchildentry.newkey_rid));
                    newroot->child[0] = node_off;
                    newroot->child[1] = *(newchildentry.node_off);
                    release(pool,controlblock->root_node);
                }
                release(pool,0); //release the control block
            }
            release(pool,node_off); //release the current node
            return;
        }
    }
}

RID b_tree_insert(BufferPool *pool, RID rid, b_tree_row_row_cmp_t cmp, b_tree_insert_nonleaf_handler_t insert_handler) {
    BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
    off_t root_off = controlblock->root_node;
    release(pool,0);
    newchildentry_t newchildentry;
    RID newchildentry_rid;
    get_rid_block_addr(newchildentry_rid) = -1;
    off_t newchildentry_node_off = -1;
    newchildentry.newkey_rid = &newchildentry_rid;
    newchildentry.node_off = &newchildentry_node_off;
    inside_insert(pool,root_off,rid,newchildentry,cmp,insert_handler);
    return newchildentry_rid;
}

void inside_delete(BufferPool *pool, off_t parent_node_off, off_t node_off, RID rid, RID *oldchildentry, b_tree_row_row_cmp_t cmp, b_tree_insert_nonleaf_handler_t insert_handler, b_tree_delete_nonleaf_handler_t delete_handler){
    BNode *nodeptr = (BNode*)get_page(pool,node_off);
    if(nodeptr->leaf == '1'){ //leaf node
        if(nodeptr->n > DEGREE){
            int i = 0;
            for(;i<nodeptr->n;++i){
                if(cmp(nodeptr->row_ptr[i],rid) == 0){
                    break;
                }
            }
            memmove(nodeptr->row_ptr + i, nodeptr->row_ptr + i + 1, (nodeptr->n-i-1)*sizeof(RID));
            --nodeptr->n;

            get_rid_block_addr(*oldchildentry) = -1;
        }
        else{
            int k = 0;
            for(;k<nodeptr->n;++k){
                if(cmp(nodeptr->row_ptr[k],rid) == 0){
                    break;
                }
            }
            memmove(nodeptr->row_ptr + k, nodeptr->row_ptr + k + 1, (nodeptr->n-k-1)*sizeof(RID));
            --nodeptr->n;
            //get_rid_block_addr(*oldchildentry) = -1;
            BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
            if(node_off == controlblock->root_node){ //N is root
                
            }
            else{
                BNode *parent_node = (BNode*)get_page(pool,parent_node_off);
                int i = 0;
                for(;i<=parent_node->n;++i){
                    if(parent_node->child[i] == node_off) break;
                }
                off_t sibling_off = (i == 0) ? parent_node->child[1] : parent_node->child[i-1];
                BNode *sibling_node = (BNode*)get_page(pool,sibling_off);

                if(sibling_node->n > DEGREE){ //redistribution
                    if(i == 0){ //sibling at right hand
                        ++nodeptr->n;
                        nodeptr->row_ptr[nodeptr->n-1] = sibling_node->row_ptr[0];
                        delete_handler(parent_node->row_ptr[0]);
                        parent_node->row_ptr[0] = insert_handler(sibling_node->row_ptr[1]);
                        --sibling_node->n;
                        memmove(sibling_node->row_ptr, sibling_node->row_ptr + 1, (sibling_node->n)*sizeof(RID));
                    }
                    else{ //sibling at left hand
                        memmove(nodeptr->row_ptr + 1, nodeptr->row_ptr, (nodeptr->n)*sizeof(RID));
                        ++nodeptr->n;
                        nodeptr->row_ptr[0] = sibling_node->row_ptr[sibling_node->n-1];
                        delete_handler(parent_node->row_ptr[i-1]);
                        parent_node->row_ptr[i-1] = insert_handler(sibling_node->row_ptr[sibling_node->n-1]);
                        --sibling_node->n;
                    }
                    get_rid_block_addr(*oldchildentry) = -1;
                }
                else{ //merge N and sibling_node
                    if(i==0){ //sibling at right hand
                        *oldchildentry = parent_node->row_ptr[0];
                        //nodeptr->row_ptr[nodeptr->n] = parent_node->row_ptr[0];
                        memcpy(nodeptr->row_ptr + nodeptr->n, sibling_node->row_ptr, (sibling_node->n)*sizeof(RID));
                        nodeptr->n += sibling_node->n;
                        nodeptr->next = sibling_node->next;
                        sibling_node->next = controlblock->free_node_head;
                        controlblock->free_node_head = sibling_off;
                    }
                    else{ //sibling at left hand
                        *oldchildentry = parent_node->row_ptr[i-1];
                        //sibling_node->row_ptr[sibling_node->n] = parent_node->row_ptr[i-1];

                        memcpy(sibling_node->row_ptr + sibling_node->n, nodeptr->row_ptr, (nodeptr->n)*sizeof(RID));
                        //memcpy(sibling_node->child + sibling_node->n + 1, nodeptr->child, (nodeptr->n+1)*sizeof(off_t));
                        sibling_node->n += nodeptr->n;
                        sibling_node->next = nodeptr->next;
                        nodeptr->next = controlblock->free_node_head;
                        controlblock->free_node_head = node_off;
                    }
                }
                release(pool,sibling_off);
                release(pool,parent_node_off);
            }
            release(pool,0); //release controlblock
        }
        release(pool,node_off);
        return;
    }
    else{ //non-leaf node
        if(cmp(rid, nodeptr->row_ptr[0]) < 0){
            off_t temp = nodeptr->child[0];
            release(pool,node_off);
            inside_delete(pool,node_off,temp,rid,oldchildentry,cmp,insert_handler,delete_handler);
        }
        else if(cmp(rid, nodeptr->row_ptr[nodeptr->n-1]) >= 0){
            off_t temp = nodeptr->child[nodeptr->n];
            release(pool,node_off);
            inside_delete(pool,node_off,temp,rid,oldchildentry,cmp,insert_handler,delete_handler);
        }
        else{
            for(int i=0;i<nodeptr->n-1;++i){
                if(cmp(rid, nodeptr->row_ptr[i]) >= 0 && cmp(rid, nodeptr->row_ptr[i+1]) < 0){
                    off_t temp = nodeptr->child[i+1];
                    release(pool,node_off);
                    inside_delete(pool,node_off,temp,rid,oldchildentry,cmp,insert_handler,delete_handler);
                    break;
                }
            }
        }
        if(get_rid_block_addr(*oldchildentry) == -1){
            return;
        }
        else{
            nodeptr = (BNode*)get_page(pool,node_off); //N
            int i = 0;
            for(;i<nodeptr->n;++i){
                if(cmp(nodeptr->row_ptr[i],*oldchildentry) == 0){
                    break;
                }
            }
            delete_handler(nodeptr->row_ptr[i]);
            memmove(nodeptr->row_ptr + i, nodeptr->row_ptr + i + 1, (nodeptr->n - i - 1)*sizeof(RID));
            memmove(nodeptr->child + i + 1, nodeptr->child + i + 2, (nodeptr->n - i - 1)*sizeof(off_t));
            --nodeptr->n;
            if(nodeptr->n >= DEGREE){
                get_rid_block_addr(*oldchildentry) = -1;
            }
            else{
                BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
                if(node_off == controlblock->root_node){ //N is root
                    if(nodeptr->n == 0){
                        controlblock->root_node = nodeptr->child[0];
                        nodeptr->next = controlblock->free_node_head;
                        controlblock->free_node_head = node_off;
                    }
                }
                else{
                    BNode *parent_node = (BNode*)get_page(pool,parent_node_off);
                    int i = 0;
                    for(;i<=parent_node->n;++i){
                        if(parent_node->child[i] == node_off) break;
                    }
                    off_t sibling_off = (i == 0) ? parent_node->child[1] : parent_node->child[i-1];
                    BNode *sibling_node = (BNode*)get_page(pool,sibling_off);
                    if(sibling_node->n > DEGREE){ //redistribution
                        if(i == 0){ //sibling at right hand
                            ++nodeptr->n;
                            nodeptr->row_ptr[nodeptr->n-1] = parent_node->row_ptr[0];
                            parent_node->row_ptr[0] = sibling_node->row_ptr[0];
                            nodeptr->child[nodeptr->n] = sibling_node->child[0];
                            --sibling_node->n;
                            memmove(sibling_node->row_ptr, sibling_node->row_ptr + 1, (sibling_node->n)*sizeof(RID));
                            memmove(sibling_node->child, sibling_node->child + 1, (sibling_node->n+1)*sizeof(off_t));
                        }
                        else{ //sibling at left hand
                            memmove(nodeptr->row_ptr + 1, nodeptr->row_ptr, nodeptr->n*sizeof(RID));
                            memmove(nodeptr->child + 1, nodeptr->child, (nodeptr->n+1)*sizeof(off_t));
                            ++nodeptr->n;
                            nodeptr->row_ptr[0] = parent_node->row_ptr[i-1];
                            parent_node->row_ptr[i-1] = sibling_node->row_ptr[sibling_node->n-1];
                            nodeptr->child[0] = sibling_node->child[sibling_node->n];
                            --sibling_node->n;
                        }
                        get_rid_block_addr(*oldchildentry) = -1;
                    }
                    else{ //merge N and sibling_node
                        if(i==0){ //sibling at right hand
                            *oldchildentry = parent_node->row_ptr[0];
                            nodeptr->row_ptr[nodeptr->n] = insert_handler(parent_node->row_ptr[0]);
                            memcpy(nodeptr->row_ptr + nodeptr->n + 1, sibling_node->row_ptr, (sibling_node->n)*sizeof(RID));
                            memcpy(nodeptr->child + nodeptr->n + 1, sibling_node->child, (sibling_node->n+1)*sizeof(off_t));
                            nodeptr->n += (1 + sibling_node->n);
                            sibling_node->next = controlblock->free_node_head;
                            controlblock->free_node_head = sibling_off;
                        }
                        else{ //sibling at left hand
                            *oldchildentry = parent_node->row_ptr[i-1];
                            sibling_node->row_ptr[sibling_node->n] = insert_handler(parent_node->row_ptr[i-1]);
                            memcpy(sibling_node->row_ptr + sibling_node->n + 1, nodeptr->row_ptr, (nodeptr->n)*sizeof(RID));
                            memcpy(sibling_node->child + sibling_node->n + 1, nodeptr->child, (nodeptr->n+1)*sizeof(off_t));
                            sibling_node->n += (1 + nodeptr->n);
                            nodeptr->next = controlblock->free_node_head;
                            controlblock->free_node_head = node_off;
                        }
                    }
                    release(pool,sibling_off);
                    release(pool,parent_node_off);
                }
                release(pool,0); //release controlblock
            }
            release(pool,node_off);//release N
            return;
        }
    }
}

void b_tree_delete(BufferPool *pool, RID rid, b_tree_row_row_cmp_t cmp, b_tree_insert_nonleaf_handler_t insert_handler, b_tree_delete_nonleaf_handler_t delete_handler) {
    BCtrlBlock *controlblock = (BCtrlBlock*)get_page(pool,0);
    off_t root_off = controlblock->root_node;
    release(pool,0);
    RID *oldchildentry;
    RID oldchildentry_rid;
    get_rid_block_addr(oldchildentry_rid) = -1;
    oldchildentry = &oldchildentry_rid;
    inside_delete(pool,-1,root_off,rid,oldchildentry,cmp,insert_handler,delete_handler);
}
