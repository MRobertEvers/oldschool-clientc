#ifndef SRC_UI_TORIRS_CHROME_SHELL_H
#define SRC_UI_TORIRS_CHROME_SHELL_H

#include <stdint.h>

/*
 * Placement and selection state for the ONE plugin-chrome shell.
 *
 * This model deliberately knows neither a plugin host nor a window system. A
 * host supplies the currently selected plugin and the drawable it was given;
 * a platform consumes the resulting game/rail/panel rectangles. Keeping that
 * decision here gives Android, web and every desktop backend the same compact
 * threshold and, critically, one selection generation with which to reject a
 * late event from the page that was just replaced.
 */

enum ToriRSChromeShellMode
{
    TORIRS_CHROME_SHELL_COLLAPSED = 0,
    TORIRS_CHROME_SHELL_SPLIT,
    TORIRS_CHROME_SHELL_EXCLUSIVE,
    TORIRS_CHROME_SHELL_DETACHED
};

/** Selection keys reserved by the shell; plugin indices are non-negative. */
#define TORIRS_CHROME_SHELL_PAGE_NONE (-1)
#define TORIRS_CHROME_SHELL_PAGE_MANAGE (-2)

struct ToriRSChromeShellRect
{
    int x;
    int y;
    int w;
    int h;
};

struct ToriRSChromeShellInput
{
    /** Current top-level content box, in platform-independent logical units. */
    int window_w;
    int window_h;
    /** Size the game presentation had before the shell was expanded. */
    int target_game_w;
    int target_game_h;
    /** Smallest simultaneously useful game and plugin presentations. */
    int min_game_w;
    int min_panel_w;
    int preferred_panel_w;
    int rail_w;
    /** An ordinary desktop window may be asked to grow. */
    int may_grow;
};

struct ToriRSChromeShellLayout
{
    int mode; /* enum ToriRSChromeShellMode */
    struct ToriRSChromeShellRect game;
    struct ToriRSChromeShellRect rail;
    struct ToriRSChromeShellRect panel;
    /** Advisory attached-grow request. Zero means no resize request. */
    int request_window_w;
    int request_window_h;
};

struct ToriRSChromeShell
{
    /** Most recently selected plugin or host page; retained while collapsed. */
    int active_plugin;
    /** Only this state mounts controls or permits rendering/input. */
    int expanded;
    /** Changes before the old page is torn down, never zero. */
    uint32_t selection_generation;
    int detached;
    int panel_w;
};

void
ToriRSChromeShell_Init(struct ToriRSChromeShell* shell, int preferred_panel_w);

/** Select and expand exactly one plugin or PAGE_MANAGE. PAGE_NONE forgets it. */
uint32_t
ToriRSChromeShell_Select(struct ToriRSChromeShell* shell, int plugin);

/** Collapse to the small rail while remembering the most recent selection. */
uint32_t
ToriRSChromeShell_Collapse(struct ToriRSChromeShell* shell);

/** Does an event still name the page currently allowed to render/receive input? */
int
ToriRSChromeShell_Accepts(
    struct ToriRSChromeShell const* shell, int plugin, uint32_t selection_generation);

/** Detaching moves this same expanded shell; it never creates another one. */
void
ToriRSChromeShell_SetDetached(struct ToriRSChromeShell* shell, int detached);

void
ToriRSChromeShell_SetPanelWidth(
    struct ToriRSChromeShell* shell, int width, int minimum, int maximum);

void
ToriRSChromeShell_Layout(
    struct ToriRSChromeShell const* shell,
    struct ToriRSChromeShellInput const* input,
    struct ToriRSChromeShellLayout* out);

#endif
