/*
 * port_lostcity — export dat2 cache assets as LostCity *source* files.
 *
 * Unlike port_npc, which writes a binary destination cache, this writes what a
 * LostCity build takes as input: `.ob2` models, `.anim` animset archives, text
 * `.npc`/`.seq`/`.spotanim`/`.loc`/`.flo` configs, a text `.jm2` map square, and
 * the `content/pack` id lines that make any of it exist. It writes into
 * the server's content tree directly — staging elsewhere would let the ids the
 * configs were written against drift from the ids the build reads.
 *
 *   ./port_lostcity --rev osrs239 ../../cache.osrs239 \
 *       --content /path/to/LostCity_Server/content \
 *       --area areas/area_inferno --prefix inferno \
 *       --npc 7706=inferno_zuk --spotanim 1375=inferno_zuk_proj \
 *       --map 35_83 --apply
 */

#include "lc_export.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev REV <cache_dir> --content <content_dir>\n"
        "     [--area DIR] [--prefix NAME]\n"
        "     [--npc ID[=name]]... [--seq ID[=name]]... [--spotanim ID[=name]]...\n"
        "     [--loc ID]... [--map X_Z]... [--texture ID[=name]]...\n"
        "     [--max-textures N] [--no-textures] [--apply]\n"
        "\n"
        "  --area           config subdirectory under scripts/ (default areas/area_ported)\n"
        "  --prefix         basename for emitted configs and generated asset names\n"
        "  --texture        port a material outright; models and floors pull in\n"
        "                   whatever they reference without being asked\n"
        "  --max-textures   highest texture id the destination client reads, exclusive\n"
        "                   (default 50, what a stock rev-254 Pix3D unpacks). A\n"
        "                   material that does not fit is not an error: faces using it\n"
        "                   fall back to its average colour\n"
        "  --no-textures    same as --max-textures 0\n"
        "  --apply          write; without it nothing touches disk\n",
        argv0);
}

struct request
{
    int id;
    char name[96];
    int has_name;
    /** The argument verbatim, for `--map X_Z` where the id is not a number. */
    char raw[96];
};

struct request_list
{
    struct request* items;
    int count;
    int cap;
};

static int
request_add(
    struct request_list* list,
    const char* arg)
{
    if( list->count >= list->cap )
    {
        int cap = list->cap == 0 ? 16 : list->cap * 2;
        struct request* n = realloc(list->items, (size_t)cap * sizeof(*n));
        if( !n )
            return 0;
        list->items = n;
        list->cap = cap;
    }
    struct request* req = &list->items[list->count];
    memset(req, 0, sizeof(*req));
    snprintf(req->raw, sizeof(req->raw), "%s", arg);

    const char* eq = strchr(arg, '=');
    req->id = atoi(arg);
    if( eq && eq[1] )
    {
        snprintf(req->name, sizeof(req->name), "%s", eq + 1);
        req->has_name = 1;
    }
    list->count++;
    return 1;
}

