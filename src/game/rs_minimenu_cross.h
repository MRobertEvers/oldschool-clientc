#ifndef SRC_RS_MINIMENU_CROSS_H
#define SRC_RS_MINIMENU_CROSS_H

#include "revconfig/revconfig.h"
#include "ui/uitree_cross.h"

/*
 * Which click cross a minimenu action paints (reference Client doAction: the
 * only writes to crossMode). Header-only and leaf-including so both the app
 * and the uitree unit test can use it without linking game/.
 *
 * The rule the reference encodes: yellow for a walk that pathed the player,
 * red for a world-entity op that walks the player at a target, and NOTHING for
 * everything else -- UI buttons, inventory ops, Examine, the social rows, and
 * Cancel never touch crossMode, so a cross already in flight keeps running and
 * keeps its colour.
 */

/** Cross feedback for a minimenu action. UI_CROSS_OFF means "leave whatever is
 *  on screen alone", not "clear it". */
static inline enum UICrossMode
RS_Minimenu_CrossModeForAction(int action)
{
    /* Mirror doAction's `if (action >= _PRIORITY) action -= _PRIORITY`:
     * UIMinimenu_ActionDeprioritize biases by +2000 and no native rev-254 id
     * reaches 2000, so anything above that is a biased copy. */
    if( action > 2000 )
        action -= 2000;

    switch( action )
    {
    case REVCONFIG_MINIMENU_WALK:
        return UI_CROSS_WALK;

    /* World-entity ops, plus every targeting/use-on variant that walks the
     * player at a world target (reference reds TGT_OBJ/NPC/LOC and USEHELD_ON*).
     * Examine (OPNPC6/OPLOC6/OPOBJ6) is deliberately absent: no cross for it. */
    case REVCONFIG_MINIMENU_OPNPC1:
    case REVCONFIG_MINIMENU_OPNPC2:
    case REVCONFIG_MINIMENU_OPNPC3:
    case REVCONFIG_MINIMENU_OPNPC4:
    case REVCONFIG_MINIMENU_OPNPC5:
    case REVCONFIG_MINIMENU_OPLOC1:
    case REVCONFIG_MINIMENU_OPLOC2:
    case REVCONFIG_MINIMENU_OPLOC3:
    case REVCONFIG_MINIMENU_OPLOC4:
    case REVCONFIG_MINIMENU_OPLOC5:
    case REVCONFIG_MINIMENU_OPOBJ1:
    case REVCONFIG_MINIMENU_OPOBJ2:
    case REVCONFIG_MINIMENU_OPOBJ3:
    case REVCONFIG_MINIMENU_OPOBJ4:
    case REVCONFIG_MINIMENU_OPOBJ5:
    case REVCONFIG_MINIMENU_USEHELD_ONLOC:
    case REVCONFIG_MINIMENU_USEHELD_ONNPC:
    case REVCONFIG_MINIMENU_USEHELD_ONOBJ:
    case REVCONFIG_MINIMENU_TGT_LOC:
    case REVCONFIG_MINIMENU_TGT_NPC:
    case REVCONFIG_MINIMENU_TGT_OBJ:
        return UI_CROSS_INTERACT;

    default:
        /* OPNPC6/OPLOC6/OPOBJ6 (Examine), OPHELD1-6, OPHELDT_START ("Use"),
         * USEHELD_ONHELD / TGT_HELD / TGT_BUTTON (no walk), INV_BUTTON1-5,
         * IF_BUTTON/TOGGLE/SELECT, RESUME_PAUSEBUTTON, CLOSE_MODAL, the
         * friend/ignore/message/report rows, OPPLAYER_TRADEREQ/DUELREQ,
         * and CANCEL. */
        return UI_CROSS_OFF;
    }
}

#endif
