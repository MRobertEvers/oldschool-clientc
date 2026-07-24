#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_DB_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_DB_H

#include "../rsbuffer.h"

#include <stdbool.h>

/*
 * DBROW (config kind 38) and DBTABLE (config kind 39) — the client-side
 * database types read by the DB_* clientscript opcodes (7500..7510).
 *
 * A column holds `tuple_count` tuples, each of `type_count` typed fields, so
 * the flattened `values` array has `tuple_count * type_count` entries laid out
 * tuple-major (tuple 0's fields, then tuple 1's, ...). Each field's ScriptVarType
 * code decides whether it decodes as a 4-byte int or a null-terminated string.
 *
 * On a DBROW every listed column carries its values inline. On a DBTABLE a
 * column lists its types and, only when the 0x80 flag is set on the column id,
 * a block of default values in the same layout.
 */

struct RSCache_DbValue
{
    bool is_string;
    int int_value;
    char* string_value; /* owned; NULL for int fields */
};

struct RSCache_DbColumn
{
    bool present;    /* false => this column id was not listed */
    int type_count;  /* number of typed fields per tuple */
    int* types;      /* [type_count] ScriptVarType codes; owned */
    int tuple_count; /* number of tuples (0 when no value block) */
    struct RSCache_DbValue* values; /* [tuple_count * type_count]; owned; may be NULL */
};

struct RSCache_Dat2ConfigDbRow
{
    int id;
    int table_id;    /* -1 until opcode 4 sets it */
    int column_count;               /* size of `columns` (allocation count) */
    struct RSCache_DbColumn* columns; /* [column_count], indexed by column id */
};

struct RSCache_Dat2ConfigDbTable
{
    int id;
    int column_count;
    struct RSCache_DbColumn* columns; /* [column_count]; values hold defaults */
};

/* True when a ScriptVarType code decodes as a string rather than a 4-byte int.
 * Validated exhaustively against the osrs230/239/jan2026 caches, whose DB data
 * uses code 36 as the only string type (no long/8-byte base types appear). */
bool
RSCache_DbTypeIsString(int type_code);

void
RSCache_Dat2ConfigDbRowDecodeInplace(
    struct RSCache_Dat2ConfigDbRow* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigDbRowFreeInplace(struct RSCache_Dat2ConfigDbRow* entry);

void
RSCache_Dat2ConfigDbTableDecodeInplace(
    struct RSCache_Dat2ConfigDbTable* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigDbTableFreeInplace(struct RSCache_Dat2ConfigDbTable* entry);

/*
 * DBTABLEINDEX (cache table 21) — one archive per table id. Within an archive,
 * file 0 is the master index (every row id in the table) and file N (N>=1) is
 * the inverted index for column id N-1. Each index file body maps, per tuple
 * position, an indexed value to the list of row ids carrying it. DB_FIND reads
 * a column file; DB_FINDALL reads the master file. All counts are readVarInt2.
 */

enum RSCache_DbBaseType
{
    RSCACHE_DB_BASE_INT = 0,
    RSCACHE_DB_BASE_LONG = 1,
    RSCACHE_DB_BASE_STRING = 2,
};

struct RSCache_DbIndexEntry
{
    /* The indexed value, interpreted per the tuple position's base type. */
    int int_value;
    int64_t long_value;
    char* string_value; /* owned; NULL unless base type is string */
    int* row_ids;       /* owned; row ids carrying this value */
    int row_count;
};

struct RSCache_DbIndexTuple
{
    int base_type; /* enum RSCache_DbBaseType */
    struct RSCache_DbIndexEntry* entries;
    int entry_count;
};

/* One index file (master or a single column) for one table. */
struct RSCache_DbIndexFile
{
    int file_id;    /* 0 = master, N = column N-1 */
    int tuple_size; /* number of tuple positions indexed */
    struct RSCache_DbIndexTuple* tuples; /* [tuple_size] */
};

void
RSCache_Dat2DbIndexFileDecodeInplace(
    struct RSCache_DbIndexFile* entry,
    int file_id,
    const void* data,
    int data_size);

void
RSCache_Dat2DbIndexFileFreeInplace(struct RSCache_DbIndexFile* entry);

#endif
