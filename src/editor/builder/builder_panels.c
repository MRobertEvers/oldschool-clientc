#include "builder_panels.h"

#include "builder_shell.h"
#include "editor/editor_cfg.h"
#include "editor/editor_ui.h"
#include "editor/map_editor_ghost.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared --------------------------------------------------------------- */

#define BP_ISSUE_ROWS 4
/** Rows of the tree's triggers the Interactions tab lists at once. */
#define BP_EXISTING_ROWS 5
#define BP_OUTLINE_MAX (BUILDER_TRIGGERS_MAX * (BUILDER_STEPS_MAX + 1) + BUILDER_NODES_MAX + 4)
#define BP_NODE_W 120
#define BP_NODE_H 26
#define BP_GRAPH_H 260

/* The dialogue graph's hit keys: a node's body is its index + 1, its ports
 * BP_KEY_PORT + index * BP_PORTS_STRIDE + port, the new-node chip row's
 * chips BP_KEY_CHIP + kind. */
#define BP_KEY_PORT 1000
#define BP_PORTS_STRIDE 8
#define BP_KEY_CHIP 5000
#define BP_PORT_W 10
#define BP_PORT_H 8
#define BP_CHIP_W 52
#define BP_CHIP_H 18
/** A press that moves less than this is a click, not a drag. */
#define BP_DRAG_SLOP 3

enum
{
    BP_DRAG_NONE = 0,
    BP_DRAG_NODE,
    BP_DRAG_SUBTREE,
    BP_DRAG_PORT,
};

/* The text's pages and the chat box preview (design section 10). */
#define BP_PAGE_LINE_H 16
#define BP_PAGE_RULE_COLOR 0x7a8ad8
#define BP_CHAT_W 500
#define BP_CHAT_H 128
#define BP_CHAT_HEAD 46
#define BP_CHAT_TEXT_X 96
#define BP_LINT_VISIBLE 6
#define BP_NAME_CACHE 32

/* The Movement tab (docs/NPC_EDITOR_DESIGN.md 3.4, 3.5): the minimap well's
 * logical size, its zooms (px a tile), its hit keys (tiles from 1, then the
 * waypoints, then the spawns), the route list's rows. */
#define BP_MV_W 256
#define BP_MV_H 256
#define BP_MV_PX_MIN 2
#define BP_MV_PX_MAX 16
#define BP_MV_PX_DEFAULT 8
#define BP_MV_KEY_TILE 1
#define BP_MV_KEY_WP 20000
#define BP_MV_KEY_SPAWN 21000
#define BP_MV_ROUTE_ROWS 6
#define BP_MV_SPAWN_ROWS 6
#define BP_MV_SPAWNS_MAX 64
/** A Play step holds one server tick. */
#define BP_MV_PLAY_TICK_MS 600
/** Ticks Play holds the walker where a leg stops dead before it jumps (the
 *  server waits EDITOR_NPC_PATROL_STUCK_TICKS; nobody watches 19 s of it). */
#define BP_MV_PLAY_STUCK_TICKS 4
/** Tiles of one leg's walk kept for the World's line and Play: a leg longer
 *  than this is drawn straight past it. */
#define BP_MV_WALK_MAX 160
/** The Mode and tool radio groups (unique across the panels: spawns 31/32). */
#define BP_GROUP_TOOL 41
#define BP_GROUP_MODE 42
/* Colours of the well (0xRRGGBB). */
#define BP_MV_C_OPEN 0x3a5a3a
#define BP_MV_C_OPEN_ROOF 0x5c5a3a
#define BP_MV_C_BLOCKED 0x6a2e2e
#define BP_MV_C_WALL 0x1a1a1a
#define BP_MV_C_UNLOADED 0x262626
#define BP_MV_C_NODOC 0x303038
#define BP_MV_C_ROUTE 0xffb040
#define BP_MV_C_ROUTE_CLOSING 0xa87a30
#define BP_MV_C_TELEPORT 0xe05050
#define BP_MV_C_WANDER 0x50e070
#define BP_MV_C_LEASH 0x2c8c46
#define BP_MV_C_PIN 0xff8040
#define BP_MV_C_SPAWN_EDITED 0x50e070
#define BP_MV_C_SPAWN_GENERATED 0xffe070
#define BP_MV_C_SPAWN_SUPPRESSED 0xe05050
#define BP_MV_C_SPAWN_SCRIPTED 0x50d8f0
#define BP_MV_C_SELECTED 0xffffff
#define BP_MV_C_HOVER 0xe0e0e0
#define BP_MV_C_GHOST 0x60e0ff

static const char* const k_mode_words[3] = { "none", "wander", "patrol" };
static const char* const k_restrict_options[8] = { "(unstated)", "normal", "blocked", "blocked+normal",
                                                   "indoors",    "outdoors", "nomove", "passthru" };
static const char* const k_block_options[5] = { "(unstated)", "none", "npc", "all", "player" };
static const char* const k_facing_options[9] = { "(unstated)", "northwest", "north", "northeast", "west",
                                                 "east",       "southwest", "south", "southeast" };

/* Borrowed by the dropdowns for the widgets' whole life: static. */
static const char* const k_trigger_options[] = {
    "op1", "op2", "op3", "op4", "op5", "ap1", "ap2", "ap3", "ap4", "ap5", "use-on (op)", "use-on (ap)",
};
#define BP_TRIGGER_OPTION_COUNT 12

static const char* k_step_options[BUILDER_STEP_KIND_COUNT];
static const char* k_node_options[BUILDER_NODE_KIND_COUNT];
static int k_options_ready;

static const char* const k_door_options[] = { "no door", "door (closed)", "door (opened)" };
static const char* const k_ladder_options[] = { "no ladder",          "climb up",
                                                "climb down",         "climb (ask)",
                                                "climb up (ladder)",  "climb down (ladder)",
                                                "climb (ask, ladder)" };
static const char* const k_subject_options[] = { "loc", "npc" };
static const char* const k_var_options[] = { "varbit", "varp" };

/** What fields A, B, C and Text mean for each step kind ("" hides it). */
static const char* const k_step_fields[BUILDER_STEP_KIND_COUNT][4] = {
    { "", "", "", "Message" },
    { "Var", "Compare", "Value", "" },
    { "Var", "Compare", "Value", "" },
    { "", "", "", "" },
    { "", "", "", "" },
    { "Var", "Value", "", "" },
    { "Obj", "Count", "", "" },
    { "Obj", "Count", "", "" },
    { "Obj", "Count", "", "Said when missing" },
    { "Stat", "Level", "", "Said when too low" },
    { "Interface", "", "", "" },
    { "Target", "Interface", "Mode", "" },
    { "Seq", "", "", "" },
    { "Synth", "", "", "" },
    { "Coord", "", "", "" },
    { "Ticks", "", "", "" },
    { "Loc", "Ticks", "", "" },
    { "Proc", "Arguments", "", "" },
    { "Label", "", "", "" },
    { "", "", "", "RuneScript" },
};

static void
options_init(void)
{
    if( k_options_ready )
        return;
    for( int i = 0; i < BUILDER_STEP_KIND_COUNT; i++ )
        k_step_options[i] = Builder_StepKindLabel((enum Builder_StepKind)i);
    for( int i = 0; i < BUILDER_NODE_KIND_COUNT; i++ )
        k_node_options[i] = Builder_NodeKindLabel((enum Builder_NodeKind)i);
    k_options_ready = 1;
}

static void
copy_text(
    char* out,
    size_t out_size,
    const char* text)
{
    snprintf(out, out_size, "%s", text ? text : "");
}

/* ---- the builder panel ---------------------------------------------------- */

/** One lint row as the Problems panel hears it (edpanels.h,
 *  Editor_Problems_ReportOpen): kept so a regeneration that changed none of
 *  them reports nothing. */
struct bp_problem
{
    int severity;
    char where[64];
    char message[192];
};

struct builder_panel
{
    enum Builder_Subject subject;
    struct Builder_Services services;

    struct Builder_Model* model;
    int has_model;
    int read_only;
    struct Builder_RecordInfo record;
    struct Builder_Issues issues;
    struct Builder_SscResult* check;
    /** Check's BUILD job on the editor server (bp_check). */
    struct Editor_BuildJob check_job;
    char status[192];

    int sel_trigger;
    int sel_step;
    int sel_node;

    /** The one window (docs/NPC_EDITOR_DESIGN.md 3.2): the header on every
     *  tab, then (npc) the Interactions and Dialogue tabs. The outline rows
     *  live in it, after their heading, re-made on every rebuild. */
    int p_main;
    int w_tabs;
    /** "will: patrols 10 waypoints..." -- the effective behaviour (npc),
     *  and its notes ("wanderrange 5 is unused") on a second line. */
    int w_will;
    int w_will_note;
    /** The triggers the TREE declares for the subject, read-only, with
     *  Open in editor for a hand-authored one (D6). */
    int w_existing_label, w_existing_list, w_existing_open;
    struct Builder_ExistingList* existing;
    /** The outline's heading in each tab: its rows are relinked after it. */
    int w_outline_head, w_dialogue_outline_head;
    /** While rebuilding: the row the next one goes after, and its tab. */
    int outline_after;
    int outline_tab;
    /** The issues, a list (D3: labels overflowed the panel). */
    int w_issue_list;

    int w_subject, w_open, w_fromsel, w_status;

    /* The Movement tab. */
    int w_mv_eff, w_mv_map, w_mv_readout;
    int w_mv_tool_select, w_mv_tool_add, w_mv_tool_move, w_mv_fit, w_mv_goto;
    int w_mv_mode_none, w_mv_mode_patrol, w_mv_mode_wander;
    int w_mv_leash, w_mv_range, w_mv_restrict, w_mv_block, w_mv_facing, w_mv_respawn, w_mv_timer;
    int w_mv_route, w_mv_route_label, w_mv_pause, w_mv_wp_add, w_mv_wp_insert, w_mv_wp_delete, w_mv_wp_up,
        w_mv_wp_down, w_mv_play, w_mv_target;
    int w_mv_hint, w_mv_route_note, w_mv_wp_move, w_mv_play_label, w_mv_target_path;
    int w_mv_share, w_mv_newtype, w_mv_split;
    /** "Fix stuck legs", shown while a leg stops dead. */
    int w_mv_fix;
    /** Each leg as the server walks it (services.can_step), and the tiles
     *  it steps onto: what the World's line follows and Play walks. */
    struct Editor_NpcWalk mv_walk[EDITOR_NPC_PATROL_MAX];
    struct Editor_NpcWalkTile mv_walk_tiles[EDITOR_NPC_PATROL_MAX][BP_MV_WALK_MAX];
    int mv_walked;
    /** The World's pointer under the Add / Move tools: the tile it is over
     *  (stamped with the frame, so a pointer that left goes stale) and a
     *  waypoint being carried, -1 none. */
    int mv_whover_frame;
    int mv_whover_x;
    int mv_whover_z;
    int mv_whover_level;
    int mv_wdrag_wp;
    int mv_wdrag_moved;
    /** A World press under the Add tool that is not on a waypoint: its
     *  release lays one. */
    int mv_wpress;
    /** The panel was showing last tick (it opens on the selection). */
    int mv_was_visible;
    /** Play: when the walker last stepped, ticks left of a pause or a
     *  stuck hold, and where it stands. */
    int64_t mv_play_ms;
    int mv_play_hold;
    int mv_play_stuck;
    /** The record's movement as the documents hold it (services.movement),
     *  the file an edit goes to, what the server makes of it. */
    struct Editor_NpcMovement mv;
    int mv_valid;
    char mv_target[EDITOR_DOC_KEY_MAX];
    struct Editor_NpcEffective mv_eff;
    /** Each leg's steps (services.path_steps): -1 no path, -2 unknown, -3
     *  a teleport across levels. Recomputed when the route or level moves. */
    int mv_legs[EDITOR_NPC_PATROL_MAX];
    uint32_t mv_legs_hash;
    int mv_sel;
    /** 0 Select, 1 Add waypoint, 2 Move. */
    int mv_tool;
    /** The view: px a tile, the centre tile, the level shown. */
    int mv_px;
    int mv_cx;
    int mv_cz;
    int mv_level;
    int mv_fit_pending;
    int mv_hover_valid;
    int mv_hover_x;
    int mv_hover_z;
    /** A waypoint being dragged (Move), -1 none; its live tile. */
    int mv_drag_wp;
    int mv_drag_x;
    int mv_drag_z;
    /** A pan in progress (Select): where it pressed, the centre then. */
    int mv_panning;
    int mv_press_x;
    int mv_press_y;
    int mv_press_cx;
    int mv_press_cz;
    /** Play: the ghost's leg, its step along it, frames into the step. */
    int mv_play;
    int mv_play_leg;
    int mv_play_step;
    int mv_play_frame;
    int mv_frame;
    uint32_t mv_draw_hash;
    /** TORIRS_EDITOR_NPC's `photo`: frames left before the panel is laid
     *  over the column (after the first layout has shown the default set). */
    int photo_countdown;
    /* The Spawns tab. */
    int w_sp_label, w_sp_list, w_sp_goto, w_sp_select, w_sp_place, w_sp_delete;
    struct Builder_NpcSpawn sp[BP_MV_SPAWNS_MAX];
    int sp_count;
    int sp_sel;
    int w_trigger_kind, w_use_obj, w_trigger_add, w_trigger_remove, w_trigger_dialogue;
    int w_menu_text, w_rename_op;
    int w_step_kind, w_step_field[4], w_step_add, w_step_apply, w_step_remove, w_step_up,
        w_step_down;
    int w_category, w_door, w_door_other, w_ladder, w_server_apply;
    int w_regen, w_check, w_save, w_detach;

    int outline_rows[BP_OUTLINE_MAX];
    int outline_trigger[BP_OUTLINE_MAX];
    int outline_step[BP_OUTLINE_MAX];
    int outline_node[BP_OUTLINE_MAX];
    int outline_count;

    int w_graph, w_node_kind, w_node_add, w_node_remove, w_node_start;
    int w_node_text, w_node_expr, w_node_next, w_node_speaker_npc, w_node_speaker_name, w_node_obj,
        w_node_row, w_node_quest, w_node_apply;
    int w_node_arm[BUILDER_ARMS_MAX];
    int w_graph_hint, w_layout;

    /* The graph's gesture: what the press grabbed and where it went. */
    int drag_mode;
    int drag_moved;
    int press_x;
    int press_y;
    int drag_x;
    int drag_y;
    int drag_port_node;
    int drag_port;
    unsigned char drag_mask[BUILDER_NODES_MAX];
    int drag_orig_x[BUILDER_NODES_MAX];
    int drag_orig_y[BUILDER_NODES_MAX];

    /* The new-node chip row a port dropped on empty space opens, at the
     * drop; the node it makes is linked from that port. */
    int chips_open;
    char chip_from[BUILDER_NODE_ID_MAX];
    int chip_port;
    int chip_x;
    int chip_y;

    /* The selected node's text at the chat box's line rule. */
    int w_pages, w_preview, w_preview_label;
    struct Builder_ChatMetrics chat;
    struct Builder_ChatLayout layout;
    char layout_text[BUILDER_TEXT_MAX];
    int layout_node;
    int sel_page;

    /* The lint (builder_lint.h): the rows, the find box, the VLIST. */
    int w_lint_find, w_lint_list, w_lint_label;
    struct Builder_LintRows lint;
    int lint_shown[BUILDER_LINT_ROWS_MAX];
    int lint_shown_count;
    char lint_find[TORIRS_CHROME_INPUT_MAX];
    struct bp_problem problems[BUILDER_LINT_ROWS_MAX];
    char problems_source[EDITOR_PROBLEM_SOURCE_MAX];
    uint32_t problems_hash;
    int problems_reported;
    struct
    {
        char name[BUILDER_NAME_MAX];
        int kind;
        int known;
    } names[BP_NAME_CACHE];
    int name_count;
    uint32_t hotkeys_serial;
};

static void
bp_status(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* text)
{
    copy_text(self->status, sizeof(self->status), text);
    ToriRSChrome_SetText(ui->chrome, self->w_status, self->status);
    Editor_UiLog(ui, "%s builder: %s", self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", text);
}

static void
bp_show_issues(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->w_issue_list, self->issues.count);
    ToriRSChrome_SetHidden(ui->chrome, self->w_issue_list, self->issues.count == 0);
    if( ToriRSChrome_VListWindow(ui->chrome, self->w_issue_list, &first, &shown) )
        for( int i = first; i < first + shown && i < self->issues.count; i++ )
        {
            const struct Builder_Issue* issue = &self->issues.items[i];
            char line[TORIRS_CHROME_INPUT_MAX];
            snprintf(line, sizeof(line), "%s %s: %s", issue->severity == BUILDER_ISSUE_ERROR ? "error" : "warning",
                     issue->where, issue->message);
            ToriRSChrome_VListSetRow(ui->chrome, self->w_issue_list, i, line);
        }
}

/** The tree's triggers for the subject (the Interactions tab's first list):
 *  each row names the trigger and where it lives -- `builder` (this
 *  model's), `hand-authored <file>` or `test fixture <file>` (a
 *  selftest_*.rs2 binding, live in game but nobody's content). */
static int
bp_existing_kind(
    const struct builder_panel* self,
    int index)
{
    const char* file = self->existing->files[index];
    const char* base = strrchr(file, '/');
    char own[EDITOR_DOC_KEY_MAX];
    size_t own_length;
    size_t const file_length = strlen(file);

    base = base ? base + 1 : file;
    if( strncmp(base, "selftest_", 9) == 0 )
        return 2;
    if( !self->has_model )
        return 1;
    Builder_FilePath(self->subject, self->model->name, BUILDER_FILE_SCRIPT, own, sizeof(own));
    own_length = strlen(own);
    if( file_length >= own_length && strcmp(file + file_length - own_length, own) == 0 )
        return 0;
    return 1;
}

static void
bp_show_existing(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int first;
    int shown;
    int const count = self->existing ? self->existing->count : 0;
    char line[TORIRS_CHROME_LABEL_MAX];

    ToriRSChrome_VListSetCount(ui->chrome, self->w_existing_list, count);
    if( !self->has_model )
        line[0] = '\0';
    else if( !self->existing )
        snprintf(line, sizeof(line), "no catalog: the tree's triggers are not listed");
    else
    {
        int hand = 0;
        int test = 0;
        for( int i = 0; i < count; i++ )
        {
            int const kind = bp_existing_kind(self, i);
            hand += kind == 1;
            test += kind == 2;
        }
        snprintf(line, sizeof(line), "%d trigger%s in the tree: %d hand-authored, %d test fixture%s", count,
                 count == 1 ? "" : "s", hand, test, test == 1 ? "" : "s");
    }
    ToriRSChrome_SetText(ui->chrome, self->w_existing_label, line);
    if( ToriRSChrome_VListWindow(ui->chrome, self->w_existing_list, &first, &shown) )
        for( int i = first; i < first + shown && i < count; i++ )
        {
            int const kind = bp_existing_kind(self, i);
            snprintf(line, sizeof(line), "[%s]  %s%s", self->existing->names[i],
                     kind == 0 ? "builder" : kind == 2 ? "test fixture: " : "hand-authored: ",
                     kind == 0 ? "" : self->existing->files[i]);
            ToriRSChrome_VListSetRow(ui->chrome, self->w_existing_list, i, line);
        }
}

/** The header's "will:" line, from the record's movement statements. */
static void
bp_show_will(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    char sentence[TORIRS_CHROME_INPUT_MAX];
    struct Editor_NpcEffective effective;

    if( self->w_will < 0 )
        return;
    if( !self->has_model || (!self->record.has_movement && !self->mv_valid) )
    {
        ToriRSChrome_SetHidden(ui->chrome, self->w_will, 1);
        ToriRSChrome_SetHidden(ui->chrome, self->w_will_note, 1);
        return;
    }
    /* The documents' statements (unsaved edits included) over the catalog's. */
    {
        const struct Editor_NpcMovement* movement = self->mv_valid ? &self->mv : &self->record.movement;
        Editor_NpcEffectiveMode(movement, &effective);
        /* A label's 63 bytes, as the note line below shows it. */
        Editor_NpcEffectiveSentence(movement, &effective, sentence, TORIRS_CHROME_LABEL_MAX);
        /* The sentence times a lap by straight-line legs; the Movement tab
         * walks each leg as the server does and times the lap under the
         * route. Two lap times for one route (50 here, 56 there) read as a
         * bug, so with the walked legs at hand this line leaves it out. */
        if( effective.mode == EDITOR_NPC_MODE_PATROL && self->mv_valid && self->mv_walked )
        {
            char notes[TORIRS_CHROME_LABEL_MAX];
            const char* rest = strstr(sentence, "; ");
            copy_text(notes, sizeof(notes), rest ? rest : "");
            snprintf(sentence, sizeof(sentence), "patrols %d waypoint%s, lap timed below%s",
                     movement->patrol_count, movement->patrol_count == 1 ? "" : "s", notes);
        }
    }
    /* The sentence on one line, its notes ("; wanderrange 5 is unused") on
     * the next: a docked column is 200-400 px wide. */
    {
        char* semicolon = strstr(sentence, "; ");
        if( semicolon )
        {
            *semicolon = '\0';
            ToriRSChrome_SetText(ui->chrome, self->w_will_note, semicolon + 2);
            ToriRSChrome_SetHidden(ui->chrome, self->w_will_note, 0);
        }
        else
            ToriRSChrome_SetHidden(ui->chrome, self->w_will_note, 1);
    }
    snprintf(line, sizeof(line), "will: %s", sentence);
    ToriRSChrome_SetText(ui->chrome, self->w_will, line);
    ToriRSChrome_SetHidden(ui->chrome, self->w_will, 0);
}

static void bp_mv_build(struct EditorUi* ui, struct builder_panel* self);
static void bp_mv_refresh(struct EditorUi* ui, struct builder_panel* self);
static int bp_mv_activate(struct EditorUi* ui, struct builder_panel* self, int widget);
static int bp_mv_event(struct EditorUi* ui, struct builder_panel* self, const struct ToriRSChromeEvent* event);
static void bp_mv_tick(struct EditorUi* ui, struct builder_panel* self);

static void
bp_set_editable(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int off = !self->has_model || self->read_only;
    const int widgets[] = {
        self->w_trigger_add, self->w_trigger_remove, self->w_trigger_dialogue, self->w_step_add,
        self->w_step_apply,  self->w_step_remove,    self->w_step_up,          self->w_step_down,
        self->w_server_apply, self->w_regen,         self->w_detach,          self->w_rename_op,
    };

    for( size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++ )
    {
        if( widgets[i] >= 0 )
            ToriRSChrome_SetDisabled(ui->chrome, widgets[i], off);
    }
    if( self->w_node_apply >= 0 )
    {
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_apply, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_add, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_remove, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_start, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_layout, off);
    }
}

static void
step_summary(
    const struct Builder_Step* step,
    char* out,
    size_t out_size)
{
    const char* label = Builder_StepKindLabel(step->kind);

    switch( step->kind )
    {
    case BUILDER_STEP_MES:
        snprintf(out, out_size, "%s: %s", label, step->text);
        break;
    case BUILDER_STEP_IF_VAR:
    case BUILDER_STEP_ELSE_IF_VAR:
        snprintf(out, out_size, "%s %%%s %s %s", label, step->a, step->b, step->c);
        break;
    case BUILDER_STEP_RAW:
        snprintf(out, out_size, "%s (%zu bytes)", label, strlen(step->text));
        break;
    default:
        snprintf(out, out_size, "%s %s %s %s", label, step->a, step->b, step->c);
        break;
    }
}

static void
trigger_label(
    const struct builder_panel* self,
    const struct Builder_Trigger* trigger,
    char* out,
    size_t out_size)
{
    const char* word = self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";

    if( trigger->op == BUILDER_OP_AI )
        snprintf(out, out_size, "[%s] combat", trigger->ai);
    else if( trigger->op == 0 )
        snprintf(out, out_size, "[%s%su] with %s%s", trigger->approach ? "ap" : "op", word,
                 trigger->use_obj, trigger->dialogue ? " -> dialogue" : "");
    else
        snprintf(out, out_size, "[%s%s%d] %s%s", trigger->approach ? "ap" : "op", word, trigger->op,
                 trigger->label, trigger->dialogue ? " -> dialogue" : "");
}

static void
outline_push(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* label,
    int depth,
    int expander,
    int trigger,
    int step,
    int node)
{
    int widget;

    if( self->outline_count >= BP_OUTLINE_MAX )
        return;
    /* A widget is always added at the panel's end: relink it after the
     * last outline row (first: the heading), so the outline stays under
     * its heading inside the tab (ToriRSChrome_WidgetMoveAfter). */
    ToriRSChrome_PanelBeginTab(ui->chrome, self->p_main, self->outline_tab);
    widget = ToriRSChrome_TreeRow(ui->chrome, self->p_main, label, depth, expander);
    ToriRSChrome_PanelBeginTab(ui->chrome, self->p_main, -1);
    if( widget < 0 )
        return;
    ToriRSChrome_WidgetMoveAfter(ui->chrome, widget, self->outline_after);
    self->outline_after = widget;
    self->outline_rows[self->outline_count] = widget;
    self->outline_trigger[self->outline_count] = trigger;
    self->outline_step[self->outline_count] = step;
    self->outline_node[self->outline_count] = node;
    if( (trigger >= 0 && trigger == self->sel_trigger && step == self->sel_step && node < 0)
        || (node >= 0 && node == self->sel_node) )
        ToriRSChrome_TreeSelect(ui->chrome, widget);
    self->outline_count++;
}

/** The dialogue as a tree: depth-first from the start, a node reached a
 *  second time shown as a jump. The accessible form of the GRAPH. */
static void
outline_dialogue(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* id,
    int depth,
    unsigned char* seen)
{
    const struct Builder_Model* model = self->model;

    while( id[0] && depth < 24 )
    {
        int index = -1;
        const struct Builder_Node* node;
        char line[TORIRS_CHROME_LABEL_MAX];

        for( int i = 0; i < model->node_count; i++ )
        {
            if( strcmp(model->nodes[i].id, id) == 0 )
                index = i;
        }
        if( index < 0 )
            return;
        node = &model->nodes[index];
        if( seen[index] )
        {
            snprintf(line, sizeof(line), "-> %s", node->id);
            outline_push(ui, self, line, depth, TORIRS_CHROME_TREE_LEAF, -1, -1, index);
            return;
        }
        seen[index] = 1;
        snprintf(line, sizeof(line), "%s %s: %s", node->id, Builder_NodeKindLabel(node->kind),
                 node->kind == BUILDER_NODE_BRANCH ? node->obj : node->text);
        outline_push(ui, self, line, depth,
                     node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH
                         ? TORIRS_CHROME_TREE_OPEN
                         : TORIRS_CHROME_TREE_LEAF,
                     -1, -1, index);
        if( node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH )
        {
            for( int a = 0; a < node->arm_count; a++ )
            {
                if( node->kind == BUILDER_NODE_CHOICE )
                    snprintf(line, sizeof(line), "option %d: %s", a + 1, node->arms[a].text);
                else
                    snprintf(line, sizeof(line), "if %s %s %s", node->obj, node->arms[a].cmp,
                             node->arms[a].value);
                outline_push(ui, self, line, depth + 1, TORIRS_CHROME_TREE_OPEN, -1, -1, index);
                outline_dialogue(ui, self, node->arms[a].next, depth + 2, seen);
            }
            if( node->kind == BUILDER_NODE_BRANCH && node->next[0] )
            {
                outline_push(ui, self, "otherwise", depth + 1, TORIRS_CHROME_TREE_OPEN, -1, -1, index);
                outline_dialogue(ui, self, node->next, depth + 2, seen);
            }
            return;
        }
        if( node->kind == BUILDER_NODE_END )
            return;
        id = node->next;
    }
}

static void
bp_rebuild_outline(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int npc = self->subject == BUILDER_SUBJECT_NPC;

    for( int i = 0; i < self->outline_count; i++ )
        ToriRSChrome_WidgetRemove(ui->chrome, self->outline_rows[i]);
    self->outline_count = 0;
    /* The triggers and their steps, under the Interactions tab's heading. */
    self->outline_after = self->w_outline_head;
    self->outline_tab = npc ? BUILDER_NPC_TAB_INTERACTIONS : -1;
    if( !self->has_model )
    {
        outline_push(ui, self, "Open a subject to see its triggers.", 0, TORIRS_CHROME_TREE_LEAF, -1, -1, -1);
        if( npc )
        {
            self->outline_after = self->w_dialogue_outline_head;
            self->outline_tab = BUILDER_NPC_TAB_DIALOGUE;
            outline_push(ui, self, "Open a subject to see its dialogue.", 0, TORIRS_CHROME_TREE_LEAF, -1, -1, -1);
        }
        return;
    }
    if( self->model->trigger_count == 0 )
        outline_push(ui, self, "no builder triggers yet: Add trigger", 0, TORIRS_CHROME_TREE_LEAF, -1, -1, -1);
    for( int t = 0; t < self->model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &self->model->triggers[t];
        trigger_label(self, trigger, line, sizeof(line));
        outline_push(ui, self, line, 0,
                     trigger->step_count && !trigger->dialogue ? TORIRS_CHROME_TREE_OPEN
                                                               : TORIRS_CHROME_TREE_LEAF,
                     t, -1, -1);
        if( trigger->dialogue )
            continue;
        for( int s2 = 0; s2 < trigger->step_count; s2++ )
        {
            char summary[TORIRS_CHROME_LABEL_MAX];
            step_summary(&trigger->steps[s2], summary, sizeof(summary));
            snprintf(line, sizeof(line), "%d. %s", s2 + 1, summary);
            outline_push(ui, self, line, 1, TORIRS_CHROME_TREE_LEAF, t, s2, -1);
        }
    }
    if( !npc )
        return;
    /* The dialogue as a tree, under the Dialogue tab's heading: the
     * accessible form of the graph, every node with a row to select. */
    self->outline_after = self->w_dialogue_outline_head;
    self->outline_tab = BUILDER_NPC_TAB_DIALOGUE;
    if( self->model->node_count == 0 )
    {
        outline_push(ui, self, "no dialogue yet: Runs the dialogue on a trigger, or New node", 0,
                     TORIRS_CHROME_TREE_LEAF, -1, -1, -1);
        return;
    }
    {
        unsigned char seen[BUILDER_NODES_MAX];
        int heading = 0;
        memset(seen, 0, sizeof(seen));
        outline_dialogue(ui, self, self->model->start, 0, seen);
        for( int n = 0; n < self->model->node_count; n++ )
        {
            if( seen[n] )
                continue;
            if( !heading )
            {
                outline_push(ui, self, "Orphans (not reached from the start)", 0, TORIRS_CHROME_TREE_OPEN, -1, -1,
                             -1);
                heading = 1;
            }
            outline_dialogue(ui, self, self->model->nodes[n].id, 1, seen);
        }
    }
}

static uint32_t
node_color(enum Builder_NodeKind kind)
{
    static const uint32_t k_colors[BUILDER_NODE_KIND_COUNT] = {
        0x3a5f8a, 0x3a7a4a, 0x6a6a6a, 0x6a5a3a, 0x8a6a2a, 0x7a3a7a, 0x4a4a6a, 0x8a3a3a, 0x2a2a2a,
    };
    return k_colors[kind];
}

/** Where port `port` of `node` sits: its centre on the node's foot. */
static int
port_x(
    const struct Builder_Node* node,
    int port)
{
    int const ports = Builder_NodePortCount(node);
    return node->x + (BP_NODE_W * (port + 1)) / (ports + 1);
}

static int
port_y(const struct Builder_Node* node)
{
    return node->y + BP_NODE_H;
}

/**
 * A caption of any length on a GRAPH well: an item holds 31 bytes, so the
 * text goes as runs of them, each placed after the last one's width in the
 * face the chrome draws GRAPH text in (the theme's row font). Measured, so
 * the runs meet; nothing is cut. Returns the width drawn.
 */
static int
graph_text_run(
    struct EditorUi* ui,
    int widget,
    int x,
    int baseline,
    uint32_t color,
    const char* text)
{
    int const font = ui->chrome->theme.font_row;
    int width = 0;
    size_t length = strlen(text);
    size_t at = 0;

    while( at < length )
    {
        char run[TORIRS_CHROME_GRAPH_TEXT_MAX];
        size_t take = length - at;
        if( take > sizeof(run) - 1 )
            take = sizeof(run) - 1;
        memcpy(run, text + at, take);
        run[take] = '\0';
        ToriRSChrome_GraphText(ui->chrome, widget, x + width, baseline, color, run);
        width += ToriRSChrome_MeasureText(font, 1, run);
        at += take;
    }
    return width;
}

