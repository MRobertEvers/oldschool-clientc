#ifndef SRC_PLUGIN_PLUGIN_PANEL_H
#define SRC_PLUGIN_PLUGIN_PANEL_H

/*
 * The plugin window's state: its own chrome instance and the shell, sync and
 * rail that present it, the executor it is bound to, the rows the current page
 * built, the choice strings its dropdowns borrow, and the stamps that say what
 * the page was built against.
 *
 * It lives on the heap, in the PLUGINS group, and exists for as long as the
 * App does: PluginPanel_Create hands back a zeroed record with no chrome panel
 * built (`panel` -1), and PluginPanel_Free releases it with the custom-well
 * pixels it grew.
 *
 * This decides the row table: where a tracked widget's row goes, which row a
 * panel-model slot or a chrome widget maps to, the compact list of the page's
 * CUSTOM rows and how many of them still owe a present. It decides the choice
 * pool: how a `"a|b|c"` list is split into the strings a dropdown borrows, each
 * cut at 47 bytes, and when the pool is full. And it answers whether the page
 * is still built for a plugin count, window revision, selection generation,
 * registry revision and model revision, and records those after a build.
 *
 * Nothing here reads the plugin host, draws or builds chrome, routes input or
 * touches the UI tree, and nothing prints. The caller asks the host for the
 * counts and revisions it compares, decides which widget a row tracks, resets
 * the choice pool together with the rows whose widgets point into it, and does
 * all of the chrome, executor and drawing work through the fields below.
 */

#include "plugin/torirs_plugin_host_types.h"
#include "plugin/torirs_plugin_types.h"
#include "ui/torirs_chrome_exec.h"
#include "ui/torirs_chrome_shell.h"
#include "ui/uitree_debug_overlay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Enable checkbox plus config rows, across every plugin the panel shows. */
#define PLUGIN_PANEL_ROWS_MAX 320
/** Choice strings across every dropdown in the plugin window at once. */
#define PLUGIN_PANEL_CHOICES_MAX 128
/** One choice string's buffer, terminator included. */
#define PLUGIN_PANEL_CHOICE_TEXT_MAX 48

/** What one chrome widget in the plugin window edits. */
enum PluginPanelRowKind
{
    /** The roster's per-plugin enable switch. Applied immediately. */
    PLUGIN_PANEL_ROW_ENABLE = 0,
    /**
     * A config key from the plugin's schema. STAGED: the chrome widget holds
     * the edit and nothing reaches the store until Save.
     *
     * The chrome is the staging buffer, which is why there is no third copy of
     * these values anywhere -- a retained widget already holds exactly "what
     * the user typed but has not committed".
     */
    PLUGIN_PANEL_ROW_CONFIG,
    /** A semantic control belonging to the one selected panel model.
     *  Its generation and serial are checked again by PluginHost at dispatch. */
    PLUGIN_PANEL_ROW_PANEL_WIDGET,
    PLUGIN_PANEL_ROW_SAVE,
    PLUGIN_PANEL_ROW_REVERT,
};

/** One tracked chrome widget on the page. */
struct PluginPanelRow
{
    int widget;
    int plugin;
    /** enum PluginPanelRowKind. */
    int kind;
    /** CONFIG: index into the plugin's schema. */
    int cfg_index;
    /** PLUGIN_WIDGET: the id the plugin gave it. */
    char widget_id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    /** PANEL_WIDGET: model slot, identity, and semantic kind copied with the row. */
    int model_index;
    uint32_t widget_serial;
    int widget_kind;
    /** Last resolved custom drawing geometry: executor pixels for BUFFER,
     * presenter-local logical pixels for an external custom sink. */
    struct ToriRSChromeRect custom_region;
    struct ToriRSChromeRect custom_clip;
    int custom_layout_valid;
    int custom_present_pending;
};

