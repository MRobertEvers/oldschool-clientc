#include "mock230_equipment.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_content.h"

#include <stdio.h>
#include <string.h>

/*
 * Interface 84's eighteen text components, verified against cache.osrs230 with
 * `tools/dump_interface/dump_interface cache.osrs230 --iface 84`: every one of
 * them ships with an empty string and a heading above it ("Attack bonus" at
 * 84:23, "Defence bonus" at 84:29, and so on), so the ids are not guessed —
 * they are the empty rows under each heading, in screen order.
 *
 * The same numbering is what OpenRune's `enums.equipment_stats_to_slots_map`
 * and xrsps's EquipmentStatsUiService use, which is the cross-check that this
 * is the modern layout rather than a coincidence.
 *
 * Column order matters: 24/25/26 sit at x=343 and 27/28 at x=424, so the five
 * attack rows read Stab, Slash, Crush down the left and Magic, Ranged down the
 * right — not five in a column.
 */
enum
{
    COM_ATTACK_STAB = 24,
    COM_ATTACK_SLASH = 25,
    COM_ATTACK_CRUSH = 26,
    COM_ATTACK_MAGIC = 27,
    COM_ATTACK_RANGED = 28,

    COM_DEFENCE_STAB = 30,
    COM_DEFENCE_SLASH = 31,
    COM_DEFENCE_CRUSH = 32,
    COM_DEFENCE_MAGIC = 33,
    COM_DEFENCE_RANGED = 34,

    COM_MELEE_STRENGTH = 36,
    COM_RANGED_STRENGTH = 37,
    COM_MAGIC_DAMAGE = 38,
    COM_PRAYER = 39,

    COM_TARGET_UNDEAD = 41,
    COM_TARGET_SLAYER = 42,

    COM_SPEED_BASE = 53,
    COM_SPEED_CURRENT = 54,
};

static int
stats_uid(int child)
{
    return (MOCK230_EQUIPSTATS_IFACE << 16) | (child & 0xffff);
}

