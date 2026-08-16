#ifndef DUMP_INTERFACE_COMMON_H
#define DUMP_INTERFACE_COMMON_H

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

enum
{
    DUMP_IFACE_FIXED_ROOT_W = 765,
    DUMP_IFACE_FIXED_ROOT_H = 503,
    DUMP_IFACE_INV_SLOT_MAX = 20
};

enum DumpIfaceCacheMode
{
    DUMP_IFACE_CACHE_DAT1 = 0,
    DUMP_IFACE_CACHE_DAT2 = 1
};

struct DumpIfaceLoaded
{
    RSCacheDat2A_Component* comps;
    int* parent_idx;
    int* lay_x;
    int* lay_y;
    int* lay_w;
    int* lay_h;
    int count;
    enum DumpIfaceCacheMode cache_mode;
    /** Dat1 only: parallel source widgets (for inv slot sprite names). */
    struct RSCacheDat1A_ConfigComponent** dat1_src;
    struct RSCacheDat1A_ConfigComponentList* dat1_list;
};

char const*
dump_iface_component_type_name(
    RSCacheDat2A_Component const* c);

int
dump_iface_parent_file_index(
    struct DumpIfaceLoaded const* li,
    int child_index);

/** Packed widget id: (iface_archive << 16) | file_index. */
int
dump_iface_packed_id_iface(
    int packed_id);

int
dump_iface_packed_id_file(
    int packed_id);

/** Format into buf: "0x........ (iface=N<<16|file=M)". */
void
dump_iface_format_packed_id(
    char* buf,
    size_t buf_size,
    int packed_id);

int
dump_iface_decode_component_from_bytes(
    RSCacheDat2A_Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index);

void
dump_iface_resolve_layout(
    RSCacheDat2A_Component* comps,
    int n,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* parent_idx,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

int
dump_iface_detect_cache_mode(char const* cache_dir);

int
dump_iface_load_dat2(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct DumpIfaceLoaded* out);

int
dump_iface_load_dat1(
    char const* cache_dir,
    int componentno,
    int root_w,
    int root_h,
    struct DumpIfaceLoaded* out);

void
dump_iface_free(struct DumpIfaceLoaded* li);

/** Scan all dat2 interface archives for references to target_iface. Returns 0 on success. */
int
dump_iface_scan_parents(
    struct RSCacheDat2Disk* cache,
    int target_iface,
    FILE* fp);

#endif
