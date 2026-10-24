#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_ids.h"
#include "torirsserver/torirs_server_content.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The enemy health overlay -- interface `hpbar_hud` (303).
 *
 * Four All Settings > Activities rows hang off this and could not be honoured
 * because the thing they configure was never on screen:
 *
 *   111  Show normal health overlay          %hpbar_hud_standard_disabled
 *   299  Show enemy name on health overlay   %hpbar_hud_boss_name_disabled
 *   300  Compact boss health overlay         %hpbar_hud_boss_compact_enabled
 *   301  Health overlay display type         %hpbar_hud_boss_percentage_enabled
 *
 * The last three are read by the cache's own layout scripts (2101 / 2103); the
 * client needs no code for them at all. What it needs is for somebody to OPEN
 * the interface and keep it fed, and in the reference that somebody is the
 * server -- nothing in the cache opens 303.
 *
 * ---- what it is fed ----
 *
 * All of it is named by the cache, so none of these numbers is invented:
 *
 *   %hpbar_hud_npc      (varp)    the npc TYPE; the panel takes its name and
 *                                 its `param_510` override from it
 *   %hpbar_hud_hp       (varbit)  current hitpoints
 *   %hpbar_hud_basehp   (varbit)  hitpoints at full
 *   %hpbar_hud_boss     (varbit)  draw the wide boss bar rather than the
 *                                 small one
 *
 * `hpbar_hud_boss` is raised here for an npc whose record says it is a boss
 * (`param=hpbar_hud_boss,1`, from RSProx: see `hpbar_is_boss`); content can
 * raise it itself for anything else, and the panel follows.
 *
 * It IS lowered here, and that is setting 10's whole implementation:
 *
 *   10   Show boss health overlay            %hpbar_hud_boss_disabled
 *
 * The row had no reader anywhere -- not in the cache (the panel's layout script
 * 2101 branches on `%hpbar_hud_boss`, never on the setting), not in the NXT
 * engine, and not here. So a player who switched it off still got the wide bar
 * from any encounter that raised the varbit. Clearing it on the setting puts the
 * decision back where the row says it is without this file having to know which
 * npcs are bosses: content proposes, the player disposes.
 *
 * ---- the setting is INVERTED, and the cache says so in the name ----
 *
 * `hpbar_hud_standard_disabled`: 1 is off. Reading it the plain way would show
 * the overlay to exactly the players who switched it off, which is the failure
 * this whole category is full of.
 */

/*
 * ---- what makes it VISIBLE: clientscript 2376 ----
 *
 * The panel's body, 303:5 `hp`, is authored `hidden=yes`, and the update
 * script (2103) returns at once while it is hidden. The only thing that unhides
 * it is `hp_hud_open` (2377), reached only from `hp_hud_handle_event` (2376) --
 * and nothing in the cache or the rev-239 client calls 2376. The server does,
 * with RUNCLIENTSCRIPT and the same nineteen components 303's onload hands
 * 2099. Without it the panel was mounted and fed and never once drawn.
 *
 * 2376 runs after the data it shows (varps go out the moment they are written,
 * so a send after the writes arrives after them): on the first data tick, and
 * whenever the npc it shows changes, which 2377 answers with a forced redraw
 * (a new name) rather than the var-transmit hook's plain one.
 *
 * A changed npc is judged by TYPE, so a script that hid the body is a second
 * case: a fade hides 303:5 and leaves the panel open, and when it comes back
 * onto a new npc of the SAME type -- the Archive's practice mode rebuilding
 * the room, a new Guardian after a death -- nothing re-showed it and the boss
 * bar stayed gone for good. A hidden body is shown again when the panel reads
 * a different npc (uid), never on the npc it was hidden over, so a fade or a
 * cutscene that hides the bar mid-fight keeps it hidden.
 *
 * The components come from the ids table by name (com_hpbar_hud_event), never
 * as numbers: this list was rev-239 numbers after the tree moved to the 241
 * cache, and every boss bar drew its sliding shade as the fill over a `back`
 * resized to the trail.
 */
static void
hpbar_send_open_event(
    struct ToriRSServerPlayer* player,
    struct ToriRSServerIds const* ids)
{
    int args[19];

    for( int i = 0; i < 19; i++ )
    {
        assert(ids->com_hpbar_hud_event[i] > 0);
        args[i] = ids->com_hpbar_hud_event[i];
    }
    ToriRSServer_SendRunClientscript(player, 2376, args, 19);
}

