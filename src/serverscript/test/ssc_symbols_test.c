/*
 * The compiler's symbol table, against the register and against the real tree.
 *
 * This test exists because of a specific failure. `pack/npc.pack` moved to
 * `configs/all.npc.compack` and three asset namespaces were renamed
 * (`interface` -> `3_interfaces`, `synth` -> `4_soundeffects`,
 * `script` -> `12_clientscripts`). `SSC_SymbolsLoadPackDir` filters on a `.pack`
 * suffix and `kind_for_namespace` keys on the old names, so the compiler stopped
 * resolving almost everything — and **nothing said so**. `script.dat` on disk was
 * older than the change, so the server kept booting on stale bytecode, and the
 * serverscript tests all run against a vendored corpus rather than this tree.
 *
 * Three layers, because each catches something the others cannot:
 *
 *   1. a fixture tree, asserting the *kind* of each resolved name. Asserting a
 *      count instead would pass with every symbol landing in SSC_SYM_UNKNOWN,
 *      which is exactly the bug this is here for.
 *   2. the completeness invariant: every symbol kind the compiler can emit has
 *      at least one namespace in the register that maps to it. This needs no
 *      content tree, never skips, and goes red on the commit that renames a
 *      register row rather than a day later.
 *   3. probes against the real tree with stated ids. A count floor rots on the
 *      next content import; `hans = 3105` does not.
 */

#include "ssc.h"

#include "content/content_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_checks;
static int g_failures;

static void
check(int ok, const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("ssc-symbols: %-62s %s\n", what, ok ? "ok" : "FAILED");
}

