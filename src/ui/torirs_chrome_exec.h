#ifndef SRC_TORIRS_CHROME_EXEC_H
#define SRC_TORIRS_CHROME_EXEC_H

/*
 * ToriRSChrome executors — the seam between the retained widget model and
 * whatever actually presents it.
 *
 * The chrome (ui/uitree_debug_overlay.h) is a model that emits a display list
 * of rectangles, glyphs and sprites. That list is exactly the right altitude
 * for a rasteriser and the wrong one for a DOM <input>, which cannot be
 * reconstructed from RECT/TEXT/SPRITE. So this layer emits the chrome's own
 * vocabulary instead — panels, tabs, widgets, properties — and each web
 * executor maps a *checkbox* onto its DOM control.
 *
 * THE MODEL STAYS AUTHORITATIVE. An executor is a projection of it, never a
 * second copy of the truth: commands flow out, intents flow back, and an intent
 * is applied by mutating the model exactly as a click on the in-canvas chrome
 * would. That keeps the WEB and BROWSER projections aligned with the internal
 * in-canvas presenter and lets the whole seam be tested with no window.
 *
 * MUTATION JOURNAL, NO MODEL SCAN. Mutators record the changed node and
 * property in the retained model. Sync drains only those records and compares
 * each against what this executor was last told; it never scans every panel or
 * widget to discover work. Repeated writes coalesce, an idle tick is O(1), and
 * DOM controls survive their updates with focus and caret intact. A
 * complete walk is reserved for initial bind, rebind/reconnect, or explicit
 * queue-loss recovery.
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
#include "torirs_chrome_rail.h"
#include "uitree_debug_overlay.h"

#include <stdint.h>

/** Bytes of one command's string payload, terminator included. */
#define TORIRS_CHROME_TEXT_MAX TORIRS_CHROME_INPUT_MAX

/** Complete atomic transaction capacity shared by WEB and BROWSER. */
#define TORIRS_CHROME_PROTOCOL_COMMAND_MAX 8192

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
    /**
     * A transaction opens. `value` is 1 when this one RESTATES A NEW PAGE and
     * 0 when it patches the page already up; on a restatement `serial` carries
     * that page's IDENTITY (0 when none is stated).
     *
     * That flag is the whole of how a page BOUNDARY reaches an executor, and
     * it travels here -- on the page stream, immediately ahead of the
     * commands that replace the page -- rather than being inferred from a
     * selection generation arriving on the rail channel.
     *
     * The distinction is not cosmetic, and getting it from the rail was a bug
     * factory. The application's shadow of what an executor holds and the
     * executor's own retained page are two caches of one thing; if they are
     * reset by two different signals then they are reset on two different
     * frames, and every ordering of those frames is a separate failure. Reset
     * first and the following restatement is emitted as a patch to a page that
     * is already gone; restate first and the reset that follows wipes it, with
     * the shadow now claiming the executor has a page it threw away -- so the
     * next transaction is empty and the pane stays blank until something
     * unrelated dirties the model.
     *
     * One signal, in the stream, in order. An executor that retains a page
     * drops it here and treats what follows as a complete image; one that does
     * not retain anything ignores the flag and is correct either way.
     */
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
     *  Advisory: a browser executor may let its host window or page layout
     *  place and size the thing. */
    TORIRS_CHROME_CMD_PANEL_RECT,
    /**
     * `value` = which tab of this panel is showing.
     *
     * Not advisory, unlike the rect: a widget's `tab` says which tab OWNS it
     * and this says which one is up, and an executor needs both to decide what
     * to show. Without it a web executor can see that widget 7 belongs to
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
     *   `h` -- a TEXTAREA is this many lines tall; a CUSTOM region is this
     *          many logical chrome pixels tall.
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
     * An executor whose DOM controls own their own focus
     * may act on it to keep the two in step, or ignore it. Sent for every
     * widget kind, because MODELVIEW takes the focus too.
     */
    TORIRS_CHROME_CMD_WIDGET_FOCUS,

    /**
     * The option list of a dropdown, or the titles of a tab strip, is about to
     * be restated in full: `value` = how many follow.
     *
     * Restated as one value because these palettes are rebuilt wholesale
     * (every loc name in a search, every plugin in a manifest), and a DOM
     * select wants "here is the current list", not inferred insert/delete
     * operations.
     */
    /** `x` is 1 for lossless structured options, 0 for legacy label lists. */
    TORIRS_CHROME_CMD_WIDGET_OPTIONS,
    /**
     * One entry of the list just announced: `value` = its index. Legacy rows
     * use `text` as their label. Structured rows use `text` as the stable
     * value, `label` as presentation, `x` as enabled, and `detail` as the
     * human availability explanation.
     */
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

    /**
     * A CUSTOM well is now this many logical chrome pixels tall: `h`.
     *
     * The height rides WIDGET_ADD too, and used to ride only it, on the
     * reasoning that it was the widget's SHAPE and shape never changes after
     * an add. That stopped being true the moment a well could be resized in
     * place -- which it must be, because a list that grows a row is not a
     * different page and re-declaring one to say so is a visible flash. A
     * classification of "shape" against "state" is exactly the kind of thing
     * that silently stops holding; a resize with no command to carry it left
     * every web executor drawing the height the well was born with,
     * with the strip clipped or floating in dead space.
     *
     * Sent for CUSTOM only, and only on a change. An executor that lays out
     * from the display list never sees it and does not need to.
     */
    TORIRS_CHROME_CMD_WIDGET_HEIGHT,
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
    /** ToriRSChromeWidget::serial on WIDGET_ADD, zero otherwise. */
    uint32_t serial;
    char label[TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_TEXT_MAX];
    char detail[TORIRS_CHROME_TEXT_MAX];
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
    /** A CUSTOM well was released: `x`/`y` are content-local logical units. */
    TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE,
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
    /** CUSTOM_ACTIVATE only: content-local logical coordinates. */
    int x;
    int y;
    /** CUSTOM_ACTIVATE only: semantic page/node identity from the frame that
     * received the pointer. Zero is the synchronous surface-model path. */
    uint32_t selection_generation;
    uint32_t widget_serial;
    char text[TORIRS_CHROME_TEXT_MAX];
};

