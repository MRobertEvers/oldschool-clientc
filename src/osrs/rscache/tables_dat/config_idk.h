#ifndef CONFIG_IDK_H
#define CONFIG_IDK_H
#include <stdbool.h>
// Identity Kit.
// type: number = -1;
// models: Int32Array | null = null;
// recol_s: Int32Array = new Int32Array(6);
// recol_d: Int32Array = new Int32Array(6);
// heads: Int32Array = new Int32Array(5).fill(-1);
// disable: boolean = false;
struct CacheDatConfigIdk
{
    int type;
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
    bool disable;
};

struct CacheDatConfigIdkList
{
    struct CacheDatConfigIdk** idks;
    int idks_count;
};

struct CacheDatConfigIdkList*
cache_dat_config_idk_list_new_decode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size);

#endif