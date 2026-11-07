/*
 * maped_cs2_names: the `--names` directory for the editor server's CS2 job
 * (workbench 6.6, torirsmaped/torirs_maped_build.h TORIRSMAPED_BUILD_CS2),
 * run by `make -C src maped-cs2-compile` (src/maped/records.mk) before
 * `cs2 compile`, in the job's own process:
 *
 *   maped_cs2_names <scripts-dir> <runestar-names-dir or ""> <out-dir>
 *
 * EditorCs2Catalog_WriteNamesDir (iface_model.h): every RuneStar table
 * linked, and script-names.tsv from the content tree's .cs2 headers. The
 * content names its scripts and RuneStar does not, so a `~call` to a content
 * name resolves only through this (what the interface editor's in-window
 * compile wrote before Compile became a job). Prints one line; exits 0 when
 * written, 1 when not.
 */

#include "editor/iface/iface_model.h"

#include "mem/torirs_mem.h"

#include <stdio.h>

int
main(
    int argc,
    char** argv)
{
    struct EditorCs2Catalog catalog = { 0 };
    int count;
    int ok;

    if( argc != 4 )
    {
        fprintf(stderr, "usage: maped_cs2_names <scripts-dir> <runestar-names-dir or \"\"> <out-dir>\n");
        return 2;
    }
    ToriMem_Init();
    count = EditorCs2Catalog_Load(&catalog, argv[1]);
    ok = EditorCs2Catalog_WriteNamesDir(&catalog, argv[2][0] ? argv[2] : NULL, argv[3]);
    if( ok )
        printf("cs2: names: %d content script name(s)%s\n", count, argv[2][0] ? " over RuneStar's tables" : "");
    else
        fprintf(stderr, "error: maped-cs2-compile: could not write the names directory %s\n", argv[3]);
    EditorCs2Catalog_Free(&catalog);
    return ok ? 0 : 1;
}
