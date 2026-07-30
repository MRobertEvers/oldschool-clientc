#include "cachepack.h"

#include "cp_assets.h"
#include "tool_profile.h"

#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(
        stderr,
        "cachepack — unpack an OldSchool cache into editable source, and pack it back.\n"
        "\n"
        "  cachepack unpack --cache DIR --rev NAME --src DIR [--types a,b]\n"
        "                   [--compare DIR] [--compare-rev NAME]\n"
        "                   [--assets[=models,songs]] [--binary[=1,2]]\n"
        "  cachepack pack   --src DIR --out DIR [--base DIR] [--rev NAME] [--types a,b]\n"
        "                   [--assets] [--binary]\n"
        "  cachepack verify --cache DIR --rev NAME --src DIR [--types a,b]\n"
        "                   [--assets[=models,sprites]] [--tmp DIR]\n"
        "\n"
        "  unpack  writes pack/<type>.pack (id=name, seeded from the cache's gameval\n"
        "          table), configs/all.<type> (text records) and meta.ini.\n"
        "          Existing pack names are never overwritten. With --compare DIR, also\n"
        "          writes a LostCity-style review queue under\n"
        "          configs/_unpack/<rev>/ (new ids, .merge for changed records, stale\n"
        "          pack lines).\n"
        "  pack    reads that tree and writes the config records into a cache. --base\n"
        "          copies a cache first; without it --out is edited in place, which\n"
        "          grows the file because the container appends rather than compacts.\n"
        "          Server overlays (combat params, door stages) are baked with\n"
        "          mock230_pack --cache-out in the parent repo — see OSRS-Content README.\n"
        "  verify  round-trips every record through the text and reports exact /\n"
        "          same-length / differing counts per type. With --assets, also\n"
        "          round-trips the asset tables through their friendly forms in a\n"
        "          scratch directory and holds each one to its own bar. Exits\n"
        "          non-zero when a bar is missed.\n"
        "\n"
        "Options:\n"
        "  --types a,b     restrict to these config types (default: all)\n"
        "  --compare DIR   previous revision's binary cache; emit _unpack/<rev> diffs\n"
        "  --compare-rev N profile name for --compare (default: same as --rev)\n"
        "  --assets        lay the non-config tables out as named files, the way\n"
        "                  LostCity's content/ is: models/npc/goblin.model,\n"
        "                  songs/<name>.jmid, binary/title.jpg. Extensions come from\n"
        "                  the bytes, and nothing is transcoded. --assets=models,maps\n"
        "                  limits it to those kinds; --list-assets prints them.\n"
        "  --raw-assets    skip the friendly decoders, so every asset writes its raw\n"
        "                  payload. Use when a decoded form is in the way.\n"
        "  --binary        also move the non-config tables, as raw container bytes.\n"
        "                  On unpack, --binary=5,7 limits it to those idx files.\n"
        "  --tmp DIR       scratch directory for `verify --assets` (default\n"
        "                  build/cachepack_verify). Never the content tree.\n"
        "  --warn N        cap repeated warnings at N per kind (-1 for no cap, default 20)\n"
        "  --list          print the config types this build knows and exit\n"
        "  --list-assets   print the asset kinds this build knows and exit\n");
}

static void
list_types(void)
{
    printf("%-11s %-6s %-8s %s\n", "type", "group", "gameval", "notes");
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* type = cp_type(i);
        char gameval[16];
        if( type->gameval_archive < 0 )
            snprintf(gameval, sizeof(gameval), "-");
        else
            snprintf(gameval, sizeof(gameval), "%d", type->gameval_archive);
        printf("%-11s %-6d %-8s %s%s\n", type->name, type->config_kind, gameval,
               (type->flags & CP_TYPE_LOSSY) ? "lossy " : "",
               (type->flags & CP_TYPE_NO_ENCODER) ? "unpack-only" : "");
    }
}

static void
list_assets(void)
{
    printf("%-19s %-14s %-8s %-9s %s\n", "directory", "pack", "raw", "decoded", "notes");
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        const struct CP_Asset* asset = cp_asset(i);
        char decoded[16];
        snprintf(decoded, sizeof(decoded), "%s",
                 asset->codec ? asset->codec->ext : "-");
        printf("%-19s %-14s .%-7s %-9s %s\n", asset->dir, asset->pack, asset->ext, decoded,
               (asset->flags & CP_ASSET_ENCRYPTED) ? "xtea" : "");
    }
    printf("\n`raw` is the fallback extension; PNG, JPEG, GIF, MIDI, Ogg and the four\n"
           "model formats are detected from the payload and override it.\n"
           "`decoded` is the friendly form, written instead where the record decodes.\n"
           "A record the decoder declines falls back to its raw form, so nothing is\n"
           "lost — 4,139 of osrs239's 9,725 clientscripts take that path.\n"
           "--raw-assets turns the decoders off entirely.\n");
}

