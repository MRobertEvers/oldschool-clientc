#ifndef RSCACHE_RSCACHEDAT1A_CONFIGIDK_H
#define RSCACHE_RSCACHEDAT1A_CONFIGIDK_H
#include <stdbool.h>
// Identity Kit.
// type: number = -1;
// models: Int32Array | null = null;
// recol_s: Int32Array = new Int32Array(6);
// recol_d: Int32Array = new Int32Array(6);
// heads: Int32Array = new Int32Array(5).fill(-1);
// disable: boolean = false;
struct RSCacheDat1A_ConfigIdk
{
    int type;
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
    bool disable;
};

struct RSCacheDat1A_ConfigIdkList
{
    struct RSCacheDat1A_ConfigIdk** idks;
    int idks_count;
};

struct RSCacheDat1A_ConfigIdkList*
RSCacheDat1A_ConfigIdkListNewDecode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size);

void
RSCacheDat1A_ConfigIdkFree(struct RSCacheDat1A_ConfigIdk* idk);

#endif