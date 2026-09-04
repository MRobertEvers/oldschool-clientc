#ifndef SRC_UI_TORIRS_CHROME_RAIL_H
#define SRC_UI_TORIRS_CHROME_RAIL_H

/*
 * The application-owned plugin rail.
 *
 * This is deliberately not part of the retained widget model. A page is
 * mounted only while selected, whereas the rail has to remain usable after
 * the page executor has been shut down. The application publishes one copied
 * snapshot of every registered destination; a presentation returns copied
 * selection/layout events. Neither side retains pointers owned by a plugin.
 */

#include <stdint.h>

/* All 32 plugin slots plus the permanent built-in Manage Plugins entry. */
#define TORIRS_CHROME_RAIL_ENTRY_MAX 33
#define TORIRS_CHROME_RAIL_TITLE_MAX 64
#define TORIRS_CHROME_RAIL_ICON_MAX 64
#define TORIRS_CHROME_RAIL_BADGE_MAX 24
#define TORIRS_CHROME_RAIL_ICON_SIDE_MAX 64
#define TORIRS_CHROME_RAIL_ICON_PIXELS_MAX                                      \
    (TORIRS_CHROME_RAIL_ICON_SIDE_MAX * TORIRS_CHROME_RAIL_ICON_SIDE_MAX)

struct ToriRSChromeRailEntry
{
    /** enum ToriRSChromeRailEntryKind. */
    int kind;
    int plugin_index;
    int preferred_width;
    int attention;
    char title[TORIRS_CHROME_RAIL_TITLE_MAX];
    char icon_asset[TORIRS_CHROME_RAIL_ICON_MAX];
    char badge[TORIRS_CHROME_RAIL_BADGE_MAX];
};

enum ToriRSChromeRailEntryKind
{
    TORIRS_CHROME_RAIL_ENTRY_MANAGE = 1,
    TORIRS_CHROME_RAIL_ENTRY_PLUGIN,
};

/** A complete, immutable-at-the-boundary view of the one shared rail. */
struct ToriRSChromeRailSnapshot
{
    uint32_t registry_revision;
    /** Application-shell selection generation used by rail intents. */
    uint32_t selection_generation;
    /** PluginHost's active model generation, retained for diagnostics/sync. */
    uint32_t page_generation;
    int active_plugin;
    int last_selected_plugin;
    /** Plugin index or TORIRS_CHROME_SHELL_PAGE_MANAGE. */
    int selected_entry;
    int expanded;
    int entry_count;
    struct ToriRSChromeRailEntry entries[TORIRS_CHROME_RAIL_ENTRY_MAX];
};

/** One revisioned icon payload. A 0x0 payload explicitly selects fallback. */
struct ToriRSChromeRailIcon
{
    int plugin_index;
    uint32_t revision;
    int width;
    int height;
    uint32_t argb[TORIRS_CHROME_RAIL_ICON_PIXELS_MAX];
};

enum ToriRSChromeRailIntentKind
{
    /** A rail destination was pressed. `plugin_index` names it. */
    TORIRS_CHROME_RAIL_INTENT_SELECT = 1,
    /** The presenter allocated (or hid) the active page. */
    TORIRS_CHROME_RAIL_INTENT_LAYOUT,
};

/**
 * Presentation -> application event.
 *
 * A flat bounded POD so Android can copy it under a mutex and return
 * immediately. Selection and layout are fenced by the snapshot generation
 * the presentation acted on; the application rejects an event after another
 * selection has replaced that generation.
 */
struct ToriRSChromeRailIntent
{
    int kind;
    int plugin_index;
    uint32_t selection_generation;
    /** LAYOUT only: mounted semantic page generation. */
    uint32_t page_generation;
    uint64_t sequence;

    /* LAYOUT only, in logical chrome units unless scale_milli says otherwise. */
    int width;
    int height;
    /** Content-box width of a full-width CUSTOM well, in logical units.
     * Zero when the current page has no measurable custom well. */
    int custom_width;
    int scale_milli;
    int size_class;
    int visible;
    int game_visible;
};

struct ToriRSChromeExec;

/** Last snapshot handed to one executor, used to make idle publication O(1). */
struct ToriRSChromeRailSync
{
    struct ToriRSChromeRailSnapshot presented;
    void* presented_user;
    int (*presented_apply)(
        void* user, struct ToriRSChromeRailSnapshot const* snapshot);
    void* icon_user;
    int (*icon_apply)(void* user, struct ToriRSChromeRailIcon const* icon);
    uint32_t icon_revision[TORIRS_CHROME_RAIL_ENTRY_MAX];
    unsigned char icon_known[TORIRS_CHROME_RAIL_ENTRY_MAX];
    int primed;
};

void ToriRSChromeRailSnapshot_Init(struct ToriRSChromeRailSnapshot* snapshot);
/** Whether a running registered panel is an independent rail destination.
 * Managed-only pages stay reachable through the Manage Plugins roster. */
int ToriRSChromeRailSnapshot_IncludesPlugin(int panel_registered, int managed_only);
int ToriRSChromeRailSnapshot_Add(
    struct ToriRSChromeRailSnapshot* snapshot,
    int plugin_index,
    char const* title,
    char const* icon_asset,
    int preferred_width,
    char const* badge,
    int attention);
int ToriRSChromeRailSnapshot_AddManage(
    struct ToriRSChromeRailSnapshot* snapshot,
    int manage_sentinel,
    char const* title);

void ToriRSChromeRailSync_Init(struct ToriRSChromeRailSync* sync);

/** Publish only when registry/selection/expanded state or executor changed. */
int ToriRSChromeRailSync_Run(
    struct ToriRSChromeRailSync* sync,
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailSnapshot const* snapshot);

/** Publish one icon only when its host-owned revision changed. */
int ToriRSChromeRailSync_Icon(
    struct ToriRSChromeRailSync* sync,
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailIcon const* icon);

/** Drain at most `max` copied events. Valid before begin and after end. */
int ToriRSChromeRail_Poll(
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailIntent* out,
    int max);

#endif