struct PluginPanel
{
    /**
     * The plugin window: a chrome instance of its own, not a panel in the developer chrome.
     *
     * Its own instance because it is the one piece of chrome a PLAYER uses
     * rather than a developer: it has to be able to be open beside the game
     * without the editors' claim on the keyboard, it needs its own capacity for
     * a tab per plugin, and it is the surface a WEB/BROWSER executor binds to
     * while the developer chrome stays on the in-canvas one. Input routing is
     * one shared call that takes the instance (app_chrome_route_input).
     */
    struct ToriRSChrome ui;
    /** Placement and selection generation for the one shared plugin shell. */
    struct ToriRSChromeShell shell;
    /**
     * Which presentation the plugin window is bound to, and the sync driving
     * it.
     *
     * The developer chrome is deliberately NOT bound to anything: the editors
     * are in-canvas tools. The plugin window is the surface a user might want
     * elsewhere, so it is the one with a choice.
     */
    struct ToriRSChromeSync sync;
    /** Persistent navigation bridge. Unlike `sync` this is never shut down
     * when the selected page collapses. */
    struct ToriRSChromeRailSync rail;
    /** Latest presenter-owned page allocation, generation fenced. */
    struct ToriRSChromeRailIntent rail_layout;
    int rail_has_layout;
    /**
     * The vtable the shell handed over, not yet started.
     *
     * Held unbound because begin() is what opens an OS window, and a window
     * the user never asked to see must not be opened at boot. The sync above
     * Inits from this the first time the plugin window is shown.
     */
    struct ToriRSChromeExec exec_pending;
    /** enum ToriRSChromeExecKind actually bound -- which is not always the one
     *  asked for: an executor that will not start falls back to buffer. */
    int exec_kind;
    /** The kind the boot line last named, so re-binding the window on every
     *  open does not restate it. -1 until the first bind. */
    int exec_logged_kind;
    /**
     * Someone NAMED that executor, rather than getting it by default.
     *
     * Distinct from the value, because "unset" and "explicitly buffer" are the
     * same number and only one of them is a decision. Read for the "(configured)"
     * / "(default)" half of the line the bind logs.
     */
    int exec_explicit;

    /** The window's panel handle in `ui`, or -1 before it is built. */
    int panel;
    int visible;

    /**
     * Split-out choice strings for every dropdown in the plugin window, and
     * the pointer array the chrome borrows.
     *
     * The chrome's dropdown BORROWS `char const* const*` and the strings must
     * outlive the widget. A config schema's `choices` and a plugin's
     * win_set_options are one `"a|b|c"` string owned by the plugin, which is
     * neither an array nor guaranteed to outlive a reload, so the window
     * splits them into this pool at build time. The widgets point into it
     * until the panel is rebuilt.
     */
    char choice_text[PLUGIN_PANEL_CHOICES_MAX][PLUGIN_PANEL_CHOICE_TEXT_MAX];
    char const* choice_ptrs[PLUGIN_PANEL_CHOICES_MAX];
    int choice_count;

    /** Plugin count and window revision the panel was built against. Rebuilt
     *  when either stops matching: scripts register asynchronously, and a
     *  plugin can declare or drop controls at any time. */
    int built_for;
    int built_rev;
    /** ABI-21 model stamps used by the shared ToriRSChrome presenter. */
    uint32_t built_model_rev;
    uint32_t built_generation;
    uint32_t built_registry_rev;
    /** Strictly increasing within (and harmlessly across) selections. */
    uint64_t intent_sequence;

    struct PluginPanelRow rows[PLUGIN_PANEL_ROWS_MAX];
    int row_count;
    /** Active panel-model slot -> rows index + 1 (zero absent). */
    int model_rows[TORIRS_PLUGIN_WIDGETS_MAX];
    /** Compact indexes of the active page's CUSTOM rows. */
    int custom_rows[TORIRS_PLUGIN_WIDGETS_MAX];
    int custom_row_count;
    int custom_pending_count;

    /** The logic cycle the custom wells were last drawn on. */
    uint64_t custom_last_draw_cycle;
    int custom_has_draw_cycle;
    /** Reused custom-region raster; the browser transport copies before
     *  return. Grown in the PLUGINS group; PluginPanel_Free releases it. */
    uint32_t* custom_pixels;
    size_t custom_pixel_capacity;

