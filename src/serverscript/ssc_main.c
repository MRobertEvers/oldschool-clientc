/*
 * sscompile — RuneScript source to script.dat / script.idx.
 *
 *   sscompile --src DIR --out DIR [--pack DIR]... [--component-root DIR]...
 *             [--constants DIR] [--content-root DIR] [--seams DIR]...
 *             [--lane NAME]... [--no-lane NAME]... [--list-lanes]
 *
 * --src        directory of .rs2 sources, searched recursively
 * --out        where script.dat and script.idx are written
 * --pack       directory of id=name .pack files (defaults to <src>/../pack).
 *              Repeatable, and later directories are loaded after earlier ones —
 *              a content tree splits its names between the ones the cache holds
 *              and the ones only the server has.
 * --component-root
 *              additional content roots holding interface member indexes. This is
 *              how a feature overlay contributes component symbols without
 *              leaking its records into the ordinary client walk.
 * --constants  root to search for .constant files (defaults to <src>)
 * --content-root
 *              the tree holding `ported/` (defaults to <src>/../..)
 * --seams      a source root whose declarations are defaults a lane may replace
 *              (defaults to <src>/lane_seams when that directory exists)
 * --lane       compile this lane, named by its directory under `ported/`
 * --no-lane    leave this lane out even though its descriptor says default=on
 * --list-lanes print the lanes this tree declares, and stop
 *
 * Lanes are described by `<content-root>/ported/<lane>/lane.ini` and nothing
 * else — see ssc_lane.h. A lane the build does not select contributes no
 * scripts, no symbols and no component names, and its feature constant reads 0;
 * its server scripts are subtracted from the `--src` walk they live inside.
 *
 * Sources are compiled in sorted path order so script ids are stable across
 * machines. That matters more than it looks: a gosub is compiled to a script
 * *id*, so an unstable ordering silently repoints every call in the pack.
 */

#include "ssc.h"
#include "ssc_lane.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum
{
    /* Explicit --pack roots plus every enabled lane's. Was 8, which the four
     * lanes and two defaults already filled. */
    SSC_MAIN_MAX_PACKS = 64,
    SSC_MAIN_MAX_COMPONENT_ROOTS = 64,
    SSC_MAIN_MAX_SOURCE_ROOTS = 64
};

static void
usage(void)
{
    fprintf(stderr,
            "usage: sscompile --src DIR --out DIR [--pack DIR]... "
            "[--component-root DIR]... [--constants DIR] [--content-root DIR] "
            "[--seams DIR]... [--lane NAME]... [--no-lane NAME]... [--list-lanes]\n");
}

static int
path_exists(const char* path)
{
    struct stat info;

    return stat(path, &info) == 0;
}

