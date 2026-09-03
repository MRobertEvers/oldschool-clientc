#ifndef SRC_GAME_RS_UI_SLOTS_H
#define SRC_GAME_RS_UI_SLOTS_H

#include <stdint.h>

struct UITree;
struct App;

/*
 * Runtime interface-slot state, mirroring the reference client's fields
 * (Client-TS mainModalId / sideModalId / sideOverlayId[] / chatComId /
 * tutComId / sideTab). Which interface occupies a slot is runtime state the
 * server (or a user click) mutates; the slot GEOMETRY stays in RevConfig.
 *
 * All defaults here come from the baked tree, which comes from the INI:
 * side_overlay_id[] seeds from each sidebar node's componentno= and the boot
 * tab from selected= — no interface id or tab number is hardcoded in C.
 */

#define RS_UI_SLOTS_TAB_MAX 15

enum RS_UIChatFilter
{
    RS_UI_CHAT_FILTER_PUBLIC = 0,
    RS_UI_CHAT_FILTER_PRIVATE,
    RS_UI_CHAT_FILTER_TRADE,
    RS_UI_CHAT_FILTER_REPORT,
    RS_UI_CHAT_FILTER_COUNT,
};

struct RS_UISlots
{
    int side_tab;
    /** Interface id per tab, or -1 = empty (reference sideOverlayId[15]). */
    int side_overlay_id[RS_UI_SLOTS_TAB_MAX];
    /** Tree node index of each tab's sidebar container, or -1. */
    int32_t side_owner_index[RS_UI_SLOTS_TAB_MAX];

    int side_modal_id;
    int main_modal_id;
    int main_overlay_id;
    int chat_com_id;
    int tut_com_id;

    /** Tree node index of each slot= mount region, or -1 when the layout has
     *  none (found by slot tag during InitFromTree, never by coordinates). */
    int32_t main_modal_index;
    int32_t main_overlay_index;
    int32_t side_modal_index;
    int32_t chat_index;

    /** Privacy-bar modes (reference chatPublicMode/PrivateMode/TradeMode);
     *  indexed by enum RS_UIChatFilter. Report has a single mode. */
    int chat_filter_mode[RS_UI_CHAT_FILTER_COUNT];
    /** TUT_FLASH: tab icon to flash (-1 = none). */
    int flash_tab;
};

void
RS_UISlots_Init(struct RS_UISlots* slots);

/**
 * Seed tab state from the baked tree: for every UIELEM_BUILTIN_SIDEBAR node,
 * record its componentno as the tab's interface (-1 stays empty, matching the
 * reference's unassigned sideOverlayId gate) and adopt the INI-selected boot
 * tab. Call after every full tree (re)build.
 */
void
RS_UISlots_InitFromTree(
    struct RS_UISlots* slots,
    struct UITree const* tree);

/** Nonzero when the tab has an interface assigned (icon draws, click works). */
int
RS_UISlots_TabEnabled(
    struct RS_UISlots const* slots,
    int tabno);

/**
 * Nonzero when the SERVER has given this player tab `tabno` -- the question a
 * gameframe asks before it draws that tab's icon and its pressed stone.
 *
 * The lane-aware form of the line above, because "the server took that tab
 * away" is spelled differently by different frames and none of the spellings
 * is a gameframe's business:
 *
 *   rung 1  The frame carries its own sidebar mounts (every dat1 lane, and any
 *           profile that builds its gameframe out of the INI). There the tab is
 *           IF_SETTAB's and RS_UISlots_TabEnabled is the whole answer.
 *
 *   rung 2  The profile NAMES the tab: `[role:sidetab_<n>]`, bound to whatever
 *           carries that tab's fate on this lane -- on a cache gameframe the
 *           toplevel's own icon, which the cache's scripts hide with
 *           IF_SETHIDE. Not resolving is a tab that is not there; resolving
 *           hidden is one that has been taken away.
 *
 * BOTH when both are stated, and any authority that says hidden wins: a lane
 * that states both is describing one fact twice, and disagreement means one of
 * the two has gone stale.
 *
 * A lane that states neither answers 1 -- nothing on it claims the tab is
 * hidden, and a frame that drew no icons at all is a worse wrong than one that
 * drew every icon.
 *
 * @see ToriRS_CacheApiV2::tab_enabled, which is this verb reaching a plugin.
 */