    /** The platform's plugin chrome has been asked for, once, at host start.
     *  @see TORIRS_CMD_PLATFORM_PLUGIN_CHROME. */
    uint8_t chrome_requested;
};

/** A zeroed panel from the PLUGINS group, with `panel` -1. Never NULL. */
struct PluginPanel*
PluginPanel_Create(void);

/** Release the panel and its custom-well pixels. NULL is accepted. */
void
PluginPanel_Free(struct PluginPanel* panel);

/* ---- the choice pool ---------------------------------------------------- */

/** Empty the pool. Only with the rows whose widgets borrow from it. */
void
PluginPanel_ChoicesClear(struct PluginPanel* panel);

/**
 * Split a `"a|b|c"` list into the pool and hand back the slice a dropdown
 * should borrow.
 *
 * @param out_count how many entries were taken.
 * @return the first pointer of the slice, or NULL when the list is empty or
 * the pool is already full. A list longer than the room left is cut where the
 * pool ends; an entry longer than 47 bytes is cut at 47.
 */
char const* const*
PluginPanel_ChoicesAdd(
    struct PluginPanel* panel,
    char const* choices,
    int* out_count);

/* ---- the row table ------------------------------------------------------ */

/** Forget every row, custom row, pending present and model-slot mapping. */
void
PluginPanel_RowsClear(struct PluginPanel* panel);

/**
 * Track a widget the page built: a zeroed row with `widget_serial` 0 and
 * `widget_kind` -1. NULL, and nothing tracked, when `widget` is negative or
 * the table is full.
 */
struct PluginPanelRow*
PluginPanel_RowAdd(
    struct PluginPanel* panel,
    int widget,
    int plugin,
    enum PluginPanelRowKind kind,
    int cfg_index,
    char const* widget_id);

/**
 * Track a control mirrored from panel-model slot `model_index`, map the slot
 * to it, and list it as a custom row when `widget_kind` is
 * TORIRS_PANEL_WIDGET_CUSTOM and the custom list has room. NULL, and nothing
 * tracked, when `widget` is negative, `serial` is 0, the slot is outside
 * [0, TORIRS_PLUGIN_WIDGETS_MAX), or the table is full.
 */
struct PluginPanelRow*
PluginPanel_RowAddModel(
    struct PluginPanel* panel,
    int widget,
    int plugin,
    int model_index,
    uint32_t serial,
    int widget_kind,
    char const* widget_id);

/** The row at `row_index`, or NULL when no row has that index. */
struct PluginPanelRow*
PluginPanel_Row(
    struct PluginPanel* panel,
    int row_index);

/** The row index panel-model slot `model_index` maps to, or -1. */
int
PluginPanel_RowForModel(
    struct PluginPanel const* panel,
    int model_index);

/** The index of the first row tracking `widget`, or -1. */
int
PluginPanel_RowFindWidget(
    struct PluginPanel const* panel,
    int widget);

/** The `custom`th custom row, 0 <= custom < custom_row_count. */
struct PluginPanelRow*
PluginPanel_CustomRow(
    struct PluginPanel* panel,
    int custom);

/** Mark a row as owing (or not owing) a present, keeping the count. */
void
PluginPanel_CustomPendingSet(
    struct PluginPanel* panel,
    struct PluginPanelRow* row,
    int pending);

/** No custom row owes a present any more. */
void
PluginPanel_CustomPendingClear(struct PluginPanel* panel);

/* ---- what the page was built against ------------------------------------ */

/** Record what a build consumed. */
void
PluginPanel_BuiltStamp(
    struct PluginPanel* panel,
    int count,
    int rev,
    uint32_t model_rev,
    uint32_t generation,
    uint32_t registry_rev);

/** Built for this plugin count, window revision and selection generation. */
bool
PluginPanel_BuiltFor(
    struct PluginPanel const* panel,
    int count,
    int rev,
    uint32_t generation);

/** Built against this registry revision. */
bool
PluginPanel_BuiltForRegistry(
    struct PluginPanel const* panel,
    uint32_t registry_rev);

/** Built against this model revision. */
bool
PluginPanel_BuiltForModel(
    struct PluginPanel const* panel,
    uint32_t model_rev);

#endif
