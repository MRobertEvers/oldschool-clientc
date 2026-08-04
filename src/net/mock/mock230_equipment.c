#include "mock230_equipment.h"

#include "mock230.h"
#include "mock230_content.h"

#include <stdio.h>
#include <string.h>

/*
 * The screen itself is not here any more.
 *
 * interface_equipment/scripts/equipment.rs2 owns all of it: the mount, the
 * eighteen component names, the labels, the "+0" convention, the column order
 * and the tick-to-seconds conversion. Every one of those is a wording or a
 * layout decision, and the eighteen `mock230_send_if_settext` calls that used
 * to be below were an engine deciding how a screen reads.
 *
 * The numbers came from a second sum over the worn container that existed only
 * for this screen (`mock230_equipment_bonus`), beside the one the fight already
 * rolls against — `~equip_get_bonuses` in skill_combat/combat_stats.rs2. There
 * is now one, and it is content's.
 *
 * What is left is the two things that genuinely are the engine's: *when* to
 * repaint, and the wearability gate that applies to every obj in the cache.
 */

/*
 * `mock230_equipment_may_wear` stood here — 45 lines that refused a wear and
 * said OldSchool's two sentences. It went on 2026-08-02 with
 * MOCK230_FALLBACK_OPHELD, being reachable only from it.
 *
 * The rule is `~levelrequire_check` in skill_combat/scripts/levelrequire.rs2
 * now, called from `~equip`. A refusal-with-a-message is a rule and rules are
 * content's; what kept this in C was that nothing could read the data from a
 * script, and `skill_combat/configs/levelrequire.dbtable` plus `oc_param` on the
 * cache's own `skillrequire`/`levelrequire` closed that.
 *
 * `mock230_obj_require` and the `.obj` loader behind it STAY. `mock230_pack`
 * still validates those lines, `tools/gen_levelrequire_dbrow.py` generates the
 * script-readable index from them, and the selftest's level-gate leg walks the
 * whole table to assert content and the C table still agree — which is the one
 * thing standing between two forms of one fact and a silent drift.
 */

void
mock230_equipment_refresh_stats(struct Mock230Server* srv)
{
    /* Content owns the paint (`[proc,equipment_refresh]`); the engine owns
     * *when* — worn_dirty flush and bank/equip open sites call here. By name
     * rather than the unresolved hook table. */
    mock230_scripts_run_proc(srv, "[proc,equipment_refresh]", NULL, 0);
}

void
mock230_equipment_open_stats(struct Mock230Server* srv)
{
    /* The button is content's — `[if_button,wornitems:equipment]` — so this
     * seam exists only for the `::equipstats` cheat, which opens the screen
     * without walking the sidebar. Fire the same if_button trigger. */
    mock230_scripts_run_if_button(srv, mock230_ids()->com_worn_equipment_stats, 0);
}

int
mock230_equipment_worn_slot(int component)
{
    const struct Mock230EnumDef* slots = mock230_content_enum("worn_slots");

    for( int i = 0; slots && i < slots->count; i++ )
        if( slots->values[i].key == component )
            return slots->values[i].value;
    return -1;
}

/*
 * There is no worn-tab arming here either.
 *
 * `~worn_tab_login` (player/containers.rs2) does it: the equipment-stats button
 * and the eleven slot components, each with op 1 "Remove" and op 10 "Examine".
 * What a component permits is a UI policy, and rev 230 made it the server's
 * exactly so it could be one — leaving it as eleven `mock230_send_if_setevents`
 * calls with `(1 << 1) | (1 << 10)` spelled inline put it back in the engine.
 *
 * That arming is also why there is no engine fallback for the button any more.
 * A `mock230_equipment_handle_button` sitting behind the trigger dispatch looked
 * like one and was not: with no script pack loaded nothing sends IF_SETEVENTS
 * for `wornitems:equipment`, so the client never reports a click on it and the
 * fallback could never fire.
 */