static int
graph_text_width(
    struct EditorUi* ui,
    const char* text)
{
    return ToriRSChrome_MeasureText(ui->chrome->theme.font_row, 1, text);
}

/** A chat line's bytes as the box shows them: tags dropped, <lt> / <gt>
 *  as their glyphs. */
static void
chat_visible_text(
    const char* line,
    char* out,
    size_t out_size)
{
    size_t length = 0;

    for( const char* at = line; *at && length + 1 < out_size; at++ )
    {
        if( *at == '<' )
        {
            const char* end = strchr(at, '>');
            if( end )
            {
                if( end - at == 3 && strncmp(at, "<lt>", 4) == 0 )
                    out[length++] = '<';
                else if( end - at == 3 && strncmp(at, "<gt>", 4) == 0 )
                    out[length++] = '>';
                at = end;
                continue;
            }
        }
        out[length++] = *at;
    }
    out[length] = '\0';
}

/**
 * A node's caption, measured against its box: "id kind", else the id alone,
 * else the id cut to fit with an ellipsis (the whole id is the outline's
 * row and the form's). Never wider than the box.
 */
static void
graph_node_label(
    struct EditorUi* ui,
    const struct Builder_Node* node,
    char* out,
    size_t out_size)
{
    int const room = BP_NODE_W - 8;
    size_t length;

    snprintf(out, out_size, "%s %s", node->id, Builder_NodeKindToken(node->kind));
    if( strlen(node->id) + 1 + strlen(Builder_NodeKindToken(node->kind)) < out_size
        && graph_text_width(ui, out) <= room )
        return;
    snprintf(out, out_size, "%s", node->id);
    if( strlen(node->id) < out_size && graph_text_width(ui, out) <= room )
        return;
    length = strlen(out);
    while( length > 0 )
    {
        char cut[TORIRS_CHROME_GRAPH_TEXT_MAX + 4];
        snprintf(cut, sizeof(cut), "%.*s...", (int)length, node->id);
        if( strlen(cut) < out_size && graph_text_width(ui, cut) <= room )
        {
            snprintf(out, out_size, "%s", cut);
            return;
        }
        length--;
    }
    out[0] = '\0';
}

static void
bp_redraw_graph(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;

    if( self->w_graph < 0 )
        return;
    ToriRSChrome_GraphBegin(chrome, self->w_graph);
    if( self->has_model )
    {
        const struct Builder_Model* model = self->model;
        unsigned char orphans[BUILDER_NODES_MAX];

        (void)Builder_ModelOrphans(model, orphans);
        /* Links first, under the nodes: port to the target's head. */
        for( int n = 0; n < model->node_count; n++ )
        {
            const struct Builder_Node* node = &model->nodes[n];
            int const ports = Builder_NodePortCount(node);
            for( int p = 0; p < ports; p++ )
            {
                const char* next = Builder_NodePortNextConst(node, p);
                const struct Builder_Node* to = next[0] ? Builder_ModelNodeConst(model, next) : NULL;
                if( !to )
                    continue;
                ToriRSChrome_GraphLine(chrome, self->w_graph, port_x(node, p), port_y(node),
                                       to->x + BP_NODE_W / 2, to->y, 0xc8c8c8);
            }
        }
        for( int n = 0; n < model->node_count; n++ )
        {
            const struct Builder_Node* node = &model->nodes[n];
            int const ports = Builder_NodePortCount(node);
            char label[TORIRS_CHROME_GRAPH_TEXT_MAX];

            ToriRSChrome_GraphRect(chrome, self->w_graph, node->x, node->y, BP_NODE_W, BP_NODE_H,
                                   node_color(node->kind), 1, n + 1);
            if( orphans[n] )
                ToriRSChrome_GraphRect(chrome, self->w_graph, node->x, node->y, BP_NODE_W, BP_NODE_H,
                                       0xd86a4a, 0, 0);
            if( n == self->sel_node )
                ToriRSChrome_GraphRect(chrome, self->w_graph, node->x - 2, node->y - 2, BP_NODE_W + 4,
                                       BP_NODE_H + 4, 0xffd24a, 0, 0);
            if( strcmp(node->id, model->start) == 0 )
                ToriRSChrome_GraphRect(chrome, self->w_graph, node->x - 4, node->y + 4, 3, BP_NODE_H - 8,
                                       0x4aff7a, 1, 0);
            graph_node_label(ui, node, label, sizeof(label));
            ToriRSChrome_GraphText(chrome, self->w_graph, node->x + 4, node->y + 17, 0xffffff, label);
            /* The out-ports, keyed so a press on one starts a link. A linked
             * port is filled; an open one (the dialogue ends there) is not. */
            for( int p = 0; p < ports && p < BP_PORTS_STRIDE; p++ )
            {
                int const linked = Builder_NodePortNextConst(node, p)[0] != '\0';
                int const px = port_x(node, p) - BP_PORT_W / 2;
                int const py = port_y(node) - BP_PORT_H / 2;
                ToriRSChrome_GraphRect(chrome, self->w_graph, px, py, BP_PORT_W, BP_PORT_H,
                                       linked ? 0xe8e8e8 : 0x303030, 1,
                                       BP_KEY_PORT + n * BP_PORTS_STRIDE + p);
                ToriRSChrome_GraphRect(chrome, self->w_graph, px, py, BP_PORT_W, BP_PORT_H, 0xe8e8e8, 0, 0);
            }
        }
        /* The link being drawn: from the grabbed port to the pointer. */
        if( self->drag_mode == BP_DRAG_PORT && self->drag_port_node >= 0
            && self->drag_port_node < model->node_count )
        {
            const struct Builder_Node* from = &model->nodes[self->drag_port_node];
            ToriRSChrome_GraphLine(chrome, self->w_graph, port_x(from, self->drag_port), port_y(from),
                                   self->drag_x, self->drag_y, 0xffd24a);
        }
        /* The new-node chip row, at the drop: one chip per kind. */
        if( self->chips_open )
        {
            int const row_w = BUILDER_NODE_KIND_COUNT * (BP_CHIP_W + 2) + 2;
            ToriRSChrome_GraphRect(chrome, self->w_graph, self->chip_x - 1, self->chip_y - 1, row_w,
                                   BP_CHIP_H + 2, 0x101010, 1, 0);
            for( int k = 0; k < BUILDER_NODE_KIND_COUNT; k++ )
            {
                char chip[TORIRS_CHROME_GRAPH_TEXT_MAX];
                char* space;
                int const x = self->chip_x + k * (BP_CHIP_W + 2);
                int text_w;

                ToriRSChrome_GraphRect(chrome, self->w_graph, x, self->chip_y, BP_CHIP_W, BP_CHIP_H,
                                       node_color((enum Builder_NodeKind)k), 1, BP_KEY_CHIP + k);
                /* The chip is narrow: the kind's first word ("NPC", "Player",
                 * ...), every one of which fits it. */
                snprintf(chip, sizeof(chip), "%s", Builder_NodeKindLabel((enum Builder_NodeKind)k));
                space = strchr(chip, ' ');
                if( space )
                    *space = '\0';
                text_w = graph_text_width(ui, chip);
                assert(text_w <= BP_CHIP_W);
                ToriRSChrome_GraphText(chrome, self->w_graph, x + (BP_CHIP_W - text_w) / 2,
                                       self->chip_y + 13, 0xffffff, chip);
            }
        }
    }
    ToriRSChrome_GraphEnd(chrome, self->w_graph);
}

/* ---- the text's pages and the chat-box preview ----------------------------- */

static const struct Builder_Node*
bp_selected_node(const struct builder_panel* self)
{
    if( !self->has_model || self->sel_node < 0 || self->sel_node >= self->model->node_count )
        return NULL;
    return &self->model->nodes[self->sel_node];
}

/**
 * The text the pages lay out: what the node form's Text field holds now, so
 * the pages follow the typing -- unless the field is full and the model's
 * page is longer (a one-line field holds TORIRS_CHROME_INPUT_MAX - 1 bytes,
 * a page BUILDER_TEXT_MAX - 1), when the model's text is the one shown.
 */
static const char*
bp_layout_source(
    struct EditorUi* ui,
    const struct builder_panel* self)
{
    const struct Builder_Node* node = bp_selected_node(self);
    const char* field;
    const char* model;

    if( !node || !Builder_ChatNodeText(node) || self->w_node_text < 0 )
        return "";
    field = ToriRSChrome_Text(ui->chrome, self->w_node_text);
    model = Builder_ChatNodeText(node);
    if( strlen(field) >= TORIRS_CHROME_INPUT_MAX - 1 && strlen(model) > strlen(field) )
        return model;
    return field;
}

static void
bp_layout_text(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    copy_text(self->layout_text, sizeof(self->layout_text), bp_layout_source(ui, self));
    self->layout_node = self->sel_node;
    Builder_ChatLayoutText(self->layout_text, &self->chat, &self->layout);
    if( self->sel_page >= self->layout.page_count )
        self->sel_page = self->layout.page_count - 1;
    if( self->sel_page < 0 )
        self->sel_page = 0;
}

/** The Text field as the chat box breaks it: a row per line, a thin rule
 *  where the client starts the next page, each page keyed (a click shows
 *  it in the preview). */
static void
bp_redraw_pages(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    const struct Builder_Node* node = bp_selected_node(self);
    int const lines = self->layout.line_count > 0 ? self->layout.line_count : 1;

    if( self->w_pages < 0 )
        return;
    ToriRSChrome_SetCustomHeight(chrome, self->w_pages, lines * BP_PAGE_LINE_H + 4);
    ToriRSChrome_GraphBegin(chrome, self->w_pages);
    if( node && Builder_ChatNodeText(node) )
    {
        for( int page = 0; page < self->layout.page_count; page++ )
        {
            int const first = page * self->chat.page_lines;
            int count = self->layout.line_count - first;
            if( count > self->chat.page_lines )
                count = self->chat.page_lines;
            ToriRSChrome_GraphRect(chrome, self->w_pages, 0, 2 + first * BP_PAGE_LINE_H, self->chat.width + 8,
                                   count * BP_PAGE_LINE_H, page == self->sel_page ? 0x34404e : 0x2a2a2a, 1,
                                   page + 1);
        }
        /* The box's right edge: where the rule breaks a line. */
        ToriRSChrome_GraphRect(chrome, self->w_pages, self->chat.width + 4, 2, 1, lines * BP_PAGE_LINE_H, 0x505050,
                               1, 0);
        for( int i = 0; i < self->layout.line_count; i++ )
        {
            char line[BUILDER_CHAT_TEXT_MAX];
            char shown[BUILDER_CHAT_TEXT_MAX];
            int const y = 2 + i * BP_PAGE_LINE_H;

            if( i > 0 && self->layout.lines[i].page != self->layout.lines[i - 1].page )
            {
                char caption[TORIRS_CHROME_GRAPH_TEXT_MAX];
                snprintf(caption, sizeof(caption), "page %d", self->layout.lines[i].page + 1);
                ToriRSChrome_GraphRect(chrome, self->w_pages, 0, y, self->chat.width + 8, 1, BP_PAGE_RULE_COLOR, 1,
                                       0);
                ToriRSChrome_GraphText(chrome, self->w_pages, self->chat.width + 10, y + 12, BP_PAGE_RULE_COLOR,
                                       caption);
            }
            Builder_ChatLineText(&self->layout, i, line, sizeof(line));
            chat_visible_text(line, shown, sizeof(shown));
            graph_text_run(ui, self->w_pages, 4, y + 12, 0xe8e8e8, shown);
        }
    }
    ToriRSChrome_GraphEnd(chrome, self->w_pages);
}

/** The selected page as the chat box draws it: the head beside the text,
 *  the speaker over it, the page's lines centred under it and the continue
 *  line; a choice is its title and options. */
static void
bp_redraw_preview(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    const struct Builder_Node* node = bp_selected_node(self);
    int const column = BP_CHAT_W - BP_CHAT_TEXT_X - 8;
    int const centre = BP_CHAT_TEXT_X + column / 2;
    char line[TORIRS_CHROME_INPUT_MAX];

    if( self->w_preview < 0 )
        return;
    ToriRSChrome_GraphBegin(chrome, self->w_preview);
    ToriRSChrome_GraphRect(chrome, self->w_preview, 0, 0, BP_CHAT_W, BP_CHAT_H, 0xc9b98f, 1, 0);
    ToriRSChrome_GraphRect(chrome, self->w_preview, 0, 0, BP_CHAT_W, BP_CHAT_H, 0x5a4a2a, 0, 0);
    if( !node )
    {
        ToriRSChrome_GraphText(chrome, self->w_preview, 8, 20, 0x3a2a1a, "Select a node to see its page.");
        ToriRSChrome_SetText(chrome, self->w_preview_label, "");
    }
    else if( node->kind == BUILDER_NODE_CHOICE )
    {
        const char* title = node->text[0] ? node->text : "Select an option";
        int const title_w = graph_text_width(ui, title);
        graph_text_run(ui, self->w_preview, BP_CHAT_W / 2 - title_w / 2, 18, 0x800000, title);
        for( int a = 0; a < node->arm_count; a++ )
        {
            char shown[TORIRS_CHROME_INPUT_MAX];
            int width;
            chat_visible_text(node->arms[a].text, shown, sizeof(shown));
            width = graph_text_width(ui, shown);
            graph_text_run(ui, self->w_preview, BP_CHAT_W / 2 - width / 2, 38 + a * 16, 0x000000, shown);
        }
        snprintf(line, sizeof(line), "chat box preview: %s, %d option(s)", node->id, node->arm_count);
        ToriRSChrome_SetText(chrome, self->w_preview_label, line);
    }
    else if( !Builder_ChatNodeText(node) )
    {
        snprintf(line, sizeof(line), "A %s shows no page.", Builder_NodeKindLabel(node->kind));
        ToriRSChrome_GraphText(chrome, self->w_preview, 8, 20, 0x3a2a1a, line);
        ToriRSChrome_SetText(chrome, self->w_preview_label, "");
    }
    else
    {
        int const head = node->kind == BUILDER_NODE_NPC || node->kind == BUILDER_NODE_PLAYER;
        int const first = self->sel_page * self->chat.page_lines;
        const char* speaker = "";
        int head_drawn = 0;

        if( node->kind == BUILDER_NODE_NPC )
            speaker = node->speaker_name[0] ? node->speaker_name
                      : self->record.display[0] ? self->record.display
                                                : self->model->name;
        else if( node->kind == BUILDER_NODE_PLAYER )
            speaker = self->chat.player ? self->chat.player : BUILDER_CHAT_PLAYER_STANDIN;
        if( head )
        {
            /* The npc's head left of the text, the player's right of it, as
             * the chat box sets them. */
            int const head_x = node->kind == BUILDER_NODE_NPC ? 24 : BP_CHAT_W - 24 - BP_CHAT_HEAD;
            int scene_id = 0;
            uint32_t version = 0;
            const char* npc = node->speaker_npc[0] ? node->speaker_npc : self->model->name;
            if( node->kind == BUILDER_NODE_NPC && self->services.chat_head
                && self->services.chat_head(self->services.user_data, npc, node->expr, &scene_id, &version) )
            {
                ToriRSChrome_GraphImage(chrome, self->w_preview, head_x, 24, BP_CHAT_HEAD, BP_CHAT_HEAD, scene_id, 0,
                                        version);
                head_drawn = 1;
            }
            else
            {
                ToriRSChrome_GraphRect(chrome, self->w_preview, head_x, 24, BP_CHAT_HEAD, BP_CHAT_HEAD, 0x8a7a5a, 0,
                                       0);
                ToriRSChrome_GraphText(chrome, self->w_preview, head_x + 8, 52, 0x5a4a2a, "head");
            }
        }
        if( speaker[0] )
        {
            int const width = graph_text_width(ui, speaker);
            graph_text_run(ui, self->w_preview, centre - width / 2, 18, 0x800000, speaker);
        }
        for( int i = 0; i < self->chat.page_lines && first + i < self->layout.line_count; i++ )
        {
            char raw[BUILDER_CHAT_TEXT_MAX];
            char shown[BUILDER_CHAT_TEXT_MAX];
            int width;
            Builder_ChatLineText(&self->layout, first + i, raw, sizeof(raw));
            chat_visible_text(raw, shown, sizeof(shown));
            width = graph_text_width(ui, shown);
            graph_text_run(ui, self->w_preview, centre - width / 2, 38 + i * 16, 0x000000, shown);
        }
        {
            static const char k_more[] = "Click here to continue";
            int const width = graph_text_width(ui, k_more);
            ToriRSChrome_GraphText(chrome, self->w_preview, centre - width / 2, BP_CHAT_H - 10, 0x0000ff, k_more);
        }
        snprintf(line, sizeof(line), "chat box preview: page %d of %d%s%s", self->sel_page + 1,
                 self->layout.page_count, head && !head_drawn ? " (no head: no preview pipeline is bound)" : "",
                 self->layout.truncated ? "; the text is longer than the preview lays out" : "");
        ToriRSChrome_SetText(chrome, self->w_preview_label, line);
    }
    ToriRSChrome_GraphEnd(chrome, self->w_preview);
}

static void
bp_show_step_fields(
    struct EditorUi* ui,
    struct builder_panel* self,
    enum Builder_StepKind kind)
{
    for( int f = 0; f < 4; f++ )
    {
        const char* label = k_step_fields[kind][f];
        ToriRSChrome_SetLabel(ui->chrome, self->w_step_field[f], label[0] ? label : "-");
        ToriRSChrome_SetHidden(ui->chrome, self->w_step_field[f], label[0] == '\0');
    }
}

static void
bp_load_step(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    const struct Builder_Step* step;
    const struct Builder_Trigger* trigger;

    if( !self->has_model || self->sel_trigger < 0 || self->sel_trigger >= self->model->trigger_count )
        return;
    trigger = &self->model->triggers[self->sel_trigger];
    if( self->sel_step < 0 || self->sel_step >= trigger->step_count )
        return;
    step = &trigger->steps[self->sel_step];
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->w_step_kind, (int)step->kind);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[0], step->a);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[1], step->b);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[2], step->c);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[3], step->text);
    bp_show_step_fields(ui, self, step->kind);
}

static void
bp_read_step(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Step* step)
{
    int kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_step_kind);
    const char* text;

    if( kind < 0 || kind >= BUILDER_STEP_KIND_COUNT )
        kind = BUILDER_STEP_MES;
    memset(step, 0, sizeof(*step));
    step->kind = (enum Builder_StepKind)kind;
    copy_text(step->a, sizeof(step->a), ToriRSChrome_Text(ui->chrome, self->w_step_field[0]));
    copy_text(step->b, sizeof(step->b), ToriRSChrome_Text(ui->chrome, self->w_step_field[1]));
    copy_text(step->c, sizeof(step->c), ToriRSChrome_Text(ui->chrome, self->w_step_field[2]));
    /* RAW is one input line: "\n" stands for a line break in it. */
    text = ToriRSChrome_Text(ui->chrome, self->w_step_field[3]);
    if( step->kind == BUILDER_STEP_RAW )
    {
        size_t out = 0;
        for( const char* at = text ? text : ""; *at && out + 1 < sizeof(step->text); at++ )
        {
            if( at[0] == '\\' && at[1] == 'n' )
            {
                step->text[out++] = '\n';
                at++;
            }
            else
                step->text[out++] = *at;
        }
        step->text[out] = '\0';
    }
    else
        copy_text(step->text, sizeof(step->text), text);
}

static void
bp_load_node(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    const struct Builder_Node* node;
    struct ToriRSChrome* chrome = ui->chrome;

    if( self->w_graph < 0 || !self->has_model || self->sel_node < 0
        || self->sel_node >= self->model->node_count )
        return;
    node = &self->model->nodes[self->sel_node];
    ToriRSChrome_DropdownSetSelected(chrome, self->w_node_kind, (int)node->kind);
    ToriRSChrome_SetText(chrome, self->w_node_text,
                         node->kind == BUILDER_NODE_STEP ? node->step.text : node->text);
    ToriRSChrome_SetText(chrome, self->w_node_expr, node->expr);
    ToriRSChrome_SetText(chrome, self->w_node_next, node->next);
    ToriRSChrome_SetText(chrome, self->w_node_speaker_npc, node->speaker_npc);
    ToriRSChrome_SetText(chrome, self->w_node_speaker_name, node->speaker_name);
    ToriRSChrome_SetText(chrome, self->w_node_obj,
                         node->kind == BUILDER_NODE_STEP ? node->step.a : node->obj);
    ToriRSChrome_SetText(chrome, self->w_node_row,
                         node->kind == BUILDER_NODE_QUEST && node->quest == BUILDER_QUEST_START
                             ? node->value
                             : node->row);
    ToriRSChrome_SetText(chrome, self->w_node_quest,
                         node->kind == BUILDER_NODE_STEP
                             ? Builder_StepKindToken(node->step.kind)
                             : (node->quest == BUILDER_QUEST_COMPLETE ? "complete" : "start"));
    for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
    {
        char arm[TORIRS_CHROME_INPUT_MAX] = "";
        if( a < node->arm_count )
        {
            if( node->kind == BUILDER_NODE_CHOICE )
                snprintf(arm, sizeof(arm), "%s|%s", node->arms[a].text, node->arms[a].next);
            else
                snprintf(arm, sizeof(arm), "%s|%s|%s", node->arms[a].cmp, node->arms[a].value,
                         node->arms[a].next);
        }
        ToriRSChrome_SetText(chrome, self->w_node_arm[a], arm);
    }
}

/** Fill `node` from the node form. Returns 0 (and logs) on a malformed arm. */
static int
bp_read_node(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Node* node)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int kind = ToriRSChrome_DropdownSelected(chrome, self->w_node_kind);
    const char* quest = ToriRSChrome_Text(chrome, self->w_node_quest);

    if( kind < 0 || kind >= BUILDER_NODE_KIND_COUNT )
        return 0;
    node->kind = (enum Builder_NodeKind)kind;
    copy_text(node->expr, sizeof(node->expr), ToriRSChrome_Text(chrome, self->w_node_expr));
    copy_text(node->next, sizeof(node->next), ToriRSChrome_Text(chrome, self->w_node_next));
    copy_text(node->speaker_npc, sizeof(node->speaker_npc),
              ToriRSChrome_Text(chrome, self->w_node_speaker_npc));
    copy_text(node->speaker_name, sizeof(node->speaker_name),
              ToriRSChrome_Text(chrome, self->w_node_speaker_name));
    copy_text(node->row, sizeof(node->row), ToriRSChrome_Text(chrome, self->w_node_row));
    if( node->kind == BUILDER_NODE_STEP )
    {
        /* A dialogue step: Quest field names the step kind, Obj is its first
         * field, Text its text. */
        enum Builder_StepKind step_kind = Builder_StepKindParse(quest ? quest : "");
        if( step_kind == BUILDER_STEP_KIND_COUNT )
            step_kind = BUILDER_STEP_SET_VAR;
        if( node->step.kind != step_kind )
            Builder_StepInit(&node->step, step_kind);
        copy_text(node->step.a, sizeof(node->step.a), ToriRSChrome_Text(chrome, self->w_node_obj));
        copy_text(node->step.text, sizeof(node->step.text),
                  ToriRSChrome_Text(chrome, self->w_node_text));
    }
    else
    {
        copy_text(node->text, sizeof(node->text), ToriRSChrome_Text(chrome, self->w_node_text));
        copy_text(node->obj, sizeof(node->obj), ToriRSChrome_Text(chrome, self->w_node_obj));
        node->quest = quest && strcmp(quest, "complete") == 0 ? BUILDER_QUEST_COMPLETE
                                                              : BUILDER_QUEST_START;
        if( node->kind == BUILDER_NODE_QUEST && node->quest == BUILDER_QUEST_START )
        {
            copy_text(node->value, sizeof(node->value), node->row);
            node->row[0] = '\0';
        }
    }
    if( node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH )
    {
        node->arm_count = 0;
        for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
        {
            char arm[TORIRS_CHROME_INPUT_MAX];
            char* parts[3] = { NULL, NULL, NULL };
            int count = 0;
            struct Builder_Arm* out;

            copy_text(arm, sizeof(arm), ToriRSChrome_Text(chrome, self->w_node_arm[a]));
            if( arm[0] == '\0' )
                continue;
            parts[count++] = arm;
            for( char* at = arm; *at && count < 3; at++ )
            {
                if( *at == '|' )
                {
                    *at = '\0';
                    parts[count++] = at + 1;
                }
            }
            out = &node->arms[node->arm_count];
            memset(out, 0, sizeof(*out));
            if( node->kind == BUILDER_NODE_CHOICE )
            {
                if( count != 2 )
                {
                    Editor_UiLog(ui, "npc builder: option %d must be 'text|next node'", a + 1);
                    return 0;
                }
                copy_text(out->text, sizeof(out->text), parts[0]);
                copy_text(out->next, sizeof(out->next), parts[1]);
            }
            else
            {
                if( count != 3 )
                {
                    Editor_UiLog(ui, "npc builder: arm %d must be 'cmp|value|next node'", a + 1);
                    return 0;
                }
                copy_text(out->cmp, sizeof(out->cmp), parts[0]);
                copy_text(out->value, sizeof(out->value), parts[1]);
                copy_text(out->next, sizeof(out->next), parts[2]);
            }
            node->arm_count++;
        }
    }
    else
        node->arm_count = 0;
    return 1;
}

/* ---- the lint and its Problems rows ---------------------------------------- */

/** One name the lint asks about, asked of the tree once per open subject. */
static int
bp_name_known(
    struct builder_panel* self,
    int kind,
    const char* name)
{
    int known;

    for( int i = 0; i < self->name_count; i++ )
    {
        if( self->names[i].kind == kind && strcmp(self->names[i].name, name) == 0 )
            return self->names[i].known;
    }
    known = self->services.name_known(self->services.user_data, kind, name);
    /* "Cannot say" (no catalog yet) is asked again next time. */
    if( known >= 0 && self->name_count < BP_NAME_CACHE )
    {
        copy_text(self->names[self->name_count].name, sizeof(self->names[0].name), name);
        self->names[self->name_count].kind = kind;
        self->names[self->name_count].known = known;
        self->name_count++;
    }
    return known;
}

static int
bp_lint_var_known(
    void* user_data,
    const char* name)
{
    return bp_name_known(user_data, BUILDER_NAME_VAR, name);
}

static int
bp_lint_proc_known(
    void* user_data,
    const char* name)
{
    return bp_name_known(user_data, BUILDER_NAME_PROC, name);
}

/** A case-insensitive substring test, for the find box. */
static int
bp_contains(
    const char* text,
    const char* find)
{
    size_t const length = strlen(find);

    if( length == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i;
        for( i = 0; i < length && at[i]; i++ )
        {
            if( tolower((unsigned char)at[i]) != tolower((unsigned char)find[i]) )
                break;
        }
        if( i == length )
            return 1;
    }
    return 0;
}

static void
bp_lint_row_text(
    const struct Builder_LintRow* row,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s  %s  %s: %s", row->error ? "error" : "warning", row->where,
             Builder_LintKindLabel(row->kind), row->message);
}

/** The lint list's rows: the find box's filter over the lint. */
static void
bp_fill_lint(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int first;
    int shown;

    if( self->w_lint_list < 0 )
        return;
    self->lint_shown_count = 0;
    for( int i = 0; i < self->lint.count; i++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        bp_lint_row_text(&self->lint.rows[i], text, sizeof(text));
        if( bp_contains(text, self->lint_find) )
            self->lint_shown[self->lint_shown_count++] = i;
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->w_lint_list, self->lint_shown_count);
    if( ToriRSChrome_VListWindow(ui->chrome, self->w_lint_list, &first, &shown) )
    {
        for( int r = first; r < first + shown && r < self->lint_shown_count; r++ )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            bp_lint_row_text(&self->lint.rows[self->lint_shown[r]], text, sizeof(text));
            ToriRSChrome_VListSetRow(ui->chrome, self->w_lint_list, r, text);
        }
    }
    {
        char line[TORIRS_CHROME_INPUT_MAX];
        int errors = 0;
        for( int i = 0; i < self->lint.count; i++ )
            errors += self->lint.rows[i].error;
        if( !self->has_model )
            line[0] = '\0';
        else if( self->lint.count == 0 )
            snprintf(line, sizeof(line), "lint: clean%s",
                     self->lint.unchecked ? " (vars and procs not checked: no catalog)" : "");
        else
            snprintf(line, sizeof(line), "lint: %d error(s), %d warning(s); %d of %d shown%s%s", errors,
                     self->lint.count - errors, self->lint_shown_count, self->lint.count,
                     self->lint.dropped ? ", more past the cap" : "",
                     self->lint.unchecked ? "; vars and procs not checked: no catalog" : "");
        ToriRSChrome_SetText(ui->chrome, self->w_lint_label, line);
    }
}

static void bp_select_node(struct EditorUi* ui, struct builder_panel* self, int index);

/** What a click on a lint row (here or in Problems) does: select what the
 *  row names. */
static void
bp_open_lint(
    struct EditorUi* ui,
    struct builder_panel* self,
    int index)
{
    const struct Builder_LintRow* row;

    if( index < 0 || index >= self->lint.count )
        return;
    row = &self->lint.rows[index];
    if( row->node >= 0 )
        bp_select_node(ui, self, row->node);
    else if( row->trigger >= 0 && row->trigger < self->model->trigger_count )
    {
        self->sel_trigger = row->trigger;
        self->sel_step = row->step;
        bp_load_step(ui, self);
        bp_rebuild_outline(ui, self);
    }
    Editor_UiStatus(ui, "%s: %s", row->where, row->message);
}

static void
bp_problem_open(
    struct EditorUi* ui,
    void* user,
    int index)
{
    bp_open_lint(ui, user, index);
}

/** Lint the model, show the rows, and hand them to Problems as this
 *  subject's source. */
static void
bp_run_lint(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct Builder_LintNames names;

    if( self->w_graph < 0 )
        return;
    memset(&self->lint, 0, sizeof(self->lint));
    if( self->has_model )
    {
        memset(&names, 0, sizeof(names));
        names.user_data = self;
        if( self->services.name_known )
        {
            names.var_known = bp_lint_var_known;
            names.proc_known = bp_lint_proc_known;
        }
        (void)Builder_Lint(self->model, &names, &self->lint);
        memset(self->problems, 0, sizeof(self->problems));
        for( int i = 0; i < self->lint.count; i++ )
        {
            struct bp_problem* problem = &self->problems[i];
            problem->severity = self->lint.rows[i].error ? EDITOR_PROBLEM_ERROR : EDITOR_PROBLEM_WARNING;
            copy_text(problem->where, sizeof(problem->where), self->lint.rows[i].where);
            snprintf(problem->message, sizeof(problem->message), "%s: %s",
                     Builder_LintKindLabel(self->lint.rows[i].kind), self->lint.rows[i].message);
        }
        snprintf(self->problems_source, sizeof(self->problems_source), "%s:%s",
                 self->subject == BUILDER_SUBJECT_NPC ? BUILDER_PANEL_NPC : BUILDER_PANEL_LOC, self->model->name);
        /* Problems hears a source again only when its rows changed: a
         * drag's regeneration that moved nothing in them says nothing. */
        {
            uint32_t hash = 2166136261u;
            const unsigned char* bytes = (const unsigned char*)self->problems;
            size_t const size = sizeof(self->problems[0]) * (size_t)self->lint.count;
            for( size_t i = 0; i < size; i++ )
                hash = (hash ^ bytes[i]) * 16777619u;
            for( const char* at = self->problems_source; *at; at++ )
                hash = (hash ^ (unsigned char)*at) * 16777619u;
            hash ^= (uint32_t)self->lint.count;
            if( !self->problems_reported || hash != self->problems_hash )
            {
                /* The source's rows are these and only these: the old
                 * ones go, each row's click selects its node. */
                (void)Editor_Problems_Clear(ui, self->problems_source);
                for( int i = 0; i < self->lint.count; i++ )
                {
                    char text[EDITOR_PROBLEM_SOURCE_MAX + 64 + 192];
                    snprintf(text, sizeof(text), "%s: %s", self->problems[i].where, self->problems[i].message);
                    Editor_Problems_ReportOpen(ui, self->problems_source, "", 0, self->problems[i].severity, text,
                                               bp_problem_open, self, i);
                }
                self->problems_hash = hash;
                self->problems_reported = 1;
            }
        }
    }
    bp_fill_lint(ui, self);
}

