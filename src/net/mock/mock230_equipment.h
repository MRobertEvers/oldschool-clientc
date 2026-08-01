#ifndef SRC_NET_MOCK_MOCK230_EQUIPMENT_H
#define SRC_NET_MOCK_MOCK230_EQUIPMENT_H

/*
 * What the engine still owns of the worn tab and the equipment-stats screen.
 *
 * The screen itself is
 * `server/scripts/interface_equipment/scripts/equipment.rs2`: the mount, the
 * eighteen component names, the labels, the number formatting and the order the
 * rows are painted in. Its numbers come from `~equip_get_bonuses`, the same sum
 * the fight rolls against, so the screen cannot disagree with the fight.
 *
 * Only three functions remain, and none of them names a component.
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

/**
 * Open the screen, for a caller that is not the button.
 *
 * `[if_button,wornitems:equipment]` is the way in that a player takes and it
 * needs nothing from here; this runs the same `[proc,equipment_open]` for the
 * `::equipstats` cheat, which opens the screen without walking the sidebar.
 */
void
mock230_equipment_open_stats(struct Mock230Server* srv);

/**
 * Repaint the screen, but only while it is mounted: `[proc,equipment_refresh]`
 * is eighteen IF_SETTEXTs to components that do not exist otherwise.
 *
 * Called on the tick the worn container changed, because a bonus screen that
 * still shows the sword you just took off is worse than one that shows nothing.
 * Deciding *when* is the whole of the engine's share; what gets painted is the
 * proc's.
 */
void
mock230_equipment_refresh_stats(struct Mock230Server* srv);

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
