#include "str.h"
#include "table.h"
#include "string.h"

// Reads a string record from the table into the given record structure
void read_string(Table *table, RID rid, StringRecord *record) {
    record->idx = 0;  // Initialize the index to 0
    StringChunk *chunk = &(record->chunk);
    table_read(table, rid, (ItemPtr)chunk);  // Read the chunk from the table
}

// Checks if there are more characters to read in the string record
int has_next_char(StringRecord *record) {
    StringChunk *chunk = &(record->chunk);
    // If the current index is beyond the chunk size, check the next chunk RID
    if (record->idx >= get_str_chunk_size(chunk)) {
        RID chunk_rid = get_str_chunk_rid(chunk);
        if (get_rid_block_addr(chunk_rid) == (off_t)(-1)) {
            return 0;  // No more characters
        }
    }
    return 1;  // There are more characters
}

// Reads the next character from the string record
char next_char(Table *table, StringRecord *record) {
    StringChunk *chunk = &(record->chunk);
    char res_c;
    if (record->idx >= get_str_chunk_size(chunk)) {
        // Move to the next chunk if the current index exceeds the chunk size
        RID next_chunk_rid = get_str_chunk_rid(chunk);
        read_string(table, next_chunk_rid, record);
        res_c = get_str_chunk_data_ptr(chunk)[record->idx];
        ++record->idx;
    } else {
        // Read the character from the current chunk
        res_c = get_str_chunk_data_ptr(chunk)[record->idx];
        ++record->idx;
    }
    return res_c;
}

// Compares two string records lexicographically
// Returns 1 if a > b, -1 if a < b, 0 if a == b
int compare_string_record(Table *table, const StringRecord *a, const StringRecord *b) {
    StringRecord sra = *a;
    StringRecord srb = *b;
    while (1) {
        int has_nextchar_a = has_next_char(&sra);
        int has_nextchar_b = has_next_char(&srb);
        if (has_nextchar_a && has_nextchar_b) {
            char a_c = next_char(table, &sra);
            char b_c = next_char(table, &srb);
            if (a_c > b_c) return 1;
            else if (a_c < b_c) return -1;
            else continue;
        } else if (has_nextchar_a && !has_nextchar_b) {
            return 1;
        } else if (!has_nextchar_a && has_nextchar_b) {
            return -1;
        } else return 0;
    }
}

// Compares a string with a string record lexicographically
// Returns 1 if a > b, -1 if a < b, 0 if a == b
int compare_string_string_record(Table *table, char *a, size_t size, const StringRecord *b) {
    int i = 0;
    StringRecord srb = *b;
    while (1) {
        int has_nextchar_a = (i < size) ? 1 : 0;
        int has_nextchar_b = has_next_char(&srb);
        if (has_nextchar_a && has_nextchar_b) {
            char a_c = a[i++];
            char b_c = next_char(table, &srb);
            if (a_c > b_c) return 1;
            else if (a_c < b_c) return -1;
            else continue;
        } else if (has_nextchar_a && !has_nextchar_b) {
            return 1;
        } else if (!has_nextchar_a && has_nextchar_b) {
            return -1;
        } else return 0;
    }
}

// Writes a string to the table and returns the RID of the last chunk
RID write_string(Table *table, const char *data, off_t size) {
    RID pre_rid;
    get_rid_block_addr(pre_rid) = (off_t)(-1);  // Initialize to an invalid address
    get_rid_idx(pre_rid) = (short)(0);
    if (size == 0) {
        // Handle empty string case
        StringChunk newchunk;
        get_str_chunk_rid(&newchunk) = pre_rid;
        get_str_chunk_size(&newchunk) = 0;
        pre_rid = table_insert(table, (ItemPtr)&newchunk, calc_str_chunk_size(0));
    }
    int rest = size % STR_CHUNK_MAX_LEN;
    if (rest != 0) {
        // Handle the remainder chunk
        StringChunk newchunk;
        get_str_chunk_rid(&newchunk) = pre_rid;
        get_str_chunk_size(&newchunk) = rest;
        memmove((get_str_chunk_data_ptr(&newchunk)), data + (size - rest), rest);
        pre_rid = table_insert(table, (ItemPtr)&newchunk, (short)calc_str_chunk_size(rest));
        size -= rest;
    }
    while (size != 0) {
        // Handle full chunks
        StringChunk newchunk;
        get_str_chunk_rid(&newchunk) = pre_rid;
        get_str_chunk_size(&newchunk) = STR_CHUNK_MAX_LEN;
        memmove((get_str_chunk_data_ptr(&newchunk)), data + (size - STR_CHUNK_MAX_LEN), STR_CHUNK_MAX_LEN);
        pre_rid = table_insert(table, (ItemPtr)&newchunk, (short)sizeof(newchunk));
        size -= STR_CHUNK_MAX_LEN;
    }
    return pre_rid;
}

// Deletes a string from the table
void delete_string(Table *table, RID rid) {
    RID next_chunk_rid = rid;
    while (get_rid_block_addr(next_chunk_rid) != -1) {
        StringChunk chunk;
        table_read(table, next_chunk_rid, (ItemPtr)&chunk);
        table_delete(table, next_chunk_rid);
        next_chunk_rid = get_str_chunk_rid(&chunk);
    }
}

// Prints a string record to the console
void print_string(Table *table, const StringRecord *record) {
    StringRecord rec = *record;
    printf("\"");
    while (has_next_char(&rec)) {
        printf("%c", next_char(table, &rec));
    }
    printf("\"");
}

// Loads a string from the table into a destination buffer
size_t load_string(Table *table, const StringRecord *record, char *dest, size_t max_size) {
    StringRecord str_record = *record;
    size_t res_size = 0;
    while (has_next_char(&str_record) && res_size < max_size) {
        dest[res_size++] = next_char(table, &str_record);
    }
    return res_size;
}

/* void chunk_printer(ItemPtr item, short item_size) {
    if (item == NULL) {
        printf("NULL");
        return;
    }
    StringChunk *chunk = (StringChunk*)item;
    short size = get_str_chunk_size(chunk), i;
    printf("StringChunk(");
    print_rid(get_str_chunk_rid(chunk));
    printf(", %d, \"", size);
    for (i = 0; i < size; i++) {
        printf("%c", get_str_chunk_data_ptr(chunk)[i]);
    }
    printf("\")");
} */