static void
bp_refresh(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    bp_rebuild_outline(ui, self);
    bp_redraw_graph(ui, self);
    bp_show_issues(ui, self);
    bp_show_existing(ui, self);
    bp_mv_refresh(ui, self);
    bp_show_will(ui, self);
    bp_set_editable(ui, self);
    bp_run_lint(ui, self);
    if( self->w_graph >= 0 )
    {
        bp_layout_text(ui, self);
        bp_redraw_pages(ui, self);
        bp_redraw_preview(ui, self);
    }
}

/** Select node `index` (-1 none) everywhere it shows: the graph, the
 *  outline, the form, the pages and the preview. */
static void
bp_select_node(
    struct EditorUi* ui,
    struct builder_panel* self,
    int index)
{
    self->sel_node = self->has_model && index >= 0 && index < self->model->node_count ? index : -1;
    self->sel_page = 0;
    bp_load_node(ui, self);
    bp_rebuild_outline(ui, self);
    bp_redraw_graph(ui, self);
    bp_layout_text(ui, self);
    bp_redraw_pages(ui, self);
    bp_redraw_preview(ui, self);
}

/** The model changed: regenerate every derived file through the authority,
 *  as one stroke. */
static int
bp_commit(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    enum Builder_SyncResult result;

    assert(self->has_model);
    Builder_IssuesClear(&self->issues);
    if( self->read_only )
    {
        bp_status(ui, self, "read-only: the edit was not applied");
        bp_refresh(ui, self);
        return 0;
    }
    if( !self->services.submit.apply )
    {
        Builder_Validate(self->model, &self->issues);
        bp_status(ui, self, "no session: the model is not saved anywhere");
        bp_refresh(ui, self);
        return 0;
    }
    result = Builder_Regenerate(self->model, &self->services.submit, &self->issues);
    if( result == BUILDER_SYNC_REFUSED )
        bp_status(ui, self, "the authority refused the regeneration (someone else edited the files); re-open");
    else
    {
        char line[192];
        snprintf(line, sizeof(line), "%s: %s (%d error(s), %d warning(s))", self->model->name,
                 result == BUILDER_SYNC_APPLIED ? "regenerated" : "unchanged", self->issues.errors,
                 self->issues.count - self->issues.errors);
        bp_status(ui, self, line);
    }
    bp_refresh(ui, self);
    return result != BUILDER_SYNC_REFUSED;
}

static int
bp_open(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* name)
{
    char key[EDITOR_DOC_KEY_MAX];
    char error[160];
    const char* text = NULL;
    size_t length = 0;
    struct Builder_ExistingList* existing;

    if( !Builder_NameValid(name) )
    {
        bp_status(ui, self, "a subject is a record name: [a-z0-9_]");
        return 0;
    }
    memset(&self->record, 0, sizeof(self->record));
    if( self->services.record
        && !self->services.record(self->services.user_data, self->subject, name, -1, &self->record) )
    {
        char line[160];
        snprintf(line, sizeof(line), "no %s named %s in the catalog",
                 self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", name);
        bp_status(ui, self, line);
        return 0;
    }

    /* Another subject: the last one's Problems rows and name answers go. */
    if( self->problems_source[0] )
    {
        (void)Editor_Problems_Clear(ui, self->problems_source);
        self->problems_source[0] = '\0';
    }
    self->problems_reported = 0;
    self->name_count = 0;
    /* Another npc: the route tool, Play, the selections and the map's view
     * were the last one's (the map kept showing the sheep field under a
     * watchman by the church). */
    if( self->w_mv_map >= 0 )
    {
        self->mv_sel = -1;
        self->sp_sel = -1;
        self->mv_fit_pending = 1;
        self->mv_legs_hash = 0;
        self->mv_tool = 0;
        self->mv_wdrag_wp = -1;
        self->mv_wpress = 0;
        self->mv_play = 0;
        ToriRSChrome_RadioSelect(ui->chrome, self->w_mv_tool_select);
        ToriRSChrome_SetText(ui->chrome, self->w_mv_play, "Play");
        ToriRSChrome_SetText(ui->chrome, self->w_mv_play_label, "");
        ToriRSChrome_SetHidden(ui->chrome, self->w_mv_play_label, 1);
    }
    self->has_model = 1;
    self->read_only = 0;
    self->sel_trigger = -1;
    self->sel_step = -1;
    self->sel_node = -1;
    self->sel_page = 0;
    self->chips_open = 0;
    self->drag_mode = BP_DRAG_NONE;
    Builder_IssuesClear(&self->issues);
    Builder_FileKey(self->subject, name, BUILDER_FILE_MODEL, key, sizeof(key));
    if( self->services.submit.current )
        text = self->services.submit.current(self->services.user_data, EDITOR_DOC_TEXT, key, &length);
    if( text && length > 0 )
    {
        if( !Builder_ModelParseIni(self->model, text, length, error, sizeof(error))
            || strcmp(self->model->name, name) != 0 || self->model->subject != self->subject )
        {
            char line[192];
            Builder_ModelInit(self->model, self->subject, name);
            self->read_only = 1;
            snprintf(line, sizeof(line), "%s: the model does not read (%s); read-only", name, error);
            bp_status(ui, self, line);
            bp_refresh(ui, self);
            return 1;
        }
    }
    else
    {
        char script_key[EDITOR_DOC_KEY_MAX];
        const char* script = NULL;
        size_t script_length = 0;

        Builder_ModelInit(self->model, self->subject, name);
        /* A script in the builder's directory without a model is a detached
         * one: hand-authored, and a regeneration would overwrite it. */
        Builder_FileKey(self->subject, name, BUILDER_FILE_SCRIPT, script_key, sizeof(script_key));
        if( self->services.submit.current )
            script = self->services.submit.current(self->services.user_data, EDITOR_DOC_TEXT,
                                                   script_key, &script_length);
        if( script && script_length > 0 && !Builder_HasHeader(script, script_length) )
        {
            self->read_only = 1;
            bp_status(ui, self, "this subject's script is hand-authored (detached); open the script");
            bp_refresh(ui, self);
            return 1;
        }
    }

    if( !self->existing )
    {
        self->existing = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->existing));
        assert(self->existing);
    }
    existing = self->existing;
    memset(existing, 0, sizeof(*existing));
    if( self->services.triggers
        && self->services.triggers(self->services.user_data, name, existing) )
    {
        if( Builder_FindConflicts(self->model, existing->rows, existing->count, &self->issues) > 0 )
            self->read_only = 1;
        /* Triggers the model does not have yet would also collide the moment
         * they are added: name the ops a hand-authored script already owns.
         * A test fixture's binding (selftest_*.rs2) is listed, not warned
         * about: it is nobody's content (D3). */
        for( int i = 0; i < existing->count; i++ )
        {
            char own[EDITOR_DOC_KEY_MAX];
            size_t own_length;
            size_t file_length = strlen(existing->files[i]);
            const char* comma = strchr(existing->names[i], ',');
            const char* base = strrchr(existing->files[i], '/');
            Builder_FilePath(self->subject, name, BUILDER_FILE_SCRIPT, own, sizeof(own));
            own_length = strlen(own);
            if( !comma || strcmp(comma + 1, name) != 0 )
                continue;
            if( file_length >= own_length
                && strcmp(existing->files[i] + file_length - own_length, own) == 0 )
                continue;
            if( strncmp(base ? base + 1 : existing->files[i], "selftest_", 9) == 0 )
                continue;
            Builder_IssueAdd(&self->issues, BUILDER_ISSUE_WARNING, existing->names[i],
                             "hand-authored in %s; the builder cannot add it", existing->files[i]);
        }
    }
    else
    {
        ToriMem_Free(ToriMem_Editor(), self->existing);
        self->existing = NULL;
        Builder_IssueAdd(&self->issues, BUILDER_ISSUE_WARNING, "subject",
                         "no catalog: duplicate triggers are not checked until Check");
    }

    {
        char line[192];
        int hand = 0;
        if( self->existing )
            for( int i = 0; i < self->existing->count; i++ )
                hand += bp_existing_kind(self, i) == 1;
        snprintf(line, sizeof(line), "%s%s%s%s: %d builder trigger(s), %d node(s), %d hand-authored%s", name,
                 self->record.display[0] ? " (" : "", self->record.display, self->record.display[0] ? ")" : "",
                 self->model->trigger_count, self->model->node_count, hand,
                 self->read_only ? " -- read-only (see issues)" : "");
        bp_status(ui, self, line);
    }
    ToriRSChrome_SetText(ui->chrome, self->w_subject, name);
    bp_refresh(ui, self);
    return 1;
}

/**
 * Check: the editor server's CHECK job (TORIRSMAPED_BUILD_CHECK, `make
 * maped-script-check`), so the window never waits on a stage copy or a
 * compiler. The job compiles the server's tree with every unsaved `rs2:`
 * document written over its stage, and the builder's generated files ARE
 * such documents (Builder_FileKey): each edit regenerates them, and Check
 * regenerates once more first, so what compiles is the model as shown.
 * bp_watch_check takes the job's lines as they arrive. Returns 1 sent, 0
 * refused (the reason in the status).
 */
static int
bp_check(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char why[160];
    char line[192];

    if( !self->has_model )
    {
        bp_status(ui, self, "open a subject first");
        return 0;
    }
    if( !self->read_only && self->services.submit.apply )
    {
        /* The issues start again from the model's own (a Check before this
         * one's sscompile rows go). */
        enum Builder_SyncResult result;
        Builder_IssuesClear(&self->issues);
        result = Builder_Regenerate(self->model, &self->services.submit, &self->issues);
        if( result == BUILDER_SYNC_REFUSED )
        {
            bp_status(ui, self, "Check refused: the files changed under the builder; re-open");
            return 0;
        }
        if( result == BUILDER_SYNC_APPLIED )
            bp_refresh(ui, self);
    }
    if( !Editor_BuildJobSend(ui, &self->check_job, TORIRSMAPED_BUILD_CHECK, NULL, why, sizeof(why)) )
    {
        snprintf(line, sizeof(line), "Check refused: %s", why);
        bp_status(ui, self, line);
        return 0;
    }
    memset(self->check, 0, sizeof(*self->check));
    self->check->status = BUILDER_SSC_NOT_RUN;
    bp_status(ui, self, "check: running on the editor server...");
    bp_show_issues(ui, self);
    return 1;
}

static void
bp_check_line(
    void* user,
    const char* text)
{
    struct builder_panel* self = user;
    (void)Builder_SscTakeLine(self->check, text);
}

/** Check's job, every frame, shown or not (a Check that ends while the
 *  builder is hidden still lands): the end files this subject's diagnostics
 *  as the builder's issues and says how it went. */
static void
bp_watch_check(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int errors = 0;
    int elsewhere = 0;
    char line[192];

    if( !self->check_job.active )
        return;
    if( !Editor_BuildJobPoll(ui, &self->check_job, bp_check_line, self) )
        return;
    if( !self->check->summary[0] && self->check->diag_count == 0 && self->check_job.status != EDITOR_HOST_OK )
    {
        /* The server never ran it: say why, not "it compiles". */
        snprintf(line, sizeof(line), "Check: %s", Editor_BuildJobWhy(self->check_job.status));
        bp_status(ui, self, line);
        Editor_UiStatus(ui, "%s", line);
        return;
    }
    if( self->check->status == BUILDER_SSC_NOT_RUN && self->check_job.status == EDITOR_HOST_OK )
        self->check->status = BUILDER_SSC_OK;
    else if( self->check->status == BUILDER_SSC_NOT_RUN && self->check->diag_count > 0 )
        self->check->status = BUILDER_SSC_FAILED;
    if( self->check->seconds <= 0.0 )
        self->check->seconds = self->check_job.seconds;
    if( !self->has_model )
        return;
    {
        char prefix[EDITOR_DOC_KEY_MAX];
        size_t prefix_length;
        snprintf(prefix, sizeof(prefix), "edited/%s/", self->model->name);
        prefix_length = strlen(prefix);
        for( int i = 0; i < self->check->diag_count; i++ )
        {
            const struct Builder_SscDiag* diag = &self->check->diags[i];
            char where[64];
            if( strncmp(diag->file, prefix, prefix_length) != 0 )
            {
                elsewhere += !diag->warning;
                continue;
            }
            snprintf(where, sizeof(where), "line %d", diag->line);
            Builder_IssueAdd(&self->issues, diag->warning ? BUILDER_ISSUE_WARNING : BUILDER_ISSUE_ERROR, where,
                             "sscompile: %s", diag->message);
            if( !diag->warning )
                errors++;
        }
    }
    if( self->check->status == BUILDER_SSC_OK )
        snprintf(line, sizeof(line), "compiles (%.1f s)", self->check->seconds);
    else if( self->check->status == BUILDER_SSC_FAILED )
        snprintf(line, sizeof(line), "sscompile failed: %d error(s) here%s", errors,
                 elsewhere ? " (more in Problems)" : "");
    else
        snprintf(line, sizeof(line), "sscompile could not run (%s)",
                 self->check->status == BUILDER_SSC_UNSUPPORTED ? "not on this platform" : "no binary or stage");
    bp_status(ui, self, line);
    bp_show_issues(ui, self);
}

static int
bp_save(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int saved = 0;
    int failed = 0;
    char line[160];

    if( !self->has_model || !self->services.save )
    {
        bp_status(ui, self, self->has_model ? "no session to save through" : "open a subject first");
        return 0;
    }
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        int result;
        Builder_FileKey(self->subject, self->model->name, (enum Builder_File)f, key, sizeof(key));
        result = self->services.save(self->services.user_data, EDITOR_DOC_TEXT, key);
        if( result > 0 )
            saved++;
        else if( result < 0 )
            failed++;
    }
    snprintf(line, sizeof(line), "saved %d file(s)%s", saved, failed ? ", some refused" : "");
    bp_status(ui, self, line);
    return failed == 0;
}

static int
bp_detach(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    if( !self->has_model || self->read_only || !self->services.submit.apply )
    {
        bp_status(ui, self, "nothing to detach");
        return 0;
    }
    if( Builder_Detach(self->subject, self->model->name, &self->services.submit) == BUILDER_SYNC_REFUSED )
    {
        bp_status(ui, self, "the authority refused the detach; re-open");
        return 0;
    }
    self->read_only = 1;
    bp_status(ui, self, "detached: the scripts are hand-authored from here on");
    bp_refresh(ui, self);
    return 1;
}

static struct Builder_Trigger*
bp_selected_trigger(struct builder_panel* self)
{
    if( !self->has_model || self->sel_trigger < 0 || self->sel_trigger >= self->model->trigger_count )
        return NULL;
    return &self->model->triggers[self->sel_trigger];
}

/**
 * Rename op N (design §11 "the form"): a CFG_FIELD on the cache record's
 * `opN=`, which changes the menu the CLIENT shows -- a cache pack, not a
 * script, delivers it, and the status says so. The trigger's label follows.
 */
static int
bp_rename_op(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Trigger* trigger)
{
    char doc_key[EDITOR_DOC_KEY_MAX];
    char field[8];
    const char* text;
    const char* want;
    size_t length = 0;
    struct Editor_CfgBlock block;
    struct Editor_Cmd command;
    int line;
    int ok;

    if( !trigger || trigger->op == 0 || trigger->op == BUILDER_OP_AI )
    {
        bp_status(ui, self, "select an op trigger (op1..op5 / ap1..ap5) first");
        return 0;
    }
    want = ToriRSChrome_Text(ui->chrome, self->w_menu_text);
    if( !want || strchr(want, '\n') || strlen(want) >= EDITOR_CMD_VALUE_MAX )
        return 0;
    snprintf(doc_key, sizeof(doc_key), "%s:%s", self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc",
             self->model->name);
    snprintf(field, sizeof(field), "op%d", trigger->op);
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, doc_key, &length);
    if( !text || length == 0 )
    {
        bp_status(ui, self, "the cache record is not in configs/all.<loc|npc>");
        return 0;
    }
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, text, length);
    line = Editor_CfgBlockFieldFind(&block, field, 0);
    Editor_CmdInit(&command, EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
    command.has_before = 1;
    command.has_after = 1;
    snprintf(command.u.cfg_field.key, sizeof(command.u.cfg_field.key), "%s", field);
    if( line >= 0 )
    {
        size_t value_length = 0;
        const char* value = Editor_CfgBlockValue(&block, line, &value_length);
        if( value_length >= sizeof(command.u.cfg_field.before) )
            value_length = sizeof(command.u.cfg_field.before) - 1;
        memcpy(command.u.cfg_field.before, value, value_length);
        command.u.cfg_field.before[value_length] = '\0';
    }
    Editor_CfgBlockFree(&block);
    snprintf(command.u.cfg_field.after, sizeof(command.u.cfg_field.after), "%s", want);
    if( strcmp(command.u.cfg_field.before, command.u.cfg_field.after) == 0 )
        return 1;
    ok = self->services.submit.apply(self->services.user_data, &command);
    if( !ok )
    {
        bp_status(ui, self, "the authority refused the rename; re-open the subject");
        return 0;
    }
    copy_text(trigger->label, sizeof(trigger->label), want);
    snprintf(self->record.ops[trigger->op - 1], sizeof(self->record.ops[0]), "%s", want);
    bp_commit(ui, self);
    bp_status(ui, self, "renamed: the client's menu changes with the next client cache pack");
    return 1;
}

/* ---- the graph's edits ------------------------------------------------------ */

/** A dialogue edit can be made: a subject is open and it is not read-only.
 *  Otherwise the refusal is said, naming `what`, and 0 returned. */
static int
bp_graph_editable(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* what)
{
    if( self->w_graph < 0 )
    {
        Editor_UiStatus(ui, "%s refused: the loc builder has no dialogue graph", what);
        return 0;
    }
    if( !self->has_model )
    {
        Editor_UiStatus(ui, "%s refused: open an npc in the NPC builder first", what);
        return 0;
    }
    if( self->read_only )
    {
        Editor_UiStatus(ui, "%s refused: %s is read-only (see its issues)", what, self->model->name);
        return 0;
    }
    return 1;
}

/** "n4, n5 and n7" into `out`: the ids `mask` marks, at most a line. */
static void
bp_id_list(
    const struct Builder_Model* model,
    const unsigned char* mask,
    char* out,
    size_t out_size)
{
    int count = 0;
    int total = 0;
    size_t length = 0;

    out[0] = '\0';
    for( int i = 0; i < model->node_count; i++ )
        total += mask[i] != 0;
    for( int i = 0; i < model->node_count && length + 1 < out_size; i++ )
    {
        if( !mask[i] )
            continue;
        count++;
        length += (size_t)snprintf(out + length, out_size - length, "%s%s",
                                   count == 1         ? ""
                                   : count == total ? " and "
                                                    : ", ",
                                   model->nodes[i].id);
    }
}

/** Delete the selected node; the sentence names what lost its last way
 *  in from the start. */
static int
bp_delete_node(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    unsigned char before[BUILDER_NODES_MAX];
    unsigned char after[BUILDER_NODES_MAX];
    unsigned char fresh[BUILDER_NODES_MAX];
    char before_ids[BUILDER_NODES_MAX][BUILDER_NODE_ID_MAX];
    int before_count;
    char id[BUILDER_NODE_ID_MAX];
    char kind[64];
    char list[160];
    int orphaned = 0;
    const struct Builder_Node* node;

    if( !bp_graph_editable(ui, self, "Delete node") )
        return 0;
    node = bp_selected_node(self);
    if( !node )
    {
        Editor_UiStatus(ui, "Delete node refused: no node is selected (click one in the graph or the outline)");
        return 0;
    }
    copy_text(id, sizeof(id), node->id);
    copy_text(kind, sizeof(kind), Builder_NodeKindLabel(node->kind));
    (void)Builder_ModelOrphans(self->model, before);
    before_count = self->model->node_count;
    for( int i = 0; i < before_count; i++ )
        copy_text(before_ids[i], sizeof(before_ids[0]), before[i] ? self->model->nodes[i].id : "");
    Builder_ModelNodeRemove(self->model, id);
    (void)Builder_ModelOrphans(self->model, after);
    /* Newly orphaned: an orphan now that was not one before. */
    memset(fresh, 0, sizeof(fresh));
    for( int i = 0; i < self->model->node_count; i++ )
    {
        int was = 0;
        if( !after[i] )
            continue;
        for( int j = 0; j < before_count; j++ )
            was |= before_ids[j][0] && strcmp(before_ids[j], self->model->nodes[i].id) == 0;
        if( was )
            continue;
        fresh[i] = 1;
        orphaned++;
    }
    bp_id_list(self->model, fresh, list, sizeof(list));
    self->sel_node = -1;
    bp_commit(ui, self);
    if( orphaned )
        Editor_UiStatus(ui, "Deleted %s (%s): %s %s no way in from the start now (orphaned; Undo brings it back)",
                        id, kind, list, orphaned == 1 ? "has" : "have");
    else
        Editor_UiStatus(ui, "Deleted %s (%s); nothing was orphaned", id, kind);
    bp_select_node(ui, self, -1);
    return 1;
}

static int
bp_auto_layout(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int moved;
    int columns = 0;

    if( !bp_graph_editable(ui, self, "Auto-layout") )
        return 0;
    if( self->model->node_count == 0 )
    {
        Editor_UiStatus(ui, "Auto-layout refused: the dialogue has no nodes");
        return 0;
    }
    moved = Builder_ModelAutoLayout(self->model);
    for( int i = 0; i < self->model->node_count; i++ )
    {
        int const column = (self->model->nodes[i].x - BUILDER_LAYOUT_ORIGIN) / BUILDER_LAYOUT_COL_W + 1;
        if( column > columns )
            columns = column;
    }
    if( moved == 0 )
    {
        Editor_UiStatus(ui, "Auto-layout: %s is laid out already (%d column(s))", self->model->name, columns);
        bp_redraw_graph(ui, self);
        return 1;
    }
    bp_commit(ui, self);
    Editor_UiStatus(ui, "Auto-layout: %d node(s) moved into %d column(s) by depth (saved as pos=)", moved, columns);
    return 1;
}

/** A new node of `kind` at (x, y); linked from `from`'s `port` when `from`
 *  is a node, else after the selected node as Add node does. */
static int
bp_new_node(
    struct EditorUi* ui,
    struct builder_panel* self,
    enum Builder_NodeKind kind,
    const char* from,
    int port,
    int x,
    int y)
{
    struct Builder_Node* node;
    char id[BUILDER_NODE_ID_MAX];

    if( !bp_graph_editable(ui, self, "New node") )
        return 0;
    node = Builder_ModelNodeAdd(self->model, kind);
    if( !node )
    {
        Editor_UiStatus(ui, "New node refused: the dialogue holds %d nodes, the most the builder keeps",
                        BUILDER_NODES_MAX);
        return 0;
    }
    node->x = x < 0 ? 0 : x;
    node->y = y < 0 ? 0 : y;
    copy_text(id, sizeof(id), node->id);
    if( from && from[0] )
    {
        int const linked = Builder_ModelLink(self->model, from, port, id);
        assert(linked);
        (void)linked;
        Editor_UiStatus(ui, "New %s %s, linked from %s port %d", Builder_NodeKindLabel(kind), id, from, port + 1);
    }
    else
    {
        const struct Builder_Node* selected = bp_selected_node(self);
        if( selected && selected->kind != BUILDER_NODE_CHOICE && selected->kind != BUILDER_NODE_END
            && selected->next[0] == '\0' && strcmp(selected->id, id) != 0 )
        {
            char after[BUILDER_NODE_ID_MAX];
            copy_text(after, sizeof(after), selected->id);
            (void)Builder_ModelLink(self->model, after, Builder_NodePortCount(selected) - 1, id);
            Editor_UiStatus(ui, "New %s %s, after %s", Builder_NodeKindLabel(kind), id, after);
        }
        else
            Editor_UiStatus(ui, "New %s %s (not linked: drag a port to it)", Builder_NodeKindLabel(kind), id);
    }
    self->chips_open = 0;
    self->sel_node = Builder_ModelNodeIndex(self->model, id);
    bp_load_node(ui, self);
    bp_commit(ui, self);
    return 1;
}

/** The free spot for a node added from the keyboard: under the selected
 *  node, else the next cell of a four-wide grid. */
static void
bp_new_node_spot(
    const struct builder_panel* self,
    int* out_x,
    int* out_y)
{
    const struct Builder_Node* selected = bp_selected_node(self);
    int const k = self->model->node_count;

    if( selected )
    {
        *out_x = selected->x;
        *out_y = selected->y + BUILDER_LAYOUT_ROW_H;
        return;
    }
    *out_x = 10 + k % 4 * (BP_NODE_W + 16);
    *out_y = 10 + k / 4 * (BP_NODE_H + 30);
}

/** The node a well-local point is over (its body or a port), or -1. */
static int
bp_node_at(
    struct EditorUi* ui,
    struct builder_panel* self,
    int x,
    int y)
{
    int const key = ToriRSChrome_GraphKeyAt(ui->chrome, self->w_graph, x, y);

    if( key >= 1 && key <= self->model->node_count )
        return key - 1;
    if( key >= BP_KEY_PORT && key < BP_KEY_PORT + BUILDER_NODES_MAX * BP_PORTS_STRIDE )
        return (key - BP_KEY_PORT) / BP_PORTS_STRIDE;
    return -1;
}

/** The port drag ended at (x, y): on a node, link; on empty space, open
 *  the chip row there. */
static void
bp_port_drop(
    struct EditorUi* ui,
    struct builder_panel* self,
    int x,
    int y)
{
    int const target = bp_node_at(ui, self, x, y);
    const struct Builder_Node* from = &self->model->nodes[self->drag_port_node];
    char from_id[BUILDER_NODE_ID_MAX];

    copy_text(from_id, sizeof(from_id), from->id);
    if( target >= 0 )
    {
        char to_id[BUILDER_NODE_ID_MAX];
        copy_text(to_id, sizeof(to_id), self->model->nodes[target].id);
        if( strcmp(Builder_NodePortNextConst(from, self->drag_port), to_id) == 0 )
        {
            Editor_UiStatus(ui, "%s port %d already leads to %s", from_id, self->drag_port + 1, to_id);
            bp_redraw_graph(ui, self);
            return;
        }
        (void)Builder_ModelLink(self->model, from_id, self->drag_port, to_id);
        bp_commit(ui, self);
        Editor_UiStatus(ui, "Linked %s port %d to %s", from_id, self->drag_port + 1, to_id);
        return;
    }
    self->chips_open = 1;
    copy_text(self->chip_from, sizeof(self->chip_from), from_id);
    self->chip_port = self->drag_port;
    self->chip_x = x;
    self->chip_y = y;
    Editor_UiStatus(ui, "New node from %s port %d: pick its kind (Escape cancels)", from_id, self->drag_port + 1);
    bp_redraw_graph(ui, self);
}

/** The dialogue graph's gestures (GRAPH events of the well). */
static int
bp_graph_event(
    struct EditorUi* ui,
    struct builder_panel* self,
    const struct ToriRSChromeEvent* event)
{
    struct Builder_Model* model = self->model;

    switch( event->kind )
    {
    case TORIRS_CHROME_EVENT_GRAPH_PRESS:
        self->press_x = self->drag_x = event->x;
        self->press_y = self->drag_y = event->y;
        self->drag_moved = 0;
        self->drag_mode = BP_DRAG_NONE;
        if( self->chips_open )
        {
            /* A chip makes the node; anywhere else closes the row. */
            self->chips_open = 0;
            if( event->key >= BP_KEY_CHIP && event->key < BP_KEY_CHIP + BUILDER_NODE_KIND_COUNT
                && Builder_ModelNode(model, self->chip_from) )
            {
                (void)bp_new_node(ui, self, (enum Builder_NodeKind)(event->key - BP_KEY_CHIP), self->chip_from,
                                  self->chip_port, self->chip_x, self->chip_y);
                return 1;
            }
            bp_redraw_graph(ui, self);
        }
        if( event->key >= BP_KEY_PORT && event->key < BP_KEY_PORT + model->node_count * BP_PORTS_STRIDE )
        {
            int const node = (event->key - BP_KEY_PORT) / BP_PORTS_STRIDE;
            int const port = (event->key - BP_KEY_PORT) % BP_PORTS_STRIDE;
            if( port < Builder_NodePortCount(&model->nodes[node]) && !self->read_only )
            {
                self->drag_mode = BP_DRAG_PORT;
                self->drag_port_node = node;
                self->drag_port = port;
            }
            bp_select_node(ui, self, node);
            return 1;
        }
        if( event->key > 0 && event->key <= model->node_count )
        {
            int const node = event->key - 1;
            if( !self->read_only )
            {
                /* Shift+drag takes the node's subtree along. */
                if( ui->shift_held )
                {
                    (void)Builder_ModelReach(model, model->nodes[node].id, self->drag_mask);
                    self->drag_mode = BP_DRAG_SUBTREE;
                }
                else
                {
                    memset(self->drag_mask, 0, sizeof(self->drag_mask));
                    self->drag_mask[node] = 1;
                    self->drag_mode = BP_DRAG_NODE;
                }
                for( int i = 0; i < model->node_count; i++ )
                {
                    self->drag_orig_x[i] = model->nodes[i].x;
                    self->drag_orig_y[i] = model->nodes[i].y;
                }
            }
            bp_select_node(ui, self, node);
            return 1;
        }
        bp_select_node(ui, self, -1);
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_DRAG:
        if( self->drag_mode == BP_DRAG_NONE )
            return 1;
        if( abs(event->x - self->press_x) > BP_DRAG_SLOP || abs(event->y - self->press_y) > BP_DRAG_SLOP )
            self->drag_moved = 1;
        self->drag_x = event->x;
        self->drag_y = event->y;
        if( self->drag_mode == BP_DRAG_NODE || self->drag_mode == BP_DRAG_SUBTREE )
        {
            int dx = event->x - self->press_x;
            int dy = event->y - self->press_y;
            /* The group stops at the well's top-left edge as one piece. */
            for( int i = 0; i < model->node_count; i++ )
            {
                if( !self->drag_mask[i] )
                    continue;
                if( self->drag_orig_x[i] + dx < 0 )
                    dx = -self->drag_orig_x[i];
                if( self->drag_orig_y[i] + dy < 0 )
                    dy = -self->drag_orig_y[i];
            }
            for( int i = 0; i < model->node_count; i++ )
            {
                if( !self->drag_mask[i] )
                    continue;
                model->nodes[i].x = self->drag_orig_x[i] + dx;
                model->nodes[i].y = self->drag_orig_y[i] + dy;
            }
        }
        bp_redraw_graph(ui, self);
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_RELEASE:
    {
        int const mode = self->drag_mode;
        self->drag_mode = BP_DRAG_NONE;
        if( mode == BP_DRAG_NONE )
            return 1;
        /* The release carries where the pointer came up (both the native
         * well and the DOM's send it). */
        self->drag_x = event->x;
        self->drag_y = event->y;
        if( abs(event->x - self->press_x) > BP_DRAG_SLOP || abs(event->y - self->press_y) > BP_DRAG_SLOP )
            self->drag_moved = 1;
        if( !self->drag_moved )
        {
            bp_redraw_graph(ui, self);
            return 1;
        }
        if( mode == BP_DRAG_PORT )
        {
            bp_port_drop(ui, self, self->drag_x, self->drag_y);
            return 1;
        }
        /* The layout is part of the model: one regeneration per drag. */
        bp_commit(ui, self);
        if( mode == BP_DRAG_SUBTREE )
        {
            int count = 0;
            for( int i = 0; i < model->node_count; i++ )
                count += self->drag_mask[i] != 0;
            Editor_UiStatus(ui, "Moved %d node(s): the subtree under %s", count,
                            self->sel_node >= 0 ? model->nodes[self->sel_node].id : "?");
        }
        return 1;
    }
    case TORIRS_CHROME_EVENT_GRAPH_MENU:
        if( event->key == 0 && !self->read_only )
        {
            int const kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_node_kind);
            (void)bp_new_node(ui, self,
                              kind >= 0 && kind < BUILDER_NODE_KIND_COUNT ? (enum Builder_NodeKind)kind
                                                                           : BUILDER_NODE_NPC,
                              NULL, 0, event->x, event->y);
        }
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_KEY:
    {
        struct Editor_Chord chord;

        memset(&chord, 0, sizeof(chord));
        if( event->value == -TORIRS_CHROME_KEY_ESCAPE )
        {
            if( self->drag_mode == BP_DRAG_NODE || self->drag_mode == BP_DRAG_SUBTREE )
            {
                /* The drag wrote the positions as it went; the authority has
                 * none of them, so they go back rather than ride the next
                 * commit. */
                int count = 0;
                for( int i = 0; i < model->node_count; i++ )
                {
                    if( !self->drag_mask[i] )
                        continue;
                    model->nodes[i].x = self->drag_orig_x[i];
                    model->nodes[i].y = self->drag_orig_y[i];
                    count++;
                }
                self->drag_mode = BP_DRAG_NONE;
                Editor_UiStatus(ui, "Move cancelled: %d node(s) back where they were", count);
                bp_redraw_graph(ui, self);
            }
            else if( self->chips_open || self->drag_mode != BP_DRAG_NONE )
            {
                self->chips_open = 0;
                self->drag_mode = BP_DRAG_NONE;
                Editor_UiStatus(ui, "New node cancelled");
                bp_redraw_graph(ui, self);
            }
            return 1;
        }
        /* Delete (and its rebinds) from the well itself: the chrome keeps
         * the editing keys of a focused well, so the App never sees it. */
        if( event->value == -TORIRS_CHROME_KEY_DELETE )
            chord.key = TORIRSK_DELETE;
        else if( event->value == -TORIRS_CHROME_KEY_BACKSPACE )
            chord.key = TORIRSK_BACKSPACE;
        chord.mods = (uint8_t)((event->col & (int)(EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT | EDITOR_HK_MOD_ALT))
                               | (ui->shift_held ? (int)EDITOR_HK_MOD_SHIFT : 0));
        /* edit.delete's own chords (its contexts are the World's selections,
         * L and S, which a focused well is in neither of): a rebind of
         * Delete moves the graph's with it. */
        if( chord.key == TORIRSK_UNKNOWN )
            return 1;
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            struct Editor_Chord const bound = ui->hotkeys.bind[EDITOR_HK_EDIT_DELETE].chord[c];
            if( bound.key == chord.key && bound.mods == chord.mods )
            {
                (void)bp_delete_node(ui, self);
                break;
            }
        }
        return 1;
    }
    default:
        return 1;
    }
}