static int
parse_types(
    const char* csv,
    struct CP_Selection* sel)
{
    sel->all = false;
    sel->mask = 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", csv);
    char* save = buf;
    while( save && *save )
    {
        char* comma = strchr(save, ',');
        if( comma )
            *comma = '\0';
        int type = cp_type_by_name(save);
        if( type < 0 )
        {
            fprintf(stderr, "cachepack: unknown type '%s' (try --list)\n", save);
            return 0;
        }
        sel->mask |= 1u << type;
        save = comma ? comma + 1 : NULL;
    }
    return sel->mask != 0;
}

/** Read the identity `unpack` recorded, so `pack` does not have to be told again. */
static int
load_meta(
    const char* srcdir,
    struct RSCache* out)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/meta.ini", srcdir);
    FILE* in = fopen(path, "rb");
    if( !in )
        return 0;
    struct RSCache profile = RSCache_ProfileZero();
    char line[256];
    int seen = 0;
    while( fgets(line, sizeof(line), in) )
    {
        int value = 0;
        unsigned uvalue = 0;
        if( sscanf(line, "game = %d", &value) == 1 )
        {
            profile.game = value;
            seen++;
        }
        else if( sscanf(line, "epoch = %d", &value) == 1 )
        {
            profile.epoch = value;
            seen++;
        }
        else if( sscanf(line, "revision = %d", &value) == 1 )
            profile.revision = value;
        else if( sscanf(line, "quirks = %u", &uvalue) == 1 )
            profile.quirks = uvalue;
    }
    fclose(in);
    if( seen < 2 )
        return 0;
    *out = profile;
    return 1;
}

