#ifndef CACHEPACK_CP_REGISTER_H
#define CACHEPACK_CP_REGISTER_H

/*
 * The tree's namespace register, `content.ini`.
 *
 * cachepack deliberately does not link anything from the server repo's `src/`
 * — it is a vendored-library tool, and the two are meant to be usable apart.
 * `content.ini` is the *contract* between them rather than shared code, so this
 * is a second, much smaller reader of the same file. It answers one question:
 *
 *     may cachepack rewrite `pack/<ns>.pack`?
 *
 * The answer is no whenever the namespace's `names` authority is anything other
 * than `cache`, because then layer 0 is not machine-owned and regenerating it
 * destroys something a human wrote. Concretely: `stat` and `category` have no
 * cache table to be generated from, and `component` has no gameval archive at
 * all — every one of its names is imported from a foreign revision and lives in
 * `names/component.pack`.
 *
 * Absent file, or a namespace it says nothing about, means `cache` — which is
 * the old unconditional behaviour, so this can only ever *reduce* what gets
 * written.
 */

#include <stdbool.h>

/** Read `<srcdir>/content.ini`. Never fails; a missing file leaves every
 *  namespace writable, which is what cachepack did before this existed. */
void
cp_register_load(const char* srcdir);

/**
 * May cachepack write `pack/<ns>.pack`?
 *
 * False when the tree declared the namespace's names as authored, derived or
 * imported. `names/<ns>.pack` — the hand-written file — is never cachepack's to
 * write under any circumstances and is not asked about.
 */
bool
cp_register_may_write_pack(const char* ns);

#endif
