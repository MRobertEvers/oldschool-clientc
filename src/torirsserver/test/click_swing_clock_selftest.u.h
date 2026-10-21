/* A clicked swing runs on the clock of the tick that ships it.
 * Included from torirs_server_world_selftest.c after combat_reach_flag (it
 * borrows combat_reach_flag_spawn's held-still npc).
 *
 * OPNPC used to be handled BETWEEN ticks, while `srv->tick` was still the tick
 * that had just finished. handle_opnpc resolved the interaction on the spot, so
 * a swing it started armed `%action_delay = map_clock + rate` from the finished
 * tick and shipped a tick later, and the swing after it came `rate - 1` ticks
 * after it. It showed only where the swing loop was not already running when
 * the click arrived: the blowpipe's first shot at the next npc after a kill had
 * a short cooldown (2026-10-19). The click only latches, and phase_player
 * resolves it; client input is now queued and handled inside the next tick
 * (phase_clients_in), so the latch and the swing are that one tick's.
 *
 * Unarmed, so the rate is `[label,player_melee_attack]`'s literal 4 and no
 * weapon param stands between the click and the number. Against the old
 * handler the check fails: the varp is armed to the finished tick plus 4.
 */

static void
selftest_click_swing_clock(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int goblin;
    int delay_varp;
    int saved_weapon;
    int saved_count;
    int slot;
    uint8_t payload[2];
    int clicked_after;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: a clicked swing runs on its own tick's clock\n");
    goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "goblin_unarmed_melee_1");
    delay_varp = ToriRSServer_WorldVarp("varp6407_action_delay");
    SELFTEST_CHECK(goblin >= 0, "click_swing_clock: goblin_unarmed_melee_1 resolves");
    SELFTEST_CHECK(delay_varp >= 0, "click_swing_clock: varp6407_action_delay resolves");
    if( goblin < 0 || delay_varp < 0 )
        return;

    selftest_park_player(srv, 3222, 3218);
    slot = combat_reach_flag_spawn(srv, goblin, 3223, 3218);
    SELFTEST_CHECK(slot >= 0, "click_swing_clock: a goblin spawns beside the player");
    if( slot < 0 )
        return;

    saved_weapon = player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id;
    saved_count = player->worn[TORIRSSERVER_WEAR_WEAPON].count;
    worn_set(player, TORIRSSERVER_WEAR_WEAPON, -1, 0);
    ToriRSServer_ScriptsRunProc(srv, "[proc,player_combat_stat]", NULL, 0);
    player->varps[delay_varp] = 0;

    selftest_npc_payload(player, slot, payload);
    clicked_after = srv->tick;
    selftest_handle(player, PKTOUT_NAME_OPNPC2, payload, 2);
    SELFTEST_CHECK(srv->tick == clicked_after + 1,
                   "click_swing_clock: one tick handles the click, tick %d after %d",
                   srv->tick, clicked_after);
    SELFTEST_CHECK(player->varps[delay_varp] == srv->tick + 4,
                   "click_swing_clock: the swing is armed from the tick that ships it: "
                   "action_delay %d want %d (clicked after tick %d)",
                   player->varps[delay_varp], srv->tick + 4, clicked_after);

    ToriRSServer_CombatStopPlayer(srv);
    worn_set(player, TORIRSSERVER_WEAR_WEAPON, saved_weapon, saved_count);
    ToriRSServer_ScriptsRunProc(srv, "[proc,player_combat_stat]", NULL, 0);
    player->varps[delay_varp] = 0;
    ToriRSServer_WorldNpcFree(srv, slot);
    selftest_tick(srv);
}
