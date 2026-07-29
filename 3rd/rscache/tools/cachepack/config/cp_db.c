#include "cachepack.h"

#include "datatypes/dat2_config_db.h"

#include <stdlib.h>
#include <string.h>

/*
 * The client database: dbtable (config group 39) declares columns and their types,
 * dbrow (group 38) carries one record's values. Nothing in rev 254 resembles them —
 * they are the backing store for the modern collection log, drop tables and the
 * like, read from CS2 through the DB_* opcodes.
 *
 * **Unpack only.** rscache decodes both and encodes neither, so the register marks
 * them CP_TYPE_NO_ENCODER and `pack` leaves the source cache's own bytes in place
 * rather than writing something it cannot reproduce. The text is still worth
 * having: it is the only readable view of what a table declares and what a row
 * holds, which is what makes a DB_* script legible.
 */

static void
emit_column(
    struct CP_Lines* out,
    const char* prefix,
    int column_id,
    const struct RSCache_DbColumn* column)
{
    if( !column->present )
        return;

    char buf[8192];
    int w = snprintf(buf, sizeof(buf), "%d:", column_id);
    for( int i = 0; i < column->type_count; i++ )
        w += snprintf(buf + w, sizeof(buf) - (size_t)w, i ? ",%d" : "%d", column->types[i]);
    cp_lines_addf(out, "%stypes=%s", prefix, buf);

    for( int t = 0; t < column->tuple_count; t++ )
    {
        w = snprintf(buf, sizeof(buf), "%d:%d:", column_id, t);
        for( int f = 0; f < column->type_count; f++ )
        {
            const struct RSCache_DbValue* value = &column->values[t * column->type_count + f];
            if( w >= (int)sizeof(buf) - 32 )
                break;
            if( value->is_string )
                w += snprintf(buf + w, sizeof(buf) - (size_t)w, f ? ",%s" : "%s",
                              value->string_value ? value->string_value : "");
            else
                w += snprintf(buf + w, sizeof(buf) - (size_t)w, f ? ",%d" : "%d",
                              value->int_value);
        }
        cp_lines_add_str(out, prefix[0] ? "defaults" : "values", buf);
    }
}

int
cp_unpack_dbrow(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigDbRow entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbRowDecodeInplace(&entry, record, record_size);

    cp_emit_ref(ctx, out, "table", CP_TYPE_DBTABLE, entry.table_id, -1);
    for( int c = 0; c < entry.column_count; c++ )
        emit_column(out, "", c, &entry.columns[c]);

    RSCache_Dat2ConfigDbRowFreeInplace(&entry);
    return 1;
}

int
cp_unpack_dbtable(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigDbTable entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbTableDecodeInplace(&entry, record, record_size);

    for( int c = 0; c < entry.column_count; c++ )
        emit_column(out, "default", c, &entry.columns[c]);

    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return 1;
}

/*
 * Both packers refuse rather than approximate.
 *
 * A dbrow's on-disk layout interleaves a column bitmap with per-column type lists
 * whose widths depend on the table's declaration, and the library has no encoder
 * for it. Writing one from the decoder's struct would be an unvalidated guess at a
 * format nothing in the repo can check — and a wrong dbrow does not fail loudly,
 * it feeds a CS2 script a plausible value from the wrong column.
 */
static uint32_t
refuse(
    struct CP_Ctx* ctx,
    const char* type,
    const struct CP_Config* config)
{
    fprintf(
        stderr,
        "cachepack: %s [%s]: rscache has no %s encoder — the source cache's records are "
        "kept unchanged\n",
        type,
        config->debugname,
        type);
    return 0;
}

uint32_t
cp_pack_dbrow(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    return refuse(ctx, "dbrow", config);
}

uint32_t
cp_pack_dbtable(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    return refuse(ctx, "dbtable", config);
}
