#ifndef SRC_EDITOR_EDITOR_RECDEF_H
#define SRC_EDITOR_EDITOR_RECDEF_H

/**
 * A loc / npc / obj definition from its TEXT: the `[name]` block of
 * configs/all.<type> decoded into the record the client draws from.
 *
 * Why this exists. The editor's own client reads the baked cache it booted
 * on, and that cache holds no record a lane made since (Copy to new). Until
 * 2026-11-11 such a record could not be drawn, placed, swapped to or
 * previewed in the editor -- before Save, after Save, after RUN, after a
 * restart -- because every one of those asks the cache for the id. The text
 * the editor's authority holds IS the record, though: every field line of
 * the block, the unsaved edits included. Decoded here, the World draws what
 * the block says (a recolour typed in the Record panel shows), with no
 * build in between.
 *
 * How. Not a second codec: the block's values are the values cachepack's
 * packers put into the rscache decode structs (3rd/rscache/tools/cachepack/
 * config/cp_loc.c, cp_npc.c, cp_obj.c -- the text is that struct, spelled),
 * so the fields go into an RSCache_Dat2Config{Loc,Npc,Obj} that
 * RSCache_Dat2Config*Init has given the client defaults, the decoder's own
 * Finish derives what a whole record derives (a loc's `active`, an npc's
 * footprint), and the SAME adaptor the cache loaders use
 * (ToriRS_*FromRSCacheDat2) makes the client's record. A key written
 * `default` states nothing.
 *
 * What is NOT carried, because the client's record has no reader for it in
 * an editor: params (`param=`: the scripts', resolved by RUN's pack), the
 * rev-237 sub / conditional ops, a loc's campaigns and random anims. They
 * are counted in the report, never guessed.
 *
 * Names. A field that refers to another record may spell its name
 * (`anim=quest_mcannon_crate_shake`, `readyanim=human_ready`,
 * `multiloc1=...`, `certlink=...`): the caller's resolver answers the id.
 * A bare number is taken as the id, as cachepack takes it.
 */

#include "engine/torirs_types.h"

#include <stddef.h>

struct RSCache;

/** The id of record `name` of `type` ("seq", "loc", "npc", "obj", "varp",
 *  "varbit", "category"), or -1 when there is none. */
typedef int (*Editor_RecDefResolveFn)(
    void* user,
    const char* type,
    const char* name);

struct Editor_RecDefContext
{
    /** The cache identity: which decoder flags a derived default follows.
     *  Borrowed. */
    const struct RSCache* profile;
    /** May be NULL: then only numbers resolve. */
    Editor_RecDefResolveFn resolve;
    void* user;
};

/** What a decode met, for the caller's one console line. */
struct Editor_RecDefReport
{
    /** Field lines read (a `default` is not one). */
    int stated;
    /** Lines whose value could not be read (a name nobody has, a number
     *  that is not one): the field kept its default. `first_bad` is the
     *  first such "key=value", cut to fit. */
    int bad;
    char first_bad[96];
    /** Lines this decoder does not carry (see the header). */
    int skipped;
    /** Filled by Editor_LaneRecords_Define when it was given the record this
     *  one was copied from and the cache's copy of that is resident: the
     *  fields in which the two differ (Editor_RecDef*Diff), 0 when the text
     *  reads exactly as the cache's record does. */
    int differs;
    char differs_in[96];
};

/**
 * Decode the block `text` (`length` bytes: the header line and its field
 * lines, as a CFG document holds it) as record `id`. Returns a record the
 * cache provider can take (ToriMem_Assets, freed by ToriRS_*Free), or NULL
 * when the text holds no field line at all (an empty document: the record
 * does not exist). `report` may be NULL.
 */
struct ToriRS_Location*
Editor_RecDefLoc(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report);

struct ToriRS_Npctype*
Editor_RecDefNpc(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report);

struct ToriRS_Objtype*
Editor_RecDefObj(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report);

/**
 * The ledger's `id=name` lines (the text of pack/<type>.alloc): the name
 * bound to `id`, 1 when there is one. Comment lines and anything that is not
 * `<number>=<name>` are skipped. Pure; exposed for the tests.
 */
int
Editor_RecDefLedgerName(
    const char* text,
    size_t length,
    int id,
    char* name,
    size_t name_size);

/**
 * Two client records of one kind, field by field (ids, params and what this
 * decoder does not carry aside): how many fields differ, their names
 * space-separated into `out` (may be NULL with size 0), cut to fit. A copy
 * decoded from its text must equal the cache's record of its source -- the
 * blocks are the same lines -- so a non-zero answer right after "Copy to
 * new" means this decoder and the cache's have drifted apart, and the
 * console says in which fields. The tests hold the decoder to the cache's
 * with it too.
 */
int
Editor_RecDefLocDiff(
    const struct ToriRS_Location* a,
    const struct ToriRS_Location* b,
    char* out,
    size_t out_size);

int
Editor_RecDefNpcDiff(
    const struct ToriRS_Npctype* a,
    const struct ToriRS_Npctype* b,
    char* out,
    size_t out_size);

int
Editor_RecDefObjDiff(
    const struct ToriRS_Objtype* a,
    const struct ToriRS_Objtype* b,
    char* out,
    size_t out_size);

#endif
