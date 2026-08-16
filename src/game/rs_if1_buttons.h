#ifndef SRC_GAME_RS_IF1_BUTTONS_H
#define SRC_GAME_RS_IF1_BUTTONS_H

/*
 * IF1 static-button engine: what happens when a classic buttonType-bearing
 * cache component is clicked (reference useMenuOption IF_BUTTON /
 * TOGGLE_BUTTON / SELECT_BUTTON / PAUSE_BUTTON / CLOSE_BUTTON branches).
 * These components have no CS2 hook — behavior comes from buttonType +
 * the component's IF1 value script (scripts[0] opcode 5 = varp).
 *
 * Server notification goes through the sink; with no network attached the
 * sink callbacks are NULL and clicks apply locally only.
 */

struct App;

struct RS_IF1ButtonSink
{
    void* user;
    /** ClientProt IF_BUTTON (com id payload). */
    void (*if_button)(void* user, int com_id);
    /** ClientProt RESUME_PAUSEBUTTON. */
    void (*resume_pausebutton)(void* user, int com_id);
    /** ClientProt CLOSE_MODAL (local close click only). */
    void (*close_modal)(void* user);
};

/**
 * Apply a minimenu button action (REVCONFIG_MINIMENU_IF_BUTTON / _TOGGLE /
 * _SELECT / RESUME_PAUSEBUTTON / CLOSE_MODAL) to the component with com_id.
 * Returns nonzero when the click was consumed by the button engine.
 */
int
RS_IF1_ApplyButtonClick(
    struct App* app,
    int com_id,
    int action);

#endif