/** The text's pages: a click on a page shows it in the preview. */
static int
bp_pages_event(
    struct EditorUi* ui,
    struct builder_panel* self,
    const struct ToriRSChromeEvent* event)
{
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS || event->key <= 0 )
        return 1;
    self->sel_page = event->key - 1;
    if( self->sel_page >= self->layout.page_count )
        self->sel_page = self->layout.page_count - 1;
    bp_redraw_pages(ui, self);
    bp_redraw_preview(ui, self);
    return 1;
}

static int
bp_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct builder_panel* self = opaque;
    struct ToriRSChrome* chrome = ui->chrome;
    struct Builder_Trigger* trigger;

    if( widget == self->w_open || widget == self->w_subject )
    {
        bp_open(ui, self, ToriRSChrome_Text(chrome, self->w_subject));
        return 1;
    }
    if( widget == self->w_fromsel )
    {
        char name[BUILDER_NAME_MAX];
        if( !self->services.selection )
        {
            bp_status(ui, self, "no selection source here");
            return 1;
        }
        if( !self->services.selection(self->services.user_data, self->subject, name, sizeof(name)) )
        {
            bp_status(ui, self, self->subject == BUILDER_SUBJECT_NPC
                                    ? "select a spawn in the Spawns panel, or pick an npc in the Catalog, first"
                                    : "select a loc in the world first");
            return 1;
        }
        if( name[0] == '#' )
        {
            struct Builder_RecordInfo info;
            memset(&info, 0, sizeof(info));
            if( self->services.record
                && self->services.record(self->services.user_data, self->subject, "", atoi(name + 1), &info) )
                bp_open(ui, self, info.name);
            else
                bp_status(ui, self, "the selection is not in the catalog");
            return 1;
        }
        bp_open(ui, self, name);
        return 1;
    }
    if( self->w_tabs >= 0 && widget == self->w_tabs )
        return 1;
    if( self->w_mv_map >= 0 && bp_mv_activate(ui, self, widget) )
        return 1;
    if( widget == self->w_existing_list )
        return 1;
    if( widget == self->w_existing_open )
    {
        int const row = ToriRSChrome_VListSelected(chrome, self->w_existing_list);
        if( !self->existing || row < 0 || row >= self->existing->count )
            bp_status(ui, self, "pick a trigger in the tree's list first");
        else if( !self->services.open_file )
            bp_status(ui, self, "no editor to open scripts in here");
        else
        {
            const char* file = self->existing->files[row];
            /* The catalog's file is under the content root; the editor
             * opens it relative to server/scripts. */
            const char* rel = strncmp(file, "server/scripts/", 15) == 0 ? file + 15 : file;
            (void)self->services.open_file(self->services.user_data, rel, 1);
        }
        return 1;
    }
    if( widget == self->w_regen )
    {
        if( self->has_model )
            bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_check )
    {
        bp_check(ui, self);
        return 1;
    }
    if( widget == self->w_save )
    {
        bp_save(ui, self);
        return 1;
    }
    if( widget == self->w_detach )
    {
        bp_detach(ui, self);
        return 1;
    }
    if( widget == self->w_step_kind )
    {
        int kind = ToriRSChrome_DropdownSelected(chrome, self->w_step_kind);
        if( kind >= 0 && kind < BUILDER_STEP_KIND_COUNT )
            bp_show_step_fields(ui, self, (enum Builder_StepKind)kind);
        return 1;
    }
    for( int i = 0; i < self->outline_count; i++ )
    {
        if( widget != self->outline_rows[i] )
            continue;
        if( self->outline_node[i] >= 0 )
        {
            /* The tree and the graph are one model: the row selects the
             * node, and the outline is rebuilt with its row selected. */
            bp_select_node(ui, self, self->outline_node[i]);
            return 1;
        }
        else if( self->outline_trigger[i] >= 0 )
        {
            self->sel_trigger = self->outline_trigger[i];
            self->sel_step = self->outline_step[i];
            bp_load_step(ui, self);
        }
        ToriRSChrome_TreeSelect(chrome, widget);
        return 1;
    }
    if( widget == self->w_node_kind || widget == self->w_trigger_kind || widget == self->w_door
        || widget == self->w_ladder )
        return 1;
    if( self->w_lint_list >= 0 && widget == self->w_lint_list )
    {
        int const row = ToriRSChrome_VListSelected(chrome, self->w_lint_list);
        if( row >= 0 && row < self->lint_shown_count )
            bp_open_lint(ui, self, self->lint_shown[row]);
        return 1;
    }
    if( self->w_lint_find >= 0 && widget == self->w_lint_find )
    {
        copy_text(self->lint_find, sizeof(self->lint_find), ToriRSChrome_Text(chrome, self->w_lint_find));
        bp_fill_lint(ui, self);
        /* Enter in the box opens the first row it leaves. */
        if( self->lint_shown_count > 0 )
            bp_open_lint(ui, self, self->lint_shown[0]);
        return 1;
    }
    if( self->w_layout >= 0 && widget == self->w_layout )
    {
        (void)bp_auto_layout(ui, self);
        return 1;
    }

    /* Everything below edits the model. */
    if( !self->has_model )
    {
        int ours = widget == self->w_trigger_add || widget == self->w_trigger_remove
                   || widget == self->w_step_add || widget == self->w_step_apply
                   || widget == self->w_step_remove || widget == self->w_step_up
                   || widget == self->w_step_down || widget == self->w_server_apply
                   || widget == self->w_trigger_dialogue || widget == self->w_node_add
                   || widget == self->w_node_remove || widget == self->w_node_apply
                   || widget == self->w_node_start || widget == self->w_rename_op;
        if( ours )
            bp_status(ui, self, "open a subject first");
        return ours;
    }

    if( widget == self->w_trigger_add )
    {
        int option = ToriRSChrome_DropdownSelected(chrome, self->w_trigger_kind);
        int op = option >= 0 && option < 10 ? option % 5 + 1 : 0;
        int approach = option >= 5 && option != 10;
        const char* use = ToriRSChrome_Text(chrome, self->w_use_obj);
        struct Builder_Trigger* added;

        if( option < 0 )
            return 1;
        if( op == 0 && (!use || !use[0]) )
        {
            bp_status(ui, self, "a use-on trigger needs the held obj (or *)");
            return 1;
        }
        added = Builder_ModelTriggerAdd(self->model, op, approach, op == 0 ? use : "");
        if( !added )
        {
            bp_status(ui, self, "the subject has as many triggers as the builder holds");
            return 1;
        }
        if( op > 0 && added->label[0] == '\0' )
            copy_text(added->label, sizeof(added->label), self->record.ops[op - 1]);
        self->sel_trigger = (int)(added - self->model->triggers);
        self->sel_step = -1;
        bp_commit(ui, self);
        return 1;
    }
    trigger = bp_selected_trigger(self);
    if( widget == self->w_trigger_remove )
    {
        if( trigger )
        {
            Builder_ModelTriggerRemove(self->model, self->sel_trigger);
            self->sel_trigger = -1;
            self->sel_step = -1;
            bp_commit(ui, self);
        }
        return 1;
    }
    if( widget == self->w_rename_op )
    {
        bp_rename_op(ui, self, trigger);
        return 1;
    }
    if( widget == self->w_trigger_dialogue )
    {
        if( trigger )
        {
            trigger->dialogue = ToriRSChrome_Checked(chrome, self->w_trigger_dialogue);
            if( trigger->dialogue && self->model->node_count == 0 )
            {
                struct Builder_Node* node = Builder_ModelNodeAdd(self->model, BUILDER_NODE_NPC);
                if( node )
                    copy_text(node->text, sizeof(node->text), "Hello, {player}.");
            }
            bp_commit(ui, self);
        }
        return 1;
    }
    if( widget == self->w_step_add || widget == self->w_step_apply )
    {
        struct Builder_Step step;
        if( !trigger )
        {
            bp_status(ui, self, "select a trigger first");
            return 1;
        }
        bp_read_step(ui, self, &step);
        if( widget == self->w_step_apply && self->sel_step >= 0 && self->sel_step < trigger->step_count )
            trigger->steps[self->sel_step] = step;
        else
        {
            int index = Builder_TriggerStepAdd(trigger, &step);
            if( index < 0 )
            {
                bp_status(ui, self, "the trigger has as many steps as the builder holds");
                return 1;
            }
            self->sel_step = index;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_step_remove || widget == self->w_step_up || widget == self->w_step_down )
    {
        if( !trigger || self->sel_step < 0 || self->sel_step >= trigger->step_count )
            return 1;
        if( widget == self->w_step_remove )
        {
            Builder_TriggerStepRemove(trigger, self->sel_step);
            self->sel_step = -1;
        }
        else
        {
            int to = self->sel_step + (widget == self->w_step_up ? -1 : 1);
            if( to < 0 || to >= trigger->step_count )
                return 1;
            Builder_TriggerStepMove(trigger, self->sel_step, to);
            self->sel_step = to;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_server_apply )
    {
        struct Builder_Server* server = &self->model->server;
        int door = ToriRSChrome_DropdownSelected(chrome, self->w_door);
        int ladder = ToriRSChrome_DropdownSelected(chrome, self->w_ladder);

        copy_text(server->category, sizeof(server->category),
                  ToriRSChrome_Text(chrome, self->w_category));
        server->door = door == 1 ? BUILDER_DOOR_CLOSED : door == 2 ? BUILDER_DOOR_OPENED
                                                                   : BUILDER_DOOR_NONE;
        copy_text(server->door_other, sizeof(server->door_other),
                  ToriRSChrome_Text(chrome, self->w_door_other));
        server->ladder = BUILDER_LADDER_NONE;
        server->ladder_animated = 0;
        if( ladder >= 1 && ladder <= 6 )
        {
            server->ladder = (enum Builder_LadderDir)((ladder - 1) % 3 + 1);
            server->ladder_animated = ladder >= 4;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_node_add )
    {
        int const kind = ToriRSChrome_DropdownSelected(chrome, self->w_node_kind);
        int x;
        int y;
        bp_new_node_spot(self, &x, &y);
        (void)bp_new_node(ui, self,
                          kind >= 0 && kind < BUILDER_NODE_KIND_COUNT ? (enum Builder_NodeKind)kind : BUILDER_NODE_NPC,
                          NULL, 0, x, y);
        return 1;
    }
    if( widget == self->w_node_remove )
    {
        (void)bp_delete_node(ui, self);
        return 1;
    }
    if( widget == self->w_node_apply || widget == self->w_node_start )
    {
        struct Builder_Node* node;
        if( self->sel_node < 0 || self->sel_node >= self->model->node_count )
            return 1;
        node = &self->model->nodes[self->sel_node];
        if( widget == self->w_node_start )
            copy_text(self->model->start, sizeof(self->model->start), node->id);
        else
        {
            struct Builder_Node edited = *node;
            if( !bp_read_node(ui, self, &edited) )
                return 1;
            *node = edited;
        }
        bp_commit(ui, self);
        return 1;
    }
    return 0;
}

static int
bp_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct builder_panel* self = opaque;

    if( self->w_mv_map >= 0 && event->widget == self->w_mv_map )
        return bp_mv_event(ui, self, event);
    if( self->w_graph < 0 )
        return 0;
    if( event->widget != self->w_graph && event->widget != self->w_pages && event->widget != self->w_preview )
        return 0;
    if( !self->has_model )
        return 1;
    if( event->widget == self->w_pages )
        return bp_pages_event(ui, self, event);
    if( event->widget == self->w_preview )
        return 1;
    return bp_graph_event(ui, self, event);
}

/** With nothing open, open the npc the selection names (the Spawns panel's
 *  row, else the Catalog's npc pick), quietly: no selection is not an error
 *  here. */
static void
bp_open_selection(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char name[BUILDER_NAME_MAX];

    if( self->has_model || !self->services.selection
        || !self->services.selection(self->services.user_data, self->subject, name, sizeof(name)) )
        return;
    /* "#<id>" (a pick with no name) resolves through the catalog. */
    if( name[0] == '#' )
    {
        struct Builder_RecordInfo info;
        memset(&info, 0, sizeof(info));
        if( !self->services.record
            || !self->services.record(self->services.user_data, self->subject, "", atoi(name + 1), &info) )
            return;
        copy_text(name, sizeof(name), info.name);
    }
    (void)bp_open(ui, self, name);
}

static void
bp_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct builder_panel* self = opaque;
    int visible = ui->visible && ui->chrome->panels[self->p_main].visible;

    bp_watch_check(ui, self);
    /* TORIRS_EDITOR_NPC=<name>[,<tab>[,photo]] -- a headless photograph has
     * no pointer: open that npc in the NPC editor at the first frame with a
     * session, show the panel, pick the tab (0 Movement .. 3 Dialogue) and,
     * with `photo`, lay the panel over the whole right column so the tab
     * is in the picture. Read once per process. */
    if( self->subject == BUILDER_SUBJECT_NPC && ui->editor )
    {
        static int resolved;
        if( !resolved )
        {
            const char* value = getenv("TORIRS_EDITOR_NPC");
            resolved = 1;
            if( value && value[0] )
            {
                char name[BUILDER_NAME_MAX];
                const char* comma = strchr(value, ',');
                size_t const n = comma ? (size_t)(comma - value) : strlen(value);
                snprintf(name, sizeof(name), "%.*s", (int)(n < sizeof(name) - 1 ? n : sizeof(name) - 1), value);
                ToriRSChrome_SetText(ui->chrome, self->w_subject, name);
                if( bp_open(ui, self, name) )
                {
                    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_main, 1);
                    if( comma && self->w_tabs >= 0 )
                        ToriRSChrome_PanelSetActiveTab(ui->chrome, self->p_main, atoi(comma + 1));
                    if( comma && strstr(comma, "photo") && self->services.photo_place )
                        self->photo_countdown = 120;
                    Editor_UiLog(ui, "editor: TORIRS_EDITOR_NPC opened %s", name);
                }
            }
        }
        if( self->photo_countdown > 0 && --self->photo_countdown == 0 && self->services.photo_place )
            self->services.photo_place(self->services.user_data, self->p_main);
        visible = ui->visible && ui->chrome->panels[self->p_main].visible;
    }
    if( visible && ToriRSChrome_VListNeedsRows(ui->chrome, self->w_existing_list) )
        bp_show_existing(ui, self);
    if( visible && ToriRSChrome_VListNeedsRows(ui->chrome, self->w_issue_list) )
        bp_show_issues(ui, self);
    /* The NPC editor shown with nothing open: open what the user is pointing
     * at. View > NPC editor used to open on "Open a subject." with an npc
     * selected. */
    if( visible && !self->mv_was_visible && self->w_mv_map >= 0 )
        bp_open_selection(ui, self);
    self->mv_was_visible = visible;
    if( visible && self->w_mv_map >= 0 )
        bp_mv_tick(ui, self);
    if( self->w_graph < 0 )
        return;
    /* A rebind relabels the graph's buttons. */
    if( self->hotkeys_serial != ui->hotkeys.serial )
    {
        self->hotkeys_serial = ui->hotkeys.serial;
        edp_button_chord(ui, self->w_layout, EDITOR_HK_GRAPH_AUTO_LAYOUT);
        edp_button_chord(ui, self->w_node_add, EDITOR_HK_GRAPH_NEW_NODE);
        edp_button_chord(ui, self->w_node_remove, EDITOR_HK_EDIT_DELETE);
    }
    if( !visible )
        return;
    /* The pages and the preview follow the Text field as it is typed. */
    if( self->layout_node != self->sel_node || strcmp(self->layout_text, bp_layout_source(ui, self)) != 0 )
    {
        bp_layout_text(ui, self);
        bp_redraw_pages(ui, self);
        bp_redraw_preview(ui, self);
    }
    /* The find box filters as it is typed; the list fills what scrolled in. */
    if( strcmp(self->lint_find, ToriRSChrome_Text(ui->chrome, self->w_lint_find)) != 0 )
    {
        copy_text(self->lint_find, sizeof(self->lint_find), ToriRSChrome_Text(ui->chrome, self->w_lint_find));
        bp_fill_lint(ui, self);
    }
    else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->w_lint_list) )
        bp_fill_lint(ui, self);
}

static void
bp_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct builder_panel* self = opaque;

    /* The editor opened or closed: a builder
     * starts (and stays) hidden until the View menu opens it, and closing the
     * editor closes it. The registry has just shown the form's window. */
    (void)visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_main, 0);
}

/** The NPC editor grows into the column (EditorUiPanel::stretch): the map
 *  well is the part that stretches, so a tall column shows the whole
 *  Movement tab and a short one still shows the header, the tabs and the
 *  well. */
static int
bp_stretch(
    struct EditorUi* ui,
    void* opaque,
    int* out_min_h,
    int* out_natural_h)
{
    struct builder_panel* self = opaque;

    assert(out_min_h);
    assert(out_natural_h);
    *out_min_h = ToriRSChrome_Px(ui->chrome, 360);
    *out_natural_h = ToriRSChrome_Px(ui->chrome, 720);
    return ToriRSChrome_Px(ui->chrome, self->w_mv_map >= 0 ? BP_MV_H : BP_GRAPH_H);
}

static void
bp_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct builder_panel* self = opaque;
    if( self->problems_source[0] )
        (void)Editor_Problems_Clear(ui, self->problems_source);
    if( self->services.release )
        self->services.release(self->services.user_data);
    ToriMem_Free(ToriMem_Editor(), self->check);
    ToriMem_Free(ToriMem_Editor(), self->model);
    ToriMem_Free(ToriMem_Editor(), self->existing);
    ToriMem_Free(ToriMem_Editor(), self);
}

static void
bp_build(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int npc = self->subject == BUILDER_SUBJECT_NPC;
    int p;

    options_init();
    /* One window, docked by the layout like every other panel (D1: three
     * floating windows at hard-coded coordinates); its width is the
     * column's, so every label here is short enough for a 260 px column
     * and says the rest in its tooltip (D2). */
    self->p_main = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, EDP_COL_X + EDP_COL_W + 8,
                                         npc ? 120 : 140, 300, npc ? "NPC editor" : "Loc builder");
    assert(self->p_main >= 0);
    p = self->p_main;
    ToriRSChrome_PanelSetScrollable(chrome, p, 1);
    ToriRSChrome_PanelSetResizable(chrome, p, 1);
    ToriRSChrome_PanelSetClosable(chrome, p, 1);
    ToriRSChrome_PanelBeginTab(chrome, p, -1);

    /* The header, on every tab. */
    self->w_subject = ToriRSChrome_TextInput(chrome, p, npc ? "NPC" : "Loc", "");
    ToriRSChrome_SetTooltip(chrome, self->w_subject, "The record's pack symbol; Enter opens it");
    self->w_open = ToriRSChrome_Button(chrome, p, "Open");
    self->w_fromsel = ToriRSChrome_Button(chrome, p, "From selection");
    ToriRSChrome_SetTooltip(chrome, self->w_fromsel,
                            npc ? "The Spawns panel's selected spawn, else the Catalog's pick"
                                : "The loc selected in the world");
    self->w_status = ToriRSChrome_Label(chrome, p, "Open a subject.");
    self->w_will = -1;
    self->w_will_note = -1;
    if( npc )
    {
        self->w_will = ToriRSChrome_Label(chrome, p, "");
        ToriRSChrome_SetTooltip(chrome, self->w_will, "The rule: patrol rows, then defaultmode, then wanderrange");
        ToriRSChrome_SetHidden(chrome, self->w_will, 1);
        self->w_will_note = ToriRSChrome_Label(chrome, p, "");
        ToriRSChrome_SetHidden(chrome, self->w_will_note, 1);
    }
    self->w_tabs = -1;
    self->w_mv_map = -1;
    self->w_sp_list = -1;
    self->mv_sel = -1;
    self->sp_sel = -1;
    self->mv_px = BP_MV_PX_DEFAULT;
    self->mv_drag_wp = -1;
    if( npc )
    {
        static const char* const k_tabs[BUILDER_NPC_TAB_COUNT] = { "Movement", "Spawns", "Interactions", "Dialogue" };
        self->w_tabs = ToriRSChrome_Tabs(chrome, p, k_tabs, BUILDER_NPC_TAB_COUNT, BUILDER_NPC_TAB_MOVEMENT);
        assert(self->w_tabs >= 0);
        bp_mv_build(ui, self);
        ToriRSChrome_PanelBeginTab(chrome, p, BUILDER_NPC_TAB_INTERACTIONS);
    }

    /* ---- Interactions: the tree's triggers, then the builder's ---- */
    ToriRSChrome_Heading(chrome, p, "In the tree");
    self->w_existing_label = ToriRSChrome_Label(chrome, p, "");
    self->w_existing_list = ToriRSChrome_VList(chrome, p, "", BP_EXISTING_ROWS, 0);
    assert(self->w_existing_list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->w_existing_list, "Every trigger the tree declares; hand-authored = read-only");
    self->w_existing_open = ToriRSChrome_Button(chrome, p, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->w_existing_open, "The selected trigger's script in your editor");

    ToriRSChrome_Heading(chrome, p, "Builder triggers");
    self->w_trigger_kind = ToriRSChrome_Dropdown(chrome, p, "Trigger", k_trigger_options,
                                                 BP_TRIGGER_OPTION_COUNT, 0);
    self->w_use_obj = ToriRSChrome_TextInput(chrome, p, "Held obj", "");
    ToriRSChrome_SetTooltip(chrome, self->w_use_obj, "For a use-on trigger: the obj used on the subject, * for any");
    self->w_trigger_add = ToriRSChrome_Button(chrome, p, "Add trigger");
    self->w_trigger_remove = ToriRSChrome_Button(chrome, p, "Remove trigger");
    self->w_trigger_dialogue = npc ? ToriRSChrome_Checkbox(chrome, p, "Runs the dialogue", 0) : -1;
    if( npc )
        ToriRSChrome_SetTooltip(chrome, self->w_trigger_dialogue,
                                "The selected trigger runs the Dialogue tab's graph, not steps");
    self->w_menu_text = ToriRSChrome_TextInput(chrome, p, "Menu text", "");
    ToriRSChrome_SetTooltip(chrome, self->w_menu_text, "The cache's opN: what the right-click menu shows");
    self->w_rename_op = ToriRSChrome_Button(chrome, p, "Rename op");
    ToriRSChrome_SetTooltip(chrome, self->w_rename_op,
                            "Menu text becomes the cache's opN: needs a cache pack");

    self->w_outline_head = ToriRSChrome_Heading(chrome, p, "Outline");
    ToriRSChrome_SetTooltip(chrome, self->w_outline_head, "The builder's triggers and their steps; click to select");

    ToriRSChrome_Heading(chrome, p, "Step");
    self->w_step_kind =
        ToriRSChrome_Dropdown(chrome, p, "Kind", k_step_options, BUILDER_STEP_KIND_COUNT, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_step_kind, "Compare: = ! < > <= >=; Coord: level_mx_mz_lx_lz");
    for( int f = 0; f < 4; f++ )
        self->w_step_field[f] = ToriRSChrome_TextInput(chrome, p, "-", "");
    self->w_step_add = ToriRSChrome_Button(chrome, p, "Add step");
    self->w_step_apply = ToriRSChrome_Button(chrome, p, "Apply step");
    ToriRSChrome_SetTooltip(chrome, self->w_step_apply, "Write the fields into the selected step");
    self->w_step_remove = ToriRSChrome_Button(chrome, p, "Remove step");
    self->w_step_up = ToriRSChrome_Button(chrome, p, "Move up");
    self->w_step_down = ToriRSChrome_Button(chrome, p, "Move down");
    bp_show_step_fields(ui, self, BUILDER_STEP_MES);

    ToriRSChrome_Heading(chrome, p, "Server fields");
    self->w_category = ToriRSChrome_TextInput(chrome, p, "Category", "");
    if( npc )
    {
        self->w_door = -1;
        self->w_door_other = -1;
        self->w_ladder = -1;
    }
    else
    {
        self->w_door = ToriRSChrome_Dropdown(chrome, p, "Door", k_door_options, 3, 0);
        self->w_door_other = ToriRSChrome_TextInput(chrome, p, "Other half", "");
        ToriRSChrome_SetTooltip(chrome, self->w_door_other, "The door's other half (a double door)");
        self->w_ladder = ToriRSChrome_Dropdown(chrome, p, "Ladder", k_ladder_options, 7, 0);
    }
    self->w_server_apply = ToriRSChrome_Button(chrome, p, "Apply fields");
    ToriRSChrome_SetTooltip(chrome, self->w_server_apply, "Write the server fields into the subject's config");

    ToriRSChrome_Separator(chrome, p);
    self->w_regen = ToriRSChrome_Button(chrome, p, "Regenerate");
    self->w_check = ToriRSChrome_Button(chrome, p, "Check");
    ToriRSChrome_SetTooltip(chrome, self->w_check, "Compile the scripts on the editor server (sscompile)");
    self->w_save = ToriRSChrome_Button(chrome, p, "Save");
    self->w_detach = ToriRSChrome_Button(chrome, p, "Detach");
    ToriRSChrome_SetTooltip(chrome, self->w_detach,
                            "Drop the model: the scripts become hand-authored");
    self->w_issue_list = ToriRSChrome_VList(chrome, p, "Issues", BP_ISSUE_ROWS, 0);
    assert(self->w_issue_list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->w_issue_list, "What stops the builder writing: also in Problems");
    ToriRSChrome_SetHidden(chrome, self->w_issue_list, 1);

    /* ---- Dialogue ---- */
    self->w_graph = -1;
    self->w_node_kind = self->w_node_add = self->w_node_remove = self->w_node_start = -1;
    self->w_node_apply = -1;
    self->w_node_text = -1;
    self->w_graph_hint = self->w_layout = -1;
    self->w_pages = self->w_preview = self->w_preview_label = -1;
    self->w_lint_find = self->w_lint_list = self->w_lint_label = -1;
    self->w_dialogue_outline_head = -1;
    self->layout_node = -2;
    self->drag_port_node = -1;
    Builder_ChatMetricsBaked(&self->chat);
    if( npc )
    {
        ToriRSChrome_PanelBeginTab(chrome, p, BUILDER_NPC_TAB_DIALOGUE);
        self->w_graph = ToriRSChrome_Graph(chrome, p, "Dialogue graph", BP_GRAPH_H);
        self->w_graph_hint = ToriRSChrome_Paragraph(
            chrome, p,
            "Drag a port (the box on a node's foot) to a node to link it; drop it on empty space for a new "
            "node. Shift+drag moves a node with everything after it.");
        self->w_layout = ToriRSChrome_Button(chrome, p, "Auto-layout");
        ToriRSChrome_SetTooltip(chrome, self->w_layout, "Lay the graph out left to right by depth (saved as pos=)");
        self->w_node_kind =
            ToriRSChrome_Dropdown(chrome, p, "Node kind", k_node_options, BUILDER_NODE_KIND_COUNT, 0);
        self->w_node_add = ToriRSChrome_Button(chrome, p, "New node");
        ToriRSChrome_SetTooltip(chrome, self->w_node_add, "A node of the kind above, after the selected one");
        self->w_node_remove = ToriRSChrome_Button(chrome, p, "Delete node");
        ToriRSChrome_SetTooltip(chrome, self->w_node_remove, "Delete the selected node; says what it orphans");
        self->w_node_start = ToriRSChrome_Button(chrome, p, "Make it the start");
        self->w_node_text = ToriRSChrome_TextInput(chrome, p, "Text ({player} = name)", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_text, "The node's text; {player} is the player's name");
        /* The Text field as the chat box breaks it, and the page it shows. */
        self->w_pages = ToriRSChrome_Graph(chrome, p, "Pages", BP_PAGE_LINE_H + 4);
        ToriRSChrome_SetTooltip(chrome, self->w_pages, "The text at the chat box's line rule; click a page");
        self->w_preview = ToriRSChrome_Graph(chrome, p, "Chat box preview", BP_CHAT_H + 2);
        self->w_preview_label = ToriRSChrome_Label(chrome, p, "");
        self->w_node_expr = ToriRSChrome_TextInput(chrome, p, "Expression", "neutral");
        ToriRSChrome_SetTooltip(chrome, self->w_node_expr, "The chathead's expression (^chat_...)");
        self->w_node_next = ToriRSChrome_TextInput(chrome, p, "Next node", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_next, "The node after this one, or a branch's default arm");
        self->w_node_speaker_npc = ToriRSChrome_TextInput(chrome, p, "Speaker npc", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_speaker_npc, "Another npc speaking this line (override)");
        self->w_node_speaker_name = ToriRSChrome_TextInput(chrome, p, "Speaker name", "");
        self->w_node_obj = ToriRSChrome_TextInput(chrome, p, "Obj / var", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_obj, "The obj, var or step field the node kind reads");
        self->w_node_row = ToriRSChrome_TextInput(chrome, p, "Quest row", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_row, "The quest row, or a first stage");
        self->w_node_quest = ToriRSChrome_TextInput(chrome, p, "Quest action", "");
        ToriRSChrome_SetTooltip(chrome, self->w_node_quest, "The quest action, or a step kind");
        for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
        {
            char label[48];
            snprintf(label, sizeof(label), "Port %d", a + 1);
            self->w_node_arm[a] = ToriRSChrome_TextInput(chrome, p, label, "");
            ToriRSChrome_SetTooltip(chrome, self->w_node_arm[a], "text|next (a choice) or cmp|value|next (a branch)");
        }
        self->w_node_apply = ToriRSChrome_Button(chrome, p, "Apply to node");
        self->w_dialogue_outline_head = ToriRSChrome_Heading(chrome, p, "Outline");
        ToriRSChrome_SetTooltip(chrome, self->w_dialogue_outline_head,
                                "The dialogue as a tree from the start; a click selects");
        /* The lint, the dialogue's own Problems rows: a click selects. */
        ToriRSChrome_Heading(chrome, p, "Problems");
        self->w_lint_label = ToriRSChrome_Label(chrome, p, "");
        self->w_lint_find = ToriRSChrome_TextInput(chrome, p, "Find in problems", "");
        self->w_lint_list = ToriRSChrome_VList(chrome, p, "Lint", BP_LINT_VISIBLE, 0);
        assert(self->w_lint_list >= 0);
        self->hotkeys_serial = ui->hotkeys.serial;
        edp_button_chord(ui, self->w_layout, EDITOR_HK_GRAPH_AUTO_LAYOUT);
        edp_button_chord(ui, self->w_node_add, EDITOR_HK_GRAPH_NEW_NODE);
        edp_button_chord(ui, self->w_node_remove, EDITOR_HK_EDIT_DELETE);
    }
    ToriRSChrome_PanelBeginTab(chrome, p, -1);
    ToriRSChrome_PanelSetVisible(chrome, self->p_main, 0);
    bp_refresh(ui, self);
}

