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

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Dat2Edit;

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

/*
 * The incremental CLIENT pack (`pack --incremental`), on the server pack's unit
 * model. A unit is one config type; its key is the same hash of its own files
 * and routing files, its recorded lookups are replayed the same way, and the
 * global inputs (fields/, content.ini, meta.ini, the asset and interface
 * indexes, the param types, this binary, the lanes) repack every type when
 * they move. `<out>/cp.client.state` keeps the keys and lookups.
 *
 * What differs is the output. The client cache is written IN PLACE (copied
 * from --base on the first build only), so a repacked type's records are
 * captured (cp_client_put) and compared, file by file, with the archive the
 * cache already holds: an archive is staged only when a record in it differs
 * or is new. Every staged archive goes through ONE RSCache_Dat2Edit commit,
 * which writes those archives and nothing else and bumps the config table's
 * reference table (its version, and each written archive's crc and version).
 * A build where nothing differs commits nothing and leaves the reference
 * table alone.
 *
 * `<out>/client.changed` lists what this build wrote, one `archive <table>
 * <archive> <type>` line each (empty when nothing changed), for the editor's
 * RUN to name in its reload (torirsmaped/torirs_maped_build.h).
 *
 * Pack-on-base semantics, as the full pack has: a record deleted from the text
 * is not removed from an archive that holds it.
 */

/** Every client config record write goes through here: into the type being
 *  captured when the incremental client pack is comparing one, else straight
 *  to RSCache_Dat2EditPutFile. Same return. */
bool
cp_client_put(
    struct RSCache_Dat2Edit* edit,
    int table_id,
    int archive_id,
    int file_id,
    const uint8_t* data,
    uint32_t size);

/** The client half of one type (cp_pack.c's pack_type with no server dir).
 *  Returns 1 on success; records and failures counted out. */
int
cp_client_pack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct RSCache_Dat2Edit* edit,
    int config_table,
    int* out_records,
    int* out_failed);

/** `pack --incremental --src S --out O [--base B]`. Returns 1 on success. */
int
cp_pack_client_incremental(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* base_cache_dir,
    const char* out_cache_dir);

#endif
