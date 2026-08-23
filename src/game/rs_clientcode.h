#ifndef SRC_GAME_RS_CLIENTCODE_H
#define SRC_GAME_RS_CLIENTCODE_H

#include <stdint.h>

struct App;
struct UITree;
struct RS_Social;
struct UITreeComponent;

/*
 * clientCode-driven dynamic content, mirroring the reference clientComponent
 * pass (Client-TS 10924): components whose cache clientCode marks them as
 * client-populated (friends rows, ignore rows, list scroll heights, design
 * preview rotation, welcome text) are refreshed from live state each tick.
 *
 * The clientCode numbers are protocol constants of the 254-era interface
 * data, not layout — they identify BEHAVIOR baked into cache components.
 */
enum RS_ClientCodeId
{
    RS_CC_FRIENDS_START = 1,
    RS_CC_FRIENDS_END = 100,
    RS_CC_FRIENDS_UPDATE_START = 101,
    RS_CC_FRIENDS_UPDATE_END = 200,
    RS_CC_ADD_FRIEND = 201,
    RS_CC_DEL_FRIEND = 202,
    RS_CC_FRIENDS_SIZE = 203,
    RS_CC_LOGOUT = 205,
    RS_CC_BANKMODE = 206,
    RS_CC_CHANGE_HEAD_L = 300,
    RS_CC_CHANGE_FEET_R = 313,
    RS_CC_RECOLOUR_HAIR_L = 314,
    RS_CC_RECOLOUR_SKIN_R = 323,
    RS_CC_SWITCH_TO_MALE = 324,
    RS_CC_SWITCH_TO_FEMALE = 325,
    RS_CC_ACCEPT_DESIGN = 326,
    RS_CC_DESIGN_PREVIEW = 327,
    RS_CC_IGNORES_START = 401,
    RS_CC_IGNORES_END = 500,
    RS_CC_ADD_IGNORE = 501,
    RS_CC_DEL_IGNORE = 502,
    RS_CC_IGNORES_SIZE = 503,
    RS_CC_REPORT_RULE1 = 601,
    RS_CC_REPORT_RULE12 = 612,
    RS_CC_MOD_MUTE = 613,
    /* Welcome-screen rows, filled from LAST_LOGIN_INFO. 650 and 655 are the
     * same row on two different welcome screens -- the cache ships one for an
     * account with recovery questions set and one for an account without, and
     * the packet's days_since_recovery is what chooses between them. */
    RS_CC_LAST_LOGIN_INFO = 650,
    RS_CC_UNREAD_MESSAGES = 651,
    RS_CC_RECOVERY1 = 652,
    RS_CC_RECOVERY2 = 653,
    RS_CC_RECOVERY3 = 654,
    RS_CC_LAST_LOGIN_INFO2 = 655,
    RS_CC_FRIENDS2_START = 701,
    RS_CC_FRIENDS2_END = 800,
    RS_CC_FRIENDS2_UPDATE_START = 801,
    RS_CC_FRIENDS2_UPDATE_END = 900,
};

/*
 * Two values LAST_LOGIN_INFO's `days_since_recovery` carries that are not day
 * counts. They are protocol constants of the same 254-era data as the
 * clientCode numbers above, which is why they live beside them.
 */
enum RS_ClientCodeRecoveryDays
{
    /** The account has never set recovery questions. */
    RS_CC_RECOVERY_DAYS_NEVER_SET = 200,
    /** Nothing to say about recovery questions — and the only state in which
     *  the packet's member warning gets to use those three rows instead. */
    RS_CC_RECOVERY_DAYS_SILENT = 201,
};

/*
 * The unread-messages row is the one welcome row the reference recolours
 * rather than just retexts (Colour.YELLOW for none waiting, Colour.GREEN for
 * some). Not revconfig: this is not the row's colour, it is a colour that
 * carries a value, the same way the friends list writes @red@Offline.
 */
enum RS_ClientCodeWelcomeColour
{
    RS_CC_WELCOME_COLOUR_YELLOW = 0xffff00,
    RS_CC_WELCOME_COLOUR_GREEN = 0x00ff00,
};

/**
 * Refresh every clientCode-populated component from live state. Returns
 * nonzero when any node changed (caller redraws). app->logic_cycle drives the
 * design-preview rotation, and the preview's composite is rebuilt here when a
 * design button marked it stale (reference idkDesignRedraw, which the
 * reference likewise services from this per-component pass).
 */
int
RS_ClientCode_Tick(
    struct App* app,
    struct UITree* tree,
    struct RS_Social const* social,
    uint64_t loop_cycle);

/**
 * A clientCode-bearing button was clicked (reference clientButton). Returns
 * nonzero when the click should ALSO notify the server with IF_BUTTON.
 */
int
RS_ClientCode_Button(
    struct App* app,
    struct UITreeComponent const* component);

#endif