static void
builder_register(
    struct EditorUi* ui,
    const struct Builder_Services* services,
    enum Builder_Subject subject)
{
    struct builder_panel* self;
    struct EditorUiPanel panel;

    assert(ui);
    assert(ui->chrome);
    assert(services);

    self = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self));
    assert(self);
    memset(self, 0, sizeof(*self));
    self->model = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->model));
    assert(self->model);
    self->check = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->check));
    assert(self->check);
    memset(self->check, 0, sizeof(*self->check));
    self->check->status = BUILDER_SSC_NOT_RUN;
    self->subject = subject;
    self->services = *services;
    self->sel_trigger = -1;
    self->sel_step = -1;
    self->sel_node = -1;
    Builder_ModelInit(self->model, subject, "none");
    bp_build(ui, self);

    memset(&panel, 0, sizeof(panel));
    panel.name = subject == BUILDER_SUBJECT_NPC ? BUILDER_PANEL_NPC : BUILDER_PANEL_LOC;
    panel.title = subject == BUILDER_SUBJECT_NPC ? "NPC editor" : "Loc builder";
    panel.self = self;
    panel.chrome_panel = self->p_main;
    panel.tick = bp_tick;
    panel.activate = bp_activate;
    panel.event = bp_event;
    panel.set_visible = bp_set_visible;
    panel.stretch = subject == BUILDER_SUBJECT_NPC ? bp_stretch : NULL;
    panel.destroy = bp_destroy;
    Editor_UiAddPanel(ui, &panel);
}

void
Builder_LocPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    builder_register(ui, services, BUILDER_SUBJECT_LOC);
}

void
Builder_NpcPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    builder_register(ui, services, BUILDER_SUBJECT_NPC);
}

/* ---- the shell editor ----------------------------------------------------- */

struct shell_panel
{
    struct Builder_Services services;
    struct Builder_Shell shell;
    int has_shell;
    char doc_key[EDITOR_DOC_KEY_MAX];
    int sel_slot;
    struct Builder_PreviewVar preview[BUILDER_PREVIEW_MAX];
    int preview_count;

    int p_main;
    int p_slots;
    int w_kind, w_name, w_open, w_status, w_var_kind, w_var, w_default_none, w_summary;
    int w_new_name, w_copy;
    int w_child, w_set, w_hidden, w_insert, w_append, w_remove, w_apply;
    int w_preview, w_preview_label;
    int slot_rows[BUILDER_SHELL_SLOTS_MAX];
    int slot_row_count;
    /** The slider value last published (or heard): the slider publishes
     *  STATE_VAR_PREVIEW live, every value it passes through while it is
     *  dragged, not only on the release. */
    int preview_published;
    /** The var this panel's slider put in the published preview (kind 0:
     *  none), so closing the editor or opening another shell takes it out
     *  again: the preview changes every world view, not only this window's. */
    int published_kind;
    int published_id;
    int w_preview_stop;
    /** p_main's visibility last tick (a close stops the preview). */
    int shown;
};

static void
sp_status(
    struct EditorUi* ui,
    struct shell_panel* self,
    const char* text)
{
    ToriRSChrome_SetText(ui->chrome, self->w_status, text);
    Editor_UiLog(ui, "shell editor: %s", text);
}

static void
sp_preview_label(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    char line[TORIRS_CHROME_INPUT_MAX];
    int value = ToriRSChrome_SliderValue(ui->chrome, self->w_preview);
    const char* child;

    if( !self->has_shell || self->shell.slot_count == 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->w_preview_label, "");
        return;
    }
    child = Builder_ShellResolveChild(&self->shell, value);
    snprintf(line, sizeof(line), "%s = %d shows %s (slot %d)", self->shell.var, value,
             child ? child : "nothing (hidden)", Builder_ShellResolve(&self->shell, value) + 1);
    ToriRSChrome_SetText(ui->chrome, self->w_preview_label, line);
}

static void
sp_rebuild(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    char summary[TORIRS_CHROME_INPUT_MAX];

    ToriRSChrome_PanelClearWidgets(ui->chrome, self->p_slots);
    self->slot_row_count = 0;
    if( !self->has_shell )
    {
        ToriRSChrome_Label(ui->chrome, self->p_slots, "Open a record to see its slots.");
        ToriRSChrome_SetText(ui->chrome, self->w_summary, "");
        return;
    }
    ToriRSChrome_Label(ui->chrome, self->p_slots, "value | child");
    for( int i = 0; i < self->shell.slot_count; i++ )
    {
        char value[32];
        char line[TORIRS_CHROME_LABEL_MAX];
        int widget;

        Builder_ShellSlotValueLabel(&self->shell, i, value, sizeof(value));
        snprintf(line, sizeof(line), "%s | %s", value,
                 Builder_ShellSlotHidden(&self->shell.slots[i]) ? "none (hidden)"
                                                                 : self->shell.slots[i].child);
        widget = ToriRSChrome_TreeRow(ui->chrome, self->p_slots, line, 0, TORIRS_CHROME_TREE_LEAF);
        self->slot_rows[self->slot_row_count++] = widget;
        if( i == self->sel_slot )
            ToriRSChrome_TreeSelect(ui->chrome, widget);
    }
    Builder_ShellSummary(&self->shell, summary, sizeof(summary));
    ToriRSChrome_SetText(ui->chrome, self->w_summary, summary);
    ToriRSChrome_SliderSetRange(ui->chrome, self->w_preview, 0,
                                self->shell.slot_count > 0 ? self->shell.slot_count : 1, 1);
    self->preview_published = ToriRSChrome_SliderValue(ui->chrome, self->w_preview);
    sp_preview_label(ui, self);
}

static int
sp_stop_preview(
    struct EditorUi* ui,
    struct shell_panel* self);

static int
sp_open(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    int kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_kind);
    const char* name = ToriRSChrome_Text(ui->chrome, self->w_name);
    enum Builder_Subject subject = kind == 1 ? BUILDER_SUBJECT_NPC : BUILDER_SUBJECT_LOC;
    const char* text;
    size_t length = 0;
    char line[160];
    int is_shell;

    if( !name || !Builder_NameValid(name) )
    {
        sp_status(ui, self, "a record name: [a-z0-9_]");
        return 0;
    }
    if( !self->services.submit.current )
    {
        sp_status(ui, self, "no session");
        return 0;
    }
    /* The last shell's preview does not outlive it. */
    (void)sp_stop_preview(ui, self);
    snprintf(self->doc_key, sizeof(self->doc_key), "%s:%s",
             subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", name);
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, self->doc_key,
                                         &length);
    if( !text || length == 0 )
    {
        snprintf(line, sizeof(line), "no record %s", self->doc_key);
        sp_status(ui, self, line);
        self->has_shell = 0;
        sp_rebuild(ui, self);
        return 0;
    }
    is_shell = Builder_ShellParse(&self->shell, subject, text, length);
    self->has_shell = 1;
    self->sel_slot = -1;
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->w_var_kind,
                                     self->shell.var_kind == BUILDER_SHELL_VAR_VARP ? 1 : 0);
    ToriRSChrome_SetText(ui->chrome, self->w_var, self->shell.var);
    if( self->w_default_none >= 0 )
        ToriRSChrome_SetChecked(ui->chrome, self->w_default_none, self->shell.default_none);
    snprintf(line, sizeof(line), "%s: %s, %d slot(s)", self->doc_key,
             is_shell ? "a shell" : "not a shell yet", self->shell.slot_count);
    sp_status(ui, self, line);
    sp_rebuild(ui, self);
    return 1;
}

/** Copy to new...: the open record's definition under the typed name with
 *  a fresh id (one undo step); the copy becomes the open record. */
static void
sp_copy(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    const char* new_name = ToriRSChrome_Text(ui->chrome, self->w_new_name);
    const char* colon = strchr(self->doc_key, ':');
    char line[160];

    if( !self->has_shell || !colon )
    {
        sp_status(ui, self, "open a record first");
        return;
    }
    if( !new_name || !Builder_NameValid(new_name) )
    {
        sp_status(ui, self, "a new record name: [a-z0-9_]");
        return;
    }
    if( !self->services.copy_record )
    {
        sp_status(ui, self, "copying needs an editor server");
        return;
    }
    if( !self->services.copy_record(self->services.user_data, self->shell.subject, colon + 1, new_name) )
    {
        snprintf(line, sizeof(line), "%s was not copied to %s (see the console)", self->doc_key, new_name);
        sp_status(ui, self, line);
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->w_name, new_name);
    sp_open(ui, self);
}

static int
sp_apply(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    const char* text;
    size_t length = 0;
    struct Editor_Cmd* commands;
    int count;
    int applied = 0;
    char line[160];

    if( !self->has_shell || !self->services.submit.apply )
        return 0;
    if( self->shell.slot_count > 0 && self->shell.var_kind == BUILDER_SHELL_VAR_NONE )
    {
        sp_status(ui, self, "a shell needs its var");
        return 0;
    }
    if( self->shell.overflow )
    {
        sp_status(ui, self, "this table is longer than the editor holds; it is shown, not edited");
        return 0;
    }
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, self->doc_key,
                                         &length);
    count = Builder_ShellCommands(self->doc_key, text ? text : "", length, &self->shell, NULL, 0);
    if( count == 0 )
    {
        sp_status(ui, self, "no change");
        return 1;
    }
    commands = ToriMem_Alloc(ToriMem_Editor(), sizeof(*commands) * (size_t)count);
    assert(commands);
    Builder_ShellCommands(self->doc_key, text ? text : "", length, &self->shell, commands, count);
    self->services.submit.stroke(self->services.user_data, 1);
    for( int i = 0; i < count; i++ )
    {
        if( !self->services.submit.apply(self->services.user_data, &commands[i]) )
            break;
        applied++;
    }
    self->services.submit.stroke(self->services.user_data, 0);
    ToriMem_Free(ToriMem_Editor(), commands);
    snprintf(line, sizeof(line), "%d of %d field edit(s) applied%s", applied, count,
             applied < count ? " -- refused; re-open" : "");
    sp_status(ui, self, line);
    return applied == count;
}

static void
sp_publish_preview(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    int kind;
    int id;
    int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
    int count;

    if( !self->has_shell || !Builder_VarNameId(self->shell.var, &kind, &id) )
    {
        sp_status(ui, self, "the preview needs a varp<id>_ / varb<id>_ var name");
        return;
    }
    if( (kind == BUILDER_PREVIEW_VARP) != (self->shell.var_kind == BUILDER_SHELL_VAR_VARP) )
    {
        sp_status(ui, self, "the var's name and kind disagree");
        return;
    }
    self->preview_count = Builder_PreviewSet(self->preview, self->preview_count, kind, id,
                                             ToriRSChrome_SliderValue(ui->chrome, self->w_preview));
    count = Builder_PreviewEncode(self->preview, self->preview_count, values,
                                  TORIRSMAPED_STATE_VALUES_MAX);
    self->preview_published = ToriRSChrome_SliderValue(ui->chrome, self->w_preview);
    self->published_kind = kind;
    self->published_id = id;
    if( self->services.state_set )
        self->services.state_set(self->services.user_data, TORIRSMAPED_STATE_VAR_PREVIEW, values,
                                 count);
}

/** Take this panel's var out of the published preview (the rest of the
 *  list stays): every view resolves that var from its real value again.
 *  Returns 1 when there was one to take out. */
static int
sp_stop_preview(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
    int count;
    int kept = 0;
    int had = 0;

    (void)ui;
    if( self->published_kind == 0 )
        return 0;
    for( int i = 0; i < self->preview_count; i++ )
    {
        if( self->preview[i].kind == self->published_kind && self->preview[i].id == self->published_id )
        {
            had = 1;
            continue;
        }
        self->preview[kept++] = self->preview[i];
    }
    self->preview_count = kept;
    self->published_kind = 0;
    self->published_id = 0;
    if( !had )
        return 0;
    count = Builder_PreviewEncode(self->preview, self->preview_count, values, TORIRSMAPED_STATE_VALUES_MAX);
    if( self->services.state_set )
        self->services.state_set(self->services.user_data, TORIRSMAPED_STATE_VAR_PREVIEW, values, count);
    return 1;
}

static int
sp_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct shell_panel* self = opaque;
    struct ToriRSChrome* chrome = ui->chrome;

    if( widget == self->w_open || widget == self->w_name )
    {
        sp_open(ui, self);
        return 1;
    }
    if( widget == self->w_new_name )
        return 1;
    if( widget == self->w_copy )
    {
        sp_copy(ui, self);
        return 1;
    }
    if( widget == self->w_kind || widget == self->w_var_kind )
        return 1;
    for( int i = 0; i < self->slot_row_count; i++ )
    {
        if( widget == self->slot_rows[i] )
        {
            self->sel_slot = i;
            ToriRSChrome_TreeSelect(chrome, widget);
            ToriRSChrome_SetText(chrome, self->w_child,
                                 Builder_ShellSlotHidden(&self->shell.slots[i]) ? ""
                                                                                : self->shell.slots[i].child);
            return 1;
        }
    }
    if( widget == self->w_preview )
    {
        sp_preview_label(ui, self);
        sp_publish_preview(ui, self);
        return 1;
    }
    if( widget == self->w_preview_stop )
    {
        sp_status(ui, self, sp_stop_preview(ui, self) ? "preview stopped: the world shows the var's real value"
                                                      : "no preview to stop");
        return 1;
    }
    if( widget != self->w_set && widget != self->w_hidden && widget != self->w_insert
        && widget != self->w_append && widget != self->w_remove && widget != self->w_apply
        && widget != self->w_default_none && widget != self->w_var )
        return 0;
    if( !self->has_shell )
    {
        sp_status(ui, self, "open a record first");
        return 1;
    }
    if( widget == self->w_var )
        return 1;
    if( widget == self->w_default_none )
    {
        self->shell.default_none = ToriRSChrome_Checked(chrome, self->w_default_none);
        return 1;
    }
    if( widget == self->w_set || widget == self->w_hidden )
    {
        const char* child = widget == self->w_hidden ? "-1" : ToriRSChrome_Text(chrome, self->w_child);
        if( self->sel_slot < 0 || self->sel_slot >= self->shell.slot_count )
        {
            sp_status(ui, self, "select a slot first");
            return 1;
        }
        if( !child || (strcmp(child, "-1") != 0 && !Builder_NameValid(child)) )
        {
            sp_status(ui, self, "a child is a record name, or Hidden");
            return 1;
        }
        copy_text(self->shell.slots[self->sel_slot].child, BUILDER_NAME_MAX, child);
    }
    else if( widget == self->w_insert || widget == self->w_append )
    {
        int at = widget == self->w_append || self->sel_slot < 0 ? self->shell.slot_count
                                                               : self->sel_slot;
        if( !Builder_ShellInsertSlot(&self->shell, at, "-1") )
        {
            sp_status(ui, self, "the shell has as many slots as the editor holds");
            return 1;
        }
        self->sel_slot = at;
    }
    else if( widget == self->w_remove )
    {
        if( self->sel_slot < 0 || self->sel_slot >= self->shell.slot_count )
            return 1;
        Builder_ShellRemoveSlot(&self->shell, self->sel_slot);
        self->sel_slot = -1;
    }
    else if( widget == self->w_apply )
    {
        const char* var = ToriRSChrome_Text(chrome, self->w_var);
        int var_kind = ToriRSChrome_DropdownSelected(chrome, self->w_var_kind);
        copy_text(self->shell.var, sizeof(self->shell.var), var);
        self->shell.var_kind = self->shell.var[0] == '\0' ? BUILDER_SHELL_VAR_NONE
                               : var_kind == 1            ? BUILDER_SHELL_VAR_VARP
                                                          : BUILDER_SHELL_VAR_VARBIT;
        sp_apply(ui, self);
        /* Read back what the authority now holds. */
        {
            size_t length = 0;
            const char* text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG,
                                                             self->doc_key, &length);
            if( text )
                Builder_ShellParse(&self->shell, self->shell.subject, text, length);
        }
    }
    sp_rebuild(ui, self);
    return 1;
}

static void
sp_on_state(
    struct EditorUi* ui,
    void* opaque,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct shell_panel* self = opaque;
    struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
    int kind;
    int id;
    int n;

    if( key != TORIRSMAPED_STATE_VAR_PREVIEW )
        return;
    n = Builder_PreviewDecode(values, count, vars, BUILDER_PREVIEW_MAX);
    if( count > 0 && n == 0 && values[0] != 0 )
        return;
    memcpy(self->preview, vars, sizeof(vars[0]) * (size_t)n);
    self->preview_count = n;
    if( !self->has_shell || !Builder_VarNameId(self->shell.var, &kind, &id) )
        return;
    for( int i = 0; i < n; i++ )
    {
        if( vars[i].kind == kind && vars[i].id == id )
        {
            ToriRSChrome_SliderSet(ui->chrome, self->w_preview, vars[i].value);
            self->preview_published = ToriRSChrome_SliderValue(ui->chrome, self->w_preview);
            sp_preview_label(ui, self);
        }
    }
}

static void
sp_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct shell_panel* self = opaque;
    int visible = ui->visible && ui->chrome->panels[self->p_main].visible;

    if( ui->chrome->panels[self->p_slots].visible != visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->p_slots, visible);
    /* Closed: the world goes back to the var's real value. */
    if( self->shown && !ui->chrome->panels[self->p_main].visible )
        (void)sp_stop_preview(ui, self);
    self->shown = ui->chrome->panels[self->p_main].visible;
    /* Scrubbing: the knob's value while it is dragged (the slider itself
     * activates only on the release) is published as it changes, so every
     * view resolves the shell through each state the knob passes. */
    if( self->has_shell && self->shell.slot_count > 0
        && ToriRSChrome_SliderValue(ui->chrome, self->w_preview) != self->preview_published )
    {
        sp_preview_label(ui, self);
        sp_publish_preview(ui, self);
    }
}

static void
sp_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct shell_panel* self = opaque;

    /* Hidden until the View menu opens it, as the builders are. */
    (void)visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_slots, 0);
}

static void
sp_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Builder_ShellPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    struct shell_panel* self;
    struct EditorUiPanel panel;
    struct ToriRSChrome* chrome;
    int p;

    assert(ui);
    assert(ui->chrome);
    assert(services);

    chrome = ui->chrome;
    self = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self));
    assert(self);
    memset(self, 0, sizeof(*self));
    self->services = *services;
    self->sel_slot = -1;

    self->p_main = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 400, 80, 280, "Shell editor");
    assert(self->p_main >= 0);
    p = self->p_main;
    ToriRSChrome_PanelSetScrollable(chrome, p, 1);
    ToriRSChrome_PanelSetClosable(chrome, p, 1);
    self->w_kind = ToriRSChrome_Dropdown(chrome, p, "Record", k_subject_options, 2, 0);
    self->w_name = ToriRSChrome_TextInput(chrome, p, "Name", "");
    self->w_open = ToriRSChrome_Button(chrome, p, "Open");
    self->w_status = ToriRSChrome_Label(chrome, p, "Open a multiloc or multinpc.");
    self->w_new_name = ToriRSChrome_TextInput(chrome, p, "New name", "");
    self->w_copy = ToriRSChrome_Button(chrome, p, "Copy to new...");
    ToriRSChrome_SetTooltip(chrome, self->w_copy, "This record under New name, with a fresh id");
    self->w_var_kind = ToriRSChrome_Dropdown(chrome, p, "Var kind", k_var_options, 2, 0);
    self->w_var = ToriRSChrome_TextInput(chrome, p, "Var (varb<id>_name / varp<id>_name)", "");
    self->w_default_none = ToriRSChrome_Checkbox(chrome, p, "No default loc (multidefaultnone)", 0);
    self->w_summary = ToriRSChrome_Label(chrome, p, "");
    ToriRSChrome_Heading(chrome, p, "Selected slot");
    self->w_child = ToriRSChrome_TextInput(chrome, p, "Child", "");
    self->w_set = ToriRSChrome_Button(chrome, p, "Set child");
    self->w_hidden = ToriRSChrome_Button(chrome, p, "Hidden (none)");
    self->w_insert = ToriRSChrome_Button(chrome, p, "Insert slot before");
    self->w_append = ToriRSChrome_Button(chrome, p, "Add slot at the end");
    self->w_remove = ToriRSChrome_Button(chrome, p, "Remove slot");
    self->w_apply = ToriRSChrome_Button(chrome, p, "Apply to the record");
    ToriRSChrome_Heading(chrome, p, "Preview");
    self->w_preview = ToriRSChrome_Slider(chrome, p, "Preview value", 0, 1, 0, 1);
    self->w_preview_label = ToriRSChrome_Label(chrome, p, "");
    ToriRSChrome_SetTooltip(chrome, self->w_preview,
                            "Every view resolves shells on this var with it");
    self->w_preview_stop = ToriRSChrome_Button(chrome, p, "Stop preview");
    ToriRSChrome_SetTooltip(chrome, self->w_preview_stop,
                            "Back to the var's real value in every view (as a close does)");

    self->p_slots = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 690, 80, 240, "Shell slots");
    assert(self->p_slots >= 0);
    ToriRSChrome_PanelSetScrollable(chrome, self->p_slots, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(chrome, self->p_slots, 0);
    sp_rebuild(ui, self);

    memset(&panel, 0, sizeof(panel));
    panel.name = BUILDER_PANEL_SHELL;
    panel.title = "Shell editor";
    panel.self = self;
    panel.chrome_panel = self->p_main;
    panel.tick = sp_tick;
    panel.activate = sp_activate;
    panel.set_visible = sp_set_visible;
    panel.on_state = sp_on_state;
    panel.destroy = sp_destroy;
    Editor_UiAddPanel(ui, &panel);
}

const struct Builder_Shell*
Builder_ShellPanelShell(struct EditorUi* ui)
{
    struct shell_panel* self = Editor_UiPanelSelf(ui, BUILDER_PANEL_SHELL);
    return self && self->has_shell ? &self->shell : NULL;
}

/* ---- the Movement and Spawns tabs (docs/NPC_EDITOR_DESIGN.md 3.4-3.6) ----- */

#include "engine/world_builder/collision_map.h"

static void
bp_mv_build(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const p = self->p_main;

    /* Top to bottom in the order a route is made: what it does now, a tool
     * and clicks in the World, the legs as the server will walk them, a
     * pause, Play; then how it behaves when it is not patrolling; the drawn
     * collision map last (the World is the map now, and every button here
     * works without it). The old order put 550 px of controls between the
     * map and the route list it edits. */
    ToriRSChrome_PanelBeginTab(chrome, p, BUILDER_NPC_TAB_MOVEMENT);
    self->w_mv_eff = ToriRSChrome_Label(chrome, p, "");

    ToriRSChrome_Heading(chrome, p, "Patrol route");
    /* A route, a range and a facing belong to the TYPE: every spawn of it
     * moves alike. So before a route is laid on a type with other spawns,
     * the one step that splits this spawn off is offered HERE, where the
     * route starts (bp_mv_share_refresh hides the two controls when the
     * type has one spawn). */
    self->w_mv_share = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_newtype = ToriRSChrome_TextInput(chrome, p, "Own type name", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_newtype, "A new record name: a-z 0-9 _ (thistle_watchman)");
    self->w_mv_split = ToriRSChrome_Button(chrome, p, "Make own type");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_split, "Copy this npc to that name; re-point the chosen spawn");
    self->w_mv_tool_select = ToriRSChrome_Radio(chrome, p, "Select", BP_GROUP_TOOL, 1);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_tool_select, "World clicks belong to the armed tool again");
    self->w_mv_tool_add = ToriRSChrome_Radio(chrome, p, "Add waypoint", BP_GROUP_TOOL, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_tool_add, "Each click on a World tile lays the next waypoint");
    self->w_mv_tool_move = ToriRSChrome_Radio(chrome, p, "Move waypoint", BP_GROUP_TOOL, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_tool_move, "Drag a numbered waypoint in the World to another tile");
    self->w_mv_hint = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_route_label = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_route_note = ToriRSChrome_Label(chrome, p, "");
    /* The way out of a red leg, under the sentence that names it: moving
     * waypoints by hand until each leg walks took a dozen drags round one
     * cottage. Hidden while every leg walks. */
    self->w_mv_fix = ToriRSChrome_Button(chrome, p, "Fix stuck legs");
    ToriRSChrome_ButtonSetFill(chrome, self->w_mv_fix, 1);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_fix, "Add the fewest waypoints that take each red leg round");
    ToriRSChrome_SetHidden(chrome, self->w_mv_fix, 1);
    self->w_mv_route = ToriRSChrome_VList(chrome, p, "", BP_MV_ROUTE_ROWS, 0);
    assert(self->w_mv_route >= 0);
    self->w_mv_pause = ToriRSChrome_TextInput(chrome, p, "Pause (ticks)", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_pause, "Ticks the selected waypoint is held; Enter sets it");
    self->w_mv_play = ToriRSChrome_Button(chrome, p, "Play");
    ToriRSChrome_SetLabel(chrome, self->w_mv_play, "Play");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_play, "A marker walks the route in the World, a tile a tick");
    self->w_mv_play_label = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_goto = ToriRSChrome_Button(chrome, p, "Go to");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_goto, "The World's camera over the selected waypoint or spawn");
    self->w_mv_wp_delete = ToriRSChrome_Button(chrome, p, "Delete waypoint");
    self->w_mv_wp_up = ToriRSChrome_Button(chrome, p, "Waypoint up");
    self->w_mv_wp_down = ToriRSChrome_Button(chrome, p, "Waypoint down");
    self->w_mv_wp_insert = ToriRSChrome_Button(chrome, p, "Insert after");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_wp_insert, "A waypoint halfway along the selected one's leg");
    /* The same two edits without a pointer in the World: they act on the
     * latched tile (a Select click, or Go to tile). */
    self->w_mv_wp_add = ToriRSChrome_Button(chrome, p, "Add latched");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_wp_add, "A waypoint on the tile latched in the World");
    self->w_mv_wp_move = ToriRSChrome_Button(chrome, p, "Move to latched");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_wp_move, "The selected waypoint to the tile latched in the World");

    ToriRSChrome_Heading(chrome, p, "Behaviour");
    self->w_mv_mode_none = ToriRSChrome_Radio(chrome, p, "Stand still", BP_GROUP_MODE, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_mode_none, "defaultmode=none: it stays on its spawn tile");
    self->w_mv_mode_wander = ToriRSChrome_Radio(chrome, p, "Wander", BP_GROUP_MODE, 1);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_mode_wander, "Roams the Wander range; over a route, removes the route");
    self->w_mv_mode_patrol = ToriRSChrome_Radio(chrome, p, "Patrol", BP_GROUP_MODE, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_mode_patrol, "Walks the route above; the first waypoint sets it");
    /* Typed, not dragged: the values wanted are small and exact (3 tiles),
     * a 0..64 slider gives 4 px a tile, and a typed field is set by its
     * label from the agent API. Empty is the engine's default. */
    self->w_mv_range = ToriRSChrome_TextInput(chrome, p, "Wander range", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_range, "wanderrange: tiles from its spawn it roams; empty: 5");
    self->w_mv_facing = ToriRSChrome_Dropdown(chrome, p, "Facing", k_facing_options, 9, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_facing, "The way every spawn of this type faces when it stands");
    self->w_mv_respawn = ToriRSChrome_TextInput(chrome, p, "Respawn (ticks)", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_respawn, "respawnrate: ticks after a death; empty: the default");
    self->w_mv_leash = ToriRSChrome_TextInput(chrome, p, "Leash", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_leash, "maxrange: tiles from its SPAWN it keeps a target; empty: 7");
    self->w_mv_restrict = ToriRSChrome_Dropdown(chrome, p, "Restrict", k_restrict_options, 8, 0);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_restrict, "moverestrict: indoors / outdoors read the roof flag");
    self->w_mv_block = ToriRSChrome_Dropdown(chrome, p, "Block walk", k_block_options, 5, 0);
    self->w_mv_timer = ToriRSChrome_TextInput(chrome, p, "Timer (ticks)", "");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_timer, "The [ai_timer] interval; 0 or empty: never armed");

    self->w_mv_target = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_target_path = ToriRSChrome_Label(chrome, p, "");

    ToriRSChrome_Heading(chrome, p, "Collision map");
    self->w_mv_map = ToriRSChrome_Graph(chrome, p, "Map", BP_MV_H);
    /* The map zooms with the wheel, in a panel that scrolls. */
    ToriRSChrome_GraphSetWheel(chrome, self->w_mv_map, 1);
    ToriRSChrome_SetTooltip(chrome, self->w_mv_map, "Red tiles block. Wheel zooms, drag pans, F fits");
    self->w_mv_readout = ToriRSChrome_Label(chrome, p, "");
    self->w_mv_fit = ToriRSChrome_Button(chrome, p, "Fit");
    ToriRSChrome_SetTooltip(chrome, self->w_mv_fit, "Frame the spawns, the route and the leash");

    ToriRSChrome_PanelBeginTab(chrome, p, BUILDER_NPC_TAB_SPAWNS);
    self->w_sp_label = ToriRSChrome_Label(chrome, p, "");
    self->w_sp_list = ToriRSChrome_VList(chrome, p, "", BP_MV_SPAWN_ROWS, 0);
    assert(self->w_sp_list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->w_sp_list, "This npc's rows in the open squares: + edited ~ quest - hidden");
    self->w_sp_goto = ToriRSChrome_Button(chrome, p, "Go to spawn");
    self->w_sp_select = ToriRSChrome_Button(chrome, p, "Select in Spawns");
    ToriRSChrome_SetTooltip(chrome, self->w_sp_select, "The Spawns panel's selection, for Move and the edit form");
    self->w_sp_place = ToriRSChrome_Button(chrome, p, "Place at latched tile");
    self->w_sp_delete = ToriRSChrome_Button(chrome, p, "Delete / suppress");
    ToriRSChrome_SetTooltip(chrome, self->w_sp_delete, "An edited row goes; a generated one is suppressed");
}

/* ---- the view ---- */

static int
bp_mv_cols(const struct builder_panel* self)
{
    return BP_MV_W / self->mv_px;
}

static int
bp_mv_rows(const struct builder_panel* self)
{
    return BP_MV_H / self->mv_px;
}

/** The well-local top-left of tile (x, z); the tile may be off the view. */
static void
bp_mv_tile_point(
    const struct builder_panel* self,
    int x,
    int z,
    int* out_sx,
    int* out_sy)
{
    int const ox = self->mv_cx - bp_mv_cols(self) / 2;
    int const oz = self->mv_cz + bp_mv_rows(self) / 2;
    *out_sx = (x - ox) * self->mv_px;
    *out_sy = (oz - z) * self->mv_px;
}

static int
bp_mv_tile_in_view(
    const struct builder_panel* self,
    int x,
    int z)
{
    int sx;
    int sy;
    bp_mv_tile_point(self, x, z, &sx, &sy);
    return sx >= 0 && sy >= 0 && sx + self->mv_px <= BP_MV_W && sy + self->mv_px <= BP_MV_H;
}

/** The tile under well-local (sx, sy). */
static void
bp_mv_tile_at(
    const struct builder_panel* self,
    int sx,
    int sy,
    int* out_x,
    int* out_z)
{
    int const ox = self->mv_cx - bp_mv_cols(self) / 2;
    int const oz = self->mv_cz + bp_mv_rows(self) / 2;
    int tx = sx < 0 ? -1 : sx / self->mv_px;
    int ty = sy < 0 ? -1 : sy / self->mv_px;
    *out_x = ox + tx;
    *out_z = oz - ty;
}

