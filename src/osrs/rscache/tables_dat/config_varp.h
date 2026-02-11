#ifndef CONFIG_VARP_H
#define CONFIG_VARP_H

#include <stdbool.h>

struct CacheDatConfigVarp
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

struct CacheDatConfigVarpList
{
    struct CacheDatConfigVarp** varps;
    int varps_count;
    /* IDs of varps with code3=true (code3s array from VarpType) */
    int* code3_ids;
    int code3_count;
};

struct CacheDatConfigVarpList*
cache_dat_config_varp_list_new_decode(
    void* jagfile_varpdat_data,
    int jagfile_varpdat_data_size);

void
cache_dat_config_varp_list_free(struct CacheDatConfigVarpList* list);

#endif
