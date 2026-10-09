#ifndef RSCACHE_TOOLS_CACHEPACK_CP_INCREMENTAL_H
#define RSCACHE_TOOLS_CACHEPACK_CP_INCREMENTAL_H

/*
 * The incremental server pack. docs/serverpack.md is the design note.
 *
 * A UNIT is one config type (npc, loc, varp, ...). What a type packs to depends
 * on its own files (every `*.<type>` the walk finds, its routing files, and for
 * dbrow every `*.dbtable`), on the name and constant lookups its encoders make,
 * and on a short list of global inputs no encoder can be traced through
 * (fields/, content.ini, meta.ini, the asset and interface indexes, the param
 * types, this binary, the lanes). `<server_dir>/cp.state` keeps, per type, the
 * hash of its files, every lookup with a digest of its answer, the archives it
 * wrote and what it printed. A build reuses a type whose files and answers did
 * not move, byte for byte, and repacks the rest.
 *
 * The store is then written whole, in the order a full build writes it, so an
 * incremental build and a full one produce identical files; it is 12 MB and
 * takes milliseconds. `pack.manifest` beside it lists every archive with a
 * content hash and a revision, for the server's staleness check and a reload.
 */

#include "cachepack.h"

#include <stdint.h>

/** What an encoder looked up, recorded against the type being packed. */
enum CP_Lookup
{
    CP_LOOKUP_NAME_FIND = 1,  /* cp_name_find(type, name) -> id */
    CP_LOOKUP_NAME_ALLOC = 2, /* cp_name_find_alloc(type, name) -> id */
    CP_LOOKUP_NAME_GET = 3,   /* cp_name_get(type, id) -> name */
    CP_LOOKUP_NAME_LANE = 4,  /* cp_name_is_lane(type, name) */
    CP_LOOKUP_CONST_INT = 5,  /* cp_resolve_caret(^name) -> int */
    CP_LOOKUP_CONST_TEXT = 6, /* cp_value_constant_text(name) -> text */
};

/** Nonzero while a unit is being packed; the hooks test it before calling. */
extern int g_cp_recording;

void
cp_lookup_note(
    int op,
    int type,
    int id,
    const char* name);

/** A name table grew during packing (cp_name_ensure minted a name). A unit that
 *  does that changes what later units see, so it is never reused. */
void
cp_lookup_note_mutation(void);

/** Every server pack archive write goes through here: to disk, or into the
 *  unit being packed when one is. Same return as RSCache_Dat2DiskWriteArchive. */
int
cp_server_write(
    const char* server_dir,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size);

/** The incremental `pack --server-only` (every type). Returns 1 on success. */
int
cp_pack_server_incremental(
    struct CP_Ctx* ctx,
    const char* server_dir);

/**
 * Is the pack at `server_dir` already what a build would write? Answered from
 * stats alone — no name table loaded — against the input list the last build
 * recorded. Returns 1 (and prints the up-to-date line) when it is.
 */
int
cp_server_up_to_date(
    const char* srcdir,
    const char* server_dir,
    const char* const* lanes,
    int lane_count,
    const char* argv0);

/** Per type and names: see cp_pack.c. */
int
cp_server_pack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* server_dir,
    int* server_total,
    int* membership_errors);

int
cp_server_pack_names(
    struct CP_Ctx* ctx,
    const char* server_dir);

int
cp_server_stamp_write(
    struct CP_Ctx* ctx,
    const char* server_dir);

#endif
