#ifndef SRC_EDITOR_BUILDER_BUILDER_PANELS_H
#define SRC_EDITOR_BUILDER_BUILDER_PANELS_H

/**
 * The builder panels on the editor's chrome (docs/MAPED_DESIGN.html
 * #locbuilder, #npcbuilder; MAPED_IMPLEMENTATION.md phase 3 `builders`):
 *
 *   Editor_LocBuilder_Register    the loc interaction builder: subject, the
 *                                 op/ap/use-on triggers and their steps, the
 *                                 server fields with the door and ladder
 *                                 presets, Regenerate / Check / Save / Detach
 *   Editor_NpcBuilder_Register    the same for an npc, plus the dialogue
 *                                 graph: drawn in a GRAPH well, outlined as a
 *                                 TREE (the accessible form, and the one the
 *                                 browser chrome tab uses)
 *   Editor_ShellEditor_Register   the multiloc / multinpc shell table with
 *                                 the value column computed, and the preview
 *                                 control that sets STATE_VAR_PREVIEW
 *
 * Those three (builder_session.c) bind the panels to the client's struct
 * Editor -- Editor_Apply, the mirror's documents, the catalog. The panels
 * themselves (builder_panels.c) reach the world only through struct
 * Builder_Services, so a test drives them over a fake and the real chrome
 * model without linking the client.
 */

#include "builder_emit.h"
#include "builder_ssc.h"

#include <stdint.h>

struct EditorUi;

/** What the panels learn about a loc or npc record from the catalog. */
struct Builder_RecordInfo
{
    int found;
    int id;
    char name[BUILDER_NAME_MAX];
    /** The cache's `name=`, the menu's noun. */
    char display[BUILDER_NAME_MAX];
    /** op1..op5 as the cache states them; "" when absent. */
    char ops[5][BUILDER_NAME_MAX];
};

#define BUILDER_EXISTING_MAX 64

/** The triggers the tree declares for one subject (catalog TRIGGER rows),
 *  with the strings owned here. */
struct Builder_ExistingList
{
    char names[BUILDER_EXISTING_MAX][BUILDER_TRIGGER_NAME_MAX];
    char files[BUILDER_EXISTING_MAX][256];
    struct Builder_ExistingTrigger rows[BUILDER_EXISTING_MAX];
    int count;
};

struct Builder_Services
{
    void* user_data;
    /** Commands to the authority, and the mirror's text of a document. */
    struct Builder_Submit submit;
    /** Save one document on the authority: 1 written, 0 nothing to save,
     *  -1 refused. May be NULL (Save then says it is unavailable). */
    int (*save)(void* user_data, enum Editor_DocKind kind, const char* key);
    /** The record `name` (or, with name "", `id`) of a loc / npc. Returns
     *  1 and fills `out` when found. May be NULL. */
    int (*record)(
        void* user_data,
        enum Builder_Subject subject,
        const char* name,
        int id,
        struct Builder_RecordInfo* out);
    /** Every trigger the tree declares for `subject`. May be NULL (no
     *  conflict check: the panel says so). */
    int (*triggers)(void* user_data, const char* subject, struct Builder_ExistingList* out);
    /** Publish a shared-state field (STATE_VAR_PREVIEW). May be NULL. */
    int (*state_set)(void* user_data, uint32_t key, const int32_t* values, int count);
};

/** Register the panels over explicit services (what the tests call). The
 *  services are copied. */
void
Builder_LocPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

void
Builder_NpcPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

void
Builder_ShellPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

/** The compile check's paths (builder_ssc.h): the sscompile binary, the
 *  content root it reads, and a work directory outside it. Applies to every
 *  builder panel registered on `ui`; until set, Check says where to set it.
 *  Returns 1 when the runner accepted the paths. */
int
Builder_PanelsSetCompile(
    struct EditorUi* ui,
    const char* sscompile,
    const char* content_root,
    const char* work_dir);

/* ---- the client binding (builder_session.c) ------------------------------- */

void
Editor_LocBuilder_Register(struct EditorUi* ui);

void
Editor_NpcBuilder_Register(struct EditorUi* ui);

void
Editor_ShellEditor_Register(struct EditorUi* ui);

/* ---- what the tests read -------------------------------------------------- */

/** The panel names Editor_UiFindPanel knows them by. */
#define BUILDER_PANEL_LOC "locbuilder"
#define BUILDER_PANEL_NPC "npcbuilder"
#define BUILDER_PANEL_SHELL "shelleditor"

struct Builder_PanelView
{
    /** The model the panel holds; NULL before a subject is open. */
    const struct Builder_Model* model;
    int read_only;
    const struct Builder_Issues* issues;
    /** The last Check's result; status NOT_RUN before the first. */
    const struct Builder_SscResult* check;
    /** The panel's status line. */
    const char* status;
};

/** What a builder panel registered as `name` currently shows; 0 when there
 *  is no such panel. */
int
Builder_PanelInspect(
    struct EditorUi* ui,
    const char* name,
    struct Builder_PanelView* out);

/** Script the panels the way a user would, for the tests and for replayed
 *  input: open a subject, regenerate, check, save, detach. Each returns 1 on
 *  success. */
int
Builder_PanelOpenSubject(
    struct EditorUi* ui,
    const char* name,
    const char* subject);

/** Replace the panel's model (as an edit) and regenerate. */
int
Builder_PanelSetModel(
    struct EditorUi* ui,
    const char* name,
    const struct Builder_Model* model);

int
Builder_PanelCheck(
    struct EditorUi* ui,
    const char* name);

int
Builder_PanelSaveAll(
    struct EditorUi* ui,
    const char* name);

int
Builder_PanelDetach(
    struct EditorUi* ui,
    const char* name);

/** The shell editor's current shell, or NULL before one is open. */
const struct Builder_Shell*
Builder_ShellPanelShell(struct EditorUi* ui);

#endif
