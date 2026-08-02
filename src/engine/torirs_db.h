#ifndef TORIRS_DB_H
#define TORIRS_DB_H

#include <rscache.h>

/*
 * Engine-side wrappers for the client database (DB_* opcodes 7500..7510).
 *
 * A DBROW is cached as the decoded rscache struct directly — it already owns
 * its column types and values. A table's DBTABLEINDEX (cache table 21) is a
 * bundle of index files: file 0 is the master (all row ids), file N (N>=1) is
 * the inverted index for column id N-1.
 *
 * A DBTABLE (config kind 39) is cached the same way as a DBROW. It is the only
 * place a column's *types* and its *default values* are stated: a DBROW lists
 * only the columns it sets, so a column the row omits has no type, no arity and
 * no value anywhere except on its table.
 */

struct ToriRS_DbTableIndex
{
    int table_id;
    struct RSCache_DbIndexFile* files; /* [file_count], parallel to file_ids */
    int* file_ids;                     /* cache file id for files[i] */
    int file_count;
};

/** Master index file (all row ids in the table), or NULL if absent. */
struct RSCache_DbIndexFile*
ToriRS_DbTableIndex_Master(struct ToriRS_DbTableIndex const* idx);

/** Inverted index file for a column id, or NULL if that column is not indexed. */
struct RSCache_DbIndexFile*
ToriRS_DbTableIndex_Column(
    struct ToriRS_DbTableIndex const* idx,
    int column_id);

void
ToriRS_DbTableIndexFree(struct ToriRS_DbTableIndex* idx);

void
ToriRS_DbRowFree(struct RSCache_Dat2ConfigDbRow* row);

void
ToriRS_DbTableFree(struct RSCache_Dat2ConfigDbTable* table);

#endif