int
main(int argc, char** argv)
{
    if( argc < 2 )
    {
        usage();
        return 1;
    }

    const char* command = argv[1];
    if( strcmp(command, "--list") == 0 || strcmp(command, "list") == 0 )
    {
        list_types();
        return 0;
    }
    if( strcmp(command, "--list-assets") == 0 )
    {
        list_assets();
        return 0;
    }
    if( strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0 )
    {
        usage();
        return 0;
    }
    const char* cache_dir = NULL;
    const char* out_dir = NULL;
    const char* base_dir = NULL;
    const char* src_dir = NULL;
    const char* rev = NULL;
    const char* compare_dir = NULL;
    const char* compare_rev = NULL;
    const char* types_csv = NULL;
    const char* binary_tables = NULL;
    const char* asset_kinds = NULL;
    const char* tmp_dir = "build/cachepack_verify";
    int want_binary = 0;
    int want_assets = 0;
    int warn_limit = 20;

    for( int i = 2; i < argc; i++ )
    {
        const char* arg = argv[i];
        if( strcmp(arg, "--list") == 0 )
        {
            list_types();
            return 0;
        }
        else if( strcmp(arg, "--list-assets") == 0 )
        {
            list_assets();
            return 0;
        }
        else if( strcmp(arg, "--binary") == 0 )
            want_binary = 1;
        else if( strcmp(arg, "--assets") == 0 )
            want_assets = 1;
        else if( strcmp(arg, "--raw-assets") == 0 )
            cp_assets_set_raw(1);
        else if( strncmp(arg, "--assets=", 9) == 0 )
        {
            want_assets = 1;
            asset_kinds = arg + 9;
        }
        else if( strncmp(arg, "--binary=", 9) == 0 )
        {
            want_binary = 1;
            binary_tables = arg + 9;
        }
        else if( i + 1 >= argc )
        {
            fprintf(stderr, "cachepack: %s needs a value\n", arg);
            return 1;
        }
        else if( strcmp(arg, "--cache") == 0 )
            cache_dir = argv[++i];
        else if( strcmp(arg, "--out") == 0 )
            out_dir = argv[++i];
        else if( strcmp(arg, "--base") == 0 )
            base_dir = argv[++i];
        else if( strcmp(arg, "--src") == 0 )
            src_dir = argv[++i];
        else if( strcmp(arg, "--rev") == 0 )
            rev = argv[++i];
        else if( strcmp(arg, "--compare") == 0 )
            compare_dir = argv[++i];
        else if( strcmp(arg, "--compare-rev") == 0 )
            compare_rev = argv[++i];
        else if( strcmp(arg, "--types") == 0 )
            types_csv = argv[++i];
        else if( strcmp(arg, "--tmp") == 0 )
            tmp_dir = argv[++i];
        else if( strcmp(arg, "--warn") == 0 )
            warn_limit = atoi(argv[++i]);
        else
        {
            fprintf(stderr, "cachepack: unknown option %s\n", arg);
            return 1;
        }
    }

    if( !src_dir )
    {
        fprintf(stderr, "cachepack: --src is required\n");
        return 1;
    }

    struct CP_Selection sel = { .all = true, .mask = 0 };
    if( types_csv && !parse_types(types_csv, &sel) )
        return 1;

    struct CP_Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.warn_limit = warn_limit;
    snprintf(ctx.srcdir, sizeof(ctx.srcdir), "%s", src_dir);

    /*
     * The profile is stated, never guessed — the same rule the library holds to.
     * `pack` may take it from meta.ini because `unpack` wrote what it was told;
     * everything else has to be given --rev, because packing a record with the
     * wrong era writes a record the target client misreads rather than one that
     * fails to load.
     */
    if( rev )
    {
        if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &ctx.profile) )
            return 1;
        snprintf(ctx.rev_name, sizeof(ctx.rev_name), "%s", rev);
    }
    else if( strcmp(command, "pack") == 0 && load_meta(src_dir, &ctx.profile) )
    {
        printf("Using the identity meta.ini recorded (game %d, revision %d)\n", ctx.profile.game,
               ctx.profile.revision);
    }
    else
    {
        fprintf(stderr, "cachepack: --rev is required (or run unpack first so meta.ini exists)\n");
        return 1;
    }

    if( !cp_names_load(&ctx.names, src_dir) )
        return 1;

    int rc = 1;
    if( strcmp(command, "unpack") == 0 || strcmp(command, "verify") == 0 )
    {
        if( !cache_dir )
        {
            fprintf(stderr, "cachepack: --cache is required for %s\n", command);
            cp_names_free(&ctx.names);
            return 1;
        }
        if( !tool_dat2_open(cache_dir, &ctx.profile, &ctx.cache) )
        {
            cp_names_free(&ctx.names);
            return 1;
        }
        ctx.cache_open = true;
        snprintf(ctx.cache_dir, sizeof(ctx.cache_dir), "%s", cache_dir);
        tool_print_profile(cache_dir, &ctx.profile);

        if( strcmp(command, "unpack") == 0 && compare_dir )
        {
            const char* cmp_rev = compare_rev ? compare_rev : rev;
            if( !cmp_rev )
            {
                fprintf(stderr, "cachepack: --compare needs --rev (or --compare-rev)\n");
                tool_dat2_close(&ctx.cache);
                cp_names_free(&ctx.names);
                return 1;
            }
            if( !tool_resolve_profile(cmp_rev, NULL, NULL, NULL, NULL, &ctx.compare_profile) )
            {
                tool_dat2_close(&ctx.cache);
                cp_names_free(&ctx.names);
                return 1;
            }
            if( !tool_dat2_open(compare_dir, &ctx.compare_profile, &ctx.compare) )
            {
                tool_dat2_close(&ctx.cache);
                cp_names_free(&ctx.names);
                return 1;
            }
            ctx.compare_open = true;
            printf("Compare baseline: %s (%s)\n", compare_dir, cmp_rev);
        }
        else if( compare_dir )
        {
            fprintf(stderr, "cachepack: --compare is only valid for unpack\n");
            tool_dat2_close(&ctx.cache);
            cp_names_free(&ctx.names);
            return 1;
        }

        if( strcmp(command, "unpack") == 0 )
        {
            rc = cp_unpack_run(&ctx, &sel) ? 0 : 1;
            if( rc == 0 && want_assets )
                rc = cp_assets_export(&ctx, asset_kinds) ? 0 : 1;
            if( rc == 0 && want_binary )
                rc = cp_binary_export(&ctx, binary_tables) ? 0 : 1;
            /* The pack files gain a line per asset, so they are written again
             * after the asset tree rather than only after the configs. */
            if( rc == 0 && want_assets && !cp_names_save(&ctx.names, src_dir) )
                rc = 1;
        }
        else
        {
            rc = cp_verify_run(&ctx, &sel) ? 0 : 1;
            /* Both halves run even when the first fails, so one invocation reports
             * every table that missed its bar rather than only the earliest. */
            if( want_assets && !cp_assets_verify(&ctx, asset_kinds, tmp_dir) )
                rc = 1;
        }
    }
    else if( strcmp(command, "pack") == 0 )
    {
        if( !out_dir )
        {
            fprintf(stderr, "cachepack: --out is required for pack\n");
            cp_names_free(&ctx.names);
            return 1;
        }
        rc = cp_pack_run(&ctx, &sel, base_dir, out_dir) ? 0 : 1;
        if( want_assets && ctx.cache_open )
        {
            if( !cp_assets_import(&ctx, out_dir) )
                rc = 1;
        }
        if( want_binary && ctx.cache_open )
        {
            /* After the configs, so a binary import of the config table (if the
             * caller asked for one) is the version that lands. */
            if( !cp_binary_import(&ctx, out_dir) )
                rc = 1;
        }
    }
    else
    {
        usage();
        rc = 1;
    }

    if( ctx.cache_open )
        tool_dat2_close(&ctx.cache);
    if( ctx.compare_open )
        tool_dat2_close(&ctx.compare);
    cp_names_free(&ctx.names);
    return rc;
}
