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
 * A port is described by a manifest, because the exporter rewrites each config
 * file from what one run accumulated: a partial re-run drops whatever the last
 * run put there, so the asset list has to survive somewhere more durable than
 * shell history.
 *
 *   ./port_lostcity --manifest ports/inferno.ini --apply
 *
 * The flags the manifest replaced still work, and still override it, for a
 * one-off question about a single id:
 *
 *   ./port_lostcity --rev osrs239 ../../cache.osrs239 \
 *       --content /path/to/LostCity_Server/content \
 *       --area areas/area_inferno --prefix inferno \
 *       --npc 7706=inferno_zuk --map 35_83
 */

#include "lc_export.h"
#include "lc_manifest.h"
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
        "  %s --manifest <file.ini> [--apply] [overrides...]\n"
        "  %s --rev REV <cache_dir> --content <content_dir> [flags...] [--apply]\n"
        "\n"
        "  --manifest       port manifest: identity, destination and the whole asset\n"
        "                   list in one file. See port_lostcity/example_inferno.ini\n"
        "  --apply          write; without it nothing touches disk. Never a manifest\n"
        "                   key — whether a run writes belongs to the invocation\n"
        "\n"
        "Overrides (each wins over the same manifest key; the id flags append to\n"
        "the manifest's lists rather than replacing them):\n"
        "  --rev REV <dir>  source revision and cache directory\n"
        "  --content DIR    LostCity content root\n"
        "  --area DIR       config subdirectory under scripts/ (default areas/area_ported)\n"
        "  --prefix NAME    basename for emitted configs and generated asset names\n"
        "  --npc ID[=name] / --seq / --spotanim / --texture / --loc ID / --map X_Z\n"
        "  --texture        port a material outright; models and floors pull in\n"
        "                   whatever they reference without being asked\n"
        "  --max-textures N highest texture id the destination client reads, exclusive\n"
        "                   (default 50, what a stock rev-254 Pix3D unpacks). A\n"
        "                   material that does not fit is not an error: faces using it\n"
        "                   fall back to its average colour\n"
        "  --no-textures    same as --max-textures 0\n",
        argv0,
        argv0);
}

