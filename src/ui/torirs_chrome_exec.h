#ifndef SRC_TORIRS_CHROME_EXEC_H
#define SRC_TORIRS_CHROME_EXEC_H

/*
 * ToriRSChrome executors — the seam between the retained widget model and
 * whatever actually presents it.
 *
 * The chrome (ui/uitree_debug_overlay.h) is a model that emits a display list
 * of rectangles, glyphs and sprites. That list is exactly the right altitude
 * for a rasteriser and exactly the wrong one for anything native: a DOM
 * <input>, a Win32 EDIT control or a game interface component cannot be
 * reconstructed from RECT/TEXT/SPRITE. So this layer emits the chrome's own
 * vocabulary instead — panels, tabs, widgets, properties — and each executor
 * maps a *checkbox* onto whatever a checkbox is where it lives.
 *
 * THE MODEL STAYS AUTHORITATIVE. An executor is a projection of it, never a
 * second copy of the truth: commands flow out, intents flow back, and an intent
 * is applied by mutating the model exactly as a click on the in-canvas chrome
 * would. That is what keeps five presentations of one panel agreeing, and what
 * lets the whole thing be tested with no window at all.
 *
 * DELTAS, NOT DECLARATIONS. Sync diffs the model against a shadow of what this
 * executor was last told and emits only what changed. Re-declaring a panel
 * whenever anything in it moved would be far simpler, and would also destroy
 * and recreate native controls on every keystroke -- which loses focus, loses
 * the caret, and makes a text field impossible to type into. The shadow is the
 * price of native controls that survive their own updates.
 *
 * STRINGS ARE COPIED HERE. The chrome deliberately borrows: prim text points
 * into its widget, and a dropdown's options point at the caller's array. Those
 * lifetimes are fine inside one process and wrong the moment a command is
 * queued, posted to another thread, sent to a browser tab or written to a
 * recording. Every string entering a command is copied into it, and the borrow
 * stops at this boundary.
 *
 * NO ALLOCATION, and no dependency beyond the chrome header: an executor lives
 * or dies with a platform, but the seam has to build everywhere.
 */

#include "torirs_chrome_exec_kind.h"
#include "uitree_debug_overlay.h"

#include <stdint.h>

/** Bytes of one command's string payload, terminator included. */
#define TORIRS_CHROME_TEXT_MAX TORIRS_CHROME_INPUT_MAX

/**
 * A LISTROW's SHAPE, carried in TORIRS_CHROME_CMD_WIDGET_ADD's `w`.
 *
 * Bits rather than a second field because both answers are shape -- they are
 * fixed for the life of the row, and a row that gained or lost either is
 * re-added rather than updated -- and the ADD is the one command that states a
 * widget's shape. A field of its own would be a command every executor has to
 * switch on for one widget kind.
 *
 * LOCKED is the roster's essential row: it has no second state, so it has no
 * switch at all and its name takes the column (@see ToriRS_PluginDef::essential
 * and ToriRSChrome_ListRowLocked). Executors that never heard it drew the
 * switch anyway, unchecked -- a red cross beside Client Settings and Feature
 * Flags, which reads as two disabled plugins rather than as two that cannot be
 * disabled. A LOCKED row is always an ACTION row: with no switch, opening its
 * page is the only thing it does.
 */
#define TORIRS_CHROME_ROW_ACTION 0x1
#define TORIRS_CHROME_ROW_LOCKED 0x2

/**
 * What one command says.
 *
 * Coarse enough that an executor can switch on it and fine enough that nothing
 * has to be re-sent to say one thing changed. Panels and widgets are named by
 * their chrome handles, which are stable for as long as the widget lives --
 * removal is announced, so an executor never has to guess that one went away.
 */
enum ToriRSChromeCmdKind
{
    /** A frame's worth of deltas begins. Carries nothing; an executor that
     *  batches (a DOM layout pass, a BeginDeferWindowPos) opens here. */
    TORIRS_CHROME_CMD_SYNC_BEGIN = 1,
    /** ...and ends. An executor that batches commits here. */
    TORIRS_CHROME_CMD_SYNC_END,

