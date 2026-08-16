#include "dat2_config_db.h"

#include <stdlib.h>
#include <string.h>

bool
RSCache_DbTypeIsString(int type_code)
{
    return type_code == 36;
}

/* Reads one column's value block: a tuple count followed by
 * tuple_count * type_count typed fields. */
static struct RSCache_DbValue*
read_column_fields(
    struct RSCache_Buffer* buf,
    const int* types,
    int type_count,
    int* out_tuple_count)
{
    int tuple_count = RSCache_BufferReadUnsignedShortSmart(buf);
    int total;
    struct RSCache_DbValue* values;

    *out_tuple_count = tuple_count;
    if( tuple_count <= 0 || type_count <= 0 )
        return NULL;

    total = tuple_count * type_count;
    values = calloc((size_t)total, sizeof(*values));
    if( !values )
    {
        *out_tuple_count = 0;
        return NULL;
    }

    for( int t = 0; t < tuple_count; t++ )
    {
        for( int i = 0; i < type_count; i++ )
        {
            struct RSCache_DbValue* v = &values[(t * type_count) + i];
            if( RSCache_DbTypeIsString(types[i]) )
            {
                v->is_string = true;
                v->string_value = RSCache_BufferReadStringNullTerminated(buf);
            }
            else
            {
                v->is_string = false;
                v->int_value = RSCache_BufferG4(buf);
            }
        }
    }
    return values;
}

static void
free_column_inplace(struct RSCache_DbColumn* col)
{
    if( !col )
        return;
    if( col->values )
    {
        int total = col->tuple_count * col->type_count;
        for( int i = 0; i < total; i++ )
            free(col->values[i].string_value);
        free(col->values);
        col->values = NULL;
    }
    free(col->types);
    col->types = NULL;
    col->type_count = 0;
    col->tuple_count = 0;
    col->present = false;
}

/* Shared column loop for both DBROW (is_table=false: every column carries its
 * values) and DBTABLE (is_table=true: only columns flagged 0x80 carry default
 * values). The list is terminated by a 0xff column id. */
static void
decode_columns(
    struct RSCache_Buffer* buf,
    struct RSCache_DbColumn** cols_out,
    int* count_out,
    bool is_table)
{
    int alloc = RSCache_BufferG1(buf);
    struct RSCache_DbColumn* cols;

    if( alloc <= 0 )
    {
        *cols_out = NULL;
        *count_out = 0;
        return;
    }

    cols = calloc((size_t)alloc, sizeof(*cols));
    if( !cols )
    {
        *cols_out = NULL;
        *count_out = 0;
        return;
    }

    for( ;; )
    {
        int col = RSCache_BufferG1(buf);
        bool has_values = true;
        int type_count;
        int* types;
        struct RSCache_DbColumn scratch = { 0 };
        struct RSCache_DbColumn* dst;

        if( col == 0xff )
            break;

        if( is_table )
        {
            has_values = (col & 0x80) != 0;
            col &= 0x7f;
        }

        type_count = RSCache_BufferG1(buf);
        types = type_count > 0 ? malloc((size_t)type_count * sizeof(int)) : NULL;
        for( int i = 0; i < type_count; i++ )
            types[i] = RSCache_BufferReadUnsignedShortSmart(buf);

        /* An out-of-range column id would be malformed; decode into a scratch
         * column so the value block is still consumed and the stream stays
         * aligned, then discard it. */
        dst = (col >= 0 && col < alloc) ? &cols[col] : &scratch;
        dst->present = true;
        dst->type_count = type_count;
        dst->types = types;
        dst->tuple_count = 0;
        dst->values = NULL;
        if( has_values )
            dst->values = read_column_fields(buf, types, type_count, &dst->tuple_count);

        if( dst == &scratch )
            free_column_inplace(&scratch);
    }

    *cols_out = cols;
    *count_out = alloc;
}

void
RSCache_Dat2ConfigDbRowDecodeInplace(
    struct RSCache_Dat2ConfigDbRow* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    entry->table_id = -1;
    entry->column_count = 0;
    entry->columns = NULL;
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = RSCache_BufferG1(&buf);
        if( opcode == 0 )
            break;
        if( opcode == 3 )
            decode_columns(&buf, &entry->columns, &entry->column_count, false);
        else if( opcode == 4 )
            entry->table_id = RSCache_BufferReadVarInt2(&buf);
    }
}

void
RSCache_Dat2ConfigDbRowFreeInplace(struct RSCache_Dat2ConfigDbRow* entry)
{
    if( !entry )
        return;
    if( entry->columns )
    {
        for( int i = 0; i < entry->column_count; i++ )
            free_column_inplace(&entry->columns[i]);
        free(entry->columns);
        entry->columns = NULL;
    }
    entry->column_count = 0;
    entry->table_id = -1;
}

