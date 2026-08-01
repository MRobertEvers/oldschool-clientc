#ifndef SRC_NET_MOCK_MOCK230_EQUIPMENT_H
#define SRC_NET_MOCK_MOCK230_EQUIPMENT_H

/*
 * The equipment-stats screen, reached from "View equipment stats" on the worn
 * tab.
 *
 * Nothing about it is client-side: OldSchool's client draws eighteen empty text
 * components and waits for the server to fill them in, which is why the tab
 * looks broken rather than empty when a server forgets. The numbers are summed
 * from the same obj params the combat code rolls against, so the screen cannot
 * disagree with the fight.
 *
 * The interface and its eighteen components are named in the content tree and
 * resolved by mock230_ids — `equipment_stats` and `equipment_stats_*`. Nothing
 * about the screen's layout is written down here.
 *
 * See docs/mock230_player_systems.md §3.
 */

struct Mock230Server;

/**
 * Which equipment slot a component of the worn tab stands for, or -1.
 *
 * Reads the `worn_slots` enum, because the mapping is not the component's
 * position: the tab draws eleven slots and the wear positions they stand for
 * skip 6, 8 and 11. See player/configs/worn.enum.
 */
int
mock230_equipment_worn_slot(int component);

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

/**
 * May the player wear this obj?
 *
 * Returns 1 when they may. Returns 0 and sends the refusal — OldSchool's own
 * two lines, naming the first skill that is short — when they may not.
 *
 * The requirement itself comes from `mock230_obj_require`, merged from the
 * cache's own params and the `.obj` overlay. Two things about *this* function
 * are decisions rather than plumbing:
 *
 *   - It reads the **base** level, not the boosted one, matching the reference:
 *     a strength potion does not let you wield a rune scimitar. LostCity's
 *     `levelrequire_*` labels all call `stat_base`.
 *   - An obj with no requirement is wearable, so an item this server has never
 *     heard of stays equippable. The alternative — refusing what is unknown —
 *     would make a cache the importer has not been run against unplayable.
 */
int
mock230_equipment_may_wear(
    struct Mock230Server* srv,
    int obj_id);

#endif
