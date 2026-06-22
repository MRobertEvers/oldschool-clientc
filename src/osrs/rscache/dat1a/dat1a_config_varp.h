#ifndef RSCACHE_RSCACHEDAT1A_CONFIGVARP_H
#define RSCACHE_RSCACHEDAT1A_CONFIGVARP_H

#include <stdbool.h>

/**
 * The format of the varp changes in 254.
 *
 */
struct RSCacheDat1A_ConfigVarp
{
    int code1;
    int code2;
    bool code3;
    bool code4;
    int clientcode;
    int code7;
    bool code6;
    bool code8;
    bool code11;
    char* debugname;
};

struct RSCacheDat1A_ConfigVarpList
{
    struct RSCacheDat1A_ConfigVarp** varps;
    int varps_count;
    /* IDs of varps with code3=true (code3s array from VarpType) */
    int* code3_ids;
    int code3_count;
};

struct RSCacheDat1A_ConfigVarpList*
RSCacheDat1A_ConfigVarpListNewDecode(
    void* jagfile_varpdat_data,
    int jagfile_varpdat_data_size);

void
RSCacheDat1A_ConfigVarpListFree(struct RSCacheDat1A_ConfigVarpList* list);

#endif
