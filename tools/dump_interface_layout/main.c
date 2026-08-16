/*
 * Dump parent-relative widget layout from dat2 interface archives.
 *
 * Usage:
 *   dump_interface_layout <cache_dir> --iface 548 --list
 *   dump_interface_layout <cache_dir> --iface 548 --child 55
 *   dump_interface_layout <cache_dir> --iface 548 --json [--out file.json]
 */

#include "../dump_interface_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
print_child(
    struct DumpIfaceLoaded const* li,
    int child_id,
    FILE* fp)
{
    if( child_id < 0 || child_id >= li->count )
    {
        fprintf(fp, "child %d: not found (count=%d)\n", child_id, li->count);
        return;
    }
    RSCacheDat2A_Component const* c = &li->comps[child_id];
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
print_list(
    struct DumpIfaceLoaded const* li,
    FILE* fp)
{
    for( int i = 0; i < li->count; i++ )
    {
        RSCacheDat2A_Component const* c = &li->comps[i];
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
print_json(
    struct DumpIfaceLoaded const* li,
    int iface_id,
    FILE* fp)
{
    fprintf(fp, "{\n  \"iface\": %d,\n  \"children\": [\n", iface_id);
    int first = 1;
    for( int i = 0; i < li->count; i++ )
    {
        RSCacheDat2A_Component const* c = &li->comps[i];
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
    int root_w = DUMP_IFACE_FIXED_ROOT_W;
    int root_h = DUMP_IFACE_FIXED_ROOT_H;
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

    struct DumpIfaceLoaded li;
    if( dump_iface_load_dat2(cache, iface, root_w, root_h, &li) != 0 )
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
            dump_iface_free(&li);
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

    dump_iface_free(&li);
    RSCacheDat2Disk_Free(cache);
    return 0;
}
