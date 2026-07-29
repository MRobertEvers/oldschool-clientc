#ifndef SRC_NET_MOCK_MOCK230_EQUIPMENT_H
#define SRC_NET_MOCK_MOCK230_EQUIPMENT_H

/*
 * The equipment-stats screen (interface 84), reached from "View equipment
 * stats" on the worn tab.
 *
 * Nothing about it is client-side: OldSchool's client draws eighteen empty text
 * components and waits for the server to fill them in, which is why the tab
 * looks broken rather than empty when a server forgets. The numbers are summed
 * from the same obj params the combat code rolls against, so the screen cannot
 * disagree with the fight.
 *
 * See docs/mock230_player_systems.md §3.
 */

struct Mock230Server;

enum
{
    MOCK230_EQUIPSTATS_IFACE = 84,
    /** The side panel: an empty 162x248 layer the backpack is drawn into. */
    MOCK230_EQUIPSTATS_SIDE_IFACE = 85,
    /** 387:1, whose only op is "View equipment stats". */
    MOCK230_WORN_COM_STATS_BUTTON = 1,
};

/** Enable the worn tab's "View equipment stats" op so a click on it reaches
 *  the server. Part of the login burst. */
void
mock230_equipment_arm_worn_tab(struct Mock230Server* srv);

/** Open the screen and fill it in. Idempotent. */
void
mock230_equipment_open_stats(struct Mock230Server* srv);

/** Close it, if it is open. */
void
mock230_equipment_close_stats(struct Mock230Server* srv);

/**
 * Re-send the eighteen numbers, but only while the screen is open: they are
 * IF_SETTEXTs to components that do not exist otherwise.
 *
 * Called whenever the worn container changes, because a bonus screen that
 * still shows the sword you just took off is worse than one that shows
 * nothing.
 */
void
mock230_equipment_refresh_stats(struct Mock230Server* srv);

/**
 * Handle a button on the worn tab or on the stats screen. Returns 1 when the
 * component belonged to one of them.
 */
int
mock230_equipment_handle_button(
    struct Mock230Server* srv,
    int component,
    int sub,
    int op);

/** Total of one bonus (a Mock230CombatParam index) over everything worn. */
int
mock230_equipment_bonus(
    const struct Mock230Server* srv,
    int param);

#endif