/*
 * A boss panel's event: the bar's three colours, then 2376.
 *
 * Jagex sends this every tick a boss's panel is up and once more per hit that
 * lands on it (RSProx, Sol Heredit in 12 sessions: one a tick through the
 * fight, three on a tick two hits land, 4,610 of 5,400 ticks so; a two-hit
 * swing adds two more on its own tick, and Sol's phase lines a four-tick
 * `hp_hud_fade_in` run in place of it -- neither modelled yet). The colours are
 * Sol's, 15-bit: red under, dark green sliding, green left (14,610 of 15,061
 * across the recorded bosses).
 */
static void
hpbar_boss_event(
    struct ToriRSServerPlayer* player,
    struct ToriRSServerIds const* ids)
{
    ToriRSServer_SendIfSetcolour(player, ids->com_hpbar_hud_event[7], 25600);
    ToriRSServer_SendIfSetcolour(player, ids->com_hpbar_hud_event[8], 576);
    ToriRSServer_SendIfSetcolour(player, ids->com_hpbar_hud_event[9], 800);
    hpbar_send_open_event(player, ids);
}

/** How long the overlay lingers after the last exchange, in ticks.
 *
 *  Not zero: combat drops the target for a moment between an npc dying and the
 *  next click, and an overlay that blinked out on every gap would be unreadable
 *  in a multi-npc fight. Six ticks is the reference's own "still in combat"
 *  feel -- long enough to cover a re-target, short enough that walking away
 *  clears it. */
#define TORIRSSERVER_HPBAR_LINGER_TICKS 6



/*
 * The npc content pinned to the overlay with `hpbar_hud_npc`, or -1.
 *
 * It wins over the combat target: the Path of Crondis shows the palm's water
 * to every raider for the whole room, crocodile fights included (RSProx: the
 * overlay's npc is a palm stage on every write, never the crocodile).
 *
 * The pin is the content's until the content takes it back. A dying npc still
 * reads (its hitpoints are 0), and one that is gone HOLDS the last reading
 * (`HPBAR_PIN_HELD`): RSProx 843, 3389 and 820 delete the Vespula portal two
 * ticks after its killing blow, and the overlay keeps it, at 0, until the room
 * unpins it eight ticks later. Dropping the pin at the death left the bar on
 * the hitpoints before the blow and closed it on the combat linger instead.
 */
#define HPBAR_PIN_HELD (-2)

/*
 * A BOSS: an npc whose record carries `param=hpbar_hud_boss,1`.
 *
 * RSProx (all of 820, 843, 3282, 3285, 3389 and the rest of the archive) writes
 * `hpbar_hud_npc` with `hpbar_hud_boss` 1 for a boss and, with the normal
 * overlay off, never for anything else. A boss is shown from the raider's first
 * attack on it (two or three ticks before the hit lands), through every form it
 * changes into, and while the raider strikes other npcs -- Vasa while her
 * crystals take the hits, Tekton at the anvil -- until it is REMOVED or turns
 * into a form that is no boss, and then for eight ticks more at its last
 * reading: Tekton 3 of 3 and Vasa 4 of 4 after the removal, the vanguards 8 of
 * 9 after they turn to walk back and reset (843, 3285, 3389; the ninth 9).
 */
#define HPBAR_BOSS_HOLD_TICKS 8

static int
hpbar_is_boss(struct ToriRSServerNpc const* npc)
{
    static int s_param = -2;
    int32_t value = 0;
    const struct ToriRSServerNpcDef* def;

    assert(npc);
    if( s_param == -2 )
        s_param = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_PARAM, "hpbar_hud_boss");
    /* An authored band field first, then the record's own param table (a
     * `param=hpbar_hud_boss,1` line in a .npc file lands in the latter). */
    def = npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
    if( s_param >= 0 && def && ToriRSServer_ContentNpcParam(def, s_param, &value) )
        return value != 0;
    if( s_param >= 0 )
    {
        const struct ToriRSServerNpcParam* row = ToriRSServer_NpcParam(npc->type, s_param);
        if( row && !row->sval )
            return row->ival != 0;
    }
    /* Then the cache's own mark: param 509 is 2 on the 232 npcs whose record
     * draws the boss panel (Sol Heredit, the Dagannoth Kings, the wilderness
     * bosses, the raids' bosses; RSProx writes the panel for them) and 0 on
     * the Theatre's, which have their own. Before the combat-target fallback
     * went, every boss reached the panel through it; only the Gauntlet's and
     * the Chambers' carried `hpbar_hud_boss`. */
    {
        const struct ToriRSServerNpcParam* row = ToriRSServer_NpcParam(npc->type, 509);
        return row && !row->sval && row->ival == 2;
    }
}