    /** A panel appeared: `panel`, `value` = enum ToriRSChromePanelStyle, `text` =
     *  its title. Always the first command about that panel. */
    TORIRS_CHROME_CMD_PANEL_OPEN,
    /** A panel went away, or was hidden. Every widget of it is implicitly
     *  gone; an executor need not expect a REMOVE per row. */
    TORIRS_CHROME_CMD_PANEL_CLOSE,
    /** `text` = the new title. */
    TORIRS_CHROME_CMD_PANEL_TITLE,
    /** `x`, `y`, `w`, `h` = the panel's resolved box, in chrome pixels.
     *  Advisory: a native executor is free to let its own window manager place
     *  and size the thing, and most should. */
    TORIRS_CHROME_CMD_PANEL_RECT,
    /**
     * `value` = which tab of this panel is showing.
     *
     * Not advisory, unlike the rect: a widget's `tab` says which tab OWNS it
     * and this says which one is up, and an executor needs both to decide what
     * to show. Without it a native executor can see that widget 7 belongs to
     * tab 2 and still have no idea whether tab 2 is the one in front -- which
     * is a window that draws every tab's controls at once.
     */
    TORIRS_CHROME_CMD_PANEL_TAB,

    /**
     * A widget appeared: `widget`, `value` = enum ToriRSChromeWidgetKind, `tab` =
     * which tab owns it (-1 = all), `label`/`text` = its initial strings.
     * Always the first command about that widget.
     *
     * Two fields of the rect ride this one command because they are the
     * widget's SHAPE rather than its state, and shape never changes after an
     * add -- a row that gained or lost one is a different row, and the panel
     * rebuild that changed it gives it a new serial, which the shadow answers
     * with a remove-then-add:
     *
     *   `w` -- a LISTROW's shape bits, TORIRS_CHROME_ROW_*.
     *   `h` -- a TEXTAREA is this many lines tall.
     */
    TORIRS_CHROME_CMD_WIDGET_ADD,
    /** A widget went away. Its handle may be reused by a later ADD. */
    TORIRS_CHROME_CMD_WIDGET_REMOVE,
    TORIRS_CHROME_CMD_WIDGET_LABEL,
    TORIRS_CHROME_CMD_WIDGET_TEXT,
    /** `value` = 0/1. */
    TORIRS_CHROME_CMD_WIDGET_CHECKED,
    /** `value` = 0/1. */
    TORIRS_CHROME_CMD_WIDGET_HIDDEN,
    /** `color` = 0xRRGGBB, 0 meaning "the theme's". */
    TORIRS_CHROME_CMD_WIDGET_COLOR,
    /**
     * `value` = the selected index, or -1.
     *
     * Two kinds put something else in it, both because the thing they have one
     * of IS a selection out of an ordered set, and a channel of its own would
     * be a command every executor has to switch on for one widget kind:
     *
     *   COLORPICK -- the packed HSL16 value. @see TORIRS_CHROME_W_COLORPICK.
     *   TEXTAREA  -- the first VISIBLE LINE of the box. A presentation whose
     *                control scrolls itself (a DOM textarea, a multiline EDIT)
     *                ignores it; one that draws its own lines (the CS2 window)
     *                needs it, or a long list always shows its first four
     *                lines while the user types on the twelfth.
     */
    TORIRS_CHROME_CMD_WIDGET_SELECTED,
    /**
     * `value` = 1 when this widget now holds the keyboard, 0 when it lost it.
     *
     * The MODEL owns focus even where the presentation has its own -- a CS2
     * window's rows are drawn components with no focus concept at all, and the
     * host routes keys at the model's focused widget. So an executor that
     * cannot see the model needs telling, or it draws every field identically
     * and a click on one reads as having done nothing. That symptom is exactly
     * what this was added for.
     *
     * An executor whose controls own their own focus (a DOM input, an EDIT)
     * may act on it to keep the two in step, or ignore it. Sent for every
     * widget kind, because MODELVIEW takes the focus too.
     */
    TORIRS_CHROME_CMD_WIDGET_FOCUS,

    /**
     * The option list of a dropdown, or the titles of a tab strip, is about to
     * be restated in full: `value` = how many follow.
     *
     * Restated rather than diffed because a list is one value -- the palettes
     * this serves are rebuilt wholesale (every loc name in a search, every
     * plugin in a manifest), and an executor holding a native combo box wants
     * "here is the new list", not a sequence of inserts and deletes it has to
     * replay in order.
     */
    TORIRS_CHROME_CMD_WIDGET_OPTIONS,
    /** One entry of the list just announced: `value` = its index, `text` = it. */
    TORIRS_CHROME_CMD_WIDGET_OPTION,