void
RSCache_Dat2ConfigDbTableDecodeInplace(
    struct RSCache_Dat2ConfigDbTable* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    entry->column_count = 0;
    entry->columns = NULL;
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = RSCache_BufferG1(&buf);
        if( opcode == 0 )
            break;
        if( opcode == 1 )
            decode_columns(&buf, &entry->columns, &entry->column_count, true);
    }
}

void
RSCache_Dat2ConfigDbTableFreeInplace(struct RSCache_Dat2ConfigDbTable* entry)
{
    if( !entry )
        return;
    if( entry->columns )
    {
        for( int i = 0; i < entry->column_count; i++ )
            free_column_inplace(&entry->columns[i]);
        free(entry->columns);
        entry->columns = NULL;
    }
    entry->column_count = 0;
}

void
RSCache_Dat2DbIndexFileDecodeInplace(
    struct RSCache_DbIndexFile* entry,
    int file_id,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    entry->file_id = file_id;
    entry->tuple_size = 0;
    entry->tuples = NULL;
    if( !data || data_size <= 0 )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    entry->tuple_size = RSCache_BufferReadVarInt2(&buf);
    if( entry->tuple_size <= 0 )
    {
        entry->tuple_size = 0;
        return;
    }
    entry->tuples = calloc((size_t)entry->tuple_size, sizeof(*entry->tuples));
    if( !entry->tuples )
    {
        entry->tuple_size = 0;
        return;
    }

    for( int tp = 0; tp < entry->tuple_size; tp++ )
    {
        struct RSCache_DbIndexTuple* tuple = &entry->tuples[tp];
        tuple->base_type = RSCache_BufferG1(&buf);
        tuple->entry_count = RSCache_BufferReadVarInt2(&buf);
        if( tuple->entry_count <= 0 )
        {
            tuple->entry_count = 0;
            continue;
        }
        tuple->entries = calloc((size_t)tuple->entry_count, sizeof(*tuple->entries));
        if( !tuple->entries )
        {
            tuple->entry_count = 0;
            continue;
        }
        for( int e = 0; e < tuple->entry_count; e++ )
        {
            struct RSCache_DbIndexEntry* ent = &tuple->entries[e];
            switch( tuple->base_type )
            {
            case RSCACHE_DB_BASE_STRING:
                ent->string_value = RSCache_BufferReadStringNullTerminated(&buf);
                break;
            case RSCACHE_DB_BASE_LONG:
                ent->long_value = RSCache_BufferG8(&buf);
                break;
            default:
                ent->int_value = RSCache_BufferG4(&buf);
                break;
            }
            ent->row_count = RSCache_BufferReadVarInt2(&buf);
            if( ent->row_count > 0 )
            {
                ent->row_ids = malloc((size_t)ent->row_count * sizeof(int));
                for( int r = 0; r < ent->row_count; r++ )
                    ent->row_ids[r] = RSCache_BufferReadVarInt2(&buf);
            }
        }
    }
}

void
RSCache_Dat2DbIndexFileFreeInplace(struct RSCache_DbIndexFile* entry)
{
    if( !entry )
        return;
    for( int tp = 0; tp < entry->tuple_size; tp++ )
    {
        struct RSCache_DbIndexTuple* tuple = &entry->tuples[tp];
        for( int e = 0; e < tuple->entry_count; e++ )
        {
            free(tuple->entries[e].string_value);
            free(tuple->entries[e].row_ids);
        }
        free(tuple->entries);
    }
    free(entry->tuples);
    entry->tuples = NULL;
    entry->tuple_size = 0;
}

/* ------------------------------------------------------------------ */
/* Encoders                                                            */
/* ------------------------------------------------------------------ */

/** Bytes a column's own header and value block can take, an upper bound. */
static uint32_t
column_bound(const struct RSCache_DbColumn* col)
{
    uint32_t need = 1 + 1 + (uint32_t)col->type_count * 3; /* id, count, smart types */

    need += 3; /* tuple_count, as a smart */
    for( int t = 0; t < col->tuple_count; t++ )
    {
        for( int i = 0; i < col->type_count; i++ )
        {
            const struct RSCache_DbValue* v = &col->values[(t * col->type_count) + i];

            if( v->is_string )
                need += (uint32_t)(v->string_value ? strlen(v->string_value) : 0) + 1;
            else
                need += 4;
        }
    }
    return need;
}

static uint32_t
columns_bound(const struct RSCache_DbColumn* cols, int count)
{
    uint32_t need = 1 + 1; /* alloc, 0xff terminator */

    for( int c = 0; c < count; c++ )
    {
        if( cols[c].present )
            need += column_bound(&cols[c]);
    }
    return need;
}

