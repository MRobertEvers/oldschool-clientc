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
 * than `cache`, because then the file is not machine-owned. Concretely: `stat`
 * and `category` have no cache table at all, and `param`, `hitsplat` and `synth`
 * have no gameval archive — so every name in those files is either `<ns>_<id>`
 * filler or something a human wrote, and rewriting one deletes prose.
 *
 * ## The default is derived, not assumed
 *
 * A namespace `content.ini` says nothing about does *not* fall back to "writable".
 * It falls back to the fact the codec tables already state: a namespace is
 * machine-owned exactly when it has a gameval archive to be generated from
 * (`cp_types.c` / `cp_assets.c`, fourth column). That is the same rule
 * `ContentRegister_Validate` holds the server-side register to, derived here
 * rather than transcribed — a transcription is a third table that can drift, and
 * drift in this particular table is what deleted `configs/all.param.compack`'s header.
 *
 * A tree that ships no `content.ini` therefore gets correct behaviour rather than
 * the old unconditional yes.
 */

#include <stdbool.h>

/** Read `<srcdir>/content.ini`. Never fails; a namespace the file omits keeps
 *  the default derived from the codec tables. */
void
cp_register_load(const char* srcdir);

/**
 * May cachepack write `pack/<ns>.pack`?
 *
 * False when the tree declared the namespace's names as authored, derived or
 * imported, and false by default for any namespace with no gameval archive.
 */
bool
cp_register_may_write_pack(const char* ns);

/**
 * Refuse a `content.ini` that contradicts the codec tables.
 *
 * For every namespace the file declares, `names = cache` must hold if and only if
 * that namespace has a gameval archive. A tree that gets this wrong is asking for
 * one of two silent failures — a rewritten authored file, or cache names that are
 * never imported — so it is reported per namespace, naming both sources, and the
 * count is returned. Namespaces neither `cp_types.c` nor `cp_assets.c` knows are
 * skipped: cachepack has no opinion about `stat`.
 */
int
cp_register_check(void);

#endif