/** Frame the spawns, the route and (a wanderer's) leash. */
static void
bp_mv_fit(struct builder_panel* self)
{
    int x0 = 0;
    int z0 = 0;
    int x1 = 0;
    int z1 = 0;
    int any = 0;
    int level = -1;

    for( int i = 0; i < self->sp_count; i++ )
    {
        int const r = self->sp[i].suppressed ? 0 : self->mv_eff.mode == EDITOR_NPC_MODE_WANDER ? self->mv_eff.wander : 0;
        if( !any )
        {
            x0 = self->sp[i].x - r;
            z0 = self->sp[i].z - r;
            x1 = self->sp[i].x + r;
            z1 = self->sp[i].z + r;
            level = self->sp[i].level;
            any = 1;
            continue;
        }
        if( self->sp[i].x - r < x0 ) x0 = self->sp[i].x - r;
        if( self->sp[i].z - r < z0 ) z0 = self->sp[i].z - r;
        if( self->sp[i].x + r > x1 ) x1 = self->sp[i].x + r;
        if( self->sp[i].z + r > z1 ) z1 = self->sp[i].z + r;
    }
    for( int i = 0; i < self->mv.patrol_count; i++ )
    {
        const struct Editor_NpcWaypoint* w = &self->mv.patrol[i];
        if( w->level < 0 )
            continue;
        if( !any )
        {
            x0 = x1 = w->x;
            z0 = z1 = w->z;
            level = w->level;
            any = 1;
            continue;
        }
        if( w->x < x0 ) x0 = w->x;
        if( w->z < z0 ) z0 = w->z;
        if( w->x > x1 ) x1 = w->x;
        if( w->z > z1 ) z1 = w->z;
    }
    if( !any )
        return;
    self->mv_cx = (x0 + x1) / 2;
    self->mv_cz = (z0 + z1) / 2;
    if( level >= 0 )
        self->mv_level = level;
    /* The largest zoom that holds the extent, with a tile's margin. */
    self->mv_px = BP_MV_PX_MAX;
    while( self->mv_px > BP_MV_PX_MIN
           && ((x1 - x0 + 3) * self->mv_px > BP_MV_W || (z1 - z0 + 3) * self->mv_px > BP_MV_H) )
        self->mv_px /= 2;
    self->mv_fit_pending = 0;
}

/* ---- the record's movement into the controls ---- */

static uint32_t
bp_mv_hash_route(const struct builder_panel* self)
{
    uint32_t h = 2166136261u;
    const unsigned char* bytes = (const unsigned char*)self->mv.patrol;
    for( size_t i = 0; i < sizeof(self->mv.patrol[0]) * (size_t)self->mv.patrol_count; i++ )
        h = (h ^ bytes[i]) * 16777619u;
    h = (h ^ (uint32_t)self->mv.patrol_count) * 16777619u;
    return h;
}

static void
bp_mv_legs(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    uint32_t const hash = bp_mv_hash_route(self) ^ (uint32_t)(ui->editor ? ui->editor->last_cmd_seq : 0);
    int const size = self->mv.size > 0 ? self->mv.size : 1;

    if( hash == self->mv_legs_hash )
        return;
    self->mv_legs_hash = hash;
    /* With the scene's step test bound each leg is WALKED the way the server
     * walks it (editor_npc.h): a waypoint round a corner is reachable and the
     * npc still stands at the wall until it is teleported, which a flood
     * fill (path_steps, the fallback) reports as a fine leg. */
    self->mv_walked = self->services.can_step != NULL;
    for( int i = 0; i < self->mv.patrol_count; i++ )
    {
        const struct Editor_NpcWaypoint* a = &self->mv.patrol[i];
        const struct Editor_NpcWaypoint* b = &self->mv.patrol[(i + 1) % self->mv.patrol_count];
        struct Editor_NpcWalk* walk = &self->mv_walk[i];

        memset(walk, 0, sizeof(*walk));
        walk->result = EDITOR_NPC_WALK_UNKNOWN;
        if( a->level < 0 || b->level < 0 )
            self->mv_legs[i] = -2;
        else if( a->level != b->level )
        {
            walk->result = EDITOR_NPC_WALK_TELEPORT;
            self->mv_legs[i] = -3;
        }
        else if( self->services.can_step )
        {
            (void)Editor_NpcWalkLeg(a, b, size, self->services.can_step, self->services.user_data,
                                    self->mv_walk_tiles[i], BP_MV_WALK_MAX, walk);
            self->mv_legs[i] = walk->result == EDITOR_NPC_WALK_ARRIVES ? walk->steps
                               : walk->result == EDITOR_NPC_WALK_STUCK ? -1
                                                                       : -2;
        }
        else if( !self->services.path_steps )
            self->mv_legs[i] = -2;
        else
        {
            self->mv_legs[i] = self->services.path_steps(self->services.user_data, a->x, a->z, b->x, b->z, a->level);
            if( self->mv_legs[i] >= 0 )
            {
                walk->result = EDITOR_NPC_WALK_ARRIVES;
                walk->steps = self->mv_legs[i];
            }
            else if( self->mv_legs[i] == -1 )
                walk->result = EDITOR_NPC_WALK_STUCK;
            walk->stuck_x = self->mv_legs[i] >= 0 ? b->x : a->x;
            walk->stuck_z = self->mv_legs[i] >= 0 ? b->z : a->z;
        }
    }
}

/** How far the route reaches from the npc's spawns in the open squares
 *  (Editor_NpcRouteReach); 0 with no spawn or no route. */
static int
bp_mv_route_reach(const struct builder_panel* self)
{
    int reach = 0;

    for( int i = 0; i < self->sp_count; i++ )
    {
        int r;
        if( self->sp[i].suppressed )
            continue;
        r = Editor_NpcRouteReach(self->mv.patrol, self->mv.patrol_count, self->sp[i].x, self->sp[i].z);
        if( r > reach )
            reach = r;
    }
    return reach;
}

/** Spawns of the open npc in the open squares that are not suppressed. */
static int
bp_mv_live_spawns(const struct builder_panel* self)
{
    int live = 0;

    for( int i = 0; i < self->sp_count; i++ )
        live += !self->sp[i].suppressed;
    return live;
}

/**
 * The spawn "Make own type" splits off: the Spawns tab's row, else the
 * spawn selected in the World (the Spawns panel's selection -- a spawn just
 * placed is selected), else the only one there is. 1 and `out` filled; 0
 * when the type has no live spawn here; -1 when it has several and none is
 * chosen.
 */
static int
bp_mv_split_spawn(
    struct builder_panel* self,
    struct Builder_NpcSpawn* out)
{
    int const live = bp_mv_live_spawns(self);

    if( self->sp_sel >= 0 && self->sp_sel < self->sp_count && !self->sp[self->sp_sel].suppressed )
    {
        *out = self->sp[self->sp_sel];
        return 1;
    }
    if( self->has_model && self->services.spawn_selected
        && self->services.spawn_selected(self->services.user_data, self->model->name, out) )
        for( int i = 0; i < self->sp_count; i++ )
            if( !self->sp[i].suppressed && self->sp[i].x == out->x && self->sp[i].z == out->z
                && self->sp[i].level == out->level )
            {
                *out = self->sp[i];
                return 1;
            }
    if( live == 1 )
    {
        for( int i = 0; i < self->sp_count; i++ )
            if( !self->sp[i].suppressed )
                *out = self->sp[i];
        return 1;
    }
    return live == 0 ? 0 : -1;
}

/** "Who else moves with it", at the top of the route: with other spawns of
 *  the type in the open squares, which one Make own type would split off
 *  and the two controls that do it; with one, a line and no controls. */
static void
bp_mv_share_refresh(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    struct Builder_NpcSpawn spawn;
    int const live = bp_mv_live_spawns(self);
    int const chosen = live > 1 ? bp_mv_split_spawn(self, &spawn) : 0;

    if( !self->has_model )
        line[0] = '\0';
    else if( live > 1 && chosen > 0 )
        snprintf(line, sizeof(line), "%d spawns move alike. Own type for the one at %d,%d:", live, spawn.x, spawn.z);
    else if( live > 1 )
        snprintf(line, sizeof(line), "%d spawns move alike: select one in the World to split it off", live);
    else if( live == 1 )
        snprintf(line, sizeof(line), "1 spawn in the open squares; other squares may hold more");
    else
        snprintf(line, sizeof(line), "no spawn in the open squares yet (Spawns tab places one)");
    ToriRSChrome_SetText(ui->chrome, self->w_mv_share, line);
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_share, line[0] == '\0');
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_newtype, live <= 1);
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_split, live <= 1);
}

static void
bp_mv_route_rows(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int first;
    int shown;
    int stuck = 0;
    int teleports = 0;
    int steps = 0;
    int unknown = 0;
    char line[TORIRS_CHROME_INPUT_MAX];
    char note[TORIRS_CHROME_LABEL_MAX];

    ToriRSChrome_VListSetCount(ui->chrome, self->w_mv_route, self->mv.patrol_count);
    ToriRSChrome_VListSetSelected(ui->chrome, self->w_mv_route, self->mv_sel);
    if( ToriRSChrome_VListWindow(ui->chrome, self->w_mv_route, &first, &shown) )
        for( int r = first; r < first + shown && r < self->mv.patrol_count; r++ )
        {
            const struct Editor_NpcWaypoint* w = &self->mv.patrol[r];
            int const leg = self->mv_legs[r];
            int const next = (r + 1) % self->mv.patrol_count + 1;
            char legtext[40];
            char pause[20];
            /* One line that fits the column the panel docks in (270 px,
             * some 40 characters): number, tile, what the leg to the next
             * waypoint does, the pause held here. The long form ("leg: NO
             * PATH (teleports)  (back to 1)") was cut at the panel's edge. */
            if( self->mv.patrol_count < 2 )
                legtext[0] = '\0';
            else if( leg >= 0 )
                snprintf(legtext, sizeof(legtext), "  %d step%s to %d", leg, leg == 1 ? "" : "s", next);
            else if( leg == -3 )
                snprintf(legtext, sizeof(legtext), "  teleports to %d", next);
            else if( leg == -1 && self->mv_walked )
                snprintf(legtext, sizeof(legtext), "  STUCK at %d,%d", self->mv_walk[r].stuck_x, self->mv_walk[r].stuck_z);
            else if( leg == -1 )
                snprintf(legtext, sizeof(legtext), "  NO PATH to %d", next);
            else
                snprintf(legtext, sizeof(legtext), "  not walked");
            if( w->pause > 0 )
                snprintf(pause, sizeof(pause), ", pause %d", w->pause);
            else
                pause[0] = '\0';
            snprintf(line, sizeof(line), "%d  %d,%d%s%s%s", r + 1, w->x, w->z, w->level != 0 ? " up" : "", legtext, pause);
            ToriRSChrome_VListSetRow(ui->chrome, self->w_mv_route, r, line);
        }
    for( int i = 0; i < self->mv.patrol_count; i++ )
    {
        if( self->mv_legs[i] == -1 )
            stuck++;
        else if( self->mv_legs[i] == -2 )
            unknown++;
        else if( self->mv_legs[i] == -3 )
            teleports++;
        else if( self->mv_legs[i] > 0 )
            steps += self->mv_legs[i];
    }
    /* Two short lines (a label holds 63 bytes): the lap, then the one thing
     * to do about it. */
    if( self->mv.patrol_count == 0 )
    {
        /* The hint above the list says how to start one. */
        line[0] = '\0';
        note[0] = '\0';
    }
    else
    {
        int const ticks = steps + self->mv_eff.lap_pause;
        int const reach = bp_mv_route_reach(self);
        /* A stuck leg has no walking time (it stands, then is teleported):
         * a sum over the others undercounts the lap, and disagreed with the
         * "will:" line above it. */
        if( stuck )
            snprintf(line, sizeof(line), "lap: %d legs, %d stuck: no lap time until they walk", self->mv.patrol_count,
                     stuck);
        else
            snprintf(line, sizeof(line), "lap: %d leg%s, %d steps + %d pause = %d ticks (%d s)", self->mv.patrol_count,
                     self->mv.patrol_count == 1 ? "" : "s", steps, self->mv_eff.lap_pause, ticks, ticks * 6 / 10);
        if( self->mv.patrol_count == 1 )
            snprintf(note, sizeof(note), "one waypoint: it walks there and stays; add more for a loop");
        else if( stuck && self->mv_walked )
            snprintf(note, sizeof(note), "%d leg%s STUCK (red): press Fix stuck legs, or move a waypoint", stuck,
                     stuck == 1 ? "" : "s");
        else if( stuck )
            snprintf(note, sizeof(note), "%d leg%s STUCK (red): move a waypoint so it walks straight", stuck,
                     stuck == 1 ? "" : "s");
        else if( unknown )
            snprintf(note, sizeof(note), "%d leg%s not walked: open the square%s the route crosses", unknown,
                     unknown == 1 ? "" : "s", unknown == 1 ? "" : "s");
        else if( reach > self->mv_eff.leash )
            snprintf(note, sizeof(note), "leash %d < the route's reach %d from its spawn: raise Leash",
                     self->mv_eff.leash, reach);
        else if( teleports )
            snprintf(note, sizeof(note), "%d leg%s change level: the server teleports it there", teleports,
                     teleports == 1 ? "" : "s");
        else
            snprintf(note, sizeof(note), "every leg walks: it is never stuck and never teleports");
    }
    ToriRSChrome_SetText(ui->chrome, self->w_mv_route_label, line);
    ToriRSChrome_SetText(ui->chrome, self->w_mv_route_note, note);
    /* An empty line takes no row. */
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_route_label, line[0] == '\0');
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_route_note, note[0] == '\0');
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_fix, !(stuck && self->mv_walked && self->mv.patrol_count >= 2));
}

static void
bp_mv_spawn_rows(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int first;
    int shown;
    char line[TORIRS_CHROME_INPUT_MAX];

    ToriRSChrome_VListSetCount(ui->chrome, self->w_sp_list, self->sp_count);
    if( ToriRSChrome_VListWindow(ui->chrome, self->w_sp_list, &first, &shown) )
        for( int r = first; r < first + shown && r < self->sp_count; r++ )
        {
            const struct Builder_NpcSpawn* s2 = &self->sp[r];
            snprintf(line, sizeof(line), "%s%c %s  %d,%d lvl %d%s", r == self->sp_sel ? "* " : "",
                     s2->edited ? '+' : s2->suppressed ? '-' : s2->scripted ? '~' : ' ',
                     s2->edited ? "edited" : s2->scripted ? "quest" : "generated", s2->x, s2->z, s2->level,
                     s2->suppressed ? "  (suppressed)" : "");
            ToriRSChrome_VListSetRow(ui->chrome, self->w_sp_list, r, line);
        }
    if( !self->has_model )
        line[0] = '\0';
    else if( !self->services.spawns )
        snprintf(line, sizeof(line), "no Spawns panel here: the rows are not listed");
    else
        snprintf(line, sizeof(line), "%d spawn%s in the open squares (m%d_%d...)", self->sp_count,
                 self->sp_count == 1 ? "" : "s", self->sp_count ? self->sp[0].x / 64 : 0,
                 self->sp_count ? self->sp[0].z / 64 : 0);
    ToriRSChrome_SetText(ui->chrome, self->w_sp_label, line);
}

static void
bp_mv_refresh(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    char line[TORIRS_CHROME_LABEL_MAX];
    int editable;

    if( self->w_mv_map < 0 )
        return;
    self->mv_valid = 0;
    self->sp_count = 0;
    if( self->has_model && self->services.movement )
        self->mv_valid = self->services.movement(self->services.user_data, self->model->name, &self->mv, self->mv_target,
                                                 sizeof(self->mv_target));
    if( !self->mv_valid )
    {
        Editor_NpcMovementInit(&self->mv);
        self->mv_target[0] = '\0';
    }
    if( self->has_model && self->services.spawns )
        self->sp_count = self->services.spawns(self->services.user_data, self->model->name, self->record.id, self->sp,
                                               BP_MV_SPAWNS_MAX);
    if( self->mv_sel >= self->mv.patrol_count )
        self->mv_sel = self->mv.patrol_count - 1;
    if( self->sp_sel >= self->sp_count )
        self->sp_sel = self->sp_count - 1;
    Editor_NpcEffectiveMode(&self->mv, &self->mv_eff);
    bp_mv_legs(ui, self);

    /* The effective line. */
    if( !self->has_model )
        snprintf(line, sizeof(line), "open an npc");
    else if( !self->mv_valid )
        snprintf(line, sizeof(line), "no record session: the movement fields are not editable here");
    else
    {
        /* The four type-wide values in one line, the engine's defaults
         * spelled where the record states nothing (an empty field and an
         * "(unstated)" dropdown do not say what the npc will do). */
        char mode[24];
        char respawn[16];
        if( self->mv_eff.nomove )
            snprintf(mode, sizeof(mode), "nomove");
        else if( self->mv_eff.mode == EDITOR_NPC_MODE_PATROL )
            snprintf(mode, sizeof(mode), "patrol");
        else if( self->mv_eff.mode == EDITOR_NPC_MODE_WANDER )
            snprintf(mode, sizeof(mode), "wander %d", self->mv_eff.wander);
        else
            snprintf(mode, sizeof(mode), "stands still");
        if( self->mv.respawnrate >= 0 )
            snprintf(respawn, sizeof(respawn), "%d", self->mv.respawnrate);
        else
            snprintf(respawn, sizeof(respawn), "default");
        snprintf(line, sizeof(line), "now: %s, leash %d, facing %s, respawn %s", mode, self->mv_eff.leash,
                 self->mv.facing >= 0 ? Editor_NpcFacingWord(self->mv.facing) : "unset", respawn);
    }
    ToriRSChrome_SetText(chrome, self->w_mv_eff, line);
    ToriRSChrome_SetText(chrome, self->w_mv_hint,
                         !self->has_model        ? ""
                         : self->mv_tool == 1    ? "click World tiles in order; click a waypoint to select it"
                         : self->mv_tool == 2    ? "drag a numbered waypoint in the World to another tile"
                                                 : "Add waypoint, then click tiles in the World to lay a route");

    /* The controls read the statements; a tick on a radio is the stated or
     * effective mode. */
    editable = self->mv_valid && !self->read_only;
    ToriRSChrome_RadioSelect(chrome, self->mv.patrol_count > 0 ? self->w_mv_mode_patrol
                                     : self->mv.defaultmode_stated
                                         ? (self->mv.defaultmode == EDITOR_NPC_MODE_WANDER ? self->w_mv_mode_wander
                                            : self->mv.defaultmode == EDITOR_NPC_MODE_PATROL ? self->w_mv_mode_patrol
                                                                                              : self->w_mv_mode_none)
                                     : self->mv_eff.mode == EDITOR_NPC_MODE_WANDER ? self->w_mv_mode_wander
                                                                                   : self->w_mv_mode_none);
    if( chrome->focus != self->w_mv_leash )
    {
        if( self->mv.maxrange >= 0 )
            snprintf(line, sizeof(line), "%d", self->mv.maxrange);
        else
            line[0] = '\0';
        ToriRSChrome_SetText(chrome, self->w_mv_leash, line);
    }
    if( chrome->focus != self->w_mv_range )
    {
        if( self->mv.wanderrange >= 0 )
            snprintf(line, sizeof(line), "%d", self->mv.wanderrange);
        else
            line[0] = '\0';
        ToriRSChrome_SetText(chrome, self->w_mv_range, line);
    }
    ToriRSChrome_SetDisabled(chrome, self->w_mv_range, !editable || self->mv_eff.mode == EDITOR_NPC_MODE_PATROL);
    ToriRSChrome_DropdownSetSelected(chrome, self->w_mv_restrict,
                                     self->mv.moverestrict >= 0 ? (int)self->mv.moverestrict + 1 : 0);
    ToriRSChrome_DropdownSetSelected(chrome, self->w_mv_block, self->mv.blockwalk >= 0 ? self->mv.blockwalk + 1 : 0);
    ToriRSChrome_DropdownSetSelected(chrome, self->w_mv_facing, self->mv.facing >= 0 ? self->mv.facing + 1 : 0);
    if( chrome->focus != self->w_mv_respawn )
    {
        if( self->mv.respawnrate >= 0 )
            snprintf(line, sizeof(line), "%d", self->mv.respawnrate);
        else
            line[0] = '\0';
        ToriRSChrome_SetText(chrome, self->w_mv_respawn, line);
    }
    if( chrome->focus != self->w_mv_timer )
    {
        if( self->mv.timer >= 0 )
            snprintf(line, sizeof(line), "%d", self->mv.timer);
        else
            line[0] = '\0';
        ToriRSChrome_SetText(chrome, self->w_mv_timer, line);
    }
    if( chrome->focus != self->w_mv_pause )
    {
        if( self->mv_sel >= 0 )
            snprintf(line, sizeof(line), "%d", self->mv.patrol[self->mv_sel].pause);
        else
            line[0] = '\0';
        ToriRSChrome_SetText(chrome, self->w_mv_pause, line);
    }
    {
        int const widgets[] = { self->w_mv_mode_none, self->w_mv_mode_patrol, self->w_mv_mode_wander, self->w_mv_leash,
                                self->w_mv_restrict, self->w_mv_block, self->w_mv_facing, self->w_mv_respawn,
                                self->w_mv_timer, self->w_mv_pause, self->w_mv_wp_add, self->w_mv_wp_insert,
                                self->w_mv_wp_delete, self->w_mv_wp_up, self->w_mv_wp_down, self->w_sp_place,
                                self->w_sp_delete, self->w_mv_wp_move, self->w_mv_tool_add, self->w_mv_tool_move,
                                self->w_mv_newtype, self->w_mv_split, self->w_mv_fix };
        for( size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++ )
            ToriRSChrome_SetDisabled(chrome, widgets[i], !editable);
    }
    /* The file, on a line of its own: a label holds 63 bytes and a path
     * does not share them with a sentence. */
    {
        char path[TORIRS_CHROME_LABEL_MAX];
        path[0] = '\0';
        if( !self->has_model )
            line[0] = '\0';
        else if( self->mv_target[0] )
        {
            size_t const length = strlen(self->mv_target);
            snprintf(line, sizeof(line), "Movement is written to %s:",
                     strstr(self->mv_target, "edited/") == self->mv_target ? "the editor's own file" : "its authored file");
            if( length < sizeof(path) )
                snprintf(path, sizeof(path), "%s", self->mv_target);
            else
                snprintf(path, sizeof(path), "...%s", self->mv_target + length - (sizeof(path) - 4));
        }
        else
            snprintf(line, sizeof(line), "no authored overlay to write to: edits are refused");
        ToriRSChrome_SetText(chrome, self->w_mv_target, line);
        ToriRSChrome_SetText(chrome, self->w_mv_target_path, path);
    }
    bp_mv_share_refresh(ui, self);
    bp_mv_route_rows(ui, self);
    bp_mv_spawn_rows(ui, self);
    if( self->mv_fit_pending || (self->mv_cx == 0 && self->mv_cz == 0) )
        bp_mv_fit(self);
    self->mv_draw_hash = 0;
}

/* ---- the well ---- */

static uint32_t
bp_mv_hash_view(const struct builder_panel* self)
{
    uint32_t h = bp_mv_hash_route(self);
    int const parts[] = { self->mv_px,     self->mv_cx,      self->mv_cz,     self->mv_level,  self->mv_sel,
                          self->mv_hover_valid ? self->mv_hover_x : -1, self->mv_hover_valid ? self->mv_hover_z : -1,
                          self->mv_drag_wp, self->mv_drag_x,  self->mv_drag_z, self->sp_count,  self->mv_eff.mode,
                          self->mv_eff.wander, self->mv_eff.leash, self->mv_eff.nomove, self->mv_play,
                          self->mv_play_leg, self->mv_play_step, self->mv_tool };
    for( size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++ )
        h = (h ^ (uint32_t)parts[i]) * 16777619u;
    for( int i = 0; i < self->sp_count; i++ )
        h = (h ^ (uint32_t)(self->sp[i].x * 73856093 ^ self->sp[i].z * 19349663 ^ self->sp[i].level)) * 16777619u;
    for( int i = 0; i < self->mv.patrol_count; i++ )
        h = (h ^ (uint32_t)self->mv_legs[i]) * 16777619u;
    return h;
}

/** A rectangle of tiles as an outline, clipped to the well. */
static void
bp_mv_tile_rect_outline(
    struct EditorUi* ui,
    struct builder_panel* self,
    int x0,
    int z0,
    int x1,
    int z1,
    uint32_t color)
{
    int sx0;
    int sy0;
    int sx1;
    int sy1;
    bp_mv_tile_point(self, x0, z1, &sx0, &sy0);
    bp_mv_tile_point(self, x1, z0, &sx1, &sy1);
    sx1 += self->mv_px;
    sy1 += self->mv_px;
    if( sx0 < 0 ) sx0 = 0;
    if( sy0 < 0 ) sy0 = 0;
    if( sx1 > BP_MV_W ) sx1 = BP_MV_W;
    if( sy1 > BP_MV_H ) sy1 = BP_MV_H;
    if( sx1 <= sx0 || sy1 <= sy0 )
        return;
    ToriRSChrome_GraphRect(ui->chrome, self->w_mv_map, sx0, sy0, sx1 - sx0, sy1 - sy0, color, 0, 0);
}

/** The ghost's tile while Play runs: along leg `mv_play_leg`, step
 *  `mv_play_step` of it (a straight line of Chebyshev steps). */
static int
bp_mv_ghost_tile(
    const struct builder_panel* self,
    int* out_x,
    int* out_z)
{
    const struct Editor_NpcWaypoint* a;
    const struct Editor_NpcWaypoint* b;
    int steps;
    int dx;
    int dz;

    if( !self->mv_play || self->mv.patrol_count == 0 )
        return 0;
    a = &self->mv.patrol[self->mv_play_leg % self->mv.patrol_count];
    b = &self->mv.patrol[(self->mv_play_leg + 1) % self->mv.patrol_count];
    /* A walked leg: the tile the server's walk is on at this step; past
     * the tiles kept (or where the walk stopped) the place it ended. */
    if( self->mv_walked )
    {
        const struct Editor_NpcWalk* walk = &self->mv_walk[self->mv_play_leg % self->mv.patrol_count];
        const struct Editor_NpcWalkTile* tiles = self->mv_walk_tiles[self->mv_play_leg % self->mv.patrol_count];
        if( self->mv_play_step <= 0 )
        {
            *out_x = a->x;
            *out_z = a->z;
        }
        else if( self->mv_play_step <= walk->tile_count )
        {
            *out_x = tiles[self->mv_play_step - 1].x;
            *out_z = tiles[self->mv_play_step - 1].z;
        }
        else
        {
            *out_x = walk->stuck_x;
            *out_z = walk->stuck_z;
        }
        return 1;
    }
    steps = Editor_NpcLegSteps(a, b);
    if( steps <= 0 )
    {
        *out_x = b->x;
        *out_z = b->z;
        return 1;
    }
    dx = b->x - a->x;
    dz = b->z - a->z;
    *out_x = a->x + (dx > 0 ? (self->mv_play_step < dx ? self->mv_play_step : dx)
                            : -(self->mv_play_step < -dx ? self->mv_play_step : -dx));
    *out_z = a->z + (dz > 0 ? (self->mv_play_step < dz ? self->mv_play_step : dz)
                            : -(self->mv_play_step < -dz ? self->mv_play_step : -dz));
    return 1;
}

static void
bp_mv_redraw(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const w = self->w_mv_map;
    int const cols = bp_mv_cols(self);
    int const rows = bp_mv_rows(self);
    int const ox = self->mv_cx - cols / 2;
    int const oz = self->mv_cz + rows / 2;
    int const px = self->mv_px;
    int const tint_roof = self->mv.moverestrict == EDITOR_NPC_RESTRICT_INDOORS
                          || self->mv.moverestrict == EDITOR_NPC_RESTRICT_OUTDOORS;
    uint32_t const hash = bp_mv_hash_view(self);

    if( hash == self->mv_draw_hash && (self->mv_frame % 30) != 0 )
        return;
    self->mv_draw_hash = hash;
    ToriRSChrome_GraphBegin(chrome, w);
    /* The ground: one keyed rect a tile. */
    for( int r = 0; r < rows; r++ )
        for( int c = 0; c < cols; c++ )
        {
            int const x = ox + c;
            int const z = oz - r;
            struct Builder_TileInfo info;
            uint32_t color;
            memset(&info, 0, sizeof(info));
            if( self->services.tile_info )
                self->services.tile_info(self->services.user_data, x, z, self->mv_level, &info);
            if( info.in_scene )
            {
                int const blocked = (info.collision & (COLL_FLAG_FLOOR_BLOCKED | COLL_FLAG_LOC)) != 0;
                color = blocked ? BP_MV_C_BLOCKED : (tint_roof && (info.settings & 4)) ? BP_MV_C_OPEN_ROOF : BP_MV_C_OPEN;
            }
            else if( info.in_doc )
                color = (info.settings & 1) ? BP_MV_C_BLOCKED : (tint_roof && (info.settings & 4)) ? BP_MV_C_OPEN_ROOF
                                                                                                    : BP_MV_C_NODOC;
            else
                color = BP_MV_C_UNLOADED;
            ToriRSChrome_GraphRect(chrome, w, c * px, r * px, px, px, color, 1, BP_MV_KEY_TILE + r * cols + c);
            /* Walls as edge lines, when a tile is wide enough to show them. */
            if( info.in_scene && px >= 6 && (info.collision & 0xAA) )
            {
                int const sx = c * px;
                int const sy = r * px;
                if( info.collision & COLL_FLAG_WALL_NORTH )
                    ToriRSChrome_GraphLine(chrome, w, sx, sy, sx + px - 1, sy, BP_MV_C_WALL);
                if( info.collision & COLL_FLAG_WALL_SOUTH )
                    ToriRSChrome_GraphLine(chrome, w, sx, sy + px - 1, sx + px - 1, sy + px - 1, BP_MV_C_WALL);
                if( info.collision & COLL_FLAG_WALL_WEST )
                    ToriRSChrome_GraphLine(chrome, w, sx, sy, sx, sy + px - 1, BP_MV_C_WALL);
                if( info.collision & COLL_FLAG_WALL_EAST )
                    ToriRSChrome_GraphLine(chrome, w, sx + px - 1, sy, sx + px - 1, sy + px - 1, BP_MV_C_WALL);
            }
        }
    /* The effective squares at each spawn: a wanderer's square and leash, a
     * stander's leash, a nomove's pin; a patroller has none. */
    for( int i = 0; i < self->sp_count; i++ )
    {
        const struct Builder_NpcSpawn* s2 = &self->sp[i];
        int const size = self->mv.size > 0 ? self->mv.size : 1;
        if( s2->suppressed || s2->level != self->mv_level )
            continue;
        if( self->mv_eff.mode == EDITOR_NPC_MODE_PATROL )
            continue;
        if( self->mv_eff.nomove )
            bp_mv_tile_rect_outline(ui, self, s2->x, s2->z, s2->x + size - 1, s2->z + size - 1, BP_MV_C_PIN);
        else
        {
            if( self->mv_eff.wander > 0 )
                bp_mv_tile_rect_outline(ui, self, s2->x - self->mv_eff.wander, s2->z - self->mv_eff.wander,
                                        s2->x + size - 1 + self->mv_eff.wander, s2->z + size - 1 + self->mv_eff.wander,
                                        BP_MV_C_WANDER);
            if( self->mv_eff.leash > self->mv_eff.wander )
                bp_mv_tile_rect_outline(ui, self, s2->x - self->mv_eff.leash, s2->z - self->mv_eff.leash,
                                        s2->x + size - 1 + self->mv_eff.leash, s2->z + size - 1 + self->mv_eff.leash,
                                        BP_MV_C_LEASH);
        }
    }
    /* The route: legs between tile centres, then the waypoints over them. */
    for( int i = 0; i < self->mv.patrol_count && self->mv.patrol_count >= 2; i++ )
    {
        struct Editor_NpcWaypoint a = self->mv.patrol[i];
        struct Editor_NpcWaypoint b = self->mv.patrol[(i + 1) % self->mv.patrol_count];
        int ax, ay, bx, by;
        uint32_t color;
        if( self->mv_drag_wp == i )
        {
            a.x = self->mv_drag_x;
            a.z = self->mv_drag_z;
        }
        if( self->mv_drag_wp == (i + 1) % self->mv.patrol_count )
        {
            b.x = self->mv_drag_x;
            b.z = self->mv_drag_z;
        }
        if( a.level < 0 || b.level < 0 )
            continue;
        color = (a.level != b.level || self->mv_legs[i] == -1) ? BP_MV_C_TELEPORT
                : i + 1 == self->mv.patrol_count               ? BP_MV_C_ROUTE_CLOSING
                                                               : BP_MV_C_ROUTE;
        bp_mv_tile_point(self, a.x, a.z, &ax, &ay);
        bp_mv_tile_point(self, b.x, b.z, &bx, &by);
        ax += px / 2;
        ay += px / 2;
        bx += px / 2;
        by += px / 2;
        if( (ax < 0 && bx < 0) || (ay < 0 && by < 0) || (ax >= BP_MV_W && bx >= BP_MV_W) || (ay >= BP_MV_H && by >= BP_MV_H) )
            continue;
        ToriRSChrome_GraphLine(chrome, w, ax, ay, bx, by, color);
    }
    for( int i = 0; i < self->mv.patrol_count; i++ )
    {
        struct Editor_NpcWaypoint a = self->mv.patrol[i];
        int sx;
        int sy;
        char number[8];
        if( self->mv_drag_wp == i )
        {
            a.x = self->mv_drag_x;
            a.z = self->mv_drag_z;
        }
        if( a.level < 0 || !bp_mv_tile_in_view(self, a.x, a.z) )
            continue;
        bp_mv_tile_point(self, a.x, a.z, &sx, &sy);
        ToriRSChrome_GraphRect(chrome, w, sx, sy, px, px, a.level == self->mv_level ? BP_MV_C_ROUTE : BP_MV_C_TELEPORT, 1,
                               BP_MV_KEY_WP + i);
        if( i == self->mv_sel )
            ToriRSChrome_GraphRect(chrome, w, sx - 1, sy - 1, px + 2, px + 2, BP_MV_C_SELECTED, 0, 0);
        if( px >= 8 )
        {
            snprintf(number, sizeof(number), "%d", i + 1);
            ToriRSChrome_GraphText(chrome, w, sx + px + 1, sy + px - 1, BP_MV_C_ROUTE, number);
        }
    }
    /* The spawns: a square inset in the tile, coloured by file. */
    for( int i = 0; i < self->sp_count; i++ )
    {
        const struct Builder_NpcSpawn* s2 = &self->sp[i];
        int sx;
        int sy;
        int const inset = px >= 8 ? 2 : px >= 4 ? 1 : 0;
        uint32_t const color = s2->suppressed ? BP_MV_C_SPAWN_SUPPRESSED
                               : s2->edited   ? BP_MV_C_SPAWN_EDITED
                               : s2->scripted ? BP_MV_C_SPAWN_SCRIPTED
                                              : BP_MV_C_SPAWN_GENERATED;
        if( !bp_mv_tile_in_view(self, s2->x, s2->z) )
            continue;
        bp_mv_tile_point(self, s2->x, s2->z, &sx, &sy);
        ToriRSChrome_GraphRect(chrome, w, sx + inset, sy + inset, px - 2 * inset, px - 2 * inset,
                               s2->level == self->mv_level ? color : (color >> 1) & 0x7f7f7f, 1, BP_MV_KEY_SPAWN + i);
        if( i == self->sp_sel )
            ToriRSChrome_GraphRect(chrome, w, sx - 1, sy - 1, px + 2, px + 2, BP_MV_C_SELECTED, 0, 0);
    }
    /* The ghost at play, the hovered tile. */
    {
        int gx;
        int gz;
        int sx;
        int sy;
        if( bp_mv_ghost_tile(self, &gx, &gz) && bp_mv_tile_in_view(self, gx, gz) )
        {
            bp_mv_tile_point(self, gx, gz, &sx, &sy);
            ToriRSChrome_GraphRect(chrome, w, sx + 1, sy + 1, px - 2 > 0 ? px - 2 : 1, px - 2 > 0 ? px - 2 : 1,
                                   BP_MV_C_GHOST, 1, 0);
        }
        if( self->mv_hover_valid && bp_mv_tile_in_view(self, self->mv_hover_x, self->mv_hover_z) )
        {
            bp_mv_tile_point(self, self->mv_hover_x, self->mv_hover_z, &sx, &sy);
            ToriRSChrome_GraphRect(chrome, w, sx, sy, px, px, BP_MV_C_HOVER, 0, 0);
        }
    }
    ToriRSChrome_GraphEnd(chrome, w);
}

