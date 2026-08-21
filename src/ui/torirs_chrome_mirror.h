#ifndef SRC_TORIRS_CHROME_MIRROR_H
#define SRC_TORIRS_CHROME_MIRROR_H

/*
 * The bookkeeping every NATIVE-WIDGET executor needs, in one place.
 *
 * A native-widget executor (web, gdi, cs2) rebuilds the chrome's model out of
 * foreign controls, and all three then have exactly the same two problems:
 *
 *   1. A command names a chrome HANDLE and the executor holds a DOM id, an
 *      HWND or a component index. Something has to map one to the other, and
 *      has to keep mapping after a handle is removed and recycled.
 *   2. The user touches a control and an intent has to come back naming that
 *      same handle -- so the map has to work in both directions, and the
 *      intents have to queue until the host next drains them.
 *
 * Neither is interesting, and three copies of them would be three places for
 * the recycled-handle case to be got wrong. What is NOT here is any copy of a
 * widget's values: a DOM input, an EDIT control and an interface component all
 * hold their own text, and a fourth copy would only be a fourth thing to keep
 * in step. The mirror knows what a widget IS, not what it currently says.
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
    /** enum ToriDbgWidgetKind, as the ADD carried it. */
    int kind;
    int panel;
    /** Which tab owns it; -1 = every tab. */
    int tab;
    int hidden;
    /**
     * Position in the panel, from the order the ADDs arrived in.
     *
     * NOT the handle. Handles come off a free list, so a rebuilt panel
     * recycles them out of order and a native executor laying rows out by
     * handle index puts them on screen in an order the model never had -- the
     * Save button above the settings it commits, which is how this was found.
     * The commands arrive in row order, so a counter at ADD time is the row
     * order, and it costs one int.
     */
    int order;
    /**
     * The executor's own name for this widget: a DOM node id, an HWND, a
     * component index. `intptr_t` because two of those three are pointers and
     * the third is an int, and a union would be three casts instead of one.
     */
    intptr_t native;
};

struct ToriRSChromeMirrorPanel
{
    int live;
    int style;
    /** Which tab is showing. Kept because a widget's `tab` says which tab owns
     *  it and this says which is up -- an executor needs both to decide what
     *  to show, and only one of them arrives per command. */
    int active_tab;
    intptr_t native;
};

struct ToriRSChromeMirror
{
    struct ToriRSChromeMirrorPanel panels[TORIDBG_MAX_PANELS];
    /** Indexed BY chrome handle, so the lookup a command needs is an array
     *  index rather than a search. The cost is one slot per possible handle,
     *  which is what makes removal-then-reuse safe: the slot is cleared on
     *  REMOVE, so a stale native id cannot survive into the widget that
     *  recycles the handle. */
    struct ToriRSChromeMirrorWidget widgets[TORIDBG_MAX_WIDGETS];

    /** Next `order` to hand out. Monotonic within a panel's lifetime; a panel
     *  close resets nothing, because order only ever has to be comparable. */
    int next_order;

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
 * appeared or went away -- so an executor can create or destroy its native
 * thing on the same pass that maintains the map. Property commands return 0:
 * the executor applies those to the native control it already has.
 *
 * Deliberately does not create anything itself. It cannot: what a checkbox is
 * differs per executor, which is the entire reason they exist.
 */
int
ToriRSChromeMirror_Apply(struct ToriRSChromeMirror* mirror, struct ToriRSChromeCmd const* cmd);

/**
 * Handles in row order, written into `out` (at most `max`). @return how many.
 *
 * The walk a native-widget executor lays out with. Ordering by `order` rather
 * than by handle is the whole point -- see ToriRSChromeMirrorWidget::order.
 */
int
ToriRSChromeMirror_Order(struct ToriRSChromeMirror const* mirror, int* out, int max);

/** The widget slot for a handle, or NULL when nothing lives there. */
struct ToriRSChromeMirrorWidget*
ToriRSChromeMirror_Widget(struct ToriRSChromeMirror* mirror, int handle);

/** The panel slot for a handle, or NULL. */
struct ToriRSChromeMirrorPanel*
ToriRSChromeMirror_Panel(struct ToriRSChromeMirror* mirror, int handle);

/**
 * Reverse lookup: which chrome handle does this native id belong to? -1 if none.
 *
 * The direction an intent needs. Linear, because it runs once per click on a
 * list of at most a few hundred, and an index keyed by an opaque intptr_t
 * would be a second structure to keep in step for no measurable gain.
 */
int
ToriRSChromeMirror_HandleOfNative(struct ToriRSChromeMirror const* mirror, intptr_t native);

/**
 * Should this widget be on screen? Answers `hidden` and the tab test together.
 *
 * The one place the two are combined, for the same reason the chrome has one
 * dbg_widget_shown: an executor that checked only one of them shows an
 * inactive tab's rows, and one that checked them in different places at
 * different times shows them intermittently.
 */
int
ToriRSChromeMirror_Shown(struct ToriRSChromeMirror const* mirror, int handle);

/* ---- intents ------------------------------------------------------------- */

void
ToriRSChromeMirror_PushIntent(
    struct ToriRSChromeMirror* mirror, struct ToriRSChromeIntent const* intent);

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
