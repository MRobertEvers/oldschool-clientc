#include "engine/torirs_db.h"

#include <stdlib.h>

struct RSCache_DbIndexFile*
ToriRS_DbTableIndex_Master(struct ToriRS_DbTableIndex const* idx)
{
    if( !idx )
        return NULL;
    for( int i = 0; i < idx->file_count; i++ )
        if( idx->file_ids[i] == 0 )
            return &idx->files[i];
    return NULL;
}

struct RSCache_DbIndexFile*
ToriRS_DbTableIndex_Column(
    struct ToriRS_DbTableIndex const* idx,
    int column_id)
{
    if( !idx || column_id < 0 )
        return NULL;
    /* Column N is stored in file N+1 (file 0 is the master). */
    for( int i = 0; i < idx->file_count; i++ )
        if( idx->file_ids[i] == column_id + 1 )
            return &idx->files[i];
    return NULL;
}

void
ToriRS_DbTableIndexFree(struct ToriRS_DbTableIndex* idx)
{
    if( !idx )
        return;
    for( int i = 0; i < idx->file_count; i++ )
        RSCache_Dat2DbIndexFileFreeInplace(&idx->files[i]);
    free(idx->files);
    free(idx->file_ids);
    free(idx);
}

void
ToriRS_DbRowFree(struct RSCache_Dat2ConfigDbRow* row)
{
    if( !row )
        return;
    RSCache_Dat2ConfigDbRowFreeInplace(row);
    free(row);
}

void
ToriRS_DbTableFree(struct RSCache_Dat2ConfigDbTable* table)
{
    if( !table )
        return;
    RSCache_Dat2ConfigDbTableFreeInplace(table);
    free(table);
}
