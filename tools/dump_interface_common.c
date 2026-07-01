#include "dump_interface_common.h"

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "ui/ui_if3_layout.h"

#include <stdlib.h>
#include <string.h>

char const*
dump_iface_component_type_name(Component const* c)
{
    if( !c )
        return "unknown";

    if( c->if3 )
    {
        switch( c->type )
        {
        case 0:
            return "layer";
        case 3:
            return "rect";
        case 4:
            return "text";
        case 5:
            return "graphic";
        case 6:
            return "model";
        case 9:
            return "line";
        default:
            return "unknown_if3";
        }
    }

    switch( c->type )
    {
    case COMPONENT_TYPE_LAYER:
        return "layer";
    case COMPONENT_TYPE_UNUSED:
        return "unused";
    case COMPONENT_TYPE_INV:
        return "inv";
    case COMPONENT_TYPE_RECT:
        return "rect";
    case COMPONENT_TYPE_TEXT:
        return "text";
    case COMPONENT_TYPE_GRAPHIC:
        return "graphic";
    case COMPONENT_TYPE_MODEL:
        return "model";
    case COMPONENT_TYPE_INV_TEXT:
        return "inv_text";
    case COMPONENT_TYPE_LINE:
        return "line";
    default:
        return "unknown_if1";
    }
}

int
dump_iface_parent_file_index(
    struct DumpIfaceLoaded const* li,
    int child_index)
{
    if( !li || child_index < 0 || child_index >= li->count )
        return -1;
    return li->parent_idx ? li->parent_idx[child_index] : -1;
}

int
dump_iface_decode_component_from_bytes(
    Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index)
{
    if( !out || !data || size <= 0 )
        return -1;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    Component_init(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

void
dump_iface_resolve_layout(
    Component* comps,
    int n,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* parent_idx,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int* depth = calloc((size_t)n, sizeof(int));
    int* order = calloc((size_t)n, sizeof(int));
    if( !depth || !order )
    {
        free(depth);
        free(order);
        for( int i = 0; i < n; i++ )
        {
            if( parent_idx )
                parent_idx[i] = -1;
            out_x[i] = root_x + comps[i].baseX;
            out_y[i] = root_y + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
        }
        return;
    }

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
    }

    for( int i = 0; i < n; i++ )
    {
        int d = 0;
        int cur = i;
        while( parent_idx[cur] >= 0 )
        {
            cur = parent_idx[cur];
            d++;
            if( d > n )
                break;
        }
        depth[i] = d;
    }

    for( int i = 0; i < n; i++ )
        order[i] = i;
    for( int a = 0; a < n; a++ )
    {
        for( int b = a + 1; b < n; b++ )
        {
            if( depth[order[b]] < depth[order[a]] )
            {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }

    for( int k = 0; k < n; k++ )
    {
        int i = order[k];
        int px = root_x;
        int py = root_y;
        int pw = root_w;
        int ph = root_h;
        if( parent_idx[i] >= 0 )
        {
            int p = parent_idx[i];
            px = out_x[p];
            py = out_y[p];
            pw = out_w[p];
            ph = out_h[p];
        }

        if( !comps[i].if3 )
        {
            out_x[i] = px + comps[i].baseX;
            out_y[i] = py + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            continue;
        }

        int rel_x = 0;
        int rel_y = 0;
        int w = 0;
        int h = 0;
        ui_if3_component_parent_relative_layout(
            1,
            comps[i].widthMode,
            comps[i].heightMode,
            comps[i].xMode,
            comps[i].yMode,
            comps[i].baseX,
            comps[i].baseY,
            comps[i].baseWidth,
            comps[i].baseHeight,
            comps[i].aspect_ratio_w,
            comps[i].aspect_ratio_h,
            pw,
            ph,
            &rel_x,
            &rel_y,
            &w,
            &h);
        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = px + rel_x;
        out_y[i] = py + rel_y;
    }

    free(depth);
    free(order);
}

int
dump_iface_load(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct DumpIfaceLoaded* out)
{
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
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    if( !comps || !parent_idx || !lay_x || !lay_y || !lay_w || !lay_h )
    {
        free(comps);
        free(parent_idx);
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
        if( dump_iface_decode_component_from_bytes(
                &comps[fi], fl->files[fi], fl->file_sizes[fi], iface_id, fi) != 0 )
            Component_init(&comps[fi]);
    }

    dump_iface_resolve_layout(
        comps, n, 0, 0, root_w, root_h, parent_idx, lay_x, lay_y, lay_w, lay_h);

    out->comps = comps;
    out->parent_idx = parent_idx;
    out->lay_x = lay_x;
    out->lay_y = lay_y;
    out->lay_w = lay_w;
    out->lay_h = lay_h;
    out->count = n;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return 0;
}

void
dump_iface_free(struct DumpIfaceLoaded* li)
{
    if( !li )
        return;
    for( int i = 0; i < li->count; i++ )
        Component_free(&li->comps[i]);
    free(li->comps);
    free(li->parent_idx);
    free(li->lay_x);
    free(li->lay_y);
    free(li->lay_w);
    free(li->lay_h);
    memset(li, 0, sizeof(*li));
}