    /**
     * Which boolean art every checkbox and roster switch is to wear:
     * `value` = enum ToriRSChromeCheckStyle. `panel` and `widget` are -1.
     *
     * CHROME-WIDE, unlike everything else here, because the choice is: one
     * window whose rows disagreed about what a checkbox looks like would be a
     * worse bug than any this seam already prevents.
     *
     * Sent before any panel on the first Run, and again whenever it changes --
     * so an executor may store it and use it for every row it draws afterwards
     * rather than looking it up per widget. An executor that came up later is
     * told it as part of being caught up from nothing, which is why the shadow
     * carries it beside the panels.
     *
     * It is also a LAYOUT input, not only a palette one: the two arts are 17
     * and 18 wide (TORIRS_CHROME_M_BOX / _M_BOX_SQUARE), so an executor that
     * places its own controls has to re-place them when this arrives.
     */
    TORIRS_CHROME_CMD_CHECK_STYLE,
};

/**
 * One command. A flat POD, fixed size, no pointers — so it can be queued,
 * recorded, replayed, or serialised to a byte stream without a fixup pass.
 */
struct ToriRSChromeCmd
{
    /** enum ToriRSChromeCmdKind. */
    int kind;
    /** Panel handle. -1 on the sync markers. */
    int panel;
    /** Widget handle, or -1 for a panel-level command. */
    int widget;
    /** Which tab owns the widget; -1 = every tab. */
    int tab;
    /** Kind, index, count or flag, per the command. */
    int value;
    uint32_t color;
    int x;
    int y;
    int w;
    int h;
    char label[TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_TEXT_MAX];
};

/** What came back from a presentation the user touched. */
enum ToriRSChromeIntentKind
{
    /** A button, menu row or tab was clicked; a dropdown row was chosen. */
    TORIRS_CHROME_INTENT_ACTIVATE = 1,
    /**
     * A LISTROW's ACTION zone was used -- its settings affordance, not its
     * switch. Distinct from ACTIVATE because the two are the row's two
     * different outcomes, and an executor that could only report one of them
     * would have a list you can toggle but cannot open.
     */
    TORIRS_CHROME_INTENT_ACTION,
    /** A checkbox was toggled: `value` = its new state. */
    TORIRS_CHROME_INTENT_TOGGLE,
    /** A text field was edited: `text` = its whole new contents. */
    TORIRS_CHROME_INTENT_TEXT,
    /** A list choice was made: `value` = the index. */
    TORIRS_CHROME_INTENT_PICK,
    /** A tab was selected: `value` = the index. */
    TORIRS_CHROME_INTENT_TAB,
    /** The presentation was dismissed by its own chrome (a window's close box,
     *  a tab being shut). `widget` is -1; `panel` says which. */
    TORIRS_CHROME_INTENT_CLOSE,
};

/*
 * There was a CONFIRM here -- "the Ok beside the close box was used" -- and it
 * is gone with the button. Kept as a note rather than as a value, because a
 * kind nothing can raise is worse than an absent one: every executor still
 * switches on it, every recording still round-trips it, and the seam claims a
 * capability no presentation offers. A panel commits through its own Save row,
 * which is a widget and arrives as ACTIVATE like any other.
 */

/**
 * One thing a user did, addressed at the model.
 *
 * Idempotent by construction: every intent states a RESULT ("this is now
 * checked", "the text is now this") rather than an edit ("toggle it",
 * "insert a byte"). A duplicate delivery is then harmless, which matters
 * because the transports underneath this -- a message port, a socket, a
 * recording being replayed -- do not all promise exactly-once.
 */
struct ToriRSChromeIntent
{
    /** enum ToriRSChromeIntentKind. */
    int kind;
    int panel;
    int widget;
    int value;
    char text[TORIRS_CHROME_TEXT_MAX];
};

/**
 * One frame of pointer and keyboard, in a surface's own coordinates.
 *
 * Surface executors only. The chrome model is shared, so what a second window
 * has to send back is not intents but the raw gesture -- translated into the
 * chrome's coordinate space, which is the one thing the host cannot do for it.
 */
