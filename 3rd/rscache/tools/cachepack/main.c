#include "cachepack.h"
#include "cp_incremental.h"
#include "rscache_register.h"

#include "cp_assets.h"
#include "cp_import.h"
#include "cache_write.h"
#include "tool_profile.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
        "                   [--assets] [--binary] [--gamevals]\n"
        "  cachepack pack   --src DIR --out DIR --rev NAME --asset-only\n"
        "                   --assets=interfaces,scripts [--archive-list FILE]\n"
        "  cachepack pack   --src DIR --server-only [--lane NAME]... [--server-out DIR] [--force]\n"
        "  cachepack verify --cache DIR --rev NAME --src DIR [--types a,b]\n"
        "                   [--assets[=models,sprites]] [--tmp DIR]\n"
        "  cachepack membership --src DIR --rev NAME [--types a,b] [--check-only]\n"
        "  cachepack keys   --rev NAME [--types a,b] [--src DIR]\n"
        "  cachepack missing --src DIR [--rev NAME] [--types a,b]\n"
        "\n"
        "  Every block states every key of its type; a field the record does not\n"
        "  set is written `key=default`, a list it sets empty `key=empty`. `keys`\n"
        "  prints those keys per type for the profile.\n"
        "\n"
        "  unpack  writes pack/<type>.pack (id=name, seeded from the cache's gameval\n"
        "          table), configs/all.<type> (text records) and meta.ini.\n"
        "          Existing pack names are never overwritten. With --compare DIR, also\n"
        "          writes a LostCity-style review queue under\n"
        "          configs/_unpack/<rev>/ (new ids, .merge for changed records, stale\n"
        "          pack lines).\n"
        "  pack    reads that tree and writes the config records into a cache. --base\n"
        "          copies a cache first, so every record the tree does not change keeps\n"
        "          the bytes it had. Without it the cache is CREATED at --out and holds\n"
        "          exactly what the tree states and nothing else — which is what you\n"
        "          want when there is no pristine cache to start from, and is not what\n"
        "          you want if you expect a base cache's untouched records to survive.\n"
        "          Either way the container appends rather than compacts, so re-packing\n"
        "          the same --out repeatedly grows the file.\n"
        "          Also writes <src>/server/pack — the fields no client opcode can\n"
        "          express (hitpoints, respawnrate, door stages), one archive per\n"
        "          record at (config kind, id), under the opcodes fields/<type>.ini\n"
        "          declares; plus the name tables of the namespaces the cache has no\n"
        "          table for (stat, category) at group 128 up. Rebuilt whole on every\n"
        "          run, so --types makes the config half of it partial. --server-only\n"
        "          writes just that server pack — no cache is opened, no --out needed —\n"
        "          --lane NAME walks ported/NAME/configs too (its records are\n"
        "          defined there, below the authored overlays); --server-out puts\n"
        "          the server pack somewhere other than <src>/server/pack, one per\n"
        "          lane composition. The pack is stamped with its tree's fingerprint;\n"
        "          a --server-only run whose stamp matches does nothing (--force\n"
        "          rewrites), and the server refuses a pack whose stamp does not.\n"

        "          which is the cheap form the build runs before every server boot.\n"
        "          --asset-only opens an already-complete --out in place and skips\n"
        "          configs/server packs. With --archive-list, only rows written as\n"
        "          <asset>=<archive id> are replaced; a selected friendly form that\n"
        "          cannot encode is an error rather than a silent base-cache fallback.\n"
        "  verify  round-trips every record through the text and reports exact /\n"
        "          same-length / differing counts per type. With --assets, also\n"
        "          round-trips the asset tables through their friendly forms in a\n"
        "          scratch directory and holds each one to its own bar. Exits\n"
        "          non-zero when a bar is missed.\n"
        "  membership\n"
        "          seeds pack/<ns>.client and pack/<ns>.server — which *entities*\n"
        "          have a half on each side, as bare names, the ids coming from the\n"
        "          namespace both sides share. Written from the two gates the packer\n"
        "          already applies, so they state today's routing rather than a new\n"
        "          one; nothing reads them yet. Creates only files that do not exist\n"
        "          and never rewrites one, because a membership file is authored the\n"
        "          moment it exists. No cache is opened and no --out is needed.\n"
        "          --check-only writes nothing at all: it holds the files against the\n"
        "          routing they state — a name no config layer states, a name in\n"
        "          <ns>.server the tree says nothing about, a record under\n"
        "          server/scripts neither file claims — and exits non-zero on any of\n"
        "          them. The id-range half of that check runs in ToriRSServer_Pack, where\n"
        "          the allocation bases live.\n"
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
        "  --asset-only    replace assets in an already-complete --out; no config pass\n"
        "  --archive-list F restrict asset import to <asset>=<archive id> rows in F\n"
        "  --raw-assets    skip the friendly decoders, so every asset writes its raw\n"
        "                  payload. Use when a decoded form is in the way.\n"
        "  --gamevals      on pack, write pack/<ns>.pack back into the cache's own\n"
        "                  symbol table (idx 24), so the cache is self-describing.\n"
        "                  Names go out normalised, and archive 14 is skipped because\n"
        "                  it nests components inside interfaces. Nothing outside\n"
        "                  cachepack reads the table, so this cannot affect a boot.\n"
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

