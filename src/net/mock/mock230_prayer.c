#include "mock230_prayer.h"

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_equipment.h"
#include "mock230_ids.h"

#include "serverscript/ss_trigger.h"

#include <stdio.h>
#include <string.h>

/*
 * There are no prayer rules in this file.
 *
 * Prayer is content: `skill_prayer/scripts/prayer.rs2` owns the toggle, the
 * exclusion groups, the level and points checks, the drain arithmetic and every
 * message, and `configs/prayers.prayer` states each prayer's level, drain rate,
 * group, varbit, button and overhead icon. What is left here is the two jobs
 * only the engine can do: read the state out for the packet encoders, and give
 * content a tick to drain on.
 *
 * ------------------------------------------------------------------
 * The private mask is gone, and that was the point
 * ------------------------------------------------------------------
 *
 * This module used to keep `player->prayer_active`, a 32-bit mask it owned and
 * decided everything against, and mirror it into the cache's `prayer_<name>`
 * varbits so the prayer book would light up. Two representations of one fact,
 * and the C one was authoritative — which meant content *could not* be given
 * the rules however many procs were extracted, because whatever a script wrote
 * the next `prayer_changed` would overwrite.
 *
 * The varbits are the state now. `%prayer_ultimatestrength` is written by
 * content and read here, and there is nothing to keep in sync.
 */

/** Is this prayer switched on? Reads the cache varbit, which is the state. */
static int
prayer_is_on(
    struct Mock230Server* srv,
    const struct Mock230PrayerDef* def)
{
    if( !def || def->varbit < 0 )
        return 0;
    return mock230_varbit_get(srv->player, def->varbit) != 0;
}

const char*
mock230_prayer_name(int prayer)
{
    const struct Mock230PrayerDef* def = mock230_content_prayer(prayer);

    return def ? def->name : "prayer";
}

/*
 * Every prayer off.
 *
 * The engine names one moment — death — and content performs it, because "off"
 * for a prayer means its varbit, its overhead icon, its drain contribution, the
 * drain timer and the recomputed combat block, all of which the script states
 * in one place.
 */
void
mock230_prayer_clear(struct Mock230Server* srv)
{
    mock230_scripts_run_proc(srv, "[proc,prayer_deactivate_all]", NULL, 0);
}

/*
 * Click a prayer's button, as a player would.
 *
 * The engine has no way to switch a prayer on, and should not: the toggle is
 * `[if_button,prayerbook:prayerN]`. What it can do is deliver the click, which
 * is what the `::prayer` cheat and the selftest both actually mean. Routing
 * them through the same trigger a real click takes is also the only way either
 * of them tests the shipped path.
 *
 * Returns 1 when content claimed the click.
 */
int
mock230_prayer_click(
    struct Mock230Server* srv,
    int prayer)
{
    const struct Mock230PrayerDef* def = mock230_content_prayer(prayer);

    if( !def || def->button < 0 )
        return 0;
    srv->player->last_com = def->button;
    srv->player->last_slot = -1;
    srv->player->last_verb = 1;
    /* Both paths, in the order handle_if_button_op takes them: an
     * `[if_button,<component>]` script is found by NAME, not by the trigger
     * table, so the trigger lookup alone silently does nothing. */
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_BUTTON, def->button, -1, -1) )
        return 1;
    return mock230_scripts_run_if_button_named(srv, def->button);
}

/** 1 when this prayer's varbit is set. For the cheat's echo and the selftest —
 *  a read of the state, not of a second copy. */
int
mock230_prayer_active(
    struct Mock230Server* srv,
    int prayer)
{
    return prayer_is_on(srv, mock230_content_prayer(prayer));
}

/*
 * An overhead icon index by its `^headicon_prayer_*` constant, or -1.
 *
 * A constant lookup, and the constant is content's. Only the selftest asks: the
 * appearance encoder reads `player->headicons` and never asks what a bit means.
 */
int
mock230_prayer_headicon(const char* symbol)
{
    return mock230_content_constant_int(symbol, -1);
}

/*
 * A prayer's index by its `[symbol]` in prayers.prayer, or -1.
 */
int
mock230_prayer_index(const char* symbol)
{
    int count = mock230_content_prayer_count();

    for( int i = 0; i < count; i++ )
    {
        const struct Mock230PrayerDef* def = mock230_content_prayer(i);

        if( def && def->symbol && strcmp(def->symbol, symbol) == 0 )
            return i;
    }
    return -1;
}