/* The boss this player's overlay sticks to: its slot, HPBAR_PIN_HELD while a
 * removed one is still shown, or -1. Takes up the combat target when that is a
 * boss. */
static int
hpbar_boss_slot(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv;
    struct ToriRSServerNpc* npc;
    int slot;

    assert(player);

    srv = player->world;
    if( !srv )
        return -1;
    if( player->combat_target >= 0 && player->combat_target < TORIRSSERVER_NPC_MAX )
    {
        npc = &srv->npcs[player->combat_target];
        if( npc->active && npc->death_tick < 0 && hpbar_is_boss(npc) )
        {
            player->hpbar_boss_uid = (int)(((uint32_t)npc->generation << 16) |
                                           (uint32_t)player->combat_target);
            player->hpbar_boss_hold = 0;
        }
    }
    if( player->hpbar_boss_uid == 0 )
        return -1;
    slot = (int)((uint32_t)player->hpbar_boss_uid & 0xffffu);
    npc = &srv->npcs[slot];
    if( !npc->active || npc->generation != (uint16_t)((uint32_t)player->hpbar_boss_uid >> 16) ||
        !hpbar_is_boss(npc) )
    {
        if( !player->hpbar_open || player->hpbar_last_type < 0 )
        {
            player->hpbar_boss_uid = 0;
            return -1;
        }
        /* Shown on the tick it goes and the seven after; closed on the eighth. */
        if( player->hpbar_boss_hold == 0 )
            player->hpbar_boss_hold = HPBAR_BOSS_HOLD_TICKS + 1;
        if( --player->hpbar_boss_hold <= 0 )
        {
            player->hpbar_boss_uid = 0;
            player->hpbar_boss_hold = 0;
            return -1;
        }
        return HPBAR_PIN_HELD;
    }
    /* Dying still reads (0). */
    player->hpbar_boss_hold = 0;
    return slot;
}

static int
hpbar_pinned_slot(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv;
    struct ToriRSServerNpc* npc;
    int slot;

    assert(player);

    srv = player->world;
    if( !srv || player->hpbar_npc_uid == 0 )
        return -1;
    slot = (int)((uint32_t)player->hpbar_npc_uid & 0xffffu);
    npc = &srv->npcs[slot];
    if( !npc->active || npc->generation != (uint16_t)((uint32_t)player->hpbar_npc_uid >> 16) )
    {
        /* Nothing read yet to hold: a pin that never drew is no pin. */
        if( !player->hpbar_open || player->hpbar_last_type < 0 )
        {
            player->hpbar_npc_uid = 0;
            return -1;
        }
        return HPBAR_PIN_HELD;
    }
    return slot;
}

