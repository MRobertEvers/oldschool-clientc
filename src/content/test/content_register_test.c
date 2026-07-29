/*
 * The namespace register: defaults, overlay, and pack-filename resolution.
 *
 * Worth a test of its own because the register's failure mode is *silence*.
 * A tree that misspells `content.ini`, or writes `[varp]` where
 * `[namespace:varp]` was meant, gets the built-in defaults and loads perfectly
 * — with whatever it was trying to declare quietly ignored. So the assertions
 * that matter here are the ones proving the file was read at all.
 *
 * Run: make -C src test-content-register
 */

#include "content/content_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("content-register: %-52s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

static void
write_file(
    const char* path,
    const char* text)
{
    FILE* file = fopen(path, "wb");

    if( !file )
    {
        fprintf(stderr, "could not write %s\n", path);
        exit(1);
    }
    fputs(text, file);
    fclose(file);
}

int
main(void)
{
    struct ContentRegister reg;
    const struct ContentNamespace* ns;
    /* Not `build/content_register_test` — that is the test binary's own path,
     * and mkdir onto an existing file fails. */
    const char* dir = "build/content_register_tree";
    char path[512];

    /* ---- defaults ---------------------------------------------------- */

    ContentRegister_Defaults(&reg);
    check(reg.count > 0, "defaults declare namespaces");
    check(reg.from_file == 0, "defaults are not marked as from a file");

    ns = ContentRegister_Find(&reg, "varp");
    check(ns != NULL, "varp is a default namespace");
    check(ns && ns->ids == CONTENT_IDS_CACHE, "varp ids come from the cache");
    check(ns && ns->shared_var_domain, "varp shares the %name domain");

    ns = ContentRegister_Find(&reg, "stat");
    check(ns && ns->ids == CONTENT_IDS_PROTOCOL, "stat ids are the protocol's");
    check(ns && ns->names == CONTENT_NAMES_AUTHORED, "and its names are authored");

    check(ContentRegister_Find(&reg, "nonesuch") == NULL, "an unknown namespace is NULL");

    /* The compiler's whole filename table is this one call. */
    ns = ContentRegister_ForPackFile(&reg, "varbit.pack");
    check(ns && strcmp(ns->name, "varbit") == 0, "varbit.pack resolves to varbit");
    check(ContentRegister_ForPackFile(&reg, "varbit.txt") == NULL,
          "a non-.pack file resolves to nothing");
    check(ContentRegister_ForPackFile(&reg, "nonesuch.pack") == NULL,
          "an unregistered pack resolves to nothing");
    check(ContentRegister_ForPackFile(&reg, ".pack") == NULL, "and so does a bare .pack");

    /* ---- overlay ----------------------------------------------------- */

    mkdir("build", 0755);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/content.ini", dir);

    write_file(path,
               "; a comment\n"
               "[namespace:varp]\n"
               "names = imported\n"
               "\n"
               "[namespace:brandnew]\n"
               "ids   = server\n"
               "names = derived\n"
               "\n"
               "[something:else]\n"
               "ids = cache\n");

    {
        int before = 0;

        ContentRegister_Defaults(&reg);
        before = reg.count;

        ContentRegister_Load(&reg, dir);
        check(reg.from_file == 1, "a present content.ini is marked as read");

        /* Overlay, not replace: the twenty namespaces the file says nothing
         * about have to survive, or adopting the register would mean
         * transcribing everything that already worked. */
        check(reg.count == before + 1, "declaring one namespace adds one, keeps the rest");
        check(ContentRegister_Find(&reg, "npc") != NULL, "an undeclared default survives");

        ns = ContentRegister_Find(&reg, "varp");
        check(ns && ns->names == CONTENT_NAMES_IMPORTED, "a declared key overrides");
        check(ns && ns->ids == CONTENT_IDS_CACHE, "an omitted key keeps its default");
        check(ns && ns->shared_var_domain, "and so does an omitted vardomain");

        ns = ContentRegister_Find(&reg, "brandnew");
        check(ns != NULL, "a namespace the defaults never had is added");
        check(ns && ns->ids == CONTENT_IDS_SERVER, "with its stated id authority");
        check(ns && ns->names == CONTENT_NAMES_DERIVED, "and its stated name authority");

        check(ContentRegister_Find(&reg, "else") == NULL,
              "a section that is not [namespace:*] is ignored");
    }

    /* ---- missing file ------------------------------------------------ */

    {
        ContentRegister_Load(&reg, "build/content_register_tree/does-not-exist");
        check(reg.from_file == 0, "a missing content.ini falls back to defaults");
        check(ContentRegister_Find(&reg, "varp") != NULL, "and the defaults are intact");
    }

    printf("content-register: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