static void
write_file(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");

    if( !f )
    {
        fprintf(stderr, "ssc-symbols: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

/** The id bound to `name` under `kind`, or -1. */
static int
resolved(struct SSC_Symbols* symbols, const char* name, enum SSC_SymbolKind kind)
{
    const struct SSC_Symbol* sym = SSC_SymbolsFind(symbols, name, kind);

    return sym ? sym->value : -1;
}

/* ------------------------------------------------------------------ */
/* 1. fixture tree                                                     */
/* ------------------------------------------------------------------ */

static void
test_fixture(const char* build_dir)
{
    struct SSC_Symbols symbols;
    char root[512], pack[512], configs[512], interfaces[512], path[640];

    snprintf(root, sizeof(root), "%s/ssc_symbols_fixture", build_dir);
    snprintf(pack, sizeof(pack), "%s/pack", root);
    snprintf(configs, sizeof(configs), "%s/configs", root);
    snprintf(interfaces, sizeof(interfaces), "%s/interfaces", root);
    mkdir(root, 0755);
    mkdir(pack, 0755);
    mkdir(configs, 0755);
    mkdir(interfaces, 0755);

    /* Archive-level packs, under their post-rename names. */
    snprintf(path, sizeof(path), "%s/3_interfaces.pack", pack);
    write_file(path, "12=bankmain\n");
    snprintf(path, sizeof(path), "%s/4_soundeffects.pack", pack);
    write_file(path, "7=door_open\n");
    snprintf(path, sizeof(path), "%s/category.pack", pack);
    write_file(path, "5=cooked_meat\n");
    snprintf(path, sizeof(path), "%s/stat.pack", pack);
    write_file(path, "1=defence\n");

    /* Member-level indexes, which is where every config type now lives. */
    snprintf(path, sizeof(path), "%s/all.npc.compack", configs);
    write_file(path, "3028=goblin\n");
    snprintf(path, sizeof(path), "%s/all.obj.compack", configs);
    write_file(path, "995=coins\n");
    snprintf(path, sizeof(path), "%s/all.loc.compack", configs);
    write_file(path, "1530=poordoor\n");
    snprintf(path, sizeof(path), "%s/all.param.compack", configs);
    write_file(path, "14=attackrate\n");
    snprintf(path, sizeof(path), "%s/all.dbtable.compack", configs);
    write_file(path, "111=poh_room\n");
    snprintf(path, sizeof(path), "%s/all.dbtable", configs);
    /* cachepack's rank-0 spelling: positional, with an ABSENT line per hole in
     * the cache's column numbering (1..4 here), `default=` beside the columns. */
    write_file(path,
               "[poh_room]\n"
               "column=name,string\n"
               "column=col1,ABSENT\n"
               "column=col2,ABSENT\n"
               "column=col3,ABSENT\n"
               "column=col4,ABSENT\n"
               "column=source_offset,int,int,INDEXED\n"
               "column=members,boolean\n"
               "default=members,true\n"
               "default=source_offset,1,2\n");

    /* One level down, which is why the walk has to recurse. */
    snprintf(path, sizeof(path), "%s/bankmain.compack", interfaces);
    write_file(path, "12=items\n");

    SSC_SymbolsInit(&symbols);
    SSC_SymbolsLoadPackDir(&symbols, pack);
    SSC_SymbolsLoadPackDir(&symbols, configs);
    SSC_SymbolsLoadDbTableDir(&symbols, configs);
    /* After the packs: it needs the interface ids to compose against. */
    SSC_SymbolsLoadComponentDir(&symbols, root);

    /*
     * The kind is the assertion, not the count. A regression that files every
     * name under SSC_SYM_UNKNOWN leaves the count untouched.
     */
    check(resolved(&symbols, "goblin", SSC_SYM_NPC) == 3028,
          "a .compack under configs/ resolves, and as an npc");
    check(resolved(&symbols, "coins", SSC_SYM_OBJ) == 995, "and objs");
    check(resolved(&symbols, "poordoor", SSC_SYM_LOC) == 1530, "and locs");
    check(resolved(&symbols, "attackrate", SSC_SYM_PARAM) == 14, "and params");
    check(resolved(&symbols, "poh_room:name", SSC_SYM_DBCOLUMN) == ((111 << 12) | (0 << 4)),
          "a rank-0 column before the hole is column 0");
    check(resolved(&symbols, "poh_room:source_offset", SSC_SYM_DBCOLUMN) ==
              ((111 << 12) | (5 << 4)),
          "the column after four ABSENT lines is column 5, not 1");
    check(resolved(&symbols, "poh_room:members", SSC_SYM_DBCOLUMN) == ((111 << 12) | (6 << 4)),
          "and the one after it is 6");
    check(SSC_SymbolsFind(&symbols, "poh_room:col1", SSC_SYM_DBCOLUMN) == NULL,
          "an ABSENT hole names no column");
    {
        const struct SSC_Symbol* column =
            SSC_SymbolsFind(&symbols, "poh_room:source_offset", SSC_SYM_DBCOLUMN);

        check(column && column->text && strncmp(column->text, "int,int", 7) == 0,
              "a rank-0 column keeps its tuple types");
    }
    check(resolved(&symbols, "bankmain", SSC_SYM_INTERFACE) == 12,
          "`3_interfaces.pack` still resolves as an interface");
    check(resolved(&symbols, "door_open", SSC_SYM_SYNTH) == 7,
          "`4_soundeffects.pack` still resolves as a synth");
    check(resolved(&symbols, "cooked_meat", SSC_SYM_CATEGORY) == 5, "categories");
    check(resolved(&symbols, "defence", SSC_SYM_STAT) == 1, "stats");

    /*
     * Composed, not loaded. A compack binds a *local* child id, so reading
     * `interfaces/bankmain.compack` directly would bind `items` to 12 rather than
     * to bankmain's twelfth child. The client addresses a component as
     * `(interface << 16) | child`, and so must the compiler: 12<<16 | 12.
     */
    check(resolved(&symbols, "bankmain:items", SSC_SYM_COMPONENT) == ((12 << 16) | 12),
          "a component composes to (interface << 16) | child");

    /* And the negative: a name must not resolve under the wrong kind. */
    check(SSC_SymbolsFind(&symbols, "goblin", SSC_SYM_OBJ) == NULL,
          "an npc name does not answer as an obj");

    SSC_SymbolsFree(&symbols);
}

/* ------------------------------------------------------------------ */
/* 1b. the full-key markers                                            */
/* ------------------------------------------------------------------ */

/*
 * `key=default` / `key=empty` (content/content_value.h) through the three
 * config-text readers the compiler has: the dbtable schema walk, the varbit
 * carrier walk over `all.varbit`, and the `.varp` declaration walk.
 *
 * Each was a silent wrong answer before: `column=default` composed a
 * `table:default` column symbol a script could reference, `startbit=default`
 * was bit 0, and `basevar=default` resolved a varp that happens to be called
 * `default` -- named here on purpose, so the test can see the difference
 * between "no carrier" and "a lookup that happened to miss".
 */
static void
test_markers(const char* build_dir)
{
    struct SSC_Symbols symbols;
    char root[512], configs[512], scripts[512], path[640];
    const struct SSC_VarpCarrier* carrier;

    snprintf(root, sizeof(root), "%s/ssc_symbols_markers", build_dir);
    snprintf(configs, sizeof(configs), "%s/configs", root);
    snprintf(scripts, sizeof(scripts), "%s/scripts", root);
    mkdir(root, 0755);
    mkdir(configs, 0755);
    mkdir(scripts, 0755);

    snprintf(path, sizeof(path), "%s/all.dbtable.compack", configs);
    write_file(path, "111=poh_room\n112=t_markers\n113=t_auth\n114=t_retired\n");
    snprintf(path, sizeof(path), "%s/all.dbtable", configs);
    write_file(path,
               "[poh_room]\n"
               "column=name,string\n"
               "default=empty\n"
               "\n"
               "[t_markers]\n"
               "column=empty\n"
               "default=default\n"
               "\n"
               "[t_retired]\n"
               "columns=1\n"
               "columndef=0:old_name,string\n");
    snprintf(path, sizeof(path), "%s/t.dbtable", scripts);
    write_file(path,
               "[t_auth]\n"
               "column=default\n");

    snprintf(path, sizeof(path), "%s/all.varp.compack", configs);
    write_file(path, "5=carrier\n6=default\n");
    snprintf(path, sizeof(path), "%s/all.varbit.compack", configs);
    write_file(path, "0=vb_a\n1=vb_b\n");
    snprintf(path, sizeof(path), "%s/all.varbit", configs);
    write_file(path,
               "[vb_a]\n"
               "basevar=carrier\n"
               "startbit=default\n"
               "endbit=3\n"
               "\n"
               "[vb_b]\n"
               "basevar=default\n"
               "startbit=0\n"
               "endbit=0\n");
    snprintf(path, sizeof(path), "%s/t.varp", scripts);
    write_file(path,
               "[carrier]\n"
               "wholewrite=default\n"
               "wholeread=default\n");

    SSC_SymbolsInit(&symbols);
    SSC_SymbolsLoadPackDir(&symbols, configs);
    SSC_SymbolsLoadDbTableDir(&symbols, configs);
    SSC_SymbolsLoadDbTableDir(&symbols, scripts);
    SSC_SymbolsLoadVarbitBases(&symbols, configs);
    SSC_SymbolsLoadVarpDecls(&symbols, scripts);

    check(resolved(&symbols, "poh_room:name", SSC_SYM_DBCOLUMN) == ((111 << 12) | (0 << 4)),
          "markers: a plain column beside `default=empty` still composes");
    check(SSC_SymbolsFind(&symbols, "t_markers:empty", SSC_SYM_DBCOLUMN) == NULL &&
              SSC_SymbolsFind(&symbols, "t_markers:default", SSC_SYM_DBCOLUMN) == NULL,
          "markers: `column=empty` names no column");
    check(SSC_SymbolsFind(&symbols, "t_retired:old_name", SSC_SYM_DBCOLUMN) == NULL,
          "the retired `columndef=` spelling is refused, not read");
    check(SSC_SymbolsFind(&symbols, "t_auth:default", SSC_SYM_DBCOLUMN) == NULL,
          "markers: `column=default` is not a column called \"default\"");

    carrier = SSC_SymbolsCarrier(&symbols, 5);
    check(carrier && carrier->bits == 1, "markers: the stated basevar still carries its varbit");
    check(carrier && carrier->sample_count == 1 && carrier->sample_start[0] == -1 &&
              carrier->sample_end[0] == 3,
          "markers: `startbit=default` is no bit (-1), not bit 0");
    check(SSC_SymbolsCarrier(&symbols, 6) == NULL,
          "markers: `basevar=default` is no carrier, not the varp named default");
    check(symbols.exempt_count == 0 && symbols.read_exempt_count == 0,
          "markers: `wholewrite=default` / `wholeread=default` exempt nothing");

    SSC_SymbolsFree(&symbols);
}

/* ------------------------------------------------------------------ */
/* 2. the completeness invariant                                       */
/* ------------------------------------------------------------------ */

/*
 * Every kind the compiler can emit must have somewhere to come from.
 *
 * This is the check that would have caught the rename on the commit that made
 * it: renaming the `interface` register row orphaned SSC_SYM_INTERFACE, and
 * nothing anywhere noticed. It reads two in-repo tables and needs no tree.
 *
 * The three exemptions are structural rather than convenient. CONSTANT comes
 * from `.constant` files, SCRIPT from the compiled scripts themselves, and
 * UNKNOWN is the miss sentinel — none is a namespace and none has a pack file.
 */
static void
test_completeness(void)
{
    struct ContentRegister reg;
    int orphans = 0;

    ContentRegister_Defaults(&reg);

    for( enum SSC_SymbolKind kind = SSC_SYM_UNKNOWN + 1; kind < SSC_SYM_KIND_COUNT;
         kind = (enum SSC_SymbolKind)(kind + 1) )
    {
        int found = 0;

        if( kind == SSC_SYM_CONSTANT || kind == SSC_SYM_SCRIPT )
            continue;
        /* These four are the language's own vocabulary, not content namespaces:
         * npc modes, loc shapes, script var types and db columns are spelled by
         * the compiler, so no pack file backs them. */
        if( kind == SSC_SYM_NPC_MODE || kind == SSC_SYM_LOCSHAPE ||
            kind == SSC_SYM_TYPE || kind == SSC_SYM_DBCOLUMN )
            continue;

        for( int i = 0; i < reg.count; i++ )
        {
            if( SSC_SymbolKindForNamespace(reg.entries[i].name) == kind )
            {
                found = 1;
                break;
            }
        }
        if( !found )
        {
            printf("ssc-symbols:   no register namespace maps to symbol kind %d\n",
                   (int)kind);
            orphans++;
        }
    }
    check(orphans == 0, "every symbol kind has a namespace that maps to it");
}

/* ------------------------------------------------------------------ */
/* 3. the real tree                                                    */
/* ------------------------------------------------------------------ */

static int
directory_exists(const char* path)
{
    struct stat info;

    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/*
 * Named probes with stated ids, not a count floor — a floor rots the next time
 * anyone imports content, and a rotted floor gets lowered rather than
 * investigated.
 *
 * Skipping is loud and says why, the discipline
 * `test/test_cachepack_fidelity.sh` already follows: a check that silently
 * passes when its corpus is absent is worse than no check.
 */
static void
test_live_tree(const char* content_dir)
{
    struct SSC_Symbols symbols;
    char pack[512], configs[512], interfaces[512];

    if( !directory_exists(content_dir) )
    {
        printf("ssc-symbols: SKIPPED live tree — no content at %s\n", content_dir);
        printf("ssc-symbols:   (git submodule update --init OSRS-Content)\n");
        return;
    }

    snprintf(pack, sizeof(pack), "%s/pack", content_dir);
    snprintf(configs, sizeof(configs), "%s/configs", content_dir);
    snprintf(interfaces, sizeof(interfaces), "%s/interfaces", content_dir);

    SSC_SymbolsInit(&symbols);
    SSC_SymbolsLoadPackDir(&symbols, pack);
    SSC_SymbolsLoadPackDir(&symbols, configs);

    check(resolved(&symbols, "goblin", SSC_SYM_NPC) > 0, "live tree: goblin is an npc");
    check(resolved(&symbols, "bones", SSC_SYM_OBJ) > 0, "live tree: bones is an obj");
    check(resolved(&symbols, "attack", SSC_SYM_STAT) == 0, "live tree: attack is stat 0");
    check(resolved(&symbols, "cooked_meat", SSC_SYM_CATEGORY) == 5,
          "live tree: cooked_meat is category 5");
    check(resolved(&symbols, "bankmain", SSC_SYM_INTERFACE) > 0,
          "live tree: bankmain is an interface");
    SSC_SymbolsLoadComponentDir(&symbols, content_dir);
    check(resolved(&symbols, "bankmain:items", SSC_SYM_COMPONENT) > 0,
          "live tree: bankmain:items composes");
    check(resolved(&symbols, "attackrate", SSC_SYM_PARAM) == 14,
          "live tree: attackrate is param 14");
    /* A pack line such as `12=pvp_store_handle_event_12 hashcode(-1603147075)`
     * (pack/12_clientscripts.pack) carries provenance for an archive the cache
     * names only by hash; the name ends before it. Without the cut every entry
     * of the packs that carry one loaded under a name no script can spell --
     * which is how `split_init(..., font_496)` once failed against a pack that
     * listed it. The fonts now carry their glyph sprite's name (OSRS-Content
     * 5396b6a18e: 496 is b12_full, no suffix). */
    check(resolved(&symbols, "pvp_store_handle_event_12", SSC_SYM_SCRIPT) == 12,
          "live tree: a hashcode suffix is not part of the name");
    check(resolved(&symbols, "b12_full", SSC_SYM_FONTMETRICS) == 496,
          "live tree: font 496 is b12_full");

    SSC_SymbolsFree(&symbols);
}

int
main(int argc, char** argv)
{
    const char* build_dir = argc > 1 ? argv[1] : "build";
    const char* content_dir =
        argc > 2 ? argv[2] : "../OSRS-Content/osrs239-content";

    test_fixture(build_dir);
    test_markers(build_dir);
    test_completeness();
    test_live_tree(content_dir);

    if( g_failures )
    {
        printf("ssc-symbols: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("ssc-symbols: all %d checks passed\n", g_checks);
    return 0;
}
