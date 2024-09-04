#include "myjql.h"

#include "buffer_pool.h"
#include "b_tree.h"
#include "table.h"
#include "str.h"

#define MAX_STR_LEN 1024

typedef struct {
    RID key;
    RID value;
} Record;

// read a record from the table
void read_record(Table *table, RID rid, Record *record) {
    table_read(table, rid, (ItemPtr)record);
}

// write a record to the table and return the RID
RID write_record(Table *table, const Record *record) {
    return table_insert(table, (ItemPtr)record, sizeof(Record));
}

void delete_record(Table *table, RID rid) {
    table_delete(table, rid);
}

BufferPool bp_idx;
Table tbl_rec;
Table tbl_str;

int key_row_row_cmp(RID a, RID b) {
    Record kvp_record_a, kvp_record_b;
    read_record(&tbl_rec, a, &kvp_record_a);
    read_record(&tbl_rec, b, &kvp_record_b);
    StringRecord key_a, key_b;
    read_string(&tbl_str, kvp_record_a.key, &key_a);
    read_string(&tbl_str, kvp_record_b.key, &key_b);
    return compare_string_record(&tbl_str, &key_a, &key_b);
}

int key_ptr_row_cmp(void *p, size_t size, RID b) {
    Record kvp_record_b;
    read_record(&tbl_rec, b, &kvp_record_b);
    StringRecord key_b;
    read_string(&tbl_str, kvp_record_b.key, &key_b);
    return compare_string_string_record(&tbl_str, (char*)p, size, &key_b);
}

RID insert_handler(RID rid) {
    Record new_kvp_record;
    Record kvp_record;
    read_record(&tbl_rec, rid, &kvp_record);
    StringRecord key_to_copy;
    read_string(&tbl_str, kvp_record.key, &key_to_copy);
    char dest[MAX_STR_LEN];
    size_t len = load_string(&tbl_str, &key_to_copy, dest, MAX_STR_LEN + 1);
    new_kvp_record.key = write_string(&tbl_str, dest, len);
    return write_record(&tbl_rec, &new_kvp_record);
}

void delete_handler(RID rid) {
    Record kvp_record;
    read_record(&tbl_rec, rid, &kvp_record);
    delete_string(&tbl_str, kvp_record.key);
    delete_record(&tbl_rec, rid);
}

// display the structure of the B-Tree for debugging
void b_tree_display(){
    BufferPool *pool = &bp_idx;
    BCtrlBlock *ctrlblock = (BCtrlBlock*)get_page(pool,0);
    off_t queue[10000];
    int q_start = 0, q_end = 1;
    queue[0] = ctrlblock->root_node;
    release(pool,0);
    while(q_end != q_start){
        int len = q_end - q_start;
        for(int i = 0;i<len;++i){
            BNode *p = (BNode*)get_page(pool,queue[q_start]);
            printf("%c| ",p->leaf);
            for(int j=0;j<p->n;++j){
                Record r;
                printf("%d %d",get_rid_block_addr(p->row_ptr[j]),get_rid_idx(p->row_ptr[j]));
                read_record(&tbl_rec,p->row_ptr[j],&r);
                StringRecord sr;
                read_string(&tbl_str,r.key,&sr);
                print_string(&tbl_str,&sr);
                printf(" ");
            }
            printf("|   ");
            if(p->leaf != '1'){
                for(int j=0;j<=p->n;++j){
                    queue[q_end++] = p->child[j];
                }
            }
            release(pool,queue[q_start]);
            ++q_start;
        }
        printf("\n");
    }
}

// initialize the myjql system by setting up tables and B-Tree
void myjql_init() {
    b_tree_init("rec.idx", &bp_idx);
    table_init(&tbl_rec, "rec.data", "rec.fsm");
    table_init(&tbl_str, "str.data", "str.fsm");
}

void myjql_close() {
    /* validate_buffer_pool(&bp_idx);
    validate_buffer_pool(&tbl_rec.data_pool);
    validate_buffer_pool(&tbl_rec.fsm_pool);
    validate_buffer_pool(&tbl_str.data_pool);
    validate_buffer_pool(&tbl_str.fsm_pool); */
    b_tree_close(&bp_idx);
    table_close(&tbl_rec);
    table_close(&tbl_str);
}

// retrieve a value for a given key from the database
size_t myjql_get(const char *key, size_t key_len, char *value, size_t max_size) {
    RID get_record_rid = b_tree_search(&bp_idx, key, key_len, key_ptr_row_cmp);
    if(get_rid_block_addr(get_record_rid) == -1) // not found
        return -1;
    // found: read from the table
    Record get_record;
    read_record(&tbl_rec, get_record_rid, &get_record);
    StringRecord value_record;
    read_string(&tbl_str, get_record.value, &value_record);
    return load_string(&tbl_str, &value_record, value, max_size); // load
}

void myjql_set(const char *key, size_t key_len, const char *value, size_t value_len) {
    RID get_record_rid = b_tree_search(&bp_idx, key, key_len, key_ptr_row_cmp);
    if(get_rid_block_addr(get_record_rid) == -1){ // the record does not exist: create one
        Record kvp_record;
        kvp_record.key = write_string(&tbl_str, key, key_len);
        kvp_record.value = write_string(&tbl_str, value, value_len);
        RID kvp_record_rid = write_record(&tbl_rec, &kvp_record);
        b_tree_insert(&bp_idx, kvp_record_rid, key_row_row_cmp, insert_handler);
    }
    else{
        // the record exists: read and update it
        Record get_record;
        read_record(&tbl_rec, get_record_rid, &get_record);
        b_tree_delete(&bp_idx, get_record_rid, key_row_row_cmp, insert_handler, delete_handler);
        delete_string(&tbl_str, get_record.value);
        get_record.value = write_string(&tbl_str, value, value_len);
        delete_record(&tbl_rec, get_record_rid);
        get_record_rid = write_record(&tbl_rec, &get_record);
        b_tree_insert(&bp_idx, get_record_rid, key_row_row_cmp, insert_handler);
    }
}

void myjql_del(const char *key, size_t key_len) {
    RID get_record_rid = b_tree_search(&bp_idx, key, key_len, key_ptr_row_cmp);
    if(get_rid_block_addr(get_record_rid) == -1)  // does not exist: do nothing
        return;
    b_tree_delete(&bp_idx, get_record_rid, key_row_row_cmp, insert_handler, delete_handler);
    Record kvp_record;
    read_record(&tbl_rec, get_record_rid, &kvp_record);
    delete_string(&tbl_str, kvp_record.key);
    delete_string(&tbl_str,kvp_record.value);
    delete_record(&tbl_rec, get_record_rid);
}

/* void myjql_analyze() {
    printf("Record Table:\n");
    analyze_table(&tbl_rec);
    printf("String Table:\n");
    analyze_table(&tbl_str);
} */