int
main(int argc, char** argv)
{
    const char* rev = NULL;
    const char* cache_dir = NULL;
    const char* content_dir = NULL;
    const char* area = "areas/area_ported";
    const char* prefix = "ported";
    int apply = 0;

    struct request_list npcs = { 0 };
    struct request_list seqs = { 0 };
    struct request_list spotanims = { 0 };
    struct request_list locs = { 0 };
    struct request_list maps = { 0 };
    struct request_list textures = { 0 };
    /*
     * Defaults to what a stock rev-254 client will actually read. A material
     * that does not fit is not an error: the face falls back to that texture's
     * own average colour, which is what an unported face has always done.
     */
    int max_textures = LC_DEFAULT_MAX_TEXTURES;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 2 < argc )
        {
            rev = argv[++i];
            cache_dir = argv[++i];
        }
        else if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
            content_dir = argv[++i];
        else if( strcmp(argv[i], "--area") == 0 && i + 1 < argc )
            area = argv[++i];
        else if( strcmp(argv[i], "--prefix") == 0 && i + 1 < argc )
            prefix = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            request_add(&npcs, argv[++i]);
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            request_add(&seqs, argv[++i]);
        else if( strcmp(argv[i], "--spotanim") == 0 && i + 1 < argc )
            request_add(&spotanims, argv[++i]);
        else if( strcmp(argv[i], "--loc") == 0 && i + 1 < argc )
            request_add(&locs, argv[++i]);
        else if( strcmp(argv[i], "--map") == 0 && i + 1 < argc )
            request_add(&maps, argv[++i]);
        else if( strcmp(argv[i], "--texture") == 0 && i + 1 < argc )
            request_add(&textures, argv[++i]);
        else if( strcmp(argv[i], "--max-textures") == 0 && i + 1 < argc )
        {
            max_textures = atoi(argv[++i]);
            if( max_textures < 0 )
                max_textures = 0;
        }
        else if( strcmp(argv[i], "--no-textures") == 0 )
            max_textures = 0;
        else if( strcmp(argv[i], "--apply") == 0 )
            apply = 1;
        else if( strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if( !rev || !cache_dir || !content_dir )
    {
        usage(argv[0]);
        return 2;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 2;
    tool_print_profile(cache_dir, &profile);

    struct Tool_Dat2Cache src;
    if( !tool_dat2_open(cache_dir, &profile, &src) )
        return 1;

    struct LC_Packs packs;
    if( !lc_packs_load(&packs, content_dir) )
    {
        tool_dat2_close(&src);
        return 1;
    }

    struct LC_Out out;
    if( !lc_out_init(&out, content_dir, area, prefix, !apply) )
    {
        lc_packs_free(&packs);
        tool_dat2_close(&src);
        return 1;
    }

    struct LC_Ctx ctx;
    lc_ctx_init(&ctx, &src, &packs, &out);
    ctx.max_textures = max_textures;

    int failures = 0;

    /* Textures first, for the same reason sequences are: a material an explicit
     * request named should keep that name, not whichever one the first model
     * face to reach it generated. */
    for( int i = 0; i < textures.count; i++ )
    {
        if( lc_export_texture(
                &ctx,
                textures.items[i].id,
                textures.items[i].has_name ? textures.items[i].name : NULL) < 0 )
            failures++;
    }

    /*
     * Explicitly named sequences go first. An npc pulls in its own idle and walk
     * sequences under generated names, and the exporters are idempotent by source
     * id — so whichever asks first decides the name. Exporting npcs first would
     * silently ignore a `--seq 2863=..._defend` that named a sequence some npc
     * already claimed as its walk.
     */
    for( int i = 0; i < seqs.count; i++ )
    {
        const char* name = seqs.items[i].has_name ? seqs.items[i].name : NULL;
        if( lc_export_seq(&ctx, seqs.items[i].id, name) < 0 )
            failures++;
    }
    for( int i = 0; i < npcs.count; i++ )
    {
        char fallback[96];
        const char* name = npcs.items[i].name;
        if( !npcs.items[i].has_name )
        {
            snprintf(fallback, sizeof(fallback), "%s_npc_%d", prefix, npcs.items[i].id);
            name = fallback;
        }
        if( lc_export_npc(&ctx, npcs.items[i].id, name) < 0 )
            failures++;
    }
    for( int i = 0; i < spotanims.count; i++ )
    {
        const char* name = spotanims.items[i].has_name ? spotanims.items[i].name : NULL;
        if( lc_export_spotanim(&ctx, spotanims.items[i].id, name) < 0 )
            failures++;
    }
    for( int i = 0; i < locs.count; i++ )
    {
        if( lc_export_loc(&ctx, locs.items[i].id) < 0 )
            failures++;
    }
    /* A map argument is "X_Z", a coordinate pair rather than an id, so it comes
     * off the raw text the parser kept. */
    for( int i = 0; i < maps.count; i++ )
    {
        int map_x = 0;
        int map_z = 0;
        if( sscanf(maps.items[i].raw, "%d_%d", &map_x, &map_z) != 2 )
        {
            fprintf(
                stderr, "--map wants X_Z, e.g. --map 35_83 (got %s)\n", maps.items[i].raw);
            failures++;
            continue;
        }
        if( !lc_export_map(&ctx, map_x, map_z) )
            failures++;
    }

    if( !lc_export_flush_animsets(&ctx) )
        failures++;
    if( !lc_out_flush_configs(&out) )
        failures++;

    if( apply && !lc_packs_write(&packs, content_dir) )
        failures++;

    printf("\n%s\n", apply ? "== applied ==" : "== dry run (pass --apply to write) ==");
    printf("files: %d (%ld bytes)\n", out.files_written, out.bytes_written);
    for( int i = 0; i < LC_PACK_COUNT; i++ )
    {
        if( packs.packs[i].added > 0 )
            printf(
                "  %-9s +%d ids (max now %d)\n",
                packs.packs[i].type,
                packs.packs[i].added,
                packs.packs[i].max);
    }
    if( out.warning_count > 0 )
    {
        printf("warnings: %d\n", out.warning_count);
        for( int i = 0; i < out.warning_count; i++ )
            printf("  %s\n", out.warnings[i]);
    }
    if( failures )
        printf("failures: %d\n", failures);

    lc_ctx_free(&ctx);
    lc_out_free(&out);
    lc_packs_free(&packs);
    tool_dat2_close(&src);
    free(npcs.items);
    free(seqs.items);
    free(spotanims.items);
    free(locs.items);
    free(maps.items);
    return failures ? 1 : 0;
}
