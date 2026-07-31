/*
 * dbtable column types for the CS2 decompiler.
 *
 * `RSCache_CS2_DecompileOptions.db_columns` asks what fields a dbcolumn
 * selects, because `db_getfield` pushes one value per field and `db_find` pops
 * a search value on the field's own stack. The answer is in config group 39
 * (DBTABLE), so it needs an open cache — which is why the library takes it as a
 * hook rather than reading it itself.
 *
 * Every tool that decompiles from a cache needs exactly this, so it lives here
 * rather than in each of them: two readers of the same config would eventually
 * disagree about what a column's types are, and the symptom would be a script
 * that decompiles in one tool and not the other.
 */
#ifndef RSCACHE_TOOLS_CS2_DB_COLUMNS_H
#define RSCACHE_TOOLS_CS2_DB_COLUMNS_H

#include "cs2/cs2_decompile.h"
#include "dat2disk.h"

struct ToolDbColumns;

/**
 * Decode every dbtable in `disk`. Returns NULL when the cache has no dbtable
 * group, which is not an error — pre-2022 caches have none, and their scripts
 * use no DB_* opcode either.
 */
struct ToolDbColumns*
tool_db_columns_load(struct RSCache_Dat2Disk* disk);

void
tool_db_columns_free(struct ToolDbColumns* columns);

/** Matches RSCache_CS2_DbColumnTypes.load; pass the handle as `user`. */
int
tool_db_columns_lookup(
    void* user,
    int table_id,
    int column_id,
    enum RSCache_CS2_Type* out,
    int capacity);

#endif