struct ToriRSChromeSurfaceInput
{
    int mouse_x;
    int mouse_y;
    int mouse_down;
    int mouse_up;
    int wheel;
    /** Printable bytes typed this frame, NUL-terminated. */
    char text[32];
    /** enum ToriRSChromeKey, or TORIRS_CHROME_KEY_NONE. One per frame is enough: these
     *  are editing keys on a settings form, not a game's movement input. */
    int edit_key;
    /** The surface was resized; w/h are its new size. */
    int resized;
    int width;
    int height;
};

/**
 * What a presentation has to implement.
 *
 * TWO KINDS of executor share this table, and which entries an implementation
 * fills says which it is:
 *
 *  - A SURFACE executor puts the chrome's own rasterised output somewhere
 *    other than the game canvas -- a second OS window. The widgets are still
 *    the chrome's, drawn by the chrome; only their destination differs. It
 *    fills `present` and `surface_input`, and ignores `apply` entirely.
 *
 *  - A NATIVE-WIDGET executor rebuilds the model out of foreign controls: DOM
 *    elements, comctl32 windows, cache interface components. It cannot use the
 *    display list at all -- an <input> cannot be reconstructed from rectangles
 *    -- so for it the command stream IS the contract. It fills `apply` and
 *    `poll`.
 *
 * One table rather than two because a host drives them identically: bring it
 * up, hand it this frame, take back what the user did, tear it down. Making
 * the difference two interfaces would put the choice in the host, which is
 * exactly where it does not belong.
 *
 * None of these may block: the client's frame loop runs them, and an executor
 * that waits on a browser tab or a hung window stalls the game. One that
 * cannot keep up must drop, not delay.
 */
struct ToriRSChromeExec
{
    void* user;

    /**
     * Bring the presentation up. @return 0 when it cannot exist here -- no
     * aux-window support, a blocked popup, a missing control library.
     *
     * A false here is NOT an error: the surface falls back to the buffer
     * executor and the user gets in-canvas chrome. That is the whole reason
     * every executor is optional.
     */
    int (*begin)(void* user);

    /** Consume one command. Called between SYNC_BEGIN and SYNC_END. */
    void (*apply)(void* user, struct ToriRSChromeCmd const* cmd);

    /** Take the presentation down. Must tolerate never having begun. */
    void (*end)(void* user);

    /**
     * Drain what the user did into `out`, at most `max`. @return how many were
     * written. 0 is the common case.
     *
     * NATIVE-WIDGET executors report everything here: their controls are
     * foreign, so a click on one is only ever an intent. A SURFACE executor
     * normally has nothing to say -- its gestures go back raw through
     * `surface_input` for the chrome to hit test itself -- but it still owns
     * the WINDOW those gestures arrive in, and a window closing happens to the
     * presentation rather than inside it. That has no coordinates to report,
     * so it comes back here, as CLOSE, like every other executor's.
     */
    int (*poll)(void* user, struct ToriRSChromeIntent* out, int max);

    /**
     * SURFACE executors: put this frame's display list on the surface.
     *
     * Called after Build, with the chrome's own prims. An implementation
     * rasterises them exactly as the in-canvas path does -- same list, same
     * renderer, different destination -- which is what makes a second window
     * pixel-identical to the panel it replaced rather than a second look.
     */
    void (*present)(void* user, struct ToriRSChromePrim const* prims, int count);

    /**
     * SURFACE executors: this frame's gesture, in the surface's coordinates.
     * @return 1 when `out` was filled.
     *
     * The host feeds it straight back into the same chrome, which is why the
     * coordinates must already be surface-local: the chrome laid its panels
     * out in that space and has no idea a window moved.
     */
    int (*surface_input)(void* user, struct ToriRSChromeSurfaceInput* out);

    /**
     * SURFACE executors whose surface is a WINDOW OF THEIR OWN: how big it is
     * now, in its own pixels. @return 1 when out_w/out_h were filled; 0 when
     * there is no window up.
     *
     * Its PRESENCE is the declaration that the chrome owns the surface
     * outright -- nothing else draws into that window, so the panel is
     * stretched to fill it rather than left floating at the coordinates it
     * would use over a game canvas. @see ToriRSChromeSync_FillSurface.
     *
     * One entry rather than a flag beside a getter, because two things that
     * say the same thing are two things that can disagree -- and this is the
     * table where "which entries an implementation fills says which kind it
     * is" is already the rule. The buffer executor SHARES the canvas with the
     * game, so it has neither and its panel goes on floating.
     */
    int (*surface_size)(void* user, int* out_w, int* out_h);

