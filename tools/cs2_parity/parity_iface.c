#include "parity_iface.h"

#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "ui/ui_if3_layout.h"

#include <stdlib.h>
#include <string.h>

static int
decode_component_from_bytes(
    Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index)
{
    if( !data || size <= 0 )
        return -1;
    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (int8_t*)data, size);
    Component_init(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

static void
resolve_interface_layout_simple(
    Component* comps,
    int n,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    for( int i = 0; i < n; i++ )
    {
        out_x[i] = root_x + comps[i].baseX;
        out_y[i] = root_y + comps[i].baseY;
        out_w[i] = comps[i].baseWidth;
        out_h[i] = comps[i].baseHeight;
    }

    int root_idx = n > 0 ? 0 : -1;
    int* parent_idx = calloc((size_t)n, sizeof(int));
    if( !parent_idx )
        return;

    for( int i = 0; i < n; i++ )
    {
        parent_idx[i] = -1;
        if( comps[i].layer < 0 )
            continue;
        for( int j = 0; j < n; j++ )
        {
            if( comps[j].id == comps[i].layer )
            {
                parent_idx[i] = j;
                break;
            }
        }
        if( parent_idx[i] < 0 && root_idx >= 0 )
            parent_idx[i] = root_idx;
    }

    if( root_idx >= 0 )
    {
        out_x[root_idx] = root_x;
        out_y[root_idx] = root_y;
        out_w[root_idx] = root_w;
        out_h[root_idx] = root_h;
    }

    for( int pass = 0; pass < n; pass++ )
    {
        for( int i = 0; i < n; i++ )
        {
            int p = parent_idx[i];
            if( p < 0 )
                continue;
            int rel_x = 0;
            int rel_y = 0;
            int w = out_w[i];
            int h = out_h[i];
            ui_if3_dat2_component_parent_relative_layout(
                &comps[i], out_w[p], out_h[p], &rel_x, &rel_y, &w, &h);
            out_w[i] = w;
            out_h[i] = h;
            out_x[i] = out_x[p] + rel_x;
            out_y[i] = out_y[p] + rel_y;
        }
    }

    for( int i = 0; i < n; i++ )
    {
        if( comps[i].layer >= 0 )
            continue;
        if( i == root_idx )
            continue;
        int rel_x = 0;
        int rel_y = 0;
        int w = out_w[i];
        int h = out_h[i];
        ui_if3_dat2_component_parent_relative_layout(
            &comps[i], root_w, root_h, &rel_x, &rel_y, &w, &h);
        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = root_x + rel_x;
        out_y[i] = root_y + rel_y;
    }

    free(parent_idx);
}

int
parity_iface_load(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct ParityIfaceLoad* out)
{
    if( !cache || !out || iface_id < 0 )
        return -1;

    memset(out, 0, sizeof(*out));

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, iface_id);
    if( !arch )
        return -1;
    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    int n = fl->file_count;
    Component* comps = calloc((size_t)n, sizeof(Component));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    if( !comps || !lay_x || !lay_y || !lay_w || !lay_h )
    {
        free(comps);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    for( int fi = 0; fi < n; fi++ )
    {
        if( decode_component_from_bytes(
                &comps[fi], fl->files[fi], fl->file_sizes[fi], iface_id, fi) != 0 )
            Component_init(&comps[fi]);
    }

    resolve_interface_layout_simple(comps, n, 0, 0, root_w, root_h, lay_x, lay_y, lay_w, lay_h);

    out->comps = comps;
    out->comp_count = n;
    out->lay_x = lay_x;
    out->lay_y = lay_y;
    out->lay_w = lay_w;
    out->lay_h = lay_h;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return 0;
}

void
parity_iface_free(struct ParityIfaceLoad* load)
{
    if( !load )
        return;
    if( load->comps )
    {
        for( int i = 0; i < load->comp_count; i++ )
            Component_free(&load->comps[i]);
        free(load->comps);
    }
    free(load->lay_x);
    free(load->lay_y);
    free(load->lay_w);
    free(load->lay_h);
    memset(load, 0, sizeof(*load));
}
