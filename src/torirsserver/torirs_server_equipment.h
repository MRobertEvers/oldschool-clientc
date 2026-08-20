#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_EQUIPMENT_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_EQUIPMENT_H

/*
 * What the engine still owns of the worn tab and the equipment-stats screen.
 *
 * The screen itself is
 * `server/scripts/interface_equipment/scripts/equipment.rs2`: the mount, the
 * eighteen component names, the labels, the number formatting and the order the
 * rows are painted in. Its numbers come from `~equip_get_bonuses`, the same sum
 * the fight rolls against, so the screen cannot disagree with the fight.
 *
* Only two functions remain, and neither of them names a component: the wear
 * gate went to content with TORIRSSERVER_FALLBACK_OPHELD (see torirs_server_equipment.c).
 *
 * See docs/torirs_server_player_systems.md §3.
 */

struct ToriRSServer;

/**
 * Which equipment slot a component of the worn tab stands for, or -1.
 *
 * Reads the `worn_slots` enum, because the mapping is not the component's
 * position: the tab draws eleven slots and the wear positions they stand for
 * skip 6, 8 and 11. See player/configs/worn.enum.
 */
int
ToriRSServer_EquipmentWornSlot(int component);

/**
 * Component which represents a worn slot, or -1 when content does not expose
 * one.  This is the inverse lookup of ToriRSServer_EquipmentWornSlot().
 */
int
ToriRSServer_EquipmentWornComponent(int worn_slot);

/**
 * Open the screen, for a caller that is not the button.
 *
 * `[if_button,wornitems:equipment]` is the way in that a player takes and it
 * needs nothing from here; this runs the same `[proc,equipment_open]` for the
 * `::equipstats` cheat, which opens the screen without walking the sidebar.
 */
void
ToriRSServer_EquipmentOpenStats(struct ToriRSServer* srv);

/**
 * Ask content to repaint any open bonus view after the worn container changed.
 *
 * `[proc,equipment_refresh]` reads `if_getmain` and paints the equipment
 * screen, the bank's embedded rows, or nothing. Deciding *when* is the
 * engine's share; which view and what gets painted is the proc's.
 */
void
ToriRSServer_EquipmentRefreshStats(struct ToriRSServer* srv);


#endif
