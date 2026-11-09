#ifndef SRC_EDITOR_PANELS_EDPANEL_NPCDOC_H
#define SRC_EDITOR_PANELS_EDPANEL_NPCDOC_H

/**
 * One npc's record across its config layers, as the NPC editor edits it
 * (docs/NPC_EDITOR_DESIGN.md 3.5, 3.8): the layers the catalog names for the
 * record (configs/all.npc, every server overlay), opened as CFG documents
 * and resolved to one Editor_CfgRecord; the write-target rule
 * (Editor_CfgRecordWriteLayerFor); a field edit and a route edit as ONE
 * undo step, the route edit carrying the movement ledger's line
 * (npc_movement/<shard>/<name>.move) with it.
 *
 * The Record panel (edpanel_record.c) does the same opening for its own
 * all-fields view; this is the headless half the NPC editor's services bind
 * (builder_session.c), so the editor never depends on that panel being
 * open. Host-bound: it reads the session's documents and applies through
 * the authority. NULL arguments assert.
 */

#include "editor/editor_cfg.h"
#include "editor/editor_npc.h"

#include <stddef.h>
#include <stdint.h>

struct EditorUi;

struct Editor_NpcDoc
{
    char name[EDITOR_CMD_NAME_MAX];
    /** The server overlays stating the record, paths under server/scripts;
     *  the last may be the editor's own `edited/<name>/configs/<name>.npc`
     *  when no authored file states it (3.8 rule 3). */
    char overlays[EDITOR_CFG_RECORD_LAYERS_MAX][EDITOR_DOC_KEY_MAX];
    int overlay_count;
    /** fields/npc.ini, parsed. Owned. */
    struct Editor_CfgRegisterField* fields;
    int field_count;
    struct Editor_CfgRecord record;
    int open;
    int built;
    /** The session's fact seq the record was resolved at. */
    uint32_t built_seq;
};

/** Open `name`'s layers (the catalog's sources, plus the editor's own
 *  overlay when none is authored) and resolve. 1 when it opened; 0 with the
 *  reason in the status bar (no session, no such record). */
int
Editor_NpcDocOpen(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* name);

void
Editor_NpcDocClose(struct Editor_NpcDoc* doc);

/** The record, re-resolved when the session's facts moved. NULL unless
 *  open. */
const struct Editor_CfgRecord*
Editor_NpcDocRecord(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc);

/** The record's movement statements (the winning rows), unsaved edits
 *  included. 0 unless open. */
int
Editor_NpcDocMovement(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    struct Editor_NpcMovement* out);

/** The file an edit of `key` would be written to (3.8), as its path under
 *  server/scripts, into `out`. The layer index, -1 when there is none. */
int
Editor_NpcDocWriteTarget(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* key,
    char* out,
    size_t out_size);

/** Set field `key` (`sub` "" for a plain key, a param's name for `param`)
 *  to `value` ("" removes it) in the write-target layer, one undo step
 *  named `what`. Returns the commands the authority took, 0 when nothing
 *  changed, -1 refused (the reason in the status bar). */
int
Editor_NpcDocSetField(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* key,
    const char* sub,
    const char* value,
    const char* what);

/**
 * Replace the route (Editor_NpcRoutePlan) and pin the ledger
 * (`movement = patrols | wanders | stationary`, `source = authored`), as
 * ONE undo step named `what`. With no waypoints `mode` is the Mode radio's
 * word and the ledger says wanders or stationary. Returns the commands the
 * authority took, 0 when nothing changed, -1 refused and undone.
 */
int
Editor_NpcDocSetRoute(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    enum Editor_NpcMode mode,
    const char* what);

#endif