    /**
     * SURFACE executors whose window has no native frame: where this frame's
     * window-move handles are, in the surface's own pixels.
     *
     * Optional, and its absence is the ordinary case -- a window wearing its OS
     * frame is moved by that frame and has nothing to be told. An executor that
     * hid its frame implements this and hands the region to whatever asks the
     * window manager's questions.
     *
     * Called EVERY frame, including with an empty region. A panel that stopped
     * having a strip -- or a window whose panel went away -- has to take its
     * handles down with it, or the pump goes on swallowing presses over chrome
     * that is not there any more.
     *
     * @see ToriRSChrome_WindowDragRegion for what a handle is and why the
     * controls inside one have to be punched back out.
     */
    void (*set_drag_region)(void* user, struct ToriRSChromeDragRegion const* region);

    /** Non-zero for a surface executor, so a host can tell without inspecting
     *  which function pointers happen to be set. */
    int is_surface;
};

/**
 * What the executor was last told, so Sync can emit only what changed.
 *
 * One per executor, not one per chrome: two presentations of the same model
 * are told about it independently, and an executor that came up late has to be
 * caught up from nothing while the other one is only told the delta.
 */
struct ToriRSChromeShadowWidget
{
    int live;
    /** ToriRSChromeWidget::serial, so a RECYCLED handle is seen as a different
     *  widget rather than as the same one unchanged. Comparing kind and panel
     *  is not enough: a rebuilt panel commonly puts the same kind back on the
     *  same panel under the same handle, in a different row. */
    int serial;
    int kind;
    int panel;
    int tab;
    int hidden;
    int checked;
    int selected;
    int option_count;
    /** Does the model's focus rest here? Shadowed like any other property so
     *  a focus change is one command rather than a re-declaration. */
    int focused;
    uint32_t color;
    char label[TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_INPUT_MAX];
    /**
     * The option array the widget pointed at last time, and how long it was.
     *
     * Compared as a POINTER, deliberately: the lists are borrowed palettes of
     * hundreds of strings and a shadow holding a copy of one would be the
     * largest thing in this file by an order of magnitude. A caller that
     * rewrites a list in place under a stable pointer must say so
     * (ToriRSChrome_DropdownSetOptions with the same pointer still re-sends,
     * because the count or the selection is what it changes in practice).
     */
    char const* const* options;
};

struct ToriRSChromeShadowPanel
{
    int live;
    int style;
    int x;
    int y;
    int w;
    int h;
    int active_tab;
    char title[TORIRS_CHROME_LABEL_MAX];
};

struct ToriRSChromeSync
{
    struct ToriRSChromeExec exec;
    /** begin() has been called and succeeded. Zero disables the whole path. */
    int live;
    /** Sync has never run against this executor, so everything is new. */
    int primed;
    /**
     * The checkbox style this executor was last told about.
     *
     * Seeded to -1 by Init rather than to TICK, so the first Run states the
     * style even when the model is sitting at the default: an executor that
     * was never told cannot know, and "it happens to match the value the
     * shadow was zeroed to" is not being told.
     */
    int check_style;
    /** Commands emitted since Init, for tests and for a "did anything move"
     *  probe that costs nothing to keep. */
    int cmd_count;
    struct ToriRSChromeShadowPanel panels[TORIRS_CHROME_MAX_PANELS];
    struct ToriRSChromeShadowWidget widgets[TORIRS_CHROME_MAX_WIDGETS];
};

/* ---- driving an executor ------------------------------------------------- */

/**
 * Bind `exec` and bring it up. @return 1 when it came up, 0 when it declined.
 *
 * A 0 leaves the sync inert -- every later Run is a no-op -- so a caller that
 * wants a fallback checks this once and binds the buffer executor instead.
 */
int
ToriRSChromeSync_Init(struct ToriRSChromeSync* sync, struct ToriRSChromeExec const* exec);

/** Take the executor down and forget the shadow. Safe on an inert sync. */
void
ToriRSChromeSync_Shutdown(struct ToriRSChromeSync* sync);

/**
 * Emit the difference between `ui` and what this executor was last told.
 * @return how many commands were emitted; 0 means nothing moved.
 */