int
main(int argc, char** argv)
{
    const char* src = NULL;
    const char* out = NULL;
    const char* packs[SSC_MAIN_MAX_PACKS];
    int pack_count = 0;
    const char* component_roots[SSC_MAIN_MAX_COMPONENT_ROOTS];
    int component_root_count = 0;
    const char* constants = NULL;
    const char* content_root = NULL;
    const char* seams[SSC_MAIN_MAX_SOURCE_ROOTS];
    int seam_count = 0;
    const char* wanted_lanes[SSC_LANE_MAX];
    int wanted_lane_count = 0;
    const char* unwanted_lanes[SSC_LANE_MAX];
    int unwanted_lane_count = 0;
    int list_lanes = 0;
    struct SSC_LaneSet lane_set;
    struct SSC_SourceRoot source_roots[SSC_MAIN_MAX_SOURCE_ROOTS];
    int source_root_count = 0;
    /* Every lane's script directories, plus the seam roots. */
    const char* excludes[SSC_LANE_MAX * SSC_LANE_PATHS_MAX + SSC_MAIN_MAX_SOURCE_ROOTS];
    int exclude_count = 0;
    int enabled_lane_count = 0;
    char pack_default[1024];
    char configs_default[1024];
    char content_root_default[1024];
    char seam_default[1024];
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    int symbol_count = 0;
    int component_count = 0;
    int constant_count = 0;
    int dbcolumn_count = 0;
    int i;
    int j;
    int status = 0;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--src") == 0 && i + 1 < argc )
            src = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out = argv[++i];
        else if( strcmp(argv[i], "--pack") == 0 && i + 1 < argc )
        {
            if( pack_count == (int)(sizeof(packs) / sizeof(packs[0])) )
            {
                fprintf(stderr, "sscompile: too many --pack directories\n");
                return 2;
            }
            packs[pack_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--constants") == 0 && i + 1 < argc )
            constants = argv[++i];
        else if( strcmp(argv[i], "--component-root") == 0 && i + 1 < argc )
        {
            if( component_root_count ==
                (int)(sizeof(component_roots) / sizeof(component_roots[0])) )
            {
                fprintf(stderr, "sscompile: too many --component-root directories\n");
                return 2;
            }
            component_roots[component_root_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--content-root") == 0 && i + 1 < argc )
            content_root = argv[++i];
        else if( strcmp(argv[i], "--seams") == 0 && i + 1 < argc )
        {
            if( seam_count == (int)(sizeof(seams) / sizeof(seams[0])) )
            {
                fprintf(stderr, "sscompile: too many --seams directories\n");
                return 2;
            }
            seams[seam_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--lane") == 0 && i + 1 < argc )
        {
            if( wanted_lane_count == (int)(sizeof(wanted_lanes) / sizeof(wanted_lanes[0])) )
            {
                fprintf(stderr, "sscompile: too many --lane names\n");
                return 2;
            }
            wanted_lanes[wanted_lane_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--no-lane") == 0 && i + 1 < argc )
        {
            if( unwanted_lane_count ==
                (int)(sizeof(unwanted_lanes) / sizeof(unwanted_lanes[0])) )
            {
                fprintf(stderr, "sscompile: too many --no-lane names\n");
                return 2;
            }
            unwanted_lanes[unwanted_lane_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--list-lanes") == 0 )
            list_lanes = 1;
        else
        {
            usage();
            return 2;
        }
    }

    if( !src || (!out && !list_lanes) )
    {
        usage();
        return 2;
    }

    if( !content_root )
    {
        /* `--src` points at server/scripts, so the tree root is two levels up —
         * the same relationship the pack defaults below rely on. */
        snprintf(content_root_default, sizeof(content_root_default), "%s/../..", src);
        content_root = content_root_default;
    }

    /*
     * Lanes, before anything is loaded: they contribute packs, component roots,
     * constants and sources, and subtract sources.
     */
    if( SSC_LanesDiscover(&lane_set, content_root) < 0 )
        return 2;
    for( i = 0; i < lane_set.count; i++ )
        lane_set.lanes[i].enabled = lane_set.lanes[i].enabled_by_default;
    for( i = 0; i < wanted_lane_count; i++ )
    {
        struct SSC_Lane* lane = SSC_LaneFind(&lane_set, wanted_lanes[i]);

        /*
         * Fatal, and it has to be. A misspelled lane name that merely warned
         * would produce a pack missing a whole body of content, which reads as
         * "the feature is broken" rather than "the build did not include it" —
         * the exact confusion lanes exist to end.
         */
        if( !lane )
        {
            fprintf(stderr, "sscompile: no lane '%s' under %s/ported\n", wanted_lanes[i],
                    content_root);
            fprintf(stderr, "  declared lanes:%s", lane_set.count ? "" : " (none)");
            for( j = 0; j < lane_set.count; j++ )
                fprintf(stderr, " %s", lane_set.lanes[j].name);
            fprintf(stderr, "\n");
            return 2;
        }
        lane->enabled = 1;
    }
    for( i = 0; i < unwanted_lane_count; i++ )
    {
        struct SSC_Lane* lane = SSC_LaneFind(&lane_set, unwanted_lanes[i]);

        if( !lane )
        {
            fprintf(stderr, "sscompile: no lane '%s' under %s/ported\n", unwanted_lanes[i],
                    content_root);
            return 2;
        }
        lane->enabled = 0;
    }

    if( list_lanes )
    {
        for( i = 0; i < lane_set.count; i++ )
        {
            const struct SSC_Lane* lane = &lane_set.lanes[i];

            printf("%s\t%s\tdefault=%s\tconstant=%s\n", lane->name,
                   lane->enabled ? "on" : "off", lane->enabled_by_default ? "on" : "off",
                   lane->constant[0] ? lane->constant : "-");
        }
        return 0;
    }

    /*
     * The tree's own defaults before any lane's, so `pack_count == 0` still
     * means "the caller named no pack directory" rather than "no lane did".
     */
    if( pack_count == 0 )
    {
        /*
         * Both levels of index, because a tree keeps them in different places:
         * `pack/` names the archives of each cache index, `configs/` names the
         * records inside them. `--src` points at `server/scripts`, so the tree
         * root is two levels up.
         */
        snprintf(pack_default, sizeof(pack_default), "%s/../../pack", src);
        packs[pack_count++] = pack_default;
        snprintf(configs_default, sizeof(configs_default), "%s/../../configs", src);
        packs[pack_count++] = configs_default;
    }

    for( i = 0; i < lane_set.count; i++ )
    {
        struct SSC_Lane* lane = &lane_set.lanes[i];

        if( lane->enabled )
            enabled_lane_count++;
        for( j = 0; j < lane->script_count; j++ )
        {
            /*
             * A lane's server scripts sit *inside* the `--src` tree, so the
             * exclusion is unconditional and the lane's own root is what puts
             * them back. Excluding only when the lane was off looked equivalent
             * and was not: an enabled lane was then walked twice, once by `--src`
             * and once as its own root, and every name in it was a duplicate
             * declaration. An exclusion never cancels its own root
             * (SSC_CompileRoots), which is what makes the pair work.
             *
             * A lane whose scripts directory does not exist excludes nothing and
             * adds an empty root; both are no-ops.
             */
            excludes[exclude_count++] = lane->scripts[j];
            if( !lane->enabled )
                continue;
            if( source_root_count == SSC_MAIN_MAX_SOURCE_ROOTS )
            {
                fprintf(stderr, "sscompile: too many lane source roots\n");
                return 2;
            }
            source_roots[source_root_count].dir = lane->scripts[j];
            source_roots[source_root_count].weak = 0;
            source_root_count++;
        }
        if( !lane->enabled )
            continue;
        for( j = 0; j < lane->pack_count; j++ )
        {
            if( pack_count == SSC_MAIN_MAX_PACKS )
            {
                fprintf(stderr, "sscompile: too many --pack directories\n");
                return 2;
            }
            packs[pack_count++] = lane->packs[j];
        }
        for( j = 0; j < lane->component_root_count; j++ )
        {
            if( component_root_count == SSC_MAIN_MAX_COMPONENT_ROOTS )
            {
                fprintf(stderr, "sscompile: too many --component-root directories\n");
                return 2;
            }
            component_roots[component_root_count++] = lane->component_roots[j];
        }
    }

    if( !constants )
        constants = src;

    SSC_SymbolsInit(&symbols);
    for( i = 0; i < pack_count; i++ )
    {
        int loaded = SSC_SymbolsLoadPackDir(&symbols, packs[i]);

        /*
         * A missing --pack directory warns rather than fails.
         *
         * It used to be fatal, which looked careful and was not: a content tree
         * splits its names across several directories and the *set* of them
         * changes as the tree is reorganised, so one that has not been created
         * yet stopped the build for every tree, including the ones that never
         * had it. Nothing can pass silently either way — a name that does not
         * resolve is still a hard compile error a few lines below, so the only
         * thing tolerating this loses is a worse diagnostic for a typo'd path.
         */
        if( loaded < 0 )
        {
            fprintf(stderr, "sscompile: no symbol packs at %s (skipping)\n", packs[i]);
            continue;
        }
        symbol_count += loaded;
    }
    /* A lane's single-file indexes, after its directories: same table, and the
     * file is named because the directory around it must not be read. */
    for( i = 0; i < lane_set.count; i++ )
    {
        struct SSC_Lane* lane = &lane_set.lanes[i];

        if( !lane->enabled )
            continue;
        for( j = 0; j < lane->pack_file_count; j++ )
        {
            int loaded = SSC_SymbolsLoadPackFile(&symbols, lane->pack_files[j]);

            if( loaded < 0 )
            {
                fprintf(stderr, "sscompile: lane '%s' names a missing index %s\n", lane->name,
                        lane->pack_files[j]);
                SSC_SymbolsFree(&symbols);
                return 1;
            }
            symbol_count += loaded;
        }
    }
    /*
     * Components, after the packs because they compose against the interface ids
     * those load. There is no `pack/component.pack`: a component's name lives in
     * `interfaces/<name>.compack` and its id is `(interface << 16) | child`.
     */
    component_count = SSC_SymbolsLoadComponentDir(&symbols, content_root);
    for( i = 0; i < component_root_count; i++ )
    {
        int loaded = SSC_SymbolsLoadComponentDir(&symbols, component_roots[i]);

        if( loaded < 0 )
        {
            fprintf(stderr, "sscompile: no component indexes at %s (skipping)\n",
                    component_roots[i]);
            continue;
        }
        component_count += loaded;
    }
    /* Imported cache schemas live beside their all.<type>.compack indexes.
     * Load them only after every pack directory has contributed table names;
     * a column token needs the table id in order to be composed. */
    for( i = 0; i < pack_count; i++ )
    {
        int loaded = SSC_SymbolsLoadDbTableDir(&symbols, packs[i]);

        if( loaded > 0 )
            dbcolumn_count += loaded;
    }
    constant_count = SSC_SymbolsLoadConstantDir(&symbols, constants);
    {
        int loaded = SSC_SymbolsLoadDbTableDir(&symbols, constants);

        if( loaded > 0 )
            dbcolumn_count += loaded;
    }
    /*
     * Every lane's flag, both ways round.
     *
     * A lane that is off still has to *have* its constant, at 0: shared files
     * test `^curses_enabled` unconditionally, and an undefined constant is a
     * compile error rather than a false. This is the build's answer, not
     * content's — no file in the tree can know which lanes this pack was asked
     * for — which is why it is declared here rather than staged into a copy of
     * the constant tree, as it was when a python step owned the same two values.
     */
    for( i = 0; i < lane_set.count; i++ )
    {
        struct SSC_Lane* lane = &lane_set.lanes[i];
        char origin[256];

        if( !lane->constant[0] )
            continue;
        snprintf(origin, sizeof(origin), "lane %s", lane->name);
        if( !SSC_SymbolsDefineConstant(&symbols, lane->constant, lane->enabled ? "1" : "0",
                                       origin) )
        {
            fprintf(stderr, "sscompile: lane '%s' cannot declare ^%s\n", lane->name,
                    lane->constant);
            SSC_SymbolsFree(&symbols);
            return 1;
        }
        constant_count += (constant_count >= 0);
    }
    SSC_SymbolsSeedBuiltins(&symbols);

    /*
     * Which varps other variables live inside, and which of those content has
     * declared it may still write whole. Both after the packs, because the first
     * resolves `basevar=<name>` through the varp symbols and the second resolves
     * the `[section]` headers of a `.varp` the same way.
     */
    for( i = 0; i < pack_count; i++ )
        SSC_SymbolsLoadVarbitBases(&symbols, packs[i]);
    SSC_SymbolsLoadVarpDecls(&symbols, src);

    /* The two exemptions are counted apart because they are not the same claim:
     * `wholewrite` licenses destroying a neighbour's variable, `wholeread` only
     * licenses reading the packed word. One number would have hidden a rise in
     * the first behind a rise in the second. */
    printf("symbols: %d from packs, %d components, %d constants, %d db columns, "
           "%d carrier varp(s), %d whole-write exemption(s), %d whole-read\n",
           symbol_count, component_count, constant_count < 0 ? 0 : constant_count,
           dbcolumn_count < 0 ? 0 : dbcolumn_count, symbols.carrier_count,
           symbols.exempt_count, symbols.read_exempt_count);

    /* Named, not counted. "3 of 4 lanes" is exactly the report that lets a
     * session spend an hour on content that was never compiled. */
    if( lane_set.count )
    {
        printf("lanes: %d of %d compiled —", enabled_lane_count, lane_set.count);
        for( i = 0; i < lane_set.count; i++ )
            printf(" %s=%s", lane_set.lanes[i].name, lane_set.lanes[i].enabled ? "on" : "off");
        printf("\n");
    }

    /*
     * Before a single line is compiled, and fatal.
     *
     * A table that answers a name two ways does not fail loudly later — it
     * compiles, to whichever answer the sort happened to put first. See
     * SSC_SymbolsValidate for the two rules and why both cost nothing today.
     */
    {
        int problems = SSC_SymbolsValidate(&symbols);

        if( problems )
        {
            fprintf(stderr, "sscompile: %d symbol-table problem(s) — refusing to compile\n",
                    problems);
            SSC_SymbolsFree(&symbols);
            return 1;
        }
    }

    compiler = SSC_New(&symbols);
    if( !compiler )
    {
        fprintf(stderr, "sscompile: out of memory\n");
        SSC_SymbolsFree(&symbols);
        return 1;
    }

    /*
     * `--src` last among the strong roots and the seams last of all, so the
     * declaration order is: the base tree, then the lanes it does have, then the
     * defaults for the lanes it does not. Only that order lets a seam see that
     * its name is already taken (SSC_CompileRoots).
     */
    if( source_root_count == SSC_MAIN_MAX_SOURCE_ROOTS )
    {
        fprintf(stderr, "sscompile: too many source roots\n");
        SSC_Free(compiler);
        SSC_SymbolsFree(&symbols);
        return 1;
    }
    source_roots[source_root_count].dir = src;
    source_roots[source_root_count].weak = 0;
    source_root_count++;
    /* The convention, so a tree that has seams does not have to say so on every
     * command line and one that has none needs no flag at all. */
    if( seam_count == 0 )
    {
        snprintf(seam_default, sizeof(seam_default), "%s/lane_seams", src);
        if( path_exists(seam_default) )
            seams[seam_count++] = seam_default;
    }
    for( i = 0; i < seam_count; i++ )
    {
        if( source_root_count == SSC_MAIN_MAX_SOURCE_ROOTS )
        {
            fprintf(stderr, "sscompile: too many source roots\n");
            SSC_Free(compiler);
            SSC_SymbolsFree(&symbols);
            return 1;
        }
        source_roots[source_root_count].dir = seams[i];
        source_roots[source_root_count].weak = 1;
        source_root_count++;
    }
    /* The seam directory lives under `--src`, so it would otherwise be walked
     * twice — once as a strong root and once as a weak one, which is a duplicate
     * of every name in it. */
    for( i = 0; i < seam_count; i++ )
        excludes[exclude_count++] = seams[i];

    memset(&diag, 0, sizeof(diag));
    if( !SSC_CompileRoots(compiler, source_roots, source_root_count, excludes, exclude_count,
                          &diag) )
    {
        if( diag.file[0] )
            fprintf(stderr, "%s:%d: %s\n", diag.file, diag.line, diag.message);
        else
            fprintf(stderr, "sscompile: %s\n", diag.message);
        status = 1;
    }
    else if( !SSC_Write(compiler, out, &diag) )
    {
        fprintf(stderr, "sscompile: %s\n", diag.message);
        status = 1;
    }
    else
    {
        printf("compiled %d scripts to %s/script.dat\n", SSC_ScriptCount(compiler), out);
    }

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);
    return status;
}