void
ToriRSServer_HpBarTick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();
    int slot;
    int want_type = -1;
    int want_hp = 0;
    int want_max = 0;
    int pinned = 0;
    int boss = 0;
    int shown_uid = 0;

    assert(srv);
    assert(player);

    if( ids->iface_hpbar_hud <= 0 || ids->com_gameframe_hpbar_hud <= 0 )
        return;

    slot = hpbar_pinned_slot(player);
    pinned = slot != -1;
    if( slot == -1 )
    {
        slot = hpbar_boss_slot(player);
        boss = slot != -1;
    }
    if( slot == HPBAR_PIN_HELD )
    {
        want_type = player->hpbar_last_type;
        want_hp = player->hpbar_last_hp;
        want_max = player->hpbar_last_max;
        boss = player->hpbar_last_boss;
        player->hpbar_linger = 0;
    }
    /* Nothing else: an ordinary combat target gets no overlay from the server.
     * Jagex writes `hpbar_hud_npc` for a boss or what content pins, never for
     * the npc a player merely fights, the normal overlay on or off: index 1211
     * (the Colosseum's waves 1..6, overlay on: one write, -1), Stronghold of
     * Security 4346 and Corsair Cove 4352 (every fight: -1 only), and 4262
     * (overlay on: only its quest bosses). Fed every target, ours wrote the
     * panel's vars through every wave. */
    if( slot >= 0 )
        boss = hpbar_is_boss(&srv->npcs[slot]);
    if( slot >= 0 )
    {
        struct ToriRSServerNpc const* npc = &srv->npcs[slot];
        want_type = npc->type;
        shown_uid = (int)(((uint32_t)npc->generation << 16) | (uint32_t)slot);
        /* The phase's own pool, as the overhead bar draws it (`headbar_reserve`). */
        want_hp = npc->hitpoints - npc->headbar_reserve;
        if( want_hp < 0 )
            want_hp = 0;
        want_max = (npc->max_hitpoints > 0 ? npc->max_hitpoints : npc->hitpoints) -
                   npc->headbar_reserve;
        if( want_max < 1 )
            want_max = 1;
        /* `headbar_inverted`: the HUD counts what has been taken, as the
         * overhead bar does (RSProx 14: varbit 6099 10, 18, 27 ... 140). */
        if( npc->def && npc->def->headbar_inverted )
        {
            want_hp = want_max - want_hp;
            if( want_hp < 0 )
                want_hp = 0;
        }
        player->hpbar_linger = TORIRSSERVER_HPBAR_LINGER_TICKS;
        player->hpbar_last_type = want_type;
        player->hpbar_last_hp = want_hp;
        player->hpbar_last_max = want_max;
        player->hpbar_last_boss = boss;
    }
    else if( player->hpbar_linger > 0 )
    {
        /* Hold the last reading rather than re-reading a dead npc: the slot may
         * already belong to somebody else. */
        player->hpbar_linger--;
        want_type = player->hpbar_last_type;
        want_hp = player->hpbar_last_hp;
        want_max = player->hpbar_last_max;
    }

    /*
     * The setting, read the way its name spells it.
     *
     * Checked here rather than at the open, so switching it off mid-fight
     * closes the panel on the next tick instead of leaving the last one up.
     */
    if( want_type >= 0 && !pinned && !boss &&
        ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_standard_off) )
        want_type = -1;

    /*
     * The boss bit follows the npc shown, when that is a boss: raised for it,
     * lowered when the overlay moves off it -- but only a bit this file raised.
     * Content that raises it itself (the Path of Crondis, Olm's barrier) keeps
     * it until it lowers it.
     */
    if( ids->varbit_hpbar_hud_boss >= 0 )
    {
        int const off = ids->varbit_hpbar_hud_boss_off >= 0 &&
                        ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_boss_off);
        if( boss && want_type >= 0 && !off )
        {
            if( !ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_boss) )
            {
                ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_boss, 1);
                player->hpbar_boss_raised = 1;
            }
        }
        else if( player->hpbar_boss_raised )
        {
            ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_boss, 0);
            player->hpbar_boss_raised = 0;
        }
    }

    /*
     * Setting 10, and it is a VETO rather than a switch.
     *
     * Read every tick beside setting 111's, for the same reason: switching it
     * off mid-fight has to narrow the bar on the next tick, not on the next
     * encounter. An encounter that has raised `hpbar_hud_boss` keeps whatever it
     * set for as long as the player wants boss bars; the moment they do not, the
     * varbit goes to 0 and clientscript 2101 draws the small bar instead.
     *
     * Raised here only for a boss (`hpbar_hud_boss`, below) -- see the header.
     */
    if( ids->varbit_hpbar_hud_boss >= 0 && ids->varbit_hpbar_hud_boss_off >= 0 &&
        ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_boss_off) &&
        ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_boss) )
    {
        if( getenv("TORIRS_HPBAR_DEBUG") )
            fprintf(stderr, "hpbar: setting 10 is off — clearing the boss bar\n");
        ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_boss, 0);
    }

    /*
     * No npc to show: the panel stays MOUNTED and is told "nobody"
     * (%hpbar_hud_npc = -1). Jagex mounts 303 in the login burst and never
     * closes it: across 300 RSProx sessions not one IF_CLOSESUB of hpbar_hud,
     * 206 hpbar_hud_npc writes from -1 (the Colosseum's Sol: -1 when he is
     * gone, idx1223 [901]). Closing it here sent an IF_CLOSESUB at every
     * target's end and an IF_OPENSUB at the next.
     */
    if( want_type < 0 )
    {
        if( !player->hpbar_open )
            return;
        player->hpbar_linger = 0;
        player->hpbar_shown_uid = 0;
        if( getenv("TORIRS_HPBAR_DEBUG") )
            fprintf(stderr, "hpbar: nobody\n");
        ToriRSServer_WorldSetVarpOn(srv, player, ids->varp_hpbar_hud_npc, -1);
        return;
    }

    /*
     * The open comes FIRST, and the data one tick later.
     *
     * The panel paints from a var-transmit hook, not from its onload:
     * clientscript 2099 is 303's onload and all it does is register
     * `if_setonvartransmit("script2102(...){var1682, var1683}")`. So a value
     * written before the mount has finished is a change with nobody listening,
     * and the panel stays blank until the npc's hitpoints happen to move.
     *
     * Writing on the next tick instead costs one tick of an empty panel and
     * cannot race the mount, which on this client is a task rather than an
     * immediate build.
     */
    if( !player->hpbar_open )
    {
        int const host = ids->com_gameframe_hpbar_hud;
        player->hpbar_open = TORIRSSERVER_HPBAR_OPENING;
        if( getenv("TORIRS_HPBAR_DEBUG") )
            fprintf(
                stderr,
                "hpbar: open iface %d into %d:%d (live gameframe %d) for npc type %d (%d/%d)\n",
                ids->iface_hpbar_hud,
                TORIRSSERVER_COM_GROUP(host),
                TORIRSSERVER_COM_CHILD(host),
                ToriRSServer_PlayerGameframeIface(player),
                want_type,
                want_hp,
                want_max);
        ToriRSServer_SendIfOpensub(
            player,
            TORIRSSERVER_COM_GROUP(host),
            TORIRSSERVER_COM_CHILD(host),
            ids->iface_hpbar_hud,
            1);
        return;
    }

    {
        /*
         * Re-send on the first data tick even when nothing changed.
         *
         * The varp writers dedupe, and they are right to -- but a panel that
         * just opened onto the same npc at the same hitpoints would then be
         * fed nothing at all, because the last thing that changed those values
         * happened before it existed. Marking the carriers re-queues them
         * without pretending they changed.
         */
        int const first = player->hpbar_open == TORIRSSERVER_HPBAR_OPENING;
        int shown_type;
        int base;

        assert(ids->varp_hpbar_hud_npc >= 0);
        shown_type = player->varps[ids->varp_hpbar_hud_npc];

        player->hpbar_open = TORIRSSERVER_HPBAR_LIVE;
        ToriRSServer_WorldSetVarpOn(srv, player, ids->varp_hpbar_hud_npc, want_type);
        base = ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_hp, want_hp);
        ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_basehp, want_max);
        if( first )
        {
            ToriRSServer_WorldMarkVarp(player, ids->varp_hpbar_hud_npc);
            if( base >= 0 )
                ToriRSServer_WorldMarkVarp(player, base);
        }
        if( boss && !pinned && slot >= 0 )
        {
            /* A boss's panel is re-sent every tick it is up, and once more for
             * each hit that lands on the boss this tick (`hpbar_boss_event`). */
            int sends = 1;
            struct ToriRSServerNpc const* npc = &srv->npcs[slot];

            for( int i = 0; i < npc->hitmark_count; i++ )
                if( npc->hitmarks[i].damage > 0 )
                    sends++;
            for( int i = 0; i < sends; i++ )
                hpbar_boss_event(player, ids);
            player->hpbar_body_hidden = 0;
        }
        else if( first || shown_type != want_type ||
                 (player->hpbar_body_hidden && shown_uid != 0 &&
                  shown_uid != player->hpbar_shown_uid) )
        {
            hpbar_send_open_event(player, ids);
            player->hpbar_body_hidden = 0;
        }
        if( shown_uid != 0 )
            player->hpbar_shown_uid = shown_uid;
    }
}

void
ToriRSServer_HpBarNoteSethide(
    struct ToriRSServerPlayer* player,
    int uid,
    int hide)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();

    assert(player);

    /* hpbar_hud:hp, the body 2376 unhides (com_hpbar_hud_event[3]). */
    if( ids->com_hpbar_hud_event[3] <= 0 || uid != ids->com_hpbar_hud_event[3] )
        return;
    player->hpbar_body_hidden = hide ? 1 : 0;
}
