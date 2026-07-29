#include "mock230_equipment.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_content.h"
#include "mock230_ids.h"

#include <stdio.h>
#include <string.h>

/*
 * The screen's eighteen text rows come from pack/component.pack by name —
 * `equipment_stats_stabatt` and friends — rather than from a table of child
 * ids here. Two things that used to be comments are now checkable: that the
 * numbers are the empty rows under each heading (OpenRune's gameval table names
 * them, and `tools/dump_interface --iface 84` shows each one shipping empty
 * under its heading), and that the mock and the cache agree on which is which.
 *
 * Column order still matters and is not stated anywhere: 24/25/26 sit at x=343
 * and 27/28 at x=424, so the five attack rows read Stab, Slash, Crush down the
 * left and Magic, Ranged down the right — not five in a column. That is why
 * they are painted in this order.
 */

int
mock230_equipment_bonus(
    const struct Mock230Server* srv,
    int param)
{
    const struct Mock230Player* player = srv->player;
    int total = 0;

    if( param < 0 || param >= MOCK230_PARAM_BONUS_COUNT )
        return 0;
    for( int slot = 0; slot < MOCK230_WORN_SLOTS; slot++ )
    {
        int obj_id = player->worn[slot].obj_id;
        if( obj_id < 0 )
            continue;
        total += mock230_objinfo(obj_id)->bonus[param];
    }
    return total;
}

int
mock230_equipment_may_wear(
    struct Mock230Server* srv,
    int obj_id)
{
    /* The wire's own skill order, which is `server/pack/stat.pack`. Only the
     * name is needed here and only for the message, so this is a label table
     * rather than a second copy of the pack — the *ids* still come from it. */
    static const char* const k_stat_names[MOCK230_STAT_COUNT] = {
        "Attack", "Defence", "Strength", "Hitpoints", "Ranged", "Prayer", "Magic",
        "Cooking", "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting",
        "Smithing", "Mining", "Herblore", "Agility", "Thieving", "Slayer", "Farming",
        "Runecraft", "Hunter", "Construction",
    };
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
        if( srv->player->stat_level[stat] >= level )
            continue;

        /*
         * OldSchool's refusal, both lines, and the first unmet requirement is
         * the one it names — a player 20 levels short in two skills is told
         * about one of them, which is what the reference does rather than
         * listing them.
         */
        mock230_send_message(srv, "You are not a high enough level to use this item.");
        {
            char line[128];

            snprintf(line, sizeof(line), "You need to have a %s level of %d.",
                     k_stat_names[stat], level);
            mock230_send_message(srv, line);
        }
        return 0;
    }
    return 1;
}

/* OldSchool prints a leading '+' on non-negative bonuses; the minus sign comes
 * from the number itself. A bonus of 0 reads "+0", not "0". */
static void
put_bonus(
    struct Mock230Server* srv,
    int component,
    const char* label,
    int value)
{
    char text[64];

    snprintf(text, sizeof(text), "%s: %s%d", label, value < 0 ? "" : "+", value);
    mock230_send_if_settext(srv, component, text);
}

static void
put_percent(
    struct Mock230Server* srv,
    int component,
    const char* label,
    int percent)
{
    char text[64];

    snprintf(text, sizeof(text), "%s: %s%d%%", label, percent < 0 ? "" : "+", percent);
    mock230_send_if_settext(srv, component, text);
}

/* Ticks between swings, from the weapon's own attackrate param (cache param 14)
 * — the same number mock230_combat swings on, so the screen cannot claim a
 * speed the fight does not use. */
static int
weapon_attack_rate(const struct Mock230Player* player)
{
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
    const struct Mock230ObjInfo* info;

    if( weapon < 0 )
        return MOCK230_ATTACK_SPEED;
    info = mock230_objinfo(weapon);
    return info->has_params && info->attackrate > 0 ? info->attackrate : MOCK230_ATTACK_SPEED;
}

static void
put_speed(
    struct Mock230Server* srv,
    int component,
    const char* label,
    int ticks)
{
    char text[64];

    /* A tick is 600 ms, so the seconds are always a multiple of 0.6 and print
     * exactly in one decimal place. The two components are 83px wide and sit
     * side by side, so the unit is "s" — "seconds" overruns into its
     * neighbour, which is what the screen showed before. */
    snprintf(text, sizeof(text), "%s: %d.%ds", label, ticks * 6 / 10, (ticks * 6) % 10);
    mock230_send_if_settext(srv, component, text);
}

