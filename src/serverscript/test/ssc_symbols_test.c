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
    write_file(path,
               "[poh_room]\n"
               "columns=12\n"
               "columndef=0:name,string\n"
               "columndef=5:source_offset,int,int\n");

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
    check(resolved(&symbols, "poh_room:source_offset", SSC_SYM_DBCOLUMN) ==
              ((111 << 12) | (5 << 4)),
          "an exported columndef keeps its explicit cache column index");
    {
        const struct SSC_Symbol* column =
            SSC_SymbolsFind(&symbols, "poh_room:source_offset", SSC_SYM_DBCOLUMN);

        check(column && column->text && strcmp(column->text, "int,int") == 0,
              "an exported columndef keeps its tuple types");
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

    SSC_SymbolsFree(&symbols);
}

int
main(int argc, char** argv)
{
    const char* build_dir = argc > 1 ? argv[1] : "build";
    const char* content_dir =
        argc > 2 ? argv[2] : "../OSRS-Content/osrs239-content";

    test_fixture(build_dir);
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