int
ToriRSChromeSync_Run(struct ToriRSChromeSync* sync, struct ToriRSChrome const* ui);

/**
 * Drain the executor's intents and apply them to `ui`.
 * @return how many were applied.
 */
int
ToriRSChromeSync_Pump(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui);

/**
 * Stretch `panel` over this executor's surface, when the surface is a window
 * of its own. @return 1 when the panel was filled.
 *
 * The rule, in one place, for every presentation that owns its window: a panel
 * floating at (8,72) is right over the game canvas, where the canvas is what
 * it floats over, and wrong in a window that holds nothing else -- three bands
 * of empty background around it, and no growth when the window is dragged
 * wider. Which executors those are is not enumerated here; an executor answers
 * for itself by having a `surface_size` at all, so an executor added later
 * inherits this by implementing one.
 *
 * Called every frame, deliberately: the window's size is the user's to change
 * at any moment and there is no event this side can rely on. A repeat of the
 * size the panel already fills is compared away by ToriRSChrome_PanelFill and
 * costs a rebuild of nothing.
 *
 * @return 0 -- and leaves the panel alone -- for an executor with no window,
 * one that has not come up, and one whose window has since closed. The panel
 * keeps the geometry it had, which for the in-canvas case is the floating box
 * it was authored with.
 */
int
ToriRSChromeSync_FillSurface(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui, int panel);

/**
 * Hand `panel`'s window-move handles to an executor that asked for them.
 * @return 1 when a region was published, 0 for an executor that has no
 * `set_drag_region` -- which is every executor whose window keeps its frame.
 *
 * Called AFTER Build, unlike FillSurface, and the order is the point: the
 * handles are laid-out geometry, and publishing them before the build that
 * produced them is a frame of lag on every resize -- a window whose drag band
 * is where the panel used to be.
 */
int
ToriRSChromeSync_PublishDragRegion(
    struct ToriRSChromeSync* sync, struct ToriRSChrome const* ui, int panel);

/**
 * Apply one intent to the model, as if the in-canvas chrome had been used.
 * @return 1 when it changed something.
 *
 * Public because a host may receive intents by a route this module does not
 * own -- off the command bus, out of a recording, from a message port -- and
 * all of them have to land on the model the same way.
 */
int
ToriRSChromeIntent_Apply(struct ToriRSChrome* ui, struct ToriRSChromeIntent const* intent);

/* ---- the executors ------------------------------------------------------- */

/**
 * The default, on every platform: the in-canvas prim path.
 *
 * It consumes commands and does nothing with them, and that is correct rather
 * than a stub -- the model already draws itself through
 * ToriRSChrome_Build/Prims, so a "buffer executor" that re-implemented drawing
 * from the command stream would be a second renderer of the same pixels. What
 * this exists for is uniformity: a host binds an executor, and the one it binds
 * when there is nothing native to bind is this.
 */
struct ToriRSChromeExec
ToriRSChromeExec_Buffer(void);

/**
 * Turn a display list into pixels in `pixels` (w * h ARGB).
 *
 * Supplied by the host to a surface executor, because rasterising needs the
 * scene the baked fonts and skin live in, the frame translator and a software
 * backend -- three things ui/ deliberately does not depend on. The host already
 * owns all of them for the game canvas, so lending the same one here means the
 * second window is drawn by the same code as the first rather than by a second
 * copy of it that is almost the same.
 */
typedef void (*ToriRSChromeRasteriseFn)(
    void* user,
    int* pixels,
    int width,
    int height,
    struct ToriRSChromePrim const* prims,
    int count);

/**
 * Build the vtable for `kind`, or the buffer one when this build cannot.
 *
 * Every executor is optional at BUILD time as well as at run time -- a macOS
 * client has no GDI executor compiled in at all -- so asking for one that is
 * not here is answered the same way as one that will not start: with the
 * buffer executor, which every build has. `out_kind` reports what was actually
 * produced, so a caller can say so rather than let the difference go unnoticed.
 */
struct ToriRSChromeExec
ToriRSChromeExec_ForKind(
    int kind, void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user,
    int* out_kind);