int
RS_UISlots_TabGiven(struct App* app, int tabno);

/**
 * Advance a privacy-bar filter to its next mode (reference gameLoop click
 * handlers: public cycles 4 modes, private/trade 3, report does not cycle).
 * Returns the new mode.
 */
int
RS_UISlots_CycleChatFilter(
    struct RS_UISlots* slots,
    int filter);

/** How many modes this filter cycles through, or 0 for a filter that is not
 *  one. Report abuse answers 1: it is a click-through, not a toggle. */
int
RS_UISlots_ChatFilterModeCount(int filter);

/**
 * Set one filter's mode outright. @return 1 when it took.
 *
 * The twin of the cycle above, and both are needed because the two gestures
 * mean different things: a click steps to the next mode, and a menu row names
 * the one it means. @see RS_UISlots_SetChatFilter's body.
 */
int
RS_UISlots_SetChatFilter(
    struct RS_UISlots* slots,
    int filter,
    int mode);

/*
 * Runtime open/close API — the contract the network exec layer calls when
 * IF_OPENMAIN (197) / IF_OPENSIDE (187) / IF_OPENCHAT (141) / IF_CLOSE (174) /
 * IF_SETTAB (91) arrive; semantics mirror Client-TS tcpIn. Each updates the
 * slot ids and re-bakes the affected mount region synchronously.
 */

/** IF_OPENMAIN: open in the viewport; clears any chat dialog. */
void
RS_UISlots_OpenMain(struct App* app, int iface_id);

/** IF_OPENMAIN_SIDE: viewport modal + side modal together (e.g. bank). */
void
RS_UISlots_OpenMainSide(struct App* app, int main_iface_id, int side_iface_id);

/** IF_OPENSIDE: side modal replacing the tab area until closed. */
void
RS_UISlots_OpenSide(struct App* app, int iface_id);

/** IF_OPENCHAT: chatback dialog; suppresses chat message drawing. */
void
RS_UISlots_OpenChat(struct App* app, int iface_id);

/** IF_OPENOVERLAY: mount over the viewport (main_overlay slot). */
void
RS_UISlots_OpenOverlay(struct App* app, int iface_id);

/**
 * TUT_OPEN: the tutorial-progress interface.
 *
 * It has no region of its own. The reference draws it in the CHAT area, and
 * only when no chat dialogue is there to take precedence -- one `else if` in
 * drawChat -- so it shares the chat builtin's region here and loses to a chat
 * dialogue the same way. A gameframe therefore needs no `slot=tut` to show it,
 * which is just as well: no revconfig in this tree declares one, so the whole
 * feature was a mount into region -1 and a line on stderr.
 */
void
RS_UISlots_OpenTut(struct App* app, int iface_id);

/**
 * What the chat region is showing: the IF_OPENCHAT dialogue if one is mounted,
 * otherwise the tutorial-progress component, otherwise -1.
 *
 * The chat builtin's own content -- the message log and the input line -- is
 * suppressed whenever this is not -1, which is what makes the reference's
 * if/else-if chain hold: whatever is mounted in the region draws INSTEAD of
 * the log, not over it.
 */
int
RS_UISlots_ChatRegionIface(struct RS_UISlots const* slots);

/**
 * Should tab `tabno`'s icon be hidden on the frame at `logic_cycle`?
 *
 * The flash is a gap, not a highlight: the flagged tab's icon is simply not
 * drawn for half of every 20-tick cycle (reference drawSidebarIcons). Answers 0
 * for every tab that is not the flagged one, so a caller can ask it about all
 * of them.
 */
int
RS_UISlots_TabFlashHidden(
    struct RS_UISlots const* slots,
    int tabno,
    uint64_t logic_cycle);

/** IF_CLOSE: close main modal, side modal, and chat dialog. */
void
RS_UISlots_CloseModal(struct App* app);

/** IF_SETTAB: assign iface to a tab slot (iface_id < 0 or 65535 clears). */
void
RS_UISlots_SetTab(struct App* app, int tabno, int iface_id);

/** User/tutorial tab flip (no mount change, just selection). */
void
RS_UISlots_SetSideTab(struct App* app, int tabno);

#endif