void
mock230_equipment_refresh_stats(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    const struct Mock230Ids* ids = mock230_ids();
    int rate;

    if( !player->equip_stats_open )
        return;

    put_bonus(srv, ids->com_equipment_stats_stabatt, "Stab",
              mock230_equipment_bonus(srv, MOCK230_PARAM_STABATTACK));
    put_bonus(srv, ids->com_equipment_stats_slashatt, "Slash",
              mock230_equipment_bonus(srv, MOCK230_PARAM_SLASHATTACK));
    put_bonus(srv, ids->com_equipment_stats_crushatt, "Crush",
              mock230_equipment_bonus(srv, MOCK230_PARAM_CRUSHATTACK));
    put_bonus(srv, ids->com_equipment_stats_magicatt, "Magic",
              mock230_equipment_bonus(srv, MOCK230_PARAM_MAGICATTACK));
    put_bonus(srv, ids->com_equipment_stats_rangeatt, "Range",
              mock230_equipment_bonus(srv, MOCK230_PARAM_RANGEATTACK));

    put_bonus(srv, ids->com_equipment_stats_stabdef, "Stab",
              mock230_equipment_bonus(srv, MOCK230_PARAM_STABDEFENCE));
    put_bonus(srv, ids->com_equipment_stats_slashdef, "Slash",
              mock230_equipment_bonus(srv, MOCK230_PARAM_SLASHDEFENCE));
    put_bonus(srv, ids->com_equipment_stats_crushdef, "Crush",
              mock230_equipment_bonus(srv, MOCK230_PARAM_CRUSHDEFENCE));
    put_bonus(srv, ids->com_equipment_stats_magicdef, "Magic",
              mock230_equipment_bonus(srv, MOCK230_PARAM_MAGICDEFENCE));
    put_bonus(srv, ids->com_equipment_stats_rangedef, "Range",
              mock230_equipment_bonus(srv, MOCK230_PARAM_RANGEDEFENCE));

    put_bonus(srv, ids->com_equipment_stats_meleestrength, "Melee Str",
              mock230_equipment_bonus(srv, MOCK230_PARAM_STRENGTHBONUS));
    /* Ranged strength lives at cache param 189, far outside the contiguous
     * 0..11 block, and mock230_objinfo does not read it — so this is honestly
     * zero rather than wrong. */
    put_bonus(srv, ids->com_equipment_stats_rangestrength, "Ranged Str", 0);
    put_percent(srv, ids->com_equipment_stats_magicdamage, "Magic Dmg", 0);
    put_bonus(srv, ids->com_equipment_stats_prayer, "Prayer",
              mock230_equipment_bonus(srv, MOCK230_PARAM_PRAYERBONUS));

    /* Salve/slayer multipliers need per-item unlock data the mock has none of;
     * OpenRune's own screen prints "TODO" here. Zero is the honest reading. */
    put_percent(srv, ids->com_equipment_stats_typemultiplier, "Undead", 0);
    put_percent(srv, ids->com_equipment_stats_slayermultiplier, "Slayer", 0);

    rate = weapon_attack_rate(player);
    put_speed(srv, ids->com_equipment_stats_attackspeedbase, "Base", rate);
    /* Nothing in the mock alters attack speed (no rapid, no special attacks),
     * so the current speed IS the base one. It is still sent: the component is
     * blank until something fills it. */
    put_speed(srv, ids->com_equipment_stats_attackspeedactual, "Current", rate);
}

void
mock230_equipment_open_stats(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    const struct Mock230Ids* ids = mock230_ids();

    if( player->equip_stats_open )
        return;
    player->equip_stats_open = 1;

    /* Same two-mount shape the bank uses: the main modal first, then the side
     * panel, because the sidebar's own CS2 keys off the main mount. */
    mock230_send_if_opensub(srv, ids->iface_gameframe,
                            MOCK230_COM_CHILD(ids->com_gameframe_mainmodal),
                            ids->iface_equipment_stats, 0);

    /*
     * OpenRune also mounts `equipment_stats_side` in the side slot, and this
     * deliberately does not.
     *
     * It is one bare 162x248 layer with no children and no script of its own:
     * the backpack appears in it only because the server runs the client's
     * `interface_inv_init` script at it (RUNCLIENTSCRIPT with five *string*
     * arguments for the op labels — see OpenRune's ClientScripts.kt). The
     * mock's RUNCLIENTSCRIPT sender takes ints only, so mounting it today
     * replaces the sidebar with an empty panel, which is worse than leaving the
     * inventory tab where it is.
     */

    /* The worn container: the screen's own eleven slots paint from it. */
    mock230_send_inv_full(srv, ids->com_equipment_stats_container, ids->inv_worn, player->worn,
                          MOCK230_WORN_SLOTS);

    mock230_equipment_refresh_stats(srv);
}

void
mock230_equipment_close_stats(struct Mock230Server* srv)
{
    if( !srv->player->equip_stats_open )
        return;
    srv->player->equip_stats_open = 0;
    mock230_send_if_closesub(srv, mock230_ids()->com_gameframe_mainmodal);
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

void
mock230_equipment_arm_worn_tab(struct Mock230Server* srv)
{
    const struct Mock230EnumDef* slots = mock230_content_enum("worn_slots");

    /*
     * Arm "View equipment stats" (op 1).
     *
     * At rev 230 a component's cache ops are only *labels* — whether a click on
     * one reaches the server is the events mask the server itself sets, so an
     * unarmed button runs its local onop script and sends nothing. That is
     * exactly what this one did: the hovertext read "View equipment stats" and
     * the click went nowhere.
     */
    mock230_send_if_setevents(srv, mock230_ids()->com_worn_equipment_stats, -1, -1, 1 << 1);

    /*
     * And the eleven equipment slots' own op 1, "Remove".
     *
     * The paint script writes the label onto each slot, but a label is not
     * permission: unarmed, the client offers the ObjType's Wear/Drop rows on a
     * worn item instead — which is nonsense on something already worn — and the
     * "Remove" it does show goes to the slot's local CS2 hook. Op 10 is Examine,
     * which the client answers itself.
     */
    for( int i = 0; slots && i < slots->count; i++ )
        mock230_send_if_setevents(srv, slots->values[i].key, -1, -1, (1 << 1) | (1 << 10));
}

int
mock230_equipment_handle_button(
    struct Mock230Server* srv,
    int component,
    int sub,
    int op)
{
    (void)sub;
    (void)op;

    if( component == mock230_ids()->com_worn_equipment_stats )
    {
        mock230_equipment_open_stats(srv);
        return 1;
    }
    return 0;
}
