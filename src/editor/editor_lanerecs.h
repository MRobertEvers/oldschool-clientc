#ifndef SRC_EDITOR_EDITOR_LANERECS_H
#define SRC_EDITOR_EDITOR_LANERECS_H

/**
 * The lane's own records in the editor's client: a loc / npc / obj the
 * session's cache does not hold, defined from the text the authority holds
 * (editor_recdef.h), so it is drawn, placed, swapped to, selected and
 * previewed like any record of the cache.
 *
 * Two ways a record gets here:
 *
 *   Editor_LaneRecords_Define   "Copy to new" just made it (Editor_CopyRecord
 *                               calls this): defined before anything asks.
 *   the provider's miss         a record of an earlier session, met when a
 *                               square that places it loads or a panel asks
 *                               for it (CacheProvider_SetAbsentRecordSource).
 *                               Its name is the lane's ledger's
 *                               (pack/<type>.alloc: `id=name`), its fields
 *                               the CFG document "<type>:<name>".
 *
 * and one way it stays right: Editor_LaneRecords_Tick, once a frame,
 * compares each defined record's CFG document revision with the one it was
 * decoded from. A field set in the Record panel, an undo, a redo: the record
 * is decoded again and REPLACES the old one in the provider (retired, not
 * freed); an undone copy's record is taken out. The tick answers whether
 * anything the world may show changed, and the caller bakes the window
 * again.
 *
 * One session a process (the editor's): the state is this file's.
 */

#include "editor.h"
#include "editor_recdef.h"

struct CacheProvider;

/** Start defining lane records for `provider` from `editor`'s documents.
 *  Both are borrowed until Editor_LaneRecords_Detach. */
void
Editor_LaneRecords_Attach(
    struct Editor* editor,
    struct CacheProvider* provider);

/** Stop: the provider's source is unregistered. Records already defined stay
 *  the provider's. NULL-state tolerant (a teardown). */
void
Editor_LaneRecords_Detach(void);

/**
 * Define (or define again) record `id` of `kind` (enum
 * CacheProvider_CatalogKind: loc 0, npc 1, obj 2) named `name` from its CFG
 * document, opening it if this mirror does not hold it. 1 when the provider
 * now holds the record; 0 when the document is empty (no such record) or
 * nothing is attached. `report` (may be NULL) gets what the decode met.
 *
 * `like_id` is the record this one was just copied from, or -1: when the
 * provider holds that record (the cache's own decode of the same lines), the
 * two are compared and the report says in which fields they differ -- none,
 * unless this decoder and the cache's have drifted apart.
 */
int
Editor_LaneRecords_Define(
    int kind,
    int id,
    const char* name,
    int like_id,
    struct Editor_RecDefReport* report);

/** Once a frame. 1 when a defined record changed or went since the last
 *  call: what the world shows of it is stale. */
int
Editor_LaneRecords_Tick(void);

/** 1 when record (`kind`, `id`) is one this module defined and still holds;
 *  `name` (may be NULL) gets its record name. */
int
Editor_LaneRecords_Name(
    int kind,
    int id,
    char* name,
    size_t name_size);

/** 1 when record (`kind`, `id`) is one this module defined and that has
 *  gone since (the copy that made it was undone); `name` (may be NULL) gets
 *  its record name. A pick of it says so at once, not "still loading". */
int
Editor_LaneRecords_Gone(
    int kind,
    int id,
    char* name,
    size_t name_size);

#endif