/*
 * `cachepack keys --rev NAME [--types a,b]`: every key a block of each type
 * states under that profile, one per line as `<type> <key> <list|indexed|->`.
 * The tables live beside the unpackers (CP_KeySpec); this is how a script that
 * completes or reads the text asks for them instead of keeping a copy.
 */
static int
print_keys(
    const char* rev,
    const char* types_csv,
    const char* src_dir)
{
    struct CP_Selection sel = { .all = true, .mask = 0 };
    struct CP_Ctx ctx;

    if( !rev )
    {
        fprintf(stderr, "cachepack: keys needs --rev\n");
        return 1;
    }
    if( types_csv && !parse_types(types_csv, &sel) )
        return 1;
    memset(&ctx, 0, sizeof(ctx));
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &ctx.profile) )
        return 1;
    if( src_dir )
        snprintf(ctx.srcdir, sizeof(ctx.srcdir), "%s", src_dir);
    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        const struct CP_Type* type = cp_type((enum CP_TypeId)t);

        if( !sel.all && !(sel.mask & (1u << t)) )
            continue;
        for( const struct CP_KeySpec* spec = type->keys; spec->key; spec++ )
        {
            if( spec->applies && !spec->applies(&ctx) )
                continue;
            printf("%s %s %s%s%s\n", type->name, spec->key,
                   (spec->flags & CP_KEY_INDEXED) ? "indexed"
                   : (spec->flags & CP_KEY_LIST) ? "list"
                                                 : "-",
                   spec->sibling ? " " : "", spec->sibling ? spec->sibling : "");
        }
        /* With --src, the type's server keys too: one key set per record. */
        if( src_dir )
        {
            const struct RSCache_Register* fields = cp_ctx_fields(&ctx, (enum CP_TypeId)t);

            for( int f = 0; f < fields->count; f++ )
            {
                if( cp_keys_is_server_key(&ctx, type, &fields->entries[f]) )
                    printf("%s %s -\n", type->name, fields->entries[f].name);
            }
        }
    }
    cp_ctx_fields_free(&ctx);
    return 0;
}

