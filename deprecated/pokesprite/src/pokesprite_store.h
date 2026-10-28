#ifndef POKESPRITE_STORE_H
#define POKESPRITE_STORE_H

/*
 * The server side of PokeSprite sheets: a checked-in store, indexed by sqlite.
 *
 *   <root>/index.db      checked in, opened READ-ONLY:
 *                          sheet(key TEXT PRIMARY KEY, path TEXT, kind TEXT,
 *                                bytes INTEGER, generator TEXT)
 *                        `path` is relative to <root>.
 *   <root>/sheets/...    the .pss files, checked in
 *   <root>/missing.db    NOT checked in (runtime state; .gitignore):
 *                          missing(key TEXT PRIMARY KEY, first_seen TEXT,
 *                                  last_seen TEXT, count INTEGER)
 *
 * Nothing is rendered here. A key the index lacks is recorded in missing.db and
 * answered as absent; the offline generator (tools/pokesprite) reads that table
 * and renders exactly what players asked for. The index stays read-only so a
 * session of play never dirties the checked-in tree.
 */

#include <stdint.h>

struct PokeSpriteStore;

/**
 * Open the store at `root`. Returns NULL when there is no index.db there --
 * a store that has not been generated yet is a runtime state the caller
 * reports (503), not a contract violation.
 */
struct PokeSpriteStore*
PokeSpriteStore_Open(const char* root);

void
PokeSpriteStore_Close(struct PokeSpriteStore* store);

/**
 * The .pss bytes for `key` (ToriMem_Net()'s, caller frees from it), or NULL when the index
 * has no such sheet -- in which case the miss is recorded. `key` must satisfy
 * PokeSprite_KeyValid.
 */
uint8_t*
PokeSpriteStore_Fetch(
    struct PokeSpriteStore* store,
    const char* key,
    int* out_size);

/** Sheets in the index, and distinct keys missed since missing.db began. */
void
PokeSpriteStore_Counts(
    struct PokeSpriteStore* store,
    long* out_sheets,
    long* out_missing);

#endif