int
main(int argc, char** argv)
{
    struct LC_Manifest manifest;
    lc_manifest_init(&manifest);

    /*
     * Two passes: the manifest loads first so that a flag naming the same key
     * overwrites it by plain assignment, which is the precedence boot manifests
     * already use (CLI > manifest > default).
     */
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--manifest") == 0 && i + 1 < argc )
        {
            if( !lc_manifest_load(&manifest, argv[++i]) )
            {
                lc_manifest_free(&manifest);
                return 2;
            }
        }
    }

    int apply = 0;
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--manifest") == 0 && i + 1 < argc )
            i++; /* consumed above */
        else if( strcmp(argv[i], "--rev") == 0 && i + 2 < argc )
        {
            snprintf(manifest.rev, sizeof(manifest.rev), "%s", argv[++i]);
            snprintf(manifest.cache_dir, sizeof(manifest.cache_dir), "%s", argv[++i]);
        }
        else if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
            snprintf(manifest.content_dir, sizeof(manifest.content_dir), "%s", argv[++i]);
        else if( strcmp(argv[i], "--area") == 0 && i + 1 < argc )
            snprintf(manifest.area, sizeof(manifest.area), "%s", argv[++i]);
        else if( strcmp(argv[i], "--prefix") == 0 && i + 1 < argc )
            snprintf(manifest.prefix, sizeof(manifest.prefix), "%s", argv[++i]);
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            lc_request_add(&manifest.npcs, argv[++i]);
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            lc_request_add(&manifest.seqs, argv[++i]);
        else if( strcmp(argv[i], "--spotanim") == 0 && i + 1 < argc )
            lc_request_add(&manifest.spotanims, argv[++i]);
        else if( strcmp(argv[i], "--loc") == 0 && i + 1 < argc )
            lc_request_add(&manifest.locs, argv[++i]);
        else if( strcmp(argv[i], "--map") == 0 && i + 1 < argc )
            lc_request_add(&manifest.maps, argv[++i]);
        else if( strcmp(argv[i], "--texture") == 0 && i + 1 < argc )
            lc_request_add(&manifest.textures, argv[++i]);
        else if( strcmp(argv[i], "--max-textures") == 0 && i + 1 < argc )
        {
            manifest.max_textures = atoi(argv[++i]);
            if( manifest.max_textures < 0 )
                manifest.max_textures = 0;
        }
        else if( strcmp(argv[i], "--no-textures") == 0 )
            manifest.max_textures = 0;
        else if( strcmp(argv[i], "--apply") == 0 )
            apply = 1;
        else if( strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            lc_manifest_free(&manifest);
            return 0;
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            lc_manifest_free(&manifest);
            return 2;
        }
    }

    if( manifest.area[0] == '\0' )
        snprintf(manifest.area, sizeof(manifest.area), "%s", "areas/area_ported");
    if( manifest.prefix[0] == '\0' )
        snprintf(manifest.prefix, sizeof(manifest.prefix), "%s", "ported");
    /*
     * Defaults to what a stock rev-254 client will actually read. A material
     * that does not fit is not an error: the face falls back to that texture's
     * own average colour, which is what an unported face has always done.
     */
    if( manifest.max_textures < 0 )
        manifest.max_textures = LC_DEFAULT_MAX_TEXTURES;

    if( manifest.rev[0] == '\0' || manifest.cache_dir[0] == '\0' ||
        manifest.content_dir[0] == '\0' )
    {
        fprintf(stderr, "need rev, cache dir and content dir (manifest or flags)\n\n");
        usage(argv[0]);
        lc_manifest_free(&manifest);
        return 2;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(manifest.rev, NULL, NULL, NULL, NULL, &profile) )
    {
        lc_manifest_free(&manifest);
        return 2;
    }
    tool_print_profile(manifest.cache_dir, &profile);

    struct Tool_Dat2Cache src;
    if( !tool_dat2_open(manifest.cache_dir, &profile, &src) )
    {
        lc_manifest_free(&manifest);
        return 1;
    }

    struct LC_Packs packs;
    if( !lc_packs_load(&packs, manifest.content_dir) )
    {
        tool_dat2_close(&src);
        lc_manifest_free(&manifest);
        return 1;
    }

    struct LC_Out out;
    if( !lc_out_init(&out, manifest.content_dir, manifest.area, manifest.prefix, !apply) )
    {
        lc_packs_free(&packs);
        tool_dat2_close(&src);
        lc_manifest_free(&manifest);
        return 1;
    }

    struct LC_Ctx ctx;
    lc_ctx_init(&ctx, &src, &packs, &out);
    ctx.max_textures = manifest.max_textures;

    int failures = 0;

    /* Textures first, for the same reason sequences are: a material an explicit
     * request named should keep that name, not whichever one the first model
     * face to reach it generated. */
    for( int i = 0; i < manifest.textures.count; i++ )
    {
        const struct LC_Request* req = &manifest.textures.items[i];
        if( lc_export_texture(&ctx, req->id, req->has_name ? req->name : NULL) < 0 )
            failures++;
    }

    /*
     * Explicitly named sequences go first. An npc pulls in its own idle and walk
     * sequences under generated names, and the exporters are idempotent by source
     * id — so whichever asks first decides the name. Exporting npcs first would
     * silently ignore a `2863=..._defend` that named a sequence some npc
     * already claimed as its walk. This order is the tool's, not the manifest's:
     * moving a line between sections cannot change it.
     */
    for( int i = 0; i < manifest.seqs.count; i++ )
    {
        const struct LC_Request* req = &manifest.seqs.items[i];
        if( lc_export_seq(&ctx, req->id, req->has_name ? req->name : NULL) < 0 )
            failures++;
    }
    for( int i = 0; i < manifest.npcs.count; i++ )
    {
        const struct LC_Request* req = &manifest.npcs.items[i];
        char fallback[96];
        const char* name = req->name;
        if( !req->has_name )
        {
            snprintf(fallback, sizeof(fallback), "%s_npc_%d", manifest.prefix, req->id);
            name = fallback;
        }
        if( lc_export_npc(&ctx, req->id, name) < 0 )
            failures++;
    }
    for( int i = 0; i < manifest.spotanims.count; i++ )
    {
        const struct LC_Request* req = &manifest.spotanims.items[i];
        if( lc_export_spotanim(&ctx, req->id, req->has_name ? req->name : NULL) < 0 )
            failures++;
    }
    for( int i = 0; i < manifest.locs.count; i++ )
    {
        if( lc_export_loc(&ctx, manifest.locs.items[i].id) < 0 )
            failures++;
    }
    /* A map request is "X_Z", a coordinate pair rather than an id, so it comes
     * off the raw text the parser kept. */
    for( int i = 0; i < manifest.maps.count; i++ )
    {
        int map_x = 0;
        int map_z = 0;
        if( sscanf(manifest.maps.items[i].raw, "%d_%d", &map_x, &map_z) != 2 )
        {
            fprintf(
                stderr,
                "map wants X_Z, e.g. --map 35_83 (got %s)\n",
                manifest.maps.items[i].raw);
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

    if( apply && !lc_packs_write(&packs, manifest.content_dir) )
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
    lc_manifest_free(&manifest);
    return failures ? 1 : 0;
}
