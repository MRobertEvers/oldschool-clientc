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
    int32_t tut_index;

    /** Privacy-bar modes (reference chatPublicMode/PrivateMode/TradeMode);
     *  indexed by enum RS_UIChatFilter. Report has a single mode. */
    int chat_filter_mode[RS_UI_CHAT_FILTER_COUNT];
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
 * Advance a privacy-bar filter to its next mode (reference gameLoop click
 * handlers: public cycles 4 modes, private/trade 3, report does not cycle).
 * Returns the new mode.
 */
int
RS_UISlots_CycleChatFilter(
    struct RS_UISlots* slots,
    int filter);

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