static void
bp_mv_readout(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    struct Builder_TileInfo info;

    if( !self->mv_hover_valid )
    {
        snprintf(line, sizeof(line), "level %d, %d px a tile; centre %d,%d", self->mv_level, self->mv_px, self->mv_cx,
                 self->mv_cz);
        ToriRSChrome_SetText(ui->chrome, self->w_mv_readout, line);
        return;
    }
    memset(&info, 0, sizeof(info));
    if( self->services.tile_info )
        self->services.tile_info(self->services.user_data, self->mv_hover_x, self->mv_hover_z, self->mv_level, &info);
    snprintf(line, sizeof(line), "%d,%d lvl %d  m%d_%d  %s%s", self->mv_hover_x, self->mv_hover_z, self->mv_level,
             self->mv_hover_x / 64, self->mv_hover_z / 64,
             info.in_scene ? ((info.collision & (COLL_FLAG_FLOOR_BLOCKED | COLL_FLAG_LOC)) ? "blocked" : "walkable")
             : info.in_doc ? ((info.settings & 1) ? "blocked (jm2)" : "open (not in the world)")
                           : "square not open",
             (info.settings & 4) ? ", roofed" : "");
    ToriRSChrome_SetText(ui->chrome, self->w_mv_readout, line);
}

/* ---- the edits ---- */

static int
bp_mv_write_route(
    struct EditorUi* ui,
    struct builder_panel* self,
    const struct Editor_NpcWaypoint* route,
    int count,
    enum Editor_NpcMode mode,
    const char* what)
{
    int n;

    if( !self->has_model || !self->services.route_set )
    {
        bp_status(ui, self, "no record session: the route cannot be written here");
        return 0;
    }
    if( self->read_only )
    {
        bp_status(ui, self, "read-only: the route was not changed");
        return 0;
    }
    n = self->services.route_set(self->services.user_data, self->model->name, route, count, mode, what);
    if( n < 0 )
        return 0;
    bp_mv_refresh(ui, self);
    bp_show_will(ui, self);
    return 1;
}

static int
bp_mv_write_field(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* key,
    const char* value,
    const char* what)
{
    int n;

    if( !self->has_model || !self->services.movement_set )
    {
        bp_status(ui, self, "no record session: the field cannot be written here");
        return 0;
    }
    if( self->read_only )
    {
        bp_status(ui, self, "read-only: the field was not changed");
        return 0;
    }
    n = self->services.movement_set(self->services.user_data, self->model->name, key, value, what);
    if( n < 0 )
        return 0;
    bp_mv_refresh(ui, self);
    bp_show_will(ui, self);
    if( n > 0 )
    {
        char line[160];
        snprintf(line, sizeof(line), "%s %s %s %s -> %s", self->model->name, key, value[0] ? "=" : "removed",
                 value[0] ? value : "", self->mv_target);
        bp_status(ui, self, line);
    }
    return 1;
}

/** The route with waypoint `at` (0..count) inserted / `at` removed /
 *  moved: the callers' one helper. */
static int
bp_mv_route_copy(
    const struct builder_panel* self,
    struct Editor_NpcWaypoint* out)
{
    int n = 0;
    for( int i = 0; i < self->mv.patrol_count; i++ )
        if( self->mv.patrol[i].level >= 0 )
            out[n++] = self->mv.patrol[i];
    return n;
}

static void
bp_mv_add_waypoint(
    struct EditorUi* ui,
    struct builder_panel* self,
    int x,
    int z,
    int level,
    int after)
{
    struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX + 1];
    struct Editor_NpcWaypoint added;
    int n = bp_mv_route_copy(self, route);

    if( n >= EDITOR_NPC_PATROL_MAX )
    {
        bp_status(ui, self, "the server walks at most 16 waypoints");
        return;
    }
    memset(&added, 0, sizeof(added));
    added.x = x;
    added.z = z;
    added.level = level;
    if( after < 0 || after >= n )
        route[n] = added;
    else
    {
        memmove(&route[after + 2], &route[after + 1], sizeof(route[0]) * (size_t)(n - after - 1));
        route[after + 1] = added;
    }
    n++;
    if( bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "add waypoint") )
    {
        self->mv_sel = after < 0 || after >= n - 1 ? n - 1 : after + 1;
        bp_mv_route_rows(ui, self);
        bp_mv_refresh(ui, self);
    }
}

static int
bp_mv_set_tool(
    struct EditorUi* ui,
    struct builder_panel* self,
    int tool)
{
    struct ToriRSChrome* chrome = ui->chrome;

    if( tool != 0 && (!self->has_model || !self->mv_valid || self->read_only) )
    {
        bp_status(ui, self, !self->has_model ? "open an npc first: its name above, then Open"
                                             : "this npc's movement cannot be edited here (see the line above)");
        tool = 0;
    }
    self->mv_tool = tool;
    self->mv_wdrag_wp = -1;
    self->mv_wpress = 0;
    ToriRSChrome_RadioSelect(chrome, tool == 1 ? self->w_mv_tool_add : tool == 2 ? self->w_mv_tool_move
                                                                                 : self->w_mv_tool_select);
    ToriRSChrome_SetText(chrome, self->w_mv_hint,
                         !self->has_model ? ""
                         : tool == 1      ? "click World tiles in order; click a waypoint to select it"
                         : tool == 2      ? "drag a numbered waypoint in the World to another tile"
                                          : "Add waypoint, then click tiles in the World to lay a route");
    if( tool == 1 && bp_mv_live_spawns(self) > 1 && self->mv.patrol_count == 0 )
    {
        /* A patrol is a field of the type: say so before the first click
         * sends every spawn of it down the route. */
        char line[200];
        snprintf(line, sizeof(line), "%d spawns of %s will all walk this route; to patrol one, Make own type first (top "
                                     "of the tab)",
                 bp_mv_live_spawns(self), self->model->name);
        bp_status(ui, self, line);
    }
    else if( tool == 1 )
        bp_status(ui, self, "Add waypoint: click tiles in the World in the order it walks them; Select ends it");
    else if( tool == 2 )
        bp_status(ui, self, "Move waypoint: press a numbered waypoint in the World and drag it; Select ends it");
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
    return tool;
}

/** What Play is doing, under its button. */
static void
bp_mv_play_say(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int const count = self->mv.patrol_count;

    if( !self->mv_play || count < 2 )
        line[0] = '\0';
    else
    {
        int const leg = self->mv_play_leg % count;
        int const next = (leg + 1) % count + 1;
        int const total = self->mv_walked ? self->mv_walk[leg].steps
                                          : Editor_NpcLegSteps(&self->mv.patrol[leg], &self->mv.patrol[next - 1]);
        if( self->mv_play_stuck )
            snprintf(line, sizeof(line), "STUCK at %d,%d: after %d ticks it teleports to %d", self->mv_walk[leg].stuck_x,
                     self->mv_walk[leg].stuck_z, EDITOR_NPC_PATROL_STUCK_TICKS, next);
        else if( self->mv_play_hold > 0 )
            snprintf(line, sizeof(line), "at waypoint %d: pausing, %d tick%s left", next, self->mv_play_hold,
                     self->mv_play_hold == 1 ? "" : "s");
        else
            snprintf(line, sizeof(line), "walking to waypoint %d: step %d of %d", next, self->mv_play_step, total);
    }
    ToriRSChrome_SetText(ui->chrome, self->w_mv_play_label, line);
    ToriRSChrome_SetHidden(ui->chrome, self->w_mv_play_label, line[0] == '\0');
}

static void
bp_mv_play(
    struct EditorUi* ui,
    struct builder_panel* self,
    int on)
{
    if( on && self->mv.patrol_count < 2 )
    {
        bp_status(ui, self, "Play needs a route of two or more waypoints");
        on = 0;
    }
    self->mv_play = on;
    self->mv_play_leg = 0;
    self->mv_play_step = 0;
    self->mv_play_hold = 0;
    self->mv_play_stuck = 0;
    self->mv_play_ms = ui->chrome->now_ms;
    /* The caption says what a press does; the label the agent finds it by
     * stays "Play" (npc.play_route is the same toggle). */
    ToriRSChrome_SetText(ui->chrome, self->w_mv_play, on ? "Stop" : "Play");
    if( on )
    {
        /* The walker is drawn in the World: bring the route into view, or
         * Play shows nothing to a camera that is elsewhere. Its middle (the
         * box round its waypoints on waypoint 1's level), not waypoint 1,
         * which put half a loop off the screen. */
        int x0 = self->mv.patrol[0].x;
        int x1 = x0;
        int z0 = self->mv.patrol[0].z;
        int z1 = z0;
        for( int i = 1; i < self->mv.patrol_count; i++ )
        {
            const struct Editor_NpcWaypoint* w = &self->mv.patrol[i];
            if( w->level != self->mv.patrol[0].level )
                continue;
            x0 = w->x < x0 ? w->x : x0;
            x1 = w->x > x1 ? w->x : x1;
            z0 = w->z < z0 ? w->z : z0;
            z1 = w->z > z1 ? w->z : z1;
        }
        if( self->services.goto_tile )
            (void)self->services.goto_tile(self->services.user_data, (x0 + x1) / 2, (z0 + z1) / 2,
                                           self->mv.patrol[0].level);
        bp_status(ui, self, "Play: the magenta marker walks the route in the World, a tile every 0.6 s, as the server will");
    }
    bp_mv_play_say(ui, self);
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
}

/** One server tick of Play: a step along the walked leg; at its end the
 *  waypoint's pause, or -- where the walk stopped dead -- a short hold and
 *  the jump the server makes. */
static void
bp_mv_play_tick(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int const count = self->mv.patrol_count;
    int const leg = self->mv_play_leg % count;
    const struct Editor_NpcWaypoint* a = &self->mv.patrol[leg];
    const struct Editor_NpcWaypoint* b = &self->mv.patrol[(leg + 1) % count];
    int const total = self->mv_walked ? self->mv_walk[leg].steps : Editor_NpcLegSteps(a, b);
    int next_leg = 0;

    if( self->mv_play_hold > 0 )
        next_leg = --self->mv_play_hold == 0;
    else
    {
        if( self->mv_play_step < total )
            self->mv_play_step++;
        if( self->mv_play_step >= total )
        {
            if( self->mv_walked && self->mv_walk[leg].result == EDITOR_NPC_WALK_STUCK )
            {
                self->mv_play_stuck = 1;
                self->mv_play_hold = BP_MV_PLAY_STUCK_TICKS;
            }
            else if( b->pause > 0 )
                self->mv_play_hold = b->pause;
            else
                next_leg = 1;
        }
    }
    if( next_leg )
    {
        self->mv_play_leg = (leg + 1) % count;
        self->mv_play_step = 0;
        self->mv_play_hold = 0;
        self->mv_play_stuck = 0;
    }
    bp_mv_play_say(ui, self);
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
}

/**
 * "Make own type": a route, a wander range and a facing are fields of the
 * npc TYPE, so the one man who should patrol has to stop being a `man`.
 * Copy the open npc to the typed name, re-point the chosen spawn at the
 * copy, and open the copy -- two undo steps, one press.
 */
static void
bp_mv_make_own_type(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    const char* typed = ToriRSChrome_Text(ui->chrome, self->w_mv_newtype);
    char new_name[BUILDER_NAME_MAX];
    char old_name[BUILDER_NAME_MAX];
    struct Builder_NpcSpawn spawn;
    int have_spawn = 0;
    int live = 0;
    char line[200];

    if( !self->has_model )
    {
        bp_status(ui, self, "open an npc first: its name above, then Open");
        return;
    }
    if( !typed || !typed[0] || !Builder_NameValid(typed) || strlen(typed) >= sizeof(new_name) )
    {
        bp_status(ui, self, "type the new type's name in \"Own type name\" first: a-z 0-9 _ (thistle_watchman)");
        ToriRSChrome_FocusWidget(ui->chrome, self->w_mv_newtype, 0);
        return;
    }
    copy_text(new_name, sizeof(new_name), typed);
    copy_text(old_name, sizeof(old_name), self->model->name);
    if( strcmp(new_name, old_name) == 0 )
    {
        bp_status(ui, self, "that is this npc's name already: type a new one");
        return;
    }
    if( !self->services.copy_record )
    {
        bp_status(ui, self, "no record session here: a type cannot be copied");
        return;
    }
    /* Which spawn becomes the new type (bp_mv_split_spawn); with several
     * and none chosen, ask. None at all is fine: the copy is made and
     * opened, to be placed later. */
    live = bp_mv_live_spawns(self);
    have_spawn = bp_mv_split_spawn(self, &spawn);
    if( have_spawn < 0 )
    {
        snprintf(line, sizeof(line), "%d spawns of %s are here: click the one to split off in the World, then again",
                 live, old_name);
        bp_status(ui, self, line);
        return;
    }
    if( !self->services.copy_record(self->services.user_data, BUILDER_SUBJECT_NPC, old_name, new_name) )
    {
        snprintf(line, sizeof(line), "%s was not copied to %s (the Console says why: the name may be taken)", old_name,
                 new_name);
        bp_status(ui, self, line);
        return;
    }
    if( have_spawn )
    {
        int gone;
        int placed;
        /* The delete and the place are ONE undo step (the copy is a second:
         * the authority keeps a SAVE_AS its own step, whatever stroke is
         * open). Three steps for one press read as an Undo that did nothing.
         * The Spawns panel's edits open their own stroke unless one is open
         * (edp_apply reads tools.stroke_open), so it is raised here too. */
        int const joined = self->services.submit.stroke && !ui->tools.stroke_open;
        if( joined )
        {
            self->services.submit.stroke(self->services.submit.user_data, 1);
            ui->tools.stroke_open = 1;
        }
        gone = self->services.spawn_delete ? self->services.spawn_delete(self->services.user_data, &spawn) : 0;
        placed = self->services.spawn_place
                     ? self->services.spawn_place(self->services.user_data, new_name, spawn.x, spawn.z, spawn.level)
                     : 0;
        if( joined )
        {
            ui->tools.stroke_open = 0;
            self->services.submit.stroke(self->services.submit.user_data, 0);
        }
        if( gone <= 0 || placed <= 0 )
        {
            snprintf(line, sizeof(line), "%s is copied from %s, but its spawn at %d,%d was not re-pointed: place it by name",
                     new_name, old_name, spawn.x, spawn.z);
            (void)bp_open(ui, self, new_name);
            bp_status(ui, self, line);
            return;
        }
    }
    ToriRSChrome_SetText(ui->chrome, self->w_mv_newtype, "");
    if( !bp_open(ui, self, new_name) )
        return;
    if( have_spawn )
        snprintf(line, sizeof(line),
                 "%s is its own type now (a copy of %s); the spawn at %d,%d is a %s. Undo twice puts it back",
                 new_name, old_name, spawn.x, spawn.z, new_name);
    else
        snprintf(line, sizeof(line), "%s is a new type (a copy of %s); place it from the Spawns tab", new_name, old_name);
    bp_status(ui, self, line);
}

/** 1 when a step from some neighbour lands on waypoint `w`'s tile: a
 *  waypoint on a tree, in the river or inside a wall is walked to by no leg,
 *  and no detour helps it. */
static int
bp_mv_enterable(
    const struct builder_panel* self,
    const struct Editor_NpcWaypoint* w,
    int size)
{
    for( int dx = -1; dx <= 1; dx++ )
        for( int dz = -1; dz <= 1; dz++ )
            if( (dx != 0 || dz != 0)
                && self->services.can_step(self->services.user_data, w->x - dx, w->z - dz, dx, dz, w->level, size) == 1 )
                return 1;
    return 0;
}

/**
 * "Fix stuck legs": each leg the walk says stops dead gets the waypoints
 * Editor_NpcLegDetour finds round what stops it, the whole route written
 * as one undo step. Returns the waypoints added; -1 when nothing was
 * written (the status says why).
 */
static int
bp_mv_fix_legs(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
    struct Editor_NpcWaypoint detour[EDITOR_NPC_PATROL_MAX];
    int const size = self->mv.size > 0 ? self->mv.size : 1;
    int const count = self->mv.patrol_count;
    int n = 0;
    int fixed = 0;
    int unfixable = -1;
    int unfixable_why = 0;
    char line[256];

    if( !self->has_model || !self->mv_valid || self->read_only )
    {
        bp_status(ui, self, !self->has_model ? "open an npc with a route first"
                                             : "this npc's movement cannot be edited here (see the line above)");
        return -1;
    }
    if( !self->services.can_step || count < 2 )
    {
        bp_status(ui, self, count < 2 ? "Fix stuck legs needs a route of two or more waypoints"
                                      : "the World cannot walk legs here: no collision to test");
        return -1;
    }
    bp_mv_legs(ui, self);
    for( int i = 0; i < count; i++ )
    {
        const struct Editor_NpcWaypoint* a = &self->mv.patrol[i];
        const struct Editor_NpcWaypoint* b = &self->mv.patrol[(i + 1) % count];
        int added = 0;

        assert(n < EDITOR_NPC_PATROL_MAX);
        route[n++] = *a;
        if( self->mv_legs[i] != -1 )
            continue;
        /* Room left once the waypoints still to copy have theirs. */
        added = Editor_NpcLegDetour(a, b, size, self->services.can_step, self->services.user_data, detour,
                                    EDITOR_NPC_PATROL_MAX - n - (count - i - 1));
        if( added == 0 )
            continue;
        if( added < 0 )
        {
            /* No way round inside the search box (-1), or no room left for
             * one (-2): this leg stays as it is, and the sentence names it. */
            if( unfixable < 0 )
            {
                unfixable = i;
                unfixable_why = added;
            }
            continue;
        }
        memcpy(&route[n], detour, sizeof(detour[0]) * (size_t)added);
        n += added;
        fixed++;
    }
    /* A leg left as it was: a waypoint nothing walks onto is the usual
     * reason, and the one thing to do about it is to move THAT waypoint. */
    if( unfixable >= 0 )
    {
        int const to = (unfixable + 1) % count;
        int const blocked = !bp_mv_enterable(self, &self->mv.patrol[to], size) ? to
                            : !bp_mv_enterable(self, &self->mv.patrol[unfixable], size) ? unfixable
                                                                                         : -1;
        if( blocked >= 0 )
            snprintf(line, sizeof(line), "waypoint %d is on a tile no step lands on (a tree, water, a wall): move it",
                     blocked + 1);
        else if( unfixable_why == -2 )
            snprintf(line, sizeof(line),
                     "leg %d to %d needs more waypoints round than the %d the server walks: move waypoint %d nearer",
                     unfixable + 1, to + 1, EDITOR_NPC_PATROL_MAX, to + 1);
        else
            snprintf(line, sizeof(line), "leg %d to %d has no way round within %d tiles: move waypoint %d",
                     unfixable + 1, to + 1, EDITOR_NPC_DETOUR_MARGIN, to + 1);
    }
    if( fixed == 0 )
    {
        if( unfixable < 0 )
            snprintf(line, sizeof(line), "every leg walks already: nothing to fix");
        bp_status(ui, self, line);
        return -1;
    }
    self->mv_sel = -1;
    if( !bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "fix stuck legs") )
        return -1;
    if( unfixable >= 0 )
    {
        char why[128];
        copy_text(why, sizeof(why), line);
        snprintf(line, sizeof(line), "%d waypoint%s added round %d leg%s; still stuck: %s", n - count,
                 n - count == 1 ? "" : "s", fixed, fixed == 1 ? "" : "s", why);
    }
    else
        snprintf(line, sizeof(line), "%d waypoint%s added round %d stuck leg%s; the route now has %d", n - count,
                 n - count == 1 ? "" : "s", fixed, fixed == 1 ? "" : "s", n);
    bp_status(ui, self, line);
    return n - count;
}

static int
bp_mv_activate(
    struct EditorUi* ui,
    struct builder_panel* self,
    int widget)
{
    struct ToriRSChrome* chrome = ui->chrome;
    char value[32];

    if( widget == self->w_mv_tool_select || widget == self->w_mv_tool_add || widget == self->w_mv_tool_move )
    {
        bp_mv_set_tool(ui, self, widget == self->w_mv_tool_add ? 1 : widget == self->w_mv_tool_move ? 2 : 0);
        return 1;
    }
    if( widget == self->w_mv_wp_move )
    {
        struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
        int const n = bp_mv_route_copy(self, route);
        if( self->mv_sel < 0 || self->mv_sel >= n )
            bp_status(ui, self, "select the waypoint to move (a row of the route list) first");
        else if( !ui->tools.tile_valid )
            bp_status(ui, self, "latch a tile in the World first (the Select tool): the waypoint moves there");
        else
        {
            route[self->mv_sel].x = ui->tools.tile_x;
            route[self->mv_sel].z = ui->tools.tile_z;
            route[self->mv_sel].level = ui->tools.tile_level;
            (void)bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "move waypoint");
        }
        return 1;
    }
    if( widget == self->w_mv_split || widget == self->w_mv_newtype )
    {
        bp_mv_make_own_type(ui, self);
        return 1;
    }
    if( widget == self->w_mv_fix )
    {
        (void)bp_mv_fix_legs(ui, self);
        return 1;
    }
    if( widget == self->w_mv_fit )
    {
        bp_mv_fit(self);
        bp_mv_readout(ui, self);
        return 1;
    }
    if( widget == self->w_mv_goto || widget == self->w_sp_goto )
    {
        int x = 0;
        int z = 0;
        int level = 0;
        int have = 0;
        if( widget == self->w_mv_goto && self->mv_sel >= 0 && self->mv_sel < self->mv.patrol_count )
        {
            x = self->mv.patrol[self->mv_sel].x;
            z = self->mv.patrol[self->mv_sel].z;
            level = self->mv.patrol[self->mv_sel].level;
            have = 1;
        }
        else if( self->sp_count > 0 )
        {
            int const i = self->sp_sel >= 0 ? self->sp_sel : 0;
            x = self->sp[i].x;
            z = self->sp[i].z;
            level = self->sp[i].level;
            have = 1;
        }
        if( !have )
            bp_status(ui, self, "nothing to go to: no waypoint selected, no spawn");
        else if( !self->services.goto_tile )
            bp_status(ui, self, "no World camera to move here");
        else if( !self->services.goto_tile(self->services.user_data, x, z, level) )
            bp_status(ui, self, "the tile's square is not loaded: opening it; Go to again when it is");
        else
        {
            self->mv_cx = x;
            self->mv_cz = z;
            self->mv_level = level;
        }
        return 1;
    }
    if( widget == self->w_mv_mode_none || widget == self->w_mv_mode_patrol || widget == self->w_mv_mode_wander )
    {
        enum Editor_NpcMode const mode = widget == self->w_mv_mode_wander ? EDITOR_NPC_MODE_WANDER
                                         : widget == self->w_mv_mode_patrol ? EDITOR_NPC_MODE_PATROL
                                                                            : EDITOR_NPC_MODE_NONE;
        if( !self->mv_valid )
            return 1;
        if( mode != EDITOR_NPC_MODE_PATROL && self->mv.patrol_count > 0 )
        {
            /* Wander or None over a route: the rows go with the mode, as one
             * step (a route with defaultmode=wander is still a patrol, 1.1). */
            (void)bp_mv_write_route(ui, self, NULL, 0, mode, mode == EDITOR_NPC_MODE_WANDER ? "mode wander" : "mode none");
            return 1;
        }
        if( mode == EDITOR_NPC_MODE_PATROL && self->mv.patrol_count == 0 )
        {
            /* Patrol with nothing to walk: the route is what makes it one. */
            bp_mv_refresh(ui, self);
            (void)bp_mv_set_tool(ui, self, 1);
            bp_status(ui, self, "Patrol: click tiles in the World to lay its route (Add waypoint is armed)");
            return 1;
        }
        (void)bp_mv_write_field(ui, self, "defaultmode", k_mode_words[mode], "defaultmode");
        return 1;
    }
    if( widget == self->w_mv_leash || widget == self->w_mv_range )
    {
        const char* text = ToriRSChrome_Text(chrome, widget);
        const char* key = widget == self->w_mv_leash ? "maxrange" : "wanderrange";
        char* end = NULL;
        long n;
        if( !text || !text[0] )
        {
            (void)bp_mv_write_field(ui, self, key, "", key);
            return 1;
        }
        n = strtol(text, &end, 10);
        if( !end || *end || n < 0 || n > 64 )
        {
            bp_status(ui, self, "a range is a whole number of tiles, 0..64");
            return 1;
        }
        snprintf(value, sizeof(value), "%ld", n);
        (void)bp_mv_write_field(ui, self, key, value, key);
        return 1;
    }
    if( widget == self->w_mv_restrict || widget == self->w_mv_block || widget == self->w_mv_facing )
    {
        int const at = ToriRSChrome_DropdownSelected(chrome, widget);
        const char* key = widget == self->w_mv_restrict ? "moverestrict" : widget == self->w_mv_block ? "blockwalk" : "facing";
        const char* word = "";
        if( at > 0 )
            word = widget == self->w_mv_restrict ? k_restrict_options[at]
                   : widget == self->w_mv_block ? k_block_options[at]
                                                : k_facing_options[at];
        (void)bp_mv_write_field(ui, self, key, word, key);
        return 1;
    }
    if( widget == self->w_mv_respawn || widget == self->w_mv_timer )
    {
        const char* text = ToriRSChrome_Text(chrome, widget);
        const char* key = widget == self->w_mv_respawn ? "respawnrate" : "timer";
        char* end = NULL;
        long n;
        if( !text || !text[0] )
        {
            (void)bp_mv_write_field(ui, self, key, "", key);
            return 1;
        }
        n = strtol(text, &end, 10);
        if( !end || *end || n < 0 || n > 65535 )
        {
            bp_status(ui, self, "a tick count is a whole number 0..65535");
            return 1;
        }
        snprintf(value, sizeof(value), "%ld", n);
        (void)bp_mv_write_field(ui, self, key, value, key);
        return 1;
    }
    if( widget == self->w_mv_route )
    {
        self->mv_sel = ToriRSChrome_VListSelected(chrome, self->w_mv_route);
        if( self->mv_sel >= self->mv.patrol_count )
            self->mv_sel = -1;
        if( self->mv_sel >= 0 )
        {
            snprintf(value, sizeof(value), "%d", self->mv.patrol[self->mv_sel].pause);
            ToriRSChrome_SetText(chrome, self->w_mv_pause, value);
        }
        bp_mv_route_rows(ui, self);
        return 1;
    }
    if( widget == self->w_mv_pause )
    {
        struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
        int const n = bp_mv_route_copy(self, route);
        const char* text = ToriRSChrome_Text(chrome, widget);
        char* end = NULL;
        long pause;
        if( self->mv_sel < 0 || self->mv_sel >= n )
        {
            bp_status(ui, self, "select a waypoint in the route list first");
            return 1;
        }
        pause = strtol(text ? text : "", &end, 10);
        if( !text || !text[0] || !end || *end || pause < 0 || pause > 65535 )
        {
            bp_status(ui, self, "a pause is a whole number of ticks");
            return 1;
        }
        route[self->mv_sel].pause = (int)pause;
        (void)bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "waypoint pause");
        return 1;
    }
    if( widget == self->w_mv_wp_add )
    {
        if( !ui->tools.tile_valid )
        {
            bp_status(ui, self, "latch a tile in the World first (the Select tool), or use Add waypoint and click");
            return 1;
        }
        bp_mv_add_waypoint(ui, self, ui->tools.tile_x, ui->tools.tile_z, ui->tools.tile_level, self->mv_sel);
        return 1;
    }
    if( widget == self->w_mv_wp_insert )
    {
        struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
        int const n = bp_mv_route_copy(self, route);
        if( self->mv_sel < 0 || self->mv_sel >= n )
        {
            bp_status(ui, self, "select the waypoint to insert after");
            return 1;
        }
        {
            const struct Editor_NpcWaypoint* a = &route[self->mv_sel];
            const struct Editor_NpcWaypoint* b = &route[(self->mv_sel + 1) % n];
            bp_mv_add_waypoint(ui, self, (a->x + b->x) / 2, (a->z + b->z) / 2, a->level, self->mv_sel);
        }
        return 1;
    }
    if( widget == self->w_mv_wp_delete || widget == self->w_mv_wp_up || widget == self->w_mv_wp_down )
    {
        struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
        int const n = bp_mv_route_copy(self, route);
        int const at = self->mv_sel;
        if( at < 0 || at >= n )
        {
            bp_status(ui, self, "select a waypoint in the route list first");
            return 1;
        }
        if( widget == self->w_mv_wp_delete )
        {
            memmove(&route[at], &route[at + 1], sizeof(route[0]) * (size_t)(n - at - 1));
            if( bp_mv_write_route(ui, self, route, n - 1, EDITOR_NPC_MODE_PATROL, "delete waypoint") )
                self->mv_sel = at < n - 1 ? at : n - 2;
        }
        else
        {
            int const to = at + (widget == self->w_mv_wp_up ? -1 : 1);
            struct Editor_NpcWaypoint swap;
            if( to < 0 || to >= n )
                return 1;
            swap = route[at];
            route[at] = route[to];
            route[to] = swap;
            if( bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "reorder route") )
                self->mv_sel = to;
        }
        bp_mv_route_rows(ui, self);
        return 1;
    }
    if( widget == self->w_mv_play )
    {
        bp_mv_play(ui, self, !self->mv_play);
        return 1;
    }
    /* The Spawns tab. */
    if( widget == self->w_sp_list )
    {
        self->sp_sel = ToriRSChrome_VListSelected(chrome, self->w_sp_list);
        if( self->sp_sel >= self->sp_count )
            self->sp_sel = -1;
        bp_mv_spawn_rows(ui, self);
        return 1;
    }
    if( widget == self->w_sp_select )
    {
        if( self->sp_sel < 0 || self->sp_sel >= self->sp_count )
            bp_status(ui, self, "select a spawn in the list first");
        else if( !self->services.spawn_select || !self->services.spawn_select(self->services.user_data, &self->sp[self->sp_sel]) )
            bp_status(ui, self, "no Spawns panel here to select it in");
        return 1;
    }
    if( widget == self->w_sp_place )
    {
        if( !self->has_model )
            return 1;
        if( !ui->tools.tile_valid )
            bp_status(ui, self, "latch a tile in the World first (the select tool)");
        else if( !self->services.spawn_place
                 || self->services.spawn_place(self->services.user_data, self->model->name, ui->tools.tile_x,
                                               ui->tools.tile_z, ui->tools.tile_level)
                        <= 0 )
            bp_status(ui, self, "the spawn was not placed (see the Console)");
        else
        {
            bp_mv_refresh(ui, self);
            self->sp_sel = self->sp_count - 1;
            bp_mv_spawn_rows(ui, self);
        }
        return 1;
    }
    if( widget == self->w_sp_delete )
    {
        if( self->sp_sel < 0 || self->sp_sel >= self->sp_count )
            bp_status(ui, self, "select a spawn in the list first");
        else if( !self->services.spawn_delete
                 || self->services.spawn_delete(self->services.user_data, &self->sp[self->sp_sel]) <= 0 )
            bp_status(ui, self, "the spawn was not deleted (see the Console)");
        else
            bp_mv_refresh(ui, self);
        return 1;
    }
    return 0;
}

