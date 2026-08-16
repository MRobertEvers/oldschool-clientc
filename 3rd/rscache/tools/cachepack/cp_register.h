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
 *
 * ## The second question: `membership`
 *
 * `pack/<ns>.client` and `pack/<ns>.server` (`cp_membership.h`) are different
 * files from `pack/<ns>.pack` and need their own answer, because `names` cannot
 * carry it. `names = authored` would say the right thing about the membership
 * files and the *wrong* thing about the name table: npc, obj, loc, seq and eight
 * more are named by a gameval archive, so `cp_register_check` refuses to let them
 * claim anything but `names = cache` — and a namespace whose name table is
 * legitimately the cache's still has membership a person decides. Overloading one
 * key would make the two facts unstatable together.
 *
 *     [namespace:npc]
 *     names      = cache      ; the cache's gameval table names them
 *     membership = authored   ; a person decides which side each npc has a half on
 *
 * `authored` is the default and today the only supported value. `generated` is
 * spellable so that a tree can be refused for claiming it rather than quietly
 * getting `authored` behaviour: nothing regenerates a membership file, and a
 * namespace that believes otherwise would eventually be edited on the assumption
 * that its edits would be reproduced.
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
 * Does the tree declare `<ns>`'s membership authored?
 *
 * True by default, and true for every namespace today. A distinct question from
 * `cp_register_may_write_pack`, answered by a distinct key; see the header
 * comment for why one key could not carry both.
 *
 * "Authored" is what makes the protection a statement rather than an accident of
 * which paths happen to exist: `pack`, `unpack` and `verify` open no membership
 * file, and `cachepack membership` seeds only a namespace that has none. That
 * second guard is deliberately *not* this predicate — a file already on disk is
 * never replaced whatever the register says, because a declaration and a refusal
 * should not fail together.
 */
bool
cp_register_membership_is_authored(const char* ns);

/**
 * Refuse a `content.ini` that contradicts the codec tables.
 *
 * For every namespace the file declares, `names = cache` must hold if and only if
 * that namespace has a gameval archive. A tree that gets this wrong is asking for
 * one of two silent failures — a rewritten authored file, or cache names that are
 * never imported — so it is reported per namespace, naming both sources, and the
 * count is returned. Namespaces neither `cp_types.c` nor `cp_assets.c` knows are
 * skipped: cachepack has no opinion about `stat`.
 *
 * Also refuses `membership = generated`, for the same class of reason: nothing
 * generates a membership file, so declaring one generated is a claim the tree
 * would later be edited on.
 */
int
cp_register_check(void);

#endif