/**
 * One complete raster for a CUSTOM semantic widget.
 *
 * Only WEB/BROWSER executors consume this. BUFFER composites the retained
 * primitives directly. `argb` is call-scoped: an
 * executor crossing to another thread must copy it before returning. The two
 * identities fence an asynchronous DOM update from a recycled handle.
 */
struct ToriRSChromeCustomFrame
{
    int panel;
    int widget;
    uint32_t selection_generation;
    uint32_t widget_serial;
    /** Device pixels per logical unit, in thousandths. */
    int scale_milli;
    int width;
    int height;
    int stride;
    uint32_t const* argb;
};

/**
 * What a presentation has to implement.
 *
 * WEB/BROWSER rebuild the retained model as DOM controls. They cannot use the
 * display list -- an <input> cannot be reconstructed from rectangles -- so
 * the semantic command stream is the contract. BUFFER is an internal sink for
 * that stream because the same model already draws itself in the game canvas.
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
     * browser support, a blocked page, a missing embedded engine.
     *
     * A false here is NOT an error: the host falls back to the buffer
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
     * WEB/BROWSER report everything here: their controls are foreign, so a
     * click on one is only ever an intent.
     */
    int (*poll)(void* user, struct ToriRSChromeIntent* out, int max);

    /**
     * Application-owned rail, independent of the page executor lifecycle.
     *
     * These callbacks are valid before begin() and after end(): end removes
     * the selected page controls, while the narrow rail must remain able to
     * select a plugin and expand the one shared shell. Snapshots and returned
     * intents are fixed-size copies; no plugin-owned pointer crosses here.
     */
    int (*rail_sync)(
        void* user, struct ToriRSChromeRailSnapshot const* snapshot);
    int (*rail_icon)(void* user, struct ToriRSChromeRailIcon const* icon);
    int (*rail_poll)(void* user, struct ToriRSChromeRailIntent* out, int max);

    /** Optional web-canvas sink for a complete dirty custom-region frame.
     * Called only after the batch containing its WIDGET_ADD has ended. */
    int (*custom_present)(
        void* user, struct ToriRSChromeCustomFrame const* frame);

    /**
     * Consume an executor-side loss request.
     *
     * A bounded transport that could not deliver one atomic transaction sets
     * this latch. Sync checks it before the next drain and responds with one
     * full snapshot, rather than applying later deltas to a stale DOM.
     */
    int (*take_snapshot_request)(void* user);

    /** Same loss contract for the independently retained rail and icons. */
    int (*take_rail_snapshot_request)(void* user);
};

/**
 * What the executor was last told for nodes named by queued changes.
 *
 * This is not searched per frame. The retained model supplies exact node and
 * property records; these constant-size shadows only collapse net-zero setter
 * bursts and fence recycled identities. A newly bound executor gets a one-time
 * full snapshot.
 */
