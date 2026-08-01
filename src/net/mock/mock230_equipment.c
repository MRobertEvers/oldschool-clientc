#include "mock230_equipment.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_content.h"
#include "mock230_ids.h"

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

int
mock230_equipment_may_wear(
    struct Mock230Server* srv,
    int obj_id)
{
    const struct Mock230ObjRequire* require = mock230_obj_require(obj_id);

    if( !require )
        return 1;

    for( int i = 0; i < require->count; i++ )
    {
        int stat = require->req[i].stat;
        int level = require->req[i].level;

        if( stat < 0 || stat >= MOCK230_STAT_COUNT )
            continue;
        /* Base, not boosted: a potion does not let you wield what you could not
         * wield sober. */
        if( srv->active_player->stat_level[stat] >= level )
            continue;

        /*
         * OldSchool's refusal, both lines, and the first unmet requirement is
         * the one it names — a player 20 levels short in two skills is told
         * about one of them, which is what the reference does rather than
         * listing them.
         */
        mock230_say(srv, "equip_level_generic_message", NULL);
        {
            /*
             * The stat and the level, both as numbers, and not a word between
             * them.
             *
             * This passed the skill's *name* until 2026-08-01, out of a
             * 23-string table right here — which meant the engine held the
             * spelling of every skill in the game and a tree could not rename or
             * translate one. `stat` is a first-class RuneScript type, so
             * `[proc,equip_level_message](stat $stat, int $level)` takes the id
             * and asks `~stat_name` for the word.
             */
            int32_t args[2] = { (int32_t)stat, (int32_t)level };

            mock230_scripts_run_hook(srv, srv->hooks.equip_level_message, args, 2);
        }
        return 0;
    }
    return 1;
}

void
mock230_equipment_refresh_stats(struct Mock230Server* srv)
{
    /*
     * The gate is the mount, not a flag of our own.
     *
     * `mainmodal_group` is maintained by the IF_OPENSUB / IF_CLOSESUB encoders
     * for every mount the server makes, including the ones content drives
     * through `if_openmain` — so it cannot drift the way the `equip_stats_open`
     * bool beside it could, and did: three places cleared it and one set it.
     *
     * The gate belongs here rather than inside the proc, because the whole
     * point of it is not to run the proc at all: `~equipment_refresh` is
     * eighteen IF_SETTEXTs to components that do not exist while the screen is
     * down.
     */
    if( srv->active_player->mainmodal_group != mock230_ids()->iface_equipment_stats )
        return;
    mock230_scripts_run_hook(srv, srv->hooks.equipment_refresh, NULL, 0);
}

void
mock230_equipment_open_stats(struct Mock230Server* srv)
{
    /* The button is content's — `[if_button,wornitems:equipment]` — so this
     * seam exists only for the `::equipstats` cheat, which opens the screen
     * without walking the sidebar. It runs the same proc the button does. */
    mock230_scripts_run_hook(srv, srv->hooks.equipment_open, NULL, 0);
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