/* ---- the client-chrome id space ------------------------------------------
 *
 * Component ids the client allocates for furniture IT owns inside the game's
 * interface tree. Today that is one control -- the plugin window's "Manage
 * Plugins" launcher -- but the range is what makes it work, so it lives here
 * rather than beside the button.
 *
 * Group 0x7FFE, which is not an arbitrary high number: it is the group
 * UITree_RootIsDisplayable already recognises as "app-overlay chrome". A root
 * in any other unmounted group is deliberately dropped by the emit walk --
 * that filter exists so a CS2 script auto-mounting an interface for property
 * access cannot cover the gameframe with it -- so a private range picked for
 * being far away renders nothing at all. This one is the tree's own answer to
 * "chrome the app built", and using it is what makes the control displayable.
 *
 * A range rather than a registry, because a component id is how a click gets
 * home: the tree reports one, and the host has to recognise it as the chrome's
 * before the game's own dispatch sees it. A bounds test does that in a line.
 */
#define TORIRS_CHROME_GROUP 0x7FFE
#define TORIRS_CHROME_ID_BASE (TORIRS_CHROME_GROUP << 16)
#define TORIRS_CHROME_ID_END (TORIRS_CHROME_ID_BASE + 0x10000)

/**
 * The "Manage Plugins" button, and the four pieces of drawing under it.
 *
 * The container carries the op and answers the click; the three graphics and
 * the label are decoration. Five ids rather than one because the plate is the
 * interfaces' own wide stone button -- two 36px caps with a 20px tile stretched
 * between them -- and a label over the top, which is exactly how the 2004
 * profile authors the same control (`manage_plugins_*` in
 * revconfig/rs245_2lc). The client builds it on lanes whose gameframe comes
 * out of a cache and cannot be authored.
 *
 * Placed well clear of the base so the group has room for whatever else the
 * client puts in the tree later.
 */
#define TORIRS_CHROME_PLUGIN_BUTTON_ID (TORIRS_CHROME_ID_BASE + 0x8000)
#define TORIRS_CHROME_PLUGIN_CAP_LEFT_ID (TORIRS_CHROME_PLUGIN_BUTTON_ID + 1)
#define TORIRS_CHROME_PLUGIN_CAP_MID_ID (TORIRS_CHROME_PLUGIN_BUTTON_ID + 2)
#define TORIRS_CHROME_PLUGIN_CAP_RIGHT_ID (TORIRS_CHROME_PLUGIN_BUTTON_ID + 3)
#define TORIRS_CHROME_PLUGIN_LABEL_ID (TORIRS_CHROME_PLUGIN_BUTTON_ID + 4)

/**
 * One piece of a plate CUT FROM a lane's own button, rather than from the
 * baked skin: the client copies that button's graphics so its own control is
 * the same material as the ones beside it, and each copy needs an id.
 *
 * Past the five above with room to spare, and capped well above what a button
 * is made of (rev-239's logout plate is four children) so that a profile
 * naming something enormous as the anchor stops copying rather than running
 * into the ids of whatever the client adds here next.
 */
#define TORIRS_CHROME_PLUGIN_PIECE_ID(i) (TORIRS_CHROME_PLUGIN_BUTTON_ID + 16 + (i))
#define TORIRS_CHROME_PLUGIN_PIECE_MAX 16

struct UITree;

/**
 * May client-built chrome be added to this tree yet?
 *
 * It may not be the FIRST root, and that is not a style rule.
 * UITree_RootIsDisplayable derives "the active gameframe's group" from
 * whatever root_index happens to point at, and every other root is displayable
 * only if it matches. So a chrome root that arrives first makes the gameframe's
 * own group the odd one out: every real root stops being displayable and the
 * screen goes black -- which is exactly what a plugin button added one frame
 * too early did.
 *
 * @return 1 once a non-chrome root exists to define that group.
 */
int
ToriRSChrome_TreeAcceptsChrome(struct UITree const* tree);

#if defined(TORIRS_CHROME_EXEC_GDI_AVAILABLE)
/* ---- the Win32 native-widget executor (ui/torirs_chrome_exec_gdi.c) ------- */

/** An owned tool window of USER32 controls.
 *  @param platform struct PlatformWindow*, as every executor is handed. */
struct ToriRSChromeExec
ToriRSChromeExec_Gdi(void* platform);
#endif

#if defined(TORIRS_CHROME_EXEC_WEB_AVAILABLE)
/* ---- the web native-widget executor (ui/torirs_chrome_exec_web.c) --------- */