int
mock230_equipment_bonus(
    const struct Mock230Server* srv,
    int param)
{
    const struct Mock230Player* player = &srv->player;
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

/* OldSchool prints a leading '+' on non-negative bonuses; the minus sign comes
 * from the number itself. A bonus of 0 reads "+0", not "0". */
static void
put_bonus(
    struct Mock230Server* srv,
    int child,
    const char* label,
    int value)
{
    char text[64];

    snprintf(text, sizeof(text), "%s: %s%d", label, value < 0 ? "" : "+", value);
    mock230_send_if_settext(srv, stats_uid(child), text);
}

static void
put_percent(
    struct Mock230Server* srv,
    int child,
    const char* label,
    int percent)
{
    char text[64];

    snprintf(text, sizeof(text), "%s: %s%d%%", label, percent < 0 ? "" : "+", percent);
    mock230_send_if_settext(srv, stats_uid(child), text);
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
    int child,
    const char* label,
    int ticks)
{
    char text[64];

    /* A tick is 600 ms, so the seconds are always a multiple of 0.6 and print
     * exactly in one decimal place. The two components are 83px wide and sit
     * side by side, so the unit is "s" — "seconds" overruns into its
     * neighbour, which is what the screen showed before. */
    snprintf(text, sizeof(text), "%s: %d.%ds", label, ticks * 6 / 10, (ticks * 6) % 10);
    mock230_send_if_settext(srv, stats_uid(child), text);
}

void
mock230_equipment_refresh_stats(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int rate;

    if( !player->equip_stats_open )
        return;

    put_bonus(srv, COM_ATTACK_STAB, "Stab", mock230_equipment_bonus(srv, MOCK230_PARAM_STABATTACK));
    put_bonus(
        srv, COM_ATTACK_SLASH, "Slash", mock230_equipment_bonus(srv, MOCK230_PARAM_SLASHATTACK));
    put_bonus(
        srv, COM_ATTACK_CRUSH, "Crush", mock230_equipment_bonus(srv, MOCK230_PARAM_CRUSHATTACK));
    put_bonus(
        srv, COM_ATTACK_MAGIC, "Magic", mock230_equipment_bonus(srv, MOCK230_PARAM_MAGICATTACK));
    put_bonus(
        srv, COM_ATTACK_RANGED, "Range", mock230_equipment_bonus(srv, MOCK230_PARAM_RANGEATTACK));

    put_bonus(
        srv, COM_DEFENCE_STAB, "Stab", mock230_equipment_bonus(srv, MOCK230_PARAM_STABDEFENCE));
    put_bonus(
        srv, COM_DEFENCE_SLASH, "Slash", mock230_equipment_bonus(srv, MOCK230_PARAM_SLASHDEFENCE));
    put_bonus(
        srv, COM_DEFENCE_CRUSH, "Crush", mock230_equipment_bonus(srv, MOCK230_PARAM_CRUSHDEFENCE));
    put_bonus(
        srv, COM_DEFENCE_MAGIC, "Magic", mock230_equipment_bonus(srv, MOCK230_PARAM_MAGICDEFENCE));
    put_bonus(
        srv, COM_DEFENCE_RANGED, "Range", mock230_equipment_bonus(srv, MOCK230_PARAM_RANGEDEFENCE));

    put_bonus(
        srv,
        COM_MELEE_STRENGTH,
        "Melee Str",
        mock230_equipment_bonus(srv, MOCK230_PARAM_STRENGTHBONUS));
    /* Ranged strength lives at cache param 189, far outside the contiguous
     * 0..11 block, and mock230_objinfo does not read it — so this is honestly
     * zero rather than wrong. */
    put_bonus(srv, COM_RANGED_STRENGTH, "Ranged Str", 0);
    put_percent(srv, COM_MAGIC_DAMAGE, "Magic Dmg", 0);
    put_bonus(srv, COM_PRAYER, "Prayer", mock230_equipment_bonus(srv, MOCK230_PARAM_PRAYERBONUS));

    /* Salve/slayer multipliers need per-item unlock data the mock has none of;
     * OpenRune's own screen prints "TODO" here. Zero is the honest reading. */
    put_percent(srv, COM_TARGET_UNDEAD, "Undead", 0);
    put_percent(srv, COM_TARGET_SLAYER, "Slayer", 0);

    rate = weapon_attack_rate(player);
    put_speed(srv, COM_SPEED_BASE, "Base", rate);
    /* Nothing in the mock alters attack speed (no rapid, no special attacks),
     * so the current speed IS the base one. It is still sent: the component is
     * blank until something fills it. */
    put_speed(srv, COM_SPEED_CURRENT, "Current", rate);
}

void
mock230_equipment_open_stats(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;

    if( player->equip_stats_open )
        return;
    player->equip_stats_open = 1;

    /* Same two-mount shape the bank uses: the main modal first, then the side
     * panel, because the sidebar's own CS2 keys off the main mount. */
    mock230_send_if_opensub(
        srv, MOCK230_ROOT_IFACE, MOCK230_MAINMODAL_SLOT, MOCK230_EQUIPSTATS_IFACE, 0);

    /*
     * OpenRune also mounts interface 85 in the side slot, and this deliberately
     * does not.
     *
     * 85 is one bare 162x248 layer with no children and no script of its own:
     * the backpack appears in it only because the server runs the client's
     * `interface_inv_init` script at it (RUNCLIENTSCRIPT with five *string*
     * arguments for the op labels — see OpenRune's ClientScripts.kt). The
     * mock's RUNCLIENTSCRIPT sender takes ints only, so mounting 85 today
     * replaces the sidebar with an empty panel, which is worse than leaving the
     * inventory tab where it is.
     */

    /* The worn container: the screen's own eleven slots paint from it. */
    mock230_send_inv_full(
        srv,
        (MOCK230_EQUIPSTATS_IFACE << 16) | 0,
        MOCK230_INV_WORN,
        player->worn,
        MOCK230_WORN_SLOTS);

    mock230_equipment_refresh_stats(srv);
}

void
mock230_equipment_close_stats(struct Mock230Server* srv)
{
    if( !srv->player.equip_stats_open )
        return;
    srv->player.equip_stats_open = 0;
    mock230_send_if_closesub(srv, (MOCK230_ROOT_IFACE << 16) | MOCK230_MAINMODAL_SLOT);
}

void
mock230_equipment_arm_worn_tab(struct Mock230Server* srv)
{
    /*
     * Arm "View equipment stats" (387:1, op 1).
     *
     * At rev 230 a component's cache ops are only *labels* — whether a click on
     * one reaches the server is the events mask the server itself sets, so an
     * unarmed button runs its local onop script and sends nothing. That is
     * exactly what this one did: the hovertext read "View equipment stats" and
     * the click went nowhere.
     */
    mock230_send_if_setevents(
        srv,
        (MOCK230_WORN_IFACE << 16) | MOCK230_WORN_COM_STATS_BUTTON,
        -1,
        -1,
        1 << 1);
}

int
mock230_equipment_handle_button(
    struct Mock230Server* srv,
    int component,
    int sub,
    int op)
{
    int group = (component >> 16) & 0xffff;
    int child = component & 0xffff;

    (void)sub;
    (void)op;

    if( group == MOCK230_WORN_IFACE && child == MOCK230_WORN_COM_STATS_BUTTON )
    {
        mock230_equipment_open_stats(srv);
        return 1;
    }
    return 0;
}
