/*
 * Dump parent-relative widget layout from dat2 interface archives.
 *
 * Usage:
 *   dump_interface_layout <cache_dir> --iface 548 --list
 *   dump_interface_layout <cache_dir> --iface 548 --child 55
 *   dump_interface_layout <cache_dir> --iface 548 --json [--out file.json]
 */

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "osrs/rscache/dat2a/dat2a_component.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    FIXED_MODE_ROOT_W = 765,
    FIXED_MODE_ROOT_H = 503,
    RS_LAYOUT_UNITS = 16384
};

static int
dim_from_parent_mode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return (int)((int64_t)parent_dim * (int64_t)orig / RS_LAYOUT_UNITS);
    default:
        return orig;
    }
}

static int
axis_from_position_mode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + (parent_dim - self_dim) / 2 + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS);
    case 4:
        return parent_origin + (parent_dim - self_dim) / 2 +
               (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS);
    case 5:
        return parent_origin + parent_dim -
               (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS) - self_dim;
    default:
        return parent_origin + base;
    }
}

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
resolve_interface_layout(
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
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* depth = calloc((size_t)n, sizeof(int));
    int* order = calloc((size_t)n, sizeof(int));
    if( !parent_idx || !depth || !order )
    {
        free(parent_idx);
        free(depth);
        free(order);
        for( int i = 0; i < n; i++ )
        {
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

        int w = dim_from_parent_mode(comps[i].widthMode, comps[i].baseWidth, pw);
        int h = dim_from_parent_mode(comps[i].heightMode, comps[i].baseHeight, ph);
        if( w < 0 )
            w = 0;
        if( h < 0 )
            h = 0;
        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = axis_from_position_mode(comps[i].xMode, comps[i].baseX, px, pw, w);
        out_y[i] = axis_from_position_mode(comps[i].yMode, comps[i].baseY, py, ph, h);
    }

    free(parent_idx);
    free(depth);
    free(order);
}

struct LoadedIface
{
    Component* comps;
    int* lay_x;
    int* lay_y;
    int* lay_w;
    int* lay_h;
    int count;
};

static int
load_interface(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct LoadedIface* out)
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

    resolve_interface_layout(comps, n, 0, 0, root_w, root_h, lay_x, lay_y, lay_w, lay_h);

    out->comps = comps;
    out->lay_x = lay_x;
    out->lay_y = lay_y;
    out->lay_w = lay_w;
    out->lay_h = lay_h;
    out->count = n;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return 0;
}

static void
free_loaded_iface(struct LoadedIface* li)
{
    if( !li )
        return;
    for( int i = 0; i < li->count; i++ )
        Component_free(&li->comps[i]);
    free(li->comps);
    free(li->lay_x);
    free(li->lay_y);
    free(li->lay_w);
    free(li->lay_h);
    memset(li, 0, sizeof(*li));
}

static void
print_child(
    struct LoadedIface const* li,
    int child_id,
    FILE* fp)
{
    if( child_id < 0 || child_id >= li->count )
    {
        fprintf(fp, "child %d: not found (count=%d)\n", child_id, li->count);
        return;
    }
    Component const* c = &li->comps[child_id];
    fprintf(
        fp,
        "child_id=%d type=%d x=%d y=%d w=%d h=%d graphic=%d hidden=%d\n",
        child_id,
        c->type,
        li->lay_x[child_id],
        li->lay_y[child_id],
        li->lay_w[child_id],
        li->lay_h[child_id],
        c->graphic,
        c->hidden ? 1 : 0);
}

static void
print_list(struct LoadedIface const* li, FILE* fp)
{
    for( int i = 0; i < li->count; i++ )
    {
        Component const* c = &li->comps[i];
        if( c->type < 0 )
            continue;
        fprintf(
            fp,
            "%4d  type=%3d  x=%4d y=%4d w=%4d h=%4d  graphic=%5d  hidden=%d\n",
            i,
            c->type,
            li->lay_x[i],
            li->lay_y[i],
            li->lay_w[i],
            li->lay_h[i],
            c->graphic,
            c->hidden ? 1 : 0);
    }
}

static void
print_json(struct LoadedIface const* li, int iface_id, FILE* fp)
{
    fprintf(fp, "{\n  \"iface\": %d,\n  \"children\": [\n", iface_id);
    int first = 1;
    for( int i = 0; i < li->count; i++ )
    {
        Component const* c = &li->comps[i];
        if( c->type < 0 )
            continue;
        if( !first )
            fprintf(fp, ",\n");
        first = 0;
        fprintf(
            fp,
            "    {\"child_id\": %d, \"type\": %d, \"x\": %d, \"y\": %d, "
            "\"w\": %d, \"h\": %d, \"graphic\": %d, \"hidden\": %s}",
            i,
            c->type,
            li->lay_x[i],
            li->lay_y[i],
            li->lay_w[i],
            li->lay_h[i],
            c->graphic,
            c->hidden ? "true" : "false");
    }
    fprintf(fp, "\n  ]\n}\n");
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: dump_interface_layout <cache_dir> --iface N [--list | --child N | --json]\n"
        "       [--root-w W] [--root-h H] [--out path]\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    int iface = 548;
    int child_id = -1;
    int list_mode = 0;
    int json_mode = 0;
    int root_w = FIXED_MODE_ROOT_W;
    int root_h = FIXED_MODE_ROOT_H;
    const char* out_path = NULL;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iface") == 0 && i + 1 < argc )
            iface = atoi(argv[++i]);
        else if( strcmp(argv[i], "--child") == 0 && i + 1 < argc )
            child_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--list") == 0 )
            list_mode = 1;
        else if( strcmp(argv[i], "--json") == 0 )
            json_mode = 1;
        else if( strcmp(argv[i], "--root-w") == 0 && i + 1 < argc )
            root_w = atoi(argv[++i]);
        else if( strcmp(argv[i], "--root-h") == 0 && i + 1 < argc )
            root_h = atoi(argv[++i]);
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( argv[i][0] != '-' && !cache_dir )
            cache_dir = argv[i];
    }

    if( !cache_dir || (!list_mode && child_id < 0 && !json_mode) )
    {
        usage();
        return 1;
    }

    if( iface == 161 || iface == 164 )
    {
        root_w = 800;
        root_h = 600;
    }

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", cache_dir);
        return 1;
    }

    struct LoadedIface li;
    if( load_interface(cache, iface, root_w, root_h, &li) != 0 )
    {
        fprintf(stderr, "failed to load interface %d\n", iface);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    FILE* fp = stdout;
    if( out_path )
    {
        fp = fopen(out_path, "w");
        if( !fp )
        {
            fprintf(stderr, "failed to open %s\n", out_path);
            free_loaded_iface(&li);
            RSCacheDat2Disk_Free(cache);
            return 1;
        }
    }

    if( json_mode )
        print_json(&li, iface, fp);
    else if( list_mode )
        print_list(&li, fp);
    else
        print_child(&li, child_id, fp);

    if( fp != stdout )
        fclose(fp);

    free_loaded_iface(&li);
    RSCacheDat2Disk_Free(cache);
    return 0;
}