/** Real DOM controls, built by the page from the command stream. Takes no
 *  platform handle: the page it talks to is a global, not a window this owns. */
struct ToriRSChromeExec
ToriRSChromeExec_Web(void);
#endif

#if defined(TORIRS_CHROME_EXEC_ANDROID_AVAILABLE)
/* ---- the Android native-widget executor -------------------------------- */

/** Framework Views in ClientActivity's one shared plugin-chrome pane.
 *  No WebView and no second Activity or OS window. */
struct ToriRSChromeExec
ToriRSChromeExec_Android(void* platform);
#endif

#if defined(TORIRS_CHROME_EXEC_SDL_AVAILABLE)
/* ---- the SDL surface executor (ui/torirs_chrome_exec_sdl.c) --------------- */

/** @param platform struct PlatformWindow*, void* so ui/ needs no platform header. */
struct ToriRSChromeExec
ToriRSChromeExec_Sdl(void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user);

/** Is the aux window up? */
int
ToriRSChromeExecSdl_IsOpen(void);

/**
 * Ask the plugin window to open without an OS frame, so the panel's own title
 * bar and tab strip are what move it.
 *
 * A WISH, set before the window opens rather than an instruction to a live one:
 * the shell knows what the manifest said long before anyone presses the button
 * that opens the window, and the frame comes off at open. Held outside the
 * executor's own state, which is reset every time one is built.
 *
 * TORIRS_CHROME_BORDERLESS overrides it, matching TORIRS_CHROME_EXECUTOR and
 * TORIRS_CHROME_THEME beside it.
 */
void
ToriRSChromeExecSdl_SetBorderless(int borderless);

/** Did the window actually come up without one? False when the wish was never
 *  made, and when the video driver refused the hit test it needs. */
int
ToriRSChromeExecSdl_IsBorderless(void);
#endif

/**
 * A test double: records every command and replays queued intents.
 *
 * Not conditionally compiled. It is the only executor that can assert on what
 * the seam actually emitted, so it is what the sync tests drive, and a
 * recording of a real session is what a native executor gets developed
 * against before it has a window to draw into.
 */
#define TORIRS_CHROME_RECORD_MAX 512

struct ToriRSChromeRecorder
{
    struct ToriRSChromeCmd cmds[TORIRS_CHROME_RECORD_MAX];
    int count;
    /** Set when the recorder filled up; the cmds array holds the first
     *  TORIRS_CHROME_RECORD_MAX and `count` stops there. */
    int overflow;
    int begun;
    /** Intents to hand back on the next poll, queued by a test. */
    struct ToriRSChromeIntent pending[32];
    int pending_count;
    /** Made to fail begin(), to exercise the fallback path. */
    int refuse;
    /**
     * Non-zero makes the recorder a presentation with a WINDOW of its own,
     * this big -- so the fill rule can be exercised, and its interaction with
     * the drag and the grip pinned, on a machine with no display at all.
     *
     * Zero (the default) is the other half of the same test: an executor with
     * no window of its own must leave the panel's authored geometry alone.
     */
    int surface_w;
    int surface_h;
    /**
     * The last region published to it, and how many times one was.
     *
     * The recorder always accepts a region, even at `surface_w` 0. What is
     * being pinned is the CHROME's answer -- which boxes drag and which are
     * punched out of them -- and that is a property of the model, not of
     * whether a real window happened to open. A test that needed a window for
     * it could only run where there is a display, which is nowhere this suite
     * runs.
     */
    struct ToriRSChromeDragRegion drag;
    int drag_publishes;
};

void
ToriRSChromeRecorder_Init(struct ToriRSChromeRecorder* rec);

struct ToriRSChromeExec
ToriRSChromeExec_Recorder(struct ToriRSChromeRecorder* rec);

/** Queue an intent for the recorder's next poll. */
void
ToriRSChromeRecorder_PushIntent(
    struct ToriRSChromeRecorder* rec, struct ToriRSChromeIntent const* intent);

/** @return how many recorded commands are of `kind`. */
int
ToriRSChromeRecorder_CountKind(struct ToriRSChromeRecorder const* rec, int kind);

/** @return the first recorded command of `kind` naming `widget`, or NULL. */
struct ToriRSChromeCmd const*
ToriRSChromeRecorder_Find(struct ToriRSChromeRecorder const* rec, int kind, int widget);

#endif
