#ifndef SRC_TORIRS_CHROME_MIRROR_H
#define SRC_TORIRS_CHROME_MIRROR_H

/*
 * The bookkeeping both web executors need, in one place.
 *
 * WEB/BROWSER both need to validate a returned widget handle against the panel
 * mount that created it, and both need to queue intents until the host drains
 * them. Keeping that lifecycle index here prevents two subtly different stale
 * handle fences. Values and DOM nodes stay in the reducers that own them.
 *
 * No allocation, and no dependency beyond the seam.
 */

#include "torirs_chrome_exec.h"

#include <stdint.h>

/** Intents waiting for the host's next drain. One frame's worth of clicking is
 *  a handful; the cap is generous rather than tight. */
#define TORIRS_CHROME_MIRROR_INTENTS 32

struct ToriRSChromeMirrorWidget
{
    int live;
    int panel;
    /** Mount identity of the owning panel when this widget was added. */
    uint32_t panel_mount;
};

struct ToriRSChromeMirrorPanel
{
    int live;
    /** Advanced on every open, invalidating the old subtree in O(1). */
    uint32_t mount;
};

struct ToriRSChromeMirror
{
    struct ToriRSChromeMirrorPanel panels[TORIRS_CHROME_MAX_PANELS];
    /** Indexed by chrome handle. REMOVE clears one slot; PANEL_CLOSE changes a
     * mount and invalidates its complete subtree in O(1). */
    struct ToriRSChromeMirrorWidget widgets[TORIRS_CHROME_MAX_WIDGETS];

    uint32_t next_mount;

    struct ToriRSChromeIntent intents[TORIRS_CHROME_MIRROR_INTENTS];
    int intent_count;
    /** Set when the queue overflowed, so an executor can say so rather than
     *  silently dropping what the user did. */
    int intent_overflow;
};

void
ToriRSChromeMirror_Init(struct ToriRSChromeMirror* mirror);

/**
 * Fold one command into the mirror.
 *
 * @return 1 when the command changed the mirror's SHAPE -- a panel or widget
 * appeared or went away. Property commands return 0 because the DOM reducer,
 * not this lifecycle index, owns their values.
 *
 * Deliberately does not create anything itself.
 */
int
ToriRSChromeMirror_Apply(struct ToriRSChromeMirror* mirror, struct ToriRSChromeCmd const* cmd);

/** The widget slot for a handle, or NULL when nothing lives there. */
struct ToriRSChromeMirrorWidget*
ToriRSChromeMirror_Widget(struct ToriRSChromeMirror* mirror, int handle);

/* ---- intents ------------------------------------------------------------- */

void
ToriRSChromeMirror_PushIntent(
    struct ToriRSChromeMirror* mirror, struct ToriRSChromeIntent const* intent);

/** Consume the one-shot indication that at least one intent did not fit. */
int
ToriRSChromeMirror_TakeIntentOverflow(struct ToriRSChromeMirror* mirror);

/** Convenience: the three shapes an executor almost always pushes. */
void
ToriRSChromeMirror_PushActivate(struct ToriRSChromeMirror* mirror, int panel, int widget);
void
ToriRSChromeMirror_PushToggle(
    struct ToriRSChromeMirror* mirror, int panel, int widget, int on);
void
ToriRSChromeMirror_PushText(
    struct ToriRSChromeMirror* mirror, int panel, int widget, char const* text);

/** Drain up to `max` into `out`. @return how many. Fits struct
 *  ToriRSChromeExec::poll exactly, so an executor can hand this straight over. */
int
ToriRSChromeMirror_Poll(
    struct ToriRSChromeMirror* mirror, struct ToriRSChromeIntent* out, int max);

#endif