static int
bp_mv_event(
    struct EditorUi* ui,
    struct builder_panel* self,
    const struct ToriRSChromeEvent* event)
{
    int tx;
    int tz;

    switch( event->kind )
    {
    case TORIRS_CHROME_EVENT_GRAPH_HOVER:
        if( event->key >= BP_MV_KEY_TILE && event->key < BP_MV_KEY_WP )
        {
            int const index = event->key - BP_MV_KEY_TILE;
            int const cols = bp_mv_cols(self);
            self->mv_hover_x = self->mv_cx - cols / 2 + index % cols;
            self->mv_hover_z = self->mv_cz + bp_mv_rows(self) / 2 - index / cols;
            self->mv_hover_valid = 1;
        }
        else if( event->key >= BP_MV_KEY_WP && event->key < BP_MV_KEY_SPAWN
                 && event->key - BP_MV_KEY_WP < self->mv.patrol_count )
        {
            self->mv_hover_x = self->mv.patrol[event->key - BP_MV_KEY_WP].x;
            self->mv_hover_z = self->mv.patrol[event->key - BP_MV_KEY_WP].z;
            self->mv_hover_valid = 1;
        }
        else if( event->key >= BP_MV_KEY_SPAWN && event->key - BP_MV_KEY_SPAWN < self->sp_count )
        {
            self->mv_hover_x = self->sp[event->key - BP_MV_KEY_SPAWN].x;
            self->mv_hover_z = self->sp[event->key - BP_MV_KEY_SPAWN].z;
            self->mv_hover_valid = 1;
        }
        else
            self->mv_hover_valid = 0;
        bp_mv_readout(ui, self);
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_WHEEL:
    {
        int const px = self->mv_px;
        int ux;
        int uz;
        bp_mv_tile_at(self, event->x, event->y, &ux, &uz);
        if( event->value < 0 && px < BP_MV_PX_MAX )
            self->mv_px = px * 2;
        else if( event->value > 0 && px > BP_MV_PX_MIN )
            self->mv_px = px / 2;
        else
            return 1;
        /* The tile under the pointer stays under it. */
        {
            int nx;
            int nz;
            bp_mv_tile_at(self, event->x, event->y, &nx, &nz);
            self->mv_cx += ux - nx;
            self->mv_cz += uz - nz;
        }
        bp_mv_readout(ui, self);
        return 1;
    }
    case TORIRS_CHROME_EVENT_GRAPH_PRESS:
        self->mv_press_x = event->x;
        self->mv_press_y = event->y;
        self->mv_press_cx = self->mv_cx;
        self->mv_press_cz = self->mv_cz;
        self->mv_panning = 0;
        if( event->key >= BP_MV_KEY_WP && event->key < BP_MV_KEY_SPAWN
            && event->key - BP_MV_KEY_WP < self->mv.patrol_count )
        {
            self->mv_sel = event->key - BP_MV_KEY_WP;
            if( self->mv_tool == 2 && !self->read_only )
            {
                self->mv_drag_wp = self->mv_sel;
                self->mv_drag_x = self->mv.patrol[self->mv_sel].x;
                self->mv_drag_z = self->mv.patrol[self->mv_sel].z;
            }
            bp_mv_route_rows(ui, self);
            {
                char value[16];
                snprintf(value, sizeof(value), "%d", self->mv.patrol[self->mv_sel].pause);
                ToriRSChrome_SetText(ui->chrome, self->w_mv_pause, value);
            }
            return 1;
        }
        if( event->key >= BP_MV_KEY_SPAWN && event->key - BP_MV_KEY_SPAWN < self->sp_count )
        {
            self->sp_sel = event->key - BP_MV_KEY_SPAWN;
            bp_mv_spawn_rows(ui, self);
            return 1;
        }
        bp_mv_tile_at(self, event->x, event->y, &tx, &tz);
        if( self->mv_tool == 1 )
        {
            if( !self->has_model )
                bp_status(ui, self, "open an npc first");
            else
                bp_mv_add_waypoint(ui, self, tx, tz, self->mv_level, self->mv_sel);
            return 1;
        }
        self->mv_panning = 1;
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_DRAG:
        if( self->mv_drag_wp >= 0 )
        {
            bp_mv_tile_at(self, event->x, event->y, &tx, &tz);
            self->mv_drag_x = tx;
            self->mv_drag_z = tz;
            return 1;
        }
        if( self->mv_panning )
        {
            self->mv_cx = self->mv_press_cx - (event->x - self->mv_press_x) / self->mv_px;
            self->mv_cz = self->mv_press_cz + (event->y - self->mv_press_y) / self->mv_px;
            bp_mv_readout(ui, self);
        }
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_RELEASE:
        if( self->mv_drag_wp >= 0 )
        {
            struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
            int const n = bp_mv_route_copy(self, route);
            int const at = self->mv_drag_wp;
            bp_mv_tile_at(self, event->x, event->y, &tx, &tz);
            self->mv_drag_wp = -1;
            if( at < n && (route[at].x != tx || route[at].z != tz) )
            {
                route[at].x = tx;
                route[at].z = tz;
                (void)bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "move waypoint");
            }
            return 1;
        }
        self->mv_panning = 0;
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_KEY:
        if( event->value == 'f' || event->value == 'F' )
        {
            bp_mv_fit(self);
            bp_mv_readout(ui, self);
        }
        else if( (event->value == 'n' || event->value == 'N') && self->mv_hover_valid && self->has_model )
            bp_mv_add_waypoint(ui, self, self->mv_hover_x, self->mv_hover_z, self->mv_level, self->mv_sel);
        else if( event->value == '+' || event->value == '=' || event->value == '-' )
        {
            if( (event->value == '-') ? self->mv_px > BP_MV_PX_MIN : self->mv_px < BP_MV_PX_MAX )
                self->mv_px = event->value == '-' ? self->mv_px / 2 : self->mv_px * 2;
        }
        return 1;
    default:
        return 1;
    }
}

static void
bp_mv_tick(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int const tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->p_main);

    self->mv_frame++;
    /* The spawn files open asynchronously: ask again twice a second until
     * the rows come, and after any edit that moved the facts. */
    if( self->has_model && self->services.spawns && (self->mv_frame % 30) == 0 )
    {
        struct Builder_NpcSpawn fresh[BP_MV_SPAWNS_MAX];
        int const n = self->services.spawns(self->services.user_data, self->model->name, self->record.id, fresh,
                                            BP_MV_SPAWNS_MAX);
        if( n != self->sp_count || memcmp(fresh, self->sp, sizeof(fresh[0]) * (size_t)n) != 0 )
        {
            memcpy(self->sp, fresh, sizeof(fresh[0]) * (size_t)n);
            self->sp_count = n;
            if( self->sp_sel >= n )
                self->sp_sel = -1;
            bp_mv_spawn_rows(ui, self);
            /* The leash note and the sharing line read the spawns. */
            bp_mv_refresh(ui, self);
            if( self->mv.patrol_count == 0 )
                bp_mv_fit(self);
            self->mv_draw_hash = 0;
        }
        /* A spawn clicked in the World is the one Make own type would split
         * off: the line above the route follows the selection. */
        bp_mv_share_refresh(ui, self);
    }
    /* The documents moved under us (an undo, the Record panel, a spawn
     * edit): the controls follow. */
    if( self->has_model && ui->editor && self->mv_legs_hash != 0
        && (bp_mv_hash_route(self) ^ (uint32_t)ui->editor->last_cmd_seq) != self->mv_legs_hash )
    {
        bp_mv_refresh(ui, self);
        /* The header's "will:" line too: after an Undo it still counted the
         * waypoints the undone edit had made. */
        bp_show_will(ui, self);
    }
    /* A leg the scene could not walk (its square was still loading): ask
     * again once a second, so "not walked" does not outlive the load. */
    if( self->has_model && (self->mv_frame % 60) == 0 )
        for( int i = 0; i < self->mv.patrol_count; i++ )
            if( self->mv_legs[i] == -2 )
            {
                self->mv_legs_hash = 0;
                bp_mv_legs(ui, self);
                bp_mv_route_rows(ui, self);
                break;
            }
    /* Play walks at the server's pace: a tile a tick, by the clock (a frame
     * count ran it faster on a fast machine). A route that shrank under it
     * (an undo) stops it. */
    if( self->mv_play && self->mv.patrol_count < 2 )
        bp_mv_play(ui, self, 0);
    if( self->mv_play && ui->chrome->now_ms - self->mv_play_ms >= BP_MV_PLAY_TICK_MS )
    {
        self->mv_play_ms = ui->chrome->now_ms;
        bp_mv_play_tick(ui, self);
    }
    /* The World's pointer left (or its button was let go off the World):
     * the carried waypoint is put back, the next-waypoint ghost goes. */
    if( self->mv_whover_frame != 0 && self->mv_frame - self->mv_whover_frame == 3 )
    {
        self->mv_wdrag_wp = -1;
        self->mv_wpress = 0;
        if( ui->app_services.request_redraw )
            ui->app_services.request_redraw(ui->app_services.user_data);
    }
    if( tab != BUILDER_NPC_TAB_MOVEMENT )
    {
        if( tab == BUILDER_NPC_TAB_SPAWNS && ToriRSChrome_VListNeedsRows(ui->chrome, self->w_sp_list) )
            bp_mv_spawn_rows(ui, self);
        return;
    }
    if( ToriRSChrome_VListNeedsRows(ui->chrome, self->w_mv_route) )
        bp_mv_route_rows(ui, self);
    bp_mv_redraw(ui, self);
}

/* ---- the Movement tab's test surface ---- */

int
Builder_RouteInspect(
    struct EditorUi* ui,
    struct Builder_RouteView* out)
{
    struct builder_panel* self;

    assert(ui);
    assert(out);
    memset(out, 0, sizeof(*out));
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 )
        return 0;
    out->waypoints = self->mv.patrol;
    out->count = self->mv.patrol_count;
    out->selected = self->mv_sel;
    out->legs = self->mv_legs;
    out->effective = &self->mv_eff;
    out->target = self->mv_target;
    out->w_minimap = self->w_mv_map;
    out->tool = self->mv_tool;
    return 1;
}

int
Builder_RouteWaypointBox(
    struct EditorUi* ui,
    int index,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    struct builder_panel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 || index < 0 || index >= self->mv.patrol_count
        || !bp_mv_tile_in_view(self, self->mv.patrol[index].x, self->mv.patrol[index].z) )
        return 0;
    bp_mv_tile_point(self, self->mv.patrol[index].x, self->mv.patrol[index].z, out_x, out_y);
    *out_w = self->mv_px;
    *out_h = self->mv_px;
    return 1;
}

int
Builder_RouteTilePoint(
    struct EditorUi* ui,
    int x,
    int z,
    int* out_x,
    int* out_y)
{
    struct builder_panel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 || !bp_mv_tile_in_view(self, x, z) )
        return 0;
    bp_mv_tile_point(self, x, z, out_x, out_y);
    *out_x += self->mv_px / 2;
    *out_y += self->mv_px / 2;
    return 1;
}

/* ---- the route in the World --------------------------------------------- */

/** The NPC editor when its Movement tab is showing an npc; NULL otherwise. */
static struct builder_panel*
bp_mv_showing(struct EditorUi* ui)
{
    struct builder_panel* self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);

    if( !self || self->w_mv_map < 0 || !self->has_model || !ui->visible || !ui->chrome->panels[self->p_main].visible
        || ToriRSChrome_PanelActiveTab(ui->chrome, self->p_main) != BUILDER_NPC_TAB_MOVEMENT )
        return NULL;
    return self;
}

/** A route action run from a key or the palette: the panel and its
 *  Movement tab come up first, so the tool that is armed is on screen (and
 *  an npc is opened from the selection as View > NPC editor does). */
static void
bp_mv_bring_up(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    if( !ui->chrome->panels[self->p_main].visible )
        (void)Editor_UiTogglePanel(ui, BUILDER_PANEL_NPC);
    ToriRSChrome_PanelSetActiveTab(ui->chrome, self->p_main, BUILDER_NPC_TAB_MOVEMENT);
    /* Now, not on the panel's next tick: the action that follows needs it. */
    bp_open_selection(ui, self);
    self->mv_was_visible = 1;
}

int
Builder_RouteArmed(struct EditorUi* ui)
{
    struct builder_panel* self;

    assert(ui);
    self = bp_mv_showing(ui);
    if( !self || !self->mv_valid || self->read_only )
        return 0;
    return self->mv_tool;
}

int
Builder_RouteSetTool(
    struct EditorUi* ui,
    int tool)
{
    struct builder_panel* self;

    assert(ui);
    assert(tool >= 0);
    assert(tool <= 2);
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 )
        return 0;
    bp_mv_bring_up(ui, self);
    /* The same press twice puts the tool down (a toggle on a key). */
    return bp_mv_set_tool(ui, self, tool == self->mv_tool ? 0 : tool) == tool;
}

int
Builder_RoutePlay(struct EditorUi* ui)
{
    struct builder_panel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 )
        return 0;
    bp_mv_bring_up(ui, self);
    if( !self->has_model )
    {
        bp_status(ui, self, "Play refused: open an npc with a route first");
        return 0;
    }
    bp_mv_play(ui, self, !self->mv_play);
    return self->mv_play;
}

int
Builder_RouteFixLegs(struct EditorUi* ui)
{
    struct builder_panel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC);
    if( !self || self->w_mv_map < 0 )
        return -1;
    bp_mv_bring_up(ui, self);
    return bp_mv_fix_legs(ui, self);
}

/** The waypoint on tile (x, z, level); -1 none. */
static int
bp_mv_waypoint_at(
    const struct builder_panel* self,
    int x,
    int z,
    int level)
{
    for( int i = 0; i < self->mv.patrol_count; i++ )
        if( self->mv.patrol[i].x == x && self->mv.patrol[i].z == z && self->mv.patrol[i].level == level )
            return i;
    return -1;
}

int
Builder_RoutePointer(
    struct EditorUi* ui,
    int x,
    int z,
    int level,
    int pressed,
    int held,
    int released)
{
    struct builder_panel* self;
    int at;

    assert(ui);
    self = bp_mv_showing(ui);
    if( !self || !self->mv_valid || self->read_only || self->mv_tool == 0 )
        return 0;
    if( level < 0 )
        level = 0;
    if( level > 3 )
        level = 3;
    self->mv_whover_x = x;
    self->mv_whover_z = z;
    self->mv_whover_level = level;
    self->mv_whover_frame = self->mv_frame;
    at = bp_mv_waypoint_at(self, x, z, level);

    if( pressed )
    {
        self->mv_wdrag_wp = -1;
        self->mv_wdrag_moved = 0;
        self->mv_wpress = 0;
        if( at >= 0 )
        {
            /* On a waypoint, under either tool: it is selected, and carried
             * while the button is held. */
            char value[16];
            self->mv_sel = at;
            self->mv_wdrag_wp = at;
            snprintf(value, sizeof(value), "%d", self->mv.patrol[at].pause);
            ToriRSChrome_SetText(ui->chrome, self->w_mv_pause, value);
            bp_mv_route_rows(ui, self);
        }
        else
            self->mv_wpress = 1;
    }
    if( held && self->mv_wdrag_wp >= 0 && at != self->mv_wdrag_wp )
        self->mv_wdrag_moved = 1;
    if( released )
    {
        if( self->mv_wdrag_wp >= 0 )
        {
            struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
            int const n = bp_mv_route_copy(self, route);
            int const wp = self->mv_wdrag_wp;
            self->mv_wdrag_wp = -1;
            if( self->mv_wdrag_moved && wp < n && at < 0 )
            {
                route[wp].x = x;
                route[wp].z = z;
                route[wp].level = level;
                (void)bp_mv_write_route(ui, self, route, n, EDITOR_NPC_MODE_PATROL, "move waypoint");
            }
            else if( self->mv_wdrag_moved && at >= 0 && at != wp )
                bp_status(ui, self, "that tile is a waypoint already: drop it on another");
        }
        else if( self->mv_wpress && self->mv_tool == 1 )
            bp_mv_add_waypoint(ui, self, x, z, level, self->mv_sel);
        else if( self->mv_wpress )
            bp_status(ui, self, "Move waypoint: press ON a numbered waypoint and drag it (Add waypoint lays new ones)");
        self->mv_wpress = 0;
        self->mv_wdrag_moved = 0;
    }
    else if( !held && !pressed )
    {
        /* The button came up somewhere this was not told about. */
        self->mv_wdrag_wp = -1;
        self->mv_wpress = 0;
    }
    return 1;
}

static int
bp_mv_rect(
    struct MapEditorWanderRect* out,
    int n,
    int max,
    enum MapEditorWanderKind kind,
    int x0,
    int z0,
    int x1,
    int z1,
    int level,
    int index,
    int flags)
{
    if( n >= max )
        return n;
    memset(&out[n], 0, sizeof(out[n]));
    out[n].kind = kind;
    out[n].x0 = x0;
    out[n].z0 = z0;
    out[n].x1 = x1;
    out[n].z1 = z1;
    out[n].level = level;
    out[n].index = index;
    out[n].flags = flags;
    return n + 1;
}

/**
 * One leg as the World draws it: the tiles the walk steps onto as straight
 * runs (so the line turns where the npc turns), then what the walk did not
 * cover -- and where it stops dead, the marker it stands on and the jump the
 * server makes, in the teleport colour.
 */
static int
bp_mv_leg_rects(
    const struct Editor_NpcWaypoint* a,
    const struct Editor_NpcWaypoint* b,
    const struct Editor_NpcWalk* walk,
    const struct Editor_NpcWalkTile* tiles,
    int index,
    int flags,
    struct MapEditorWanderRect* out,
    int n,
    int max)
{
    int run_x = a->x;
    int run_z = a->z;
    int px = a->x;
    int pz = a->z;
    int dx = 0;
    int dz = 0;

    if( walk->result == EDITOR_NPC_WALK_TELEPORT )
        return bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, a->x, a->z, b->x, b->z, a->level, index,
                          flags | MAP_EDITOR_WANDER_FLAG_TELEPORT);
    for( int i = 0; i < walk->tile_count; i++ )
    {
        int const ndx = tiles[i].x - px;
        int const ndz = tiles[i].z - pz;
        if( i > 0 && (ndx != dx || ndz != dz) )
        {
            n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, run_x, run_z, px, pz, a->level, index, flags);
            run_x = px;
            run_z = pz;
        }
        dx = ndx;
        dz = ndz;
        px = tiles[i].x;
        pz = tiles[i].z;
    }
    if( px != run_x || pz != run_z )
        n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, run_x, run_z, px, pz, a->level, index, flags);
    /* Past the tiles kept (a long leg, or legs measured without a walk). */
    if( walk->result != EDITOR_NPC_WALK_STUCK )
    {
        if( px != b->x || pz != b->z )
            n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, px, pz, b->x, b->z, a->level, index, flags);
        return n;
    }
    if( px != walk->stuck_x || pz != walk->stuck_z )
        n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, px, pz, walk->stuck_x, walk->stuck_z, a->level, index, flags);
    n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_STALL, walk->stuck_x, walk->stuck_z, walk->stuck_x, walk->stuck_z,
                   a->level, index, flags);
    return bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_LEG, walk->stuck_x, walk->stuck_z, b->x, b->z, a->level, index,
                      flags | MAP_EDITOR_WANDER_FLAG_UNREACHABLE);
}

int
Builder_RouteWorldRects(
    struct EditorUi* ui,
    struct MapEditorWanderRect* out,
    int max)
{
    static struct Editor_NpcWalkTile fresh_tiles[BP_MV_WALK_MAX];
    struct builder_panel* self;
    struct Editor_NpcWaypoint route[EDITOR_NPC_PATROL_MAX];
    int count;
    int size;
    int hovering;
    int carried = -1;
    int n = 0;

    assert(ui);
    assert(out || max == 0);
    self = bp_mv_showing(ui);
    if( !self || !self->mv_valid )
        return 0;
    count = self->mv.patrol_count;
    size = self->mv.size > 0 ? self->mv.size : 1;
    hovering = self->mv_tool != 0 && self->mv_whover_frame != 0 && self->mv_frame - self->mv_whover_frame <= 2;
    memcpy(route, self->mv.patrol, sizeof(route[0]) * (size_t)count);
    /* A waypoint being carried is drawn where the pointer has it, and its
     * two legs are walked again there: the drop is judged before it is made. */
    if( hovering && self->mv_wdrag_wp >= 0 && self->mv_wdrag_wp < count && self->mv_wdrag_moved )
    {
        carried = self->mv_wdrag_wp;
        route[carried].x = self->mv_whover_x;
        route[carried].z = self->mv_whover_z;
        route[carried].level = self->mv_whover_level;
    }
    for( int i = 0; i < count && count >= 2; i++ )
    {
        const struct Editor_NpcWaypoint* a = &route[i];
        const struct Editor_NpcWaypoint* b = &route[(i + 1) % count];
        int const flags = i + 1 == count ? MAP_EDITOR_WANDER_FLAG_CLOSING : 0;
        if( a->level < 0 || b->level < 0 )
            continue;
        if( carried >= 0 && (i == carried || (i + 1) % count == carried) && self->services.can_step )
        {
            struct Editor_NpcWalk walk;
            (void)Editor_NpcWalkLeg(a, b, size, self->services.can_step, self->services.user_data, fresh_tiles,
                                    BP_MV_WALK_MAX, &walk);
            n = bp_mv_leg_rects(a, b, &walk, fresh_tiles, i + 1, flags | MAP_EDITOR_WANDER_FLAG_PREVIEW, out, n, max);
        }
        else
            n = bp_mv_leg_rects(a, b, &self->mv_walk[i], self->mv_walk_tiles[i], i + 1, flags, out, n, max);
    }
    for( int i = 0; i < count; i++ )
        if( route[i].level >= 0 )
            n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_WAYPOINT, route[i].x, route[i].z, route[i].x, route[i].z,
                           route[i].level, i + 1,
                           (i == self->mv_sel ? MAP_EDITOR_WANDER_FLAG_SELECTED : 0)
                               | (i == carried ? MAP_EDITOR_WANDER_FLAG_PREVIEW : 0));
    /* Add waypoint: the tile under the pointer as the waypoint a click would
     * lay, and the leg to it walked, so a leg that will stick shows red
     * before the click. */
    if( hovering && self->mv_tool == 1 && carried < 0 && count < EDITOR_NPC_PATROL_MAX
        && bp_mv_waypoint_at(self, self->mv_whover_x, self->mv_whover_z, self->mv_whover_level) < 0 )
    {
        int const after = count == 0 ? -1 : self->mv_sel >= 0 && self->mv_sel < count ? self->mv_sel : count - 1;
        struct Editor_NpcWaypoint next;
        memset(&next, 0, sizeof(next));
        next.x = self->mv_whover_x;
        next.z = self->mv_whover_z;
        next.level = self->mv_whover_level;
        if( after >= 0 && route[after].level >= 0 && self->services.can_step )
        {
            struct Editor_NpcWalk walk;
            (void)Editor_NpcWalkLeg(&route[after], &next, size, self->services.can_step, self->services.user_data,
                                    fresh_tiles, BP_MV_WALK_MAX, &walk);
            n = bp_mv_leg_rects(&route[after], &next, &walk, fresh_tiles, after + 1, MAP_EDITOR_WANDER_FLAG_PREVIEW, out, n,
                                max);
        }
        n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_WAYPOINT, next.x, next.z, next.x, next.z, next.level, after + 2,
                       MAP_EDITOR_WANDER_FLAG_PREVIEW);
    }
    /* Play's walker, over everything. */
    {
        int gx;
        int gz;
        if( bp_mv_ghost_tile(self, &gx, &gz) )
            n = bp_mv_rect(out, n, max, MAP_EDITOR_WANDER_WALKER, gx, gz, gx + size - 1, gz + size - 1,
                           self->mv.patrol[self->mv_play_leg % count].level, 0,
                           self->mv_play_stuck ? MAP_EDITOR_WANDER_FLAG_UNREACHABLE : 0);
    }
    return n;
}

/* ---- the scripting surface ------------------------------------------------ */

static struct builder_panel*
find_builder(
    struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);
    if( strcmp(name, BUILDER_PANEL_LOC) != 0 && strcmp(name, BUILDER_PANEL_NPC) != 0 )
        return NULL;
    return Editor_UiPanelSelf(ui, name);
}

int
Builder_PanelInspect(
    struct EditorUi* ui,
    const char* name,
    struct Builder_PanelView* out)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(out);
    memset(out, 0, sizeof(*out));
    if( !self )
        return 0;
    out->model = self->has_model ? self->model : NULL;
    out->read_only = self->read_only;
    out->issues = &self->issues;
    out->check = self->check;
    out->status = self->status;
    out->lint = &self->lint;
    out->sel_node = self->sel_node;
    out->sel_page = self->sel_page;
    out->layout = &self->layout;
    out->chips_open = self->chips_open;
    out->w_graph = self->w_graph;
    out->w_pages = self->w_pages;
    out->w_preview = self->w_preview;
    return 1;
}

int
Builder_PanelOpenSubject(
    struct EditorUi* ui,
    const char* name,
    const char* subject)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(subject);
    if( !self )
        return 0;
    ToriRSChrome_SetText(ui->chrome, self->w_subject, subject);
    return bp_open(ui, self, subject);
}

int
Builder_PanelSetModel(
    struct EditorUi* ui,
    const char* name,
    const struct Builder_Model* model)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(model);
    if( !self || !self->has_model || model->subject != self->subject
        || strcmp(model->name, self->model->name) != 0 )
        return 0;
    *self->model = *model;
    return bp_commit(ui, self);
}

int
Builder_PanelCheck(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_check(ui, self) : 0;
}

int
Builder_PanelSaveAll(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_save(ui, self) : 0;
}

int
Builder_PanelDetach(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_detach(ui, self) : 0;
}

/* ---- the dialogue graph's hotkeys and the test surface ---------------------- */

void
Builder_ChatMetricsBaked(struct Builder_ChatMetrics* metrics)
{
    assert(metrics);
    memset(metrics, 0, sizeof(*metrics));
    /* p12_full at 1x, as the chrome bakes it (archive 495: the Body face). */
    for( int c = 0; c < 256; c++ )
        metrics->advance[c] = ToriRSChrome_GlyphAdvanceQ(TORIRS_CHROME_FONT_BODY, TORIRS_CHROME_SCALE_Q_ONE,
                                                         (unsigned char)c);
    metrics->width = BUILDER_CHAT_WIDTH_DEFAULT;
    metrics->page_lines = BUILDER_CHAT_PAGE_LINES_DEFAULT;
    metrics->player = BUILDER_CHAT_PLAYER_STANDIN;
}

int
Builder_GraphHotkey(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    struct builder_panel* self;
    int focused;

    assert(ui);
    assert(ui->chrome);
    self = find_builder(ui, BUILDER_PANEL_NPC);
    focused = self && self->w_graph >= 0 && ui->chrome->focus == self->w_graph;
    if( action == EDITOR_HK_EDIT_DELETE )
    {
        /* Delete is the graph's only while its well holds the keyboard;
         * anywhere else it is the World's. */
        if( !focused )
            return 0;
        (void)bp_delete_node(ui, self);
        return 1;
    }
    if( action != EDITOR_HK_GRAPH_AUTO_LAYOUT && action != EDITOR_HK_GRAPH_NEW_NODE )
        return 0;
    if( !self )
    {
        Editor_UiStatus(ui, "%s refused: this window has no NPC builder",
                        action == EDITOR_HK_GRAPH_AUTO_LAYOUT ? "Auto-layout" : "New node");
        return 1;
    }
    if( !focused && (!ui->chrome->panels[self->p_main].visible || !ui->visible) )
    {
        Editor_UiStatus(ui, "%s refused: no dialogue graph is open (View > NPC editor, Dialogue, then click its graph)",
                        action == EDITOR_HK_GRAPH_AUTO_LAYOUT ? "Auto-layout" : "New node");
        return 1;
    }
    if( action == EDITOR_HK_GRAPH_AUTO_LAYOUT )
        (void)bp_auto_layout(ui, self);
    else
    {
        int const kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_node_kind);
        int x = 0;
        int y = 0;
        if( !bp_graph_editable(ui, self, "New node") )
            return 1;
        bp_new_node_spot(self, &x, &y);
        (void)bp_new_node(ui, self,
                          kind >= 0 && kind < BUILDER_NODE_KIND_COUNT ? (enum Builder_NodeKind)kind : BUILDER_NODE_NPC,
                          NULL, 0, x, y);
    }
    return 1;
}

int
Builder_GraphFocused(struct EditorUi* ui)
{
    struct builder_panel* self;

    assert(ui);
    assert(ui->chrome);
    self = find_builder(ui, BUILDER_PANEL_NPC);
    return self && self->w_graph >= 0 && ui->chrome->focus == self->w_graph;
}

int
Builder_GraphKeys(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* in,
    struct Editor_HotkeyFrame* out)
{
    unsigned held_mods;

    assert(ui);
    assert(in);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !Builder_GraphFocused(ui) )
        return 0;
    held_mods = Editor_HotkeyHeldMods(in);
    for( int k = TORIRSK_UNKNOWN + 1; k < TORIRSK_COUNT && out->count < EDITOR_HK_FIRED_MAX; k++ )
    {
        struct Editor_Chord chord;
        int action;

        if( !in->down[k] || Editor_HotkeyKeyIsModifier(k) )
            continue;
        memset(&chord, 0, sizeof(chord));
        chord.key = (uint8_t)k;
        chord.mods = (uint8_t)held_mods;
        /* Only the graph's own rows: G and every other context stay shut
         * while the well holds the keyboard, as they do for a text field. */
        action = Editor_HotkeysMatch(&ui->hotkeys, chord, EDITOR_HK_CTX_Q);
        if( action < 0 )
            continue;
        out->fired[out->count] = action;
        out->fired_chord[out->count] = chord;
        out->count++;
        out->claimed[k] = 1;
        out->consumed[k] = 1;
        (void)Builder_GraphHotkey(ui, (enum Editor_HotkeyAction)action);
    }
    return out->count;
}

int
Builder_PanelSelectNode(
    struct EditorUi* ui,
    int index)
{
    struct builder_panel* self = find_builder(ui, BUILDER_PANEL_NPC);

    if( !self || !self->has_model )
        return 0;
    bp_select_node(ui, self, index);
    return 1;
}

int
Builder_PanelOpenLint(
    struct EditorUi* ui,
    int row)
{
    struct builder_panel* self = find_builder(ui, BUILDER_PANEL_NPC);

    if( !self || !self->has_model || row < 0 || row >= self->lint_shown_count )
        return 0;
    bp_open_lint(ui, self, self->lint_shown[row]);
    return 1;
}
