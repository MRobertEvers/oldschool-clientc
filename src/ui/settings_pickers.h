#ifndef SRC_UI_SETTINGS_PICKERS_H
#define SRC_UI_SETTINGS_PICKERS_H

/*
 * The two All Settings pickers the client builds itself: the colour swatch
 * editor and the number entry.
 *
 * Neither exists in the cache. The rows that open them are cache-authored --
 * a title, a description and a control with a "Select" op -- but the op's
 * script plays a click and returns, because in the reference the picker is the
 * ENGINE's: it opens its own and writes the row's varp itself. Read one way
 * that makes every colour row and every number row in the panel inert; read
 * the other way the cache has stated everything except the part only a client
 * can do. This is that part.
 *
 * A picker is a panel in a chrome instance the caller owns, not an instance of
 * its own, so the caller's Build/Prims/emit plumbing carries it for free. That
 * is also why the activation handling below is so careful: the latch is shared
 * with whatever else draws into the same instance.
 *
 * What is NOT here is where a popup goes -- UITree_PlacePopupBesideAnchor
 * answers that for both of these and for anything else that opens beside a
 * row -- and what a committed value means to the interface, which is the CS2
 * host's. The commit is a callback for exactly that reason: this module knows
 * a varp id and an integer, and nothing about what either is for.
 */

#include "game/rs_cs2_host.h" /* the two request types a row's op raises */

#include <stdbool.h>

struct ToriRSChrome;
struct UITree;

/**
 * Both pickers' state: the panel and widget ids the chrome handed back, a
 * visibility flag of our own, and the row each picker is open for.
 *
 * The visible flag is not redundant with the panel's. The panel's own Close
 * button hides it without telling anyone, so the two have to be reconciled
 * every tick -- and left unreconciled, the next click on the same swatch
 * "reopens" something that is already open.
 */
struct UISettingsPickers
{
    int colour_panel;
    int colour_pick;
    int colour_default_button;
    int colour_close_button;
    int colour_visible;
    /** The row the open picker belongs to, so a commit knows which varp to
     *  write and a closed All Settings knows to take the picker with it. */
    struct RS_CS2SettingsColourRequest colour_request;

    int number_panel;
    int number_input;
    int number_close_button;
    int number_visible;
    struct RS_CS2SettingsNumberRequest number_request;
};

/**
 * Where a chosen value goes: the varp the request named, in the row's own
 * encoding, which the two pickers do NOT agree about -- see the tick
 * functions.
 *
 * A callback rather than a direct write, because a varp id and an integer is
 * the whole of what this module knows. What that varp does to the interface --
 * the row's own var-transmit hook repainting its swatch, the tile markers
 * re-running their setup script in the new colour -- is the host's business
 * and happens without anything here being told.
 */
typedef void (*UISettingsPickerCommitFn)(void* userdata, int varp_id, int value);

/**
 * Create both panels in `chrome`, hidden. The chrome instance owns them, so
 * there is no matching teardown: the pickers hold ids into it and nothing
 * else.
 */
void
UISettingsPickers_Init(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome);

/**
 * Open the colour picker for `request`, beside the swatch that raised it.
 *
 * A request naming no varp opens nothing and says so: the read hub never found
 * where this row stores its colour, and a picker whose every move is discarded
 * is worse than none.
 */
void
UISettingsPickers_OpenColour(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    struct RS_CS2SettingsColourRequest const* request);

/** The same, for a number row's entry box. */
void
UISettingsPickers_OpenNumber(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    struct RS_CS2SettingsNumberRequest const* request);

/**
 * Drive the open colour picker for one frame, committing what it is told.
 * Returns non-zero when something happened that has to be redrawn.
 *
 * Call it after the input for this frame has been routed into `chrome`.
 *
 * A colour commits as `value + 1`, which is what the row reads back with
 * `calc(%var - 1)` and what makes a varp of 0 mean "never chosen" rather than
 * "black". The Default button commits the row's authored colour VERBATIM
 * rather than the nearest entry the picker's axes can hold -- restoring an
 * approximation would mean "Default" never quite got back to where the row
 * started -- while the picker still shows the nearest, because that is
 * honestly what the next pick would produce.
 */
int
UISettingsPickers_ColourTick(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    UISettingsPickerCommitFn commit,
    void* userdata);

/**
 * The same for the number entry, which commits on Enter and again on Done.
 *
 * A number commits PLAIN, unlike a colour's `value + 1`: zero is a real answer
 * for every one of these rows -- a price threshold of 0 colours everything at
 * that tier, a line limit of 0 hides the overlay -- so there is no
 * never-chosen sentinel to make room for. A field emptied and confirmed
 * commits zero rather than being ignored, for the same reason.
 *
 * What is typed is clamped to [0, INT_MAX] rather than asserted. This is a
 * number a person typed, and "2000000000000" is a typo and not a caller's bug.
 */
int
UISettingsPickers_NumberTick(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    UISettingsPickerCommitFn commit,
    void* userdata);

/** True while a picker is up. Both, because either one covers the row. */
bool
UISettingsPickers_ColourVisible(struct UISettingsPickers const* pickers);
bool
UISettingsPickers_NumberVisible(struct UISettingsPickers const* pickers);

#endif /* SRC_UI_SETTINGS_PICKERS_H */