int
main(int argc, char** argv)
{
    /*
     * Make the report appear as it is produced. Redirected to a file, stdout is
     * block-buffered at 4K, so a pack running for twenty minutes and several
     * types deep showed an empty log — the per-type lines were sitting in the
     * buffer. It reads as a hang, and it cost real time diagnosing progress
     * from the size of the .dat2 rather than from what the tool was saying.
     *
     * _IONBF rather than _IOLBF on Windows, and this is the point: MSVCRT does
     * not implement line buffering. It accepts _IOLBF and silently gives you
     * full buffering, so the obvious fix is a no-op there. stdout carries only
     * the phase and per-type lines — a few hundred over a whole run — so
     * unbuffered costs nothing. The per-archive volume is on stderr, which is
     * already unbuffered and rate-limited by cp_warn.
     */
#if defined(_WIN32)
    setvbuf(stdout, NULL, _IONBF, 0);
#else
    setvbuf(stdout, NULL, _IOLBF, 0);
#endif

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
    if( strcmp(command, "import") == 0 )
        return cp_import_command(argc - 2, argv + 2);
    const char* cache_dir = NULL;
    const char* out_dir = NULL;
    const char* base_dir = NULL;
    const char* src_dir = NULL;
    const char* rev = NULL;
    const char* compare_dir = NULL;
    const char* compare_rev = NULL;
    const char* types_csv = NULL;
    const char* binary_tables = NULL;
    const char* lanes[CP_MAX_LANES];
    int lane_count = 0;
    const char* server_out = NULL;
    int force_server = 0;
    const char* asset_kinds = NULL;
    const char* archive_list_path = NULL;
    const char* tmp_dir = "build/cachepack_verify";
    int want_gamevals = 0;
    int want_binary = 0;
    int want_assets = 0;
    int check_only = 0;
    int server_only = 0;
    int asset_only = 0;
    const char* digest_dir = NULL;
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
        else if( strcmp(arg, "--digests") == 0 && i + 1 < argc )
            digest_dir = argv[++i];
        else if( strcmp(arg, "--check-only") == 0 )
            check_only = 1;
        else if( strcmp(arg, "--server-only") == 0 )
            server_only = 1;
        else if( strcmp(arg, "--lane") == 0 && i + 1 < argc )
        {
            if( lane_count == CP_MAX_LANES )
            {
                fprintf(stderr, "cachepack: at most %d --lane options\n", CP_MAX_LANES);
                return 1;
            }
            lanes[lane_count++] = argv[++i];
        }
        else if( strcmp(arg, "--server-out") == 0 && i + 1 < argc )
            server_out = argv[++i];
        else if( strcmp(arg, "--force") == 0 )
            force_server = 1;
        else if( strcmp(arg, "--asset-only") == 0 )
            asset_only = 1;
        else if( strcmp(arg, "--assets") == 0 )
            want_assets = 1;
        else if( strcmp(arg, "--gamevals") == 0 )
            want_gamevals = 1;
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
        else if( strcmp(arg, "--archive-list") == 0 )
            archive_list_path = argv[++i];
        else if( strcmp(arg, "--warn") == 0 )
            warn_limit = atoi(argv[++i]);
        else
        {
            fprintf(stderr, "cachepack: unknown option %s\n", arg);
            return 1;
        }
    }

    if( strcmp(command, "keys") == 0 )
        return print_keys(rev, types_csv, src_dir);

    if( !src_dir )
    {
        fprintf(stderr, "cachepack: --src is required\n");
        return 1;
    }
    if( asset_only && strcmp(command, "pack") != 0 )
    {
        fprintf(stderr, "cachepack: --asset-only is valid only with pack\n");
        return 1;
    }
    if( archive_list_path && (strcmp(command, "pack") != 0 || !want_assets) )
    {
        fprintf(stderr, "cachepack: --archive-list requires pack --assets\n");
        return 1;
    }
    if( asset_only && base_dir && out_dir && strcmp(base_dir, out_dir) == 0 )
    {
        fprintf(stderr, "cachepack: --base and --out must be different directories\n");
        return 1;
    }

    struct CP_Selection sel = { .all = true, .mask = 0 };
    if( types_csv && !parse_types(types_csv, &sel) )
        return 1;

    struct CP_Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.warn_limit = warn_limit;
    snprintf(ctx.srcdir, sizeof(ctx.srcdir), "%s", src_dir);
    for( int l = 0; l < lane_count; l++ )
        ctx.lanes[l] = lanes[l];
    ctx.lane_count = lane_count;
    ctx.force_server = force_server;
    {
        struct stat self;

        if( stat(argv[0], &self) == 0 )
            ctx.writer = ((uint64_t)self.st_size << 32) ^ (uint64_t)self.st_mtime;
    }
    if( server_out )
        snprintf(ctx.server_out, sizeof(ctx.server_out), "%s", server_out);

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
    else if( (strcmp(command, "pack") == 0 || strcmp(command, "membership") == 0 ||
              strcmp(command, "missing") == 0) &&
             load_meta(src_dir, &ctx.profile) )
    {
        printf("Using the identity meta.ini recorded (game %d, revision %d)\n", ctx.profile.game,
               ctx.profile.revision);
    }
    else
    {
        fprintf(stderr, "cachepack: --rev is required (or run unpack first so meta.ini exists)\n");
        return 1;
    }

    /*
     * The whole server pack, already current: answered from stats against the
     * input list the last build recorded, before any name table is loaded —
     * loading them is most of what a build that changes nothing would cost.
     */
    if( strcmp(command, "pack") == 0 && server_only && sel.all && !force_server )
    {
        char server_dir[1100];

        if( server_out )
            snprintf(server_dir, sizeof(server_dir), "%s", server_out);
        else
            snprintf(server_dir, sizeof(server_dir), "%s/server/pack", src_dir);
        if( cp_server_up_to_date(src_dir, server_dir, (const char* const*)ctx.lanes, ctx.lane_count,
                                 argv[0]) )
            return 0;
    }

    if( !cp_names_load(&ctx.names, src_dir) )
        return 1;
    /* `server/scripts` may overlay records minted by an isolated client lane.
     * The compiler and runtime already layer each ported lane's pack; these commands
     * must resolve the same names. Import intentionally keeps the root-only view
     * so writing one lane cannot copy every other lane's ledger into it. */
    if( (strcmp(command, "pack") == 0 || strcmp(command, "membership") == 0 ||
         strcmp(command, "missing") == 0) &&
        !cp_names_load_ported_allocs(&ctx.names, src_dir, ctx.lanes, ctx.lane_count) )
    {
        cp_names_free(&ctx.names);
        return 1;
    }

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
        /* A param line's value is spelled by its param's declared type, which an
         * unpack reads from the cache it is unpacking, not from the text it is
         * about to rewrite. */
        printf("Typed %d param(s) from the cache\n", cp_param_types_from_cache(&ctx));

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
            /* The archives nothing decodes — undecoded config groups and the
             * nested gameval records — as raw bytes, so a pack with no --base
             * reproduces every archive the cache holds. Unconditional because
             * they cost a few hundred KB and skipping them is how idx17 and 21
             * config groups silently vanished from tree-only caches. */
            if( rc == 0 && !cp_raw_groups_export(&ctx) )
                rc = 1;
            if( rc == 0 && !cp_names_export_raw_gamevals(&ctx) )
                rc = 1;
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
            rc = cp_verify_run(&ctx, &sel, digest_dir) ? 0 : 1;
            /* Both halves run even when the first fails, so one invocation reports
             * every table that missed its bar rather than only the earliest. */
            if( want_assets && !cp_assets_verify(&ctx, asset_kinds, tmp_dir) )
                rc = 1;
        }
    }
    else if( strcmp(command, "pack") == 0 )
    {
        /*
         * `--check-only` answers "does every indexed archive have a file behind
         * it" without writing anything. A full pack copies the base cache and
         * emits 116,450 archives, which is too heavy to run on every `make test`
         * — it was OOM-killed the first time this bar ran that way.
         */
        if( !out_dir && !check_only && !server_only )
        {
            fprintf(stderr, "cachepack: --out is required for pack\n");
            cp_names_free(&ctx.names);
            return 1;
        }
        if( asset_only && (check_only || server_only || want_binary || want_gamevals || types_csv) )
        {
            fprintf(stderr, "cachepack: --asset-only cannot be combined with --check-only, "
                            "--server-only, --binary, --gamevals or --types\n");
            cp_names_free(&ctx.names);
            return 1;
        }
        if( asset_only && !want_assets )
        {
            fprintf(stderr, "cachepack: --asset-only requires --assets or --assets=<kinds>\n");
            cp_names_free(&ctx.names);
            return 1;
        }
        if( asset_only )
        {
            /* This is the authoring fast path. The caller may have made `out`
             * with clonefile/reflink already; in that case opening it in place
             * preserves copy-on-write sharing with the base cache. `--base`
             * remains available for callers that prefer cachepack's portable
             * byte copy. */
            if( base_dir && !cp_check_base_identity(src_dir, base_dir) )
            {
                cp_names_free(&ctx.names);
                return 1;
            }
            if( base_dir )
            {
                printf("Copying %s -> %s\n", base_dir, out_dir);
                if( tool_copy_cache_dir(base_dir, out_dir) != 0 )
                {
                    fprintf(stderr, "cachepack: failed to copy the base cache\n");
                    cp_names_free(&ctx.names);
                    return 1;
                }
            }
            if( !tool_dat2_open(out_dir, &ctx.profile, &ctx.cache) )
            {
                cp_names_free(&ctx.names);
                return 1;
            }
            ctx.cache_open = true;
            /* No config commit is required: the target began as a complete
             * cache and this mode only replaces named asset archives. */
            ctx.cache_committed = true;
            rc = 0;
        }
        else if( check_only )
        {
            /* `cp_pack_run` is what normally opens the cache, and check-only skips
             * it — so open it here. The reference table is what says which archives
             * exist, which is half the question this mode answers. */
            const char* probe = base_dir ? base_dir : cache_dir;

            if( !probe )
            {
                fprintf(stderr, "cachepack: --check-only needs --base or --cache\n");
                cp_names_free(&ctx.names);
                return 1;
            }
            if( !tool_dat2_open(probe, &ctx.profile, &ctx.cache) )
            {
                cp_names_free(&ctx.names);
                return 1;
            }
            ctx.cache_open = true;
            snprintf(ctx.cache_dir, sizeof(ctx.cache_dir), "%s", probe);
            rc = 0;
        }
        else if( server_only )
            rc = cp_pack_server_run(&ctx, &sel) ? 0 : 1;
        else
        {
            if( base_dir && !cp_check_base_identity(src_dir, base_dir) )
            {
                cp_names_free(&ctx.names);
                return 1;
            }
            rc = cp_pack_run(&ctx, &sel, base_dir, out_dir) ? 0 : 1;
        }
        /*
         * `writable` and not just `cache_open`: cache_open is set before the
         * config pass, so on an abort these three would have gone on writing
         * into a cache with no config table — a directory that looks like a
         * cache and cannot boot. --check-only writes nothing and is exempt.
         */
        bool writable = check_only || ctx.cache_committed;

        if( want_assets && ctx.cache_open && !writable )
            fprintf(stderr, "cachepack: the config pack did not commit — skipping "
                            "--assets/--binary/--gamevals rather than writing them into a "
                            "cache with no config table\n");

        /* The undecoded config groups, from their raw passthrough. Before the
         * assets so a failure surfaces next to the config report it belongs
         * with; skipped for --asset-only, whose target began as a complete
         * cache and already holds them. */
        if( !asset_only && ctx.cache_open && writable )
        {
            if( !cp_raw_groups_import(&ctx, check_only ? NULL : out_dir) )
                rc = 1;
        }
        if( want_assets && ctx.cache_open && writable )
        {
            if( !cp_assets_import(&ctx, check_only ? NULL : out_dir, asset_kinds,
                                  archive_list_path) )
                rc = 1;
        }
        /* After the configs and the assets, because it writes the *names* of what
         * they wrote — and after `--binary`, which may replace the very table
         * this is emitting into. */
        if( want_gamevals && ctx.cache_open && writable && !check_only )
        {
            if( !cp_names_emit_gamevals(&ctx, out_dir) )
                rc = 1;
        }
        if( want_binary && ctx.cache_open && writable )
        {
            /* After the configs, so a binary import of the config table (if the
             * caller asked for one) is the version that lands. */
            if( !cp_binary_import(&ctx, out_dir) )
                rc = 1;
        }
        if( want_assets && ctx.cache_open && writable && !check_only && rc == 0 )
        {
            if( !cp_scripts_link_check(&ctx) )
                rc = 1;
        }
    }
    else if( strcmp(command, "missing") == 0 )
        rc = cp_missing_run(&ctx, &sel) ? 0 : 1;
    else if( strcmp(command, "membership") == 0 )
    {
        /* No cache, no --out: the routing gates read the text tree and the name
         * packs and nothing else, which is the same reason `--server-only`
         * needs neither.
         *
         * `--check-only` is the same word `pack` and `ToriRSServer_Pack` use, and it
         * means the same thing here: read everything, write nothing, exit
         * non-zero on a disagreement. Seeding and checking are one command
         * because they walk and merge the same tree through the same gates —
         * a checker that reconstructed either would be checking itself. */
        if( check_only )
            rc = cp_membership_check(&ctx, &sel) ? 0 : 1;
        else
            rc = cp_membership_emit(&ctx, &sel) ? 0 : 1;
    }
    else
    {
        usage();
        rc = 1;
    }

    /* Before the closes below and before returning: the write path holds the
     * .dat2 and the last .idxN open across archives, and those handles are the
     * tool's, not the cache object's. Every write was flushed, so this is
     * tidiness rather than durability — but leaving OS handles open past the
     * point the cache is declared finished is how a later `rm -rf` on Windows
     * starts failing for no visible reason. */
    RSCache_Dat2DiskWriteFlush();

    if( ctx.cache_open )
        tool_dat2_close(&ctx.cache);
    if( ctx.compare_open )
        tool_dat2_close(&ctx.compare);
    cp_names_free(&ctx.names);
    free(ctx.param_types);
    return rc;
}