/**
 * Inverse of `decode_columns`.
 *
 * Columns are emitted in ascending id, which is the order the decoder's
 * id-indexed array can express — it stores a column *at* its id, so the order the
 * source listed them in is not recoverable from the struct. Every record in
 * cache.osrs239 round-trips byte-exactly under that reading, which is what makes
 * it safe rather than merely plausible.
 */
static bool
encode_columns(
    struct RSCache_Buffer* buf,
    const struct RSCache_DbColumn* cols,
    int count,
    bool is_table)
{
    RSCache_BufferP1(buf, count);
    for( int c = 0; c < count; c++ )
    {
        const struct RSCache_DbColumn* col = &cols[c];
        bool has_values;

        if( !col->present )
            continue;
        has_values = col->values != NULL;
        /* Only a table encodes the flag; a row's columns always carry values. */
        RSCache_BufferP1(buf, is_table && has_values ? (c | 0x80) : c);
        RSCache_BufferP1(buf, col->type_count);
        for( int i = 0; i < col->type_count; i++ )
            RSCache_BufferWriteUnsignedShortSmart(buf, col->types[i]);

        if( is_table && !has_values )
            continue;
        RSCache_BufferWriteUnsignedShortSmart(buf, col->tuple_count);
        for( int t = 0; t < col->tuple_count; t++ )
        {
            for( int i = 0; i < col->type_count; i++ )
            {
                const struct RSCache_DbValue* v = &col->values[(t * col->type_count) + i];

                if( v->is_string )
                {
                    const char* s = v->string_value ? v->string_value : "";

                    for( ; *s; s++ )
                        RSCache_BufferP1(buf, (uint8_t)*s);
                    RSCache_BufferP1(buf, 0);
                }
                else
                {
                    RSCache_BufferP4(buf, v->int_value);
                }
            }
        }
    }
    RSCache_BufferP1(buf, 0xff);
    return true;
}

uint32_t
RSCache_Dat2ConfigDbRowEncodeBound(const struct RSCache_Dat2ConfigDbRow* entry)
{
    uint32_t need = 1 + 1 + 5; /* opcode 3, opcode 4 + varint2, terminator */

    if( !entry )
        return 1;
    if( entry->columns )
        need += columns_bound(entry->columns, entry->column_count);
    return need + 8;
}

uint32_t
RSCache_Dat2ConfigDbRowEncode(
    const struct RSCache_Dat2ConfigDbRow* entry,
    uint8_t* out,
    uint32_t capacity)
{
    struct RSCache_Buffer buf;

    if( !entry || !out )
        return 0;
    RSCache_BufferInit(&buf, out, capacity);

    /*
     * Opcode 4 before opcode 3 — the table id, then the columns.
     *
     * The decoder accepts either order, so this is not derivable from it; the
     * bytes are the only authority. Emitting 3 first produced a record of exactly
     * the right length with the two blocks transposed, which is invisible to any
     * semantic check and caught immediately by byte comparison: 15,418 of 16,711
     * rows differed, and the 1,293 that passed were the ones with no table id.
     */
    if( entry->table_id >= 0 )
    {
        RSCache_BufferP1(&buf, 4);
        RSCache_BufferWriteVarInt2(&buf, entry->table_id);
    }
    if( entry->columns && entry->column_count > 0 )
    {
        RSCache_BufferP1(&buf, 3);
        encode_columns(&buf, entry->columns, entry->column_count, false);
    }
    RSCache_BufferP1(&buf, 0);
    if( buf.position > capacity )
        return 0;
    return buf.position;
}

uint32_t
RSCache_Dat2ConfigDbTableEncodeBound(const struct RSCache_Dat2ConfigDbTable* entry)
{
    uint32_t need = 1 + 1; /* opcode 1, terminator */

    if( !entry )
        return 1;
    if( entry->columns )
        need += columns_bound(entry->columns, entry->column_count);
    return need + 8;
}

uint32_t
RSCache_Dat2ConfigDbTableEncode(
    const struct RSCache_Dat2ConfigDbTable* entry,
    uint8_t* out,
    uint32_t capacity)
{
    struct RSCache_Buffer buf;

    if( !entry || !out )
        return 0;
    RSCache_BufferInit(&buf, out, capacity);

    if( entry->columns && entry->column_count > 0 )
    {
        RSCache_BufferP1(&buf, 1);
        encode_columns(&buf, entry->columns, entry->column_count, true);
    }
    RSCache_BufferP1(&buf, 0);
    if( buf.position > capacity )
        return 0;
    return buf.position;
}
