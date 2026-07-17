#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_IDK_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_IDK_H
#include <stdbool.h>
// Identity Kit.
// type: number = -1;
// models: Int32Array | null = null;
// recol_s: Int32Array = new Int32Array(6);
// recol_d: Int32Array = new Int32Array(6);
// heads: Int32Array = new Int32Array(5).fill(-1);
// disable: boolean = false;
struct RSCache_Dat1ConfigIdk
{
    int type;
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
    bool disable;
};

struct RSCache_Dat1ConfigIdkList
{
    struct RSCache_Dat1ConfigIdk** idks;
    int idks_count;
};

struct RSCache_Dat1ConfigIdkList*
RSCache_Dat1ConfigIdkListNewDecode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size);

void
RSCache_Dat1ConfigIdkFree(struct RSCache_Dat1ConfigIdk* idk);

#endif