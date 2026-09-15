#include "torirs_server_equipment.h"

#include "torirs_server.h"
#include "torirs_server_content.h"

#include <stdio.h>
#include <string.h>

/*
 * The screen itself is not here any more.
 *
 * interface_equipment/scripts/equipment.rs2 owns all of it: the mount, the
 * eighteen component names, the labels, the "+0" convention, the column order
 * and the tick-to-seconds conversion. Every one of those is a wording or a
 * layout decision, and the eighteen `ToriRSServer_SendIfSettext` calls that used
 * to be below were an engine deciding how a screen reads.
 *
 * The numbers came from a second sum over the worn container that existed only
 * for this screen (`ToriRSServer_EquipmentBonus`), beside the one the fight already
 * rolls against — `~equip_get_bonuses` in skill_combat/combat_stats.rs2. There
 * is now one, and it is content's.
 *
 * What is left is the two things that genuinely are the engine's: *when* to
 * repaint, and the wearability gate that applies to every obj in the cache.
 */

/*
 * `ToriRSServer_EquipmentMayWear` stood here — 45 lines that refused a wear and
 * said OldSchool's two sentences. It went on 2026-08-02 with
 * TORIRSSERVER_FALLBACK_OPHELD, being reachable only from it.
 *
 * The rule is `~levelrequire_check` in skill_combat/scripts/levelrequire.rs2
 * now, called from `~equip`. A refusal-with-a-message is a rule and rules are
 * content's; what kept this in C was that nothing could read the data from a
 * script, and `skill_combat/configs/levelrequire.dbtable` plus `oc_param` on the
 * cache's own `skillrequire`/`levelrequire` closed that.
 *
 * `ToriRSServer_ObjRequire` and the `.obj` loader behind it STAY. `ToriRSServer_Pack`
 * still validates those lines, `tools/gen_levelrequire_dbrow.py` generates the
 * script-readable index from them, and the selftest's level-gate leg walks the
 * whole table to assert content and the C table still agree — which is the one
 * thing standing between two forms of one fact and a silent drift.
 */

void
ToriRSServer_EquipmentRefreshStats(struct ToriRSServer* srv)
{
    /* No-op. Content paints via ~equipment_refresh from equip/unequip/openbank
     * and [if_open,bankmain]. Kept so call sites and selftests compile. */
    (void)srv;
}

void
ToriRSServer_EquipmentOpenStats(struct ToriRSServer* srv)
{
    /* The button is content's — `[if_button,wornitems:equipment]` — so this
     * seam exists only for the `::equipstats` cheat, which opens the screen
     * without walking the sidebar. Fire the same if_button trigger. */
    ToriRSServer_ScriptsRunIfButton(srv, ToriRSServer_Ids()->com_worn_equipment_stats, 0);
}

int
ToriRSServer_EquipmentWornSlot(int component)
{
    const struct ToriRSServerEnumDef* slots =
        ToriRSServer_ContentEnumById(ToriRSServer_Ids()->enum_worn_slots);

    for( int i = 0; slots && i < slots->count; i++ )
        if( slots->values[i].key == component )
            return slots->values[i].value;
    return -1;
}

int
ToriRSServer_EquipmentWornComponent(int worn_slot)
{
    const struct ToriRSServerEnumDef* slots =
        ToriRSServer_ContentEnumById(ToriRSServer_Ids()->enum_worn_slots);

    for( int i = 0; slots && i < slots->count; i++ )
        if( slots->values[i].value == worn_slot )
            return slots->values[i].key;
    return -1;
}

/*
 * There is no worn-tab arming here either.
 *
 * `~worn_tab_login` (player/containers.rs2) does it: the equipment-stats button
 * and the eleven slot components, each with op 1 "Remove" and op 10 "Examine".
 * What a component permits is a UI policy, and rev 230 made it the server's
 * exactly so it could be one — leaving it as eleven `ToriRSServer_SendIfSetevents`
 * calls with `(1 << 1) | (1 << 10)` spelled inline put it back in the engine.
 *
 * That arming is also why there is no engine fallback for the button any more.
 * A `ToriRSServer_EquipmentHandleButton` sitting behind the trigger dispatch looked
 * like one and was not: with no script pack loaded nothing sends IF_SETEVENTS
 * for `wornitems:equipment`, so the client never reports a click on it and the
 * fallback could never fire.
 */