struct ToriRSChromeShadowWidget
{
    int live;
    /** ToriRSChromeWidget::serial, so a RECYCLED handle is seen as a different
     *  widget rather than as the same one unchanged. Comparing kind and panel
     *  is not enough: a rebuilt panel commonly puts the same kind back on the
     *  same panel under the same handle, in a different row. */
    int serial;
    uint32_t intent_serial;
    int kind;
    int panel;
    int tab;
    int hidden;
    int checked;
    int selected;
    int option_count;
    /** Hash of the bounded option strings last delivered to this executor. */
    uint64_t options_hash;
    /** Does the model's focus rest here? Shadowed like any other property so
     *  a focus change is one command rather than a re-declaration. */
    int focused;
    /** A CUSTOM well's height in the LOGICAL units the command carries, so the
     *  comparison is against what the executor was actually told rather than
     *  against a scaled value that rounds differently. */
    int view_h;
    uint32_t color;
    char label[TORIRS_CHROME_LABEL_MAX];
    char text[TORIRS_CHROME_INPUT_MAX];
    /** Mount of the owning panel for which this shadow is valid. */
    uint32_t panel_mount;
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
    /** Advanced on every PANEL_OPEN; closing a panel invalidates rows in O(1). */
    uint32_t mount;
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
     * The next transaction restates a NEW page, so its SYNC_BEGIN says so.
     *
     * Set by Invalidate and cleared by the Run that reports it, which is what
     * makes the announcement survive an invalidation on a frame where nothing
     * else moved -- the flag waits for a transaction rather than needing one
     * to exist at the moment the page changed.
     */
    int restate;
    /** Set only by a successful full snapshot in the immediately preceding
     * Run. Consumers use it to restate separately retained bitmap payloads. */
    int last_run_restate;
    /**
     * WHICH page the pending restatement is of, reported beside the flag.
     *
     * The identity has to travel with the page for the same reason the
     * boundary does. An executor that learned the new identity from the rail
     * instead stamped the snapshot with the identity of the page it had just
     * discarded -- and then, when the rail did arrive, saw a generation it had
     * never been told about, concluded the page had changed again, and closed
     * the snapshot it had only just mounted.
     *
     * 0 means "no identity stated", which is what a fresh binding reports:
     * there is no page being replaced, so an executor keeps whatever fallback
     * it uses for a first transaction.
     */
    uint32_t page_epoch;
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
    /** Monotonic panel-mount identity used to invalidate a closed subtree in O(1). */
    uint32_t next_panel_mount;
    /**
     * Actual journal-record inspections since Init, for efficiency tests.
     * A non-empty drain currently makes one validation pass and five fixed
     * protocol-order passes; idle runs add zero. This deliberately counts
     * every pass rather than presenting semantic-record count as work done.
     */
    uint32_t change_visit_count;
    /** Transient lazy-transaction state, non-zero only inside Run. */
    int transaction_open;
    int transaction_restate;
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
 * Drain retained changes recorded by `ui` since the previous call.
 *
 * Ordinary runs visit only changed nodes; they never scan the tree or shadow.
 * A first bind, explicit invalidation, or lost/overflowed queue emits one full
 * snapshot and then returns to queue draining.
 * @return how many commands were emitted; 0 means nothing moved.
 */
int
ToriRSChromeSync_Run(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui);

/**
 * Forget the delivered shadow WITHOUT taking the executor down, and mark the
 * next Run as one authoritative restatement.
 *
 * A page-retaining executor drops its DOM when the shared shell's selection
 * moves. Queued property commands assume their panel/widget roots still exist,
 * so they cannot rebuild that discarded page even when the next page happens
 * to use similar handles. Invalidation makes the next Run walk the model once,
 * emit every root and property, and stamp the transaction with `page_epoch`.
 *
 * So this is called by whoever CHANGES the selection, not by the executor that
 * reacted to it -- the delivered shadow is the application's, and the executor
 * cannot reach it. Cheap and idempotent: it marks a restatement; it does not
 * talk to the executor immediately.
 */
void
ToriRSChromeSync_Invalidate(struct ToriRSChromeSync* sync, uint32_t page_epoch);

/**
 * Drain the executor's intents and apply them to `ui`.
 * @return how many were applied.
 */
int
ToriRSChromeSync_Pump(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui);

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
 * Its vtable is empty: the model already draws itself through
 * ToriRSChrome_Build/Prims. Sync acknowledges its queue without constructing a
 * command transaction; a later WEB/BROWSER bind catches up by full snapshot.
 */
struct ToriRSChromeExec
ToriRSChromeExec_Buffer(void);

/**
 * Build the vtable for WEB/BROWSER, or the internal buffer fallback when this
 * build cannot. `kind == -1` chooses the supported web executor for this lane.
 *
 * WEB and BROWSER are optional at build time. No SDL, GDI, Android or generic
 * PLATFORM kind is accepted here, and BUFFER is never a public selection;
 * BUFFER is solely the safe in-canvas result when the requested web transport
 * is absent. `out_kind` reports what was actually produced.
 */
struct ToriRSChromeExec
ToriRSChromeExec_ForKind(int kind, void* platform, int* out_kind);

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

#if defined(TORIRS_CHROME_EXEC_BROWSER_AVAILABLE)
/* ---- attached local-bundle browser executor ---------------------------- */

/** One shared local DOM bundle in WebView2 (win64) or MSHTML (XP). */
struct ToriRSChromeExec
ToriRSChromeExec_Browser(void* platform);
#endif

#if defined(TORIRS_CHROME_EXEC_WEB_AVAILABLE)
/* ---- the Emscripten page-DOM executor (ui/torirs_chrome_exec_web.c) ------- */

/** Real DOM controls, built by the page from the command stream. Takes no
 *  platform handle: the page it talks to is a global, not a window this owns. */
struct ToriRSChromeExec
ToriRSChromeExec_Web(void);
#endif

/**
 * A test double: records every command and replays queued intents.
 *
 * Not conditionally compiled. It is the only executor that can assert on what
 * the seam actually emitted, so it is what the sync tests drive, and a
 * recording of a real session is what a web executor gets developed
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
