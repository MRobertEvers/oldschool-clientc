/* Real cache/deck/save roundtrips, shared by the sailing gate and full suite. */
static void
selftest_sailing_lifecycle(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    fprintf(stderr, "ToriRSServer selftest: sailing save, reconnect and safe teardown\n");
    int scripts_owned = 0;
    if( !srv->scripts_ok )
    {
        const char* scripts = getenv("TORIRSSERVER_SCRIPTS");
        scripts_owned = ToriRSServer_ScriptsLoad(srv, scripts ? scripts :
            "OSRS-Content/osrs239-content/server/scripts/build");
        if( !scripts_owned && !scripts )
            scripts_owned = ToriRSServer_ScriptsLoad(srv,
                "../OSRS-Content/osrs239-content/server/scripts/build");
    }
    SELFTEST_CHECK(srv->scripts_ok, "lifecycle validation requires the real compiled content");
    if( !srv->scripts_ok ) return;
    struct ToriRSServerSailingSave old_sailing = player->sailing;
    int32_t* old_varps = malloc(sizeof(player->varps));
    assert(old_varps);
    memcpy(old_varps, player->varps, sizeof(player->varps));
    ToriRSServer_WorldSetActive(srv, player);
    memset(&player->sailing, 0, sizeof(player->sailing));
    for( int slot = 1; slot <= 5; ++slot )
    {
        char key[80];
        snprintf(key, sizeof(key), "sailing_boat_%d_owned", slot);
        int bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, key);
        SELFTEST_CHECK(bit >= 0, "owned slot %d has its native varbit", slot);
        if( bit >= 0 ) ToriRSServer_VarbitSet(srv, bit, 0);
    }
    int instances_before = ToriRSServer_MapInstanceLiveCount();
    ToriRSServer_WorldTeleport(srv, 0, 3072, 3160);
    int handle = ToriRSServer_VesselSpawn(srv, 2, 2, 5, 0, 3072, 3160, 128);
    struct ToriRSServerVessel* boat = ToriRSServer_VesselGet(srv, handle);
    SELFTEST_CHECK(boat != NULL, "spawn a real skiff in the actual ocean");
    if( !boat ) goto done;
    boat->owner_uid = player->pid + 1;
    boat->cargo_slot = 1;
    SELFTEST_CHECK(ToriRSServer_VesselBuildPlayerDeck(srv, handle), "furnish native skiff");
    ToriRSServer_WorldTeleport(srv, 0, 3059, 2979);
    SELFTEST_CHECK(ToriRSServer_VesselRecover(srv, handle, 0, 3070, 2987),
                   "retrieve at the real Pandemonium gangplank");
    for( int heading = 0; heading < 16; ++heading )
        SELFTEST_CHECK(ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, heading * 128),
                       "retrieved berth permits native heading%d at%d,%d", heading,
                       boat->fine_x, boat->fine_z);
    ToriRSServer_WorldTeleport(srv, 0, 3072, 3160);
    boat->fine_x = 3072 * 128 + 64;
    boat->fine_z = 3160 * 128 + 64;
    boat->angle = 128;
    boat->facility[TORIRSSERVER_VESSEL_FACILITY_HELM] = 2;
    int32_t restore_args[1] = {handle}, sync_args[2] = {handle, 1};
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sailing_facilities_restore]", restore_args, 1),
                   "place the upgraded helm on the real deck");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sailing_custom_sync_slot]", sync_args, 2),
                   "write the upgraded native owned-slot varps");
    boat->fine_x += 32;
    boat->fine_z += 64;
    boat->hp = 37;
    boat->name_descriptor = 2;
    boat->name_noun = 3;
    boat->anchored = 1;
    ToriRSServer_MapInstanceVarSet(boat->instance, 0, 7);
    ToriRSServer_MapInstanceVarSet(boat->instance, 9, 40);
    ToriRSServer_MapInstanceVarSet(boat->instance, 10, 31940);
    ToriRSServer_MapInstanceVarSet(boat->instance, 8, 1);
    ToriRSServer_MapInstanceVarSet(boat->instance, 13, player->pid + 1);
    player->sailing.shore_x = 3059;
    player->sailing.shore_z = 2979;
    SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, player, boat), "board the native walkable deck");
    player->navigating_vessel = handle;
    player->navigating_vessel_serial = boat->serial;
    SELFTEST_CHECK(!ToriRSServer_VesselDisembarkPlayer(srv, player), "an open sea has no gangplank landing");
    int old_serial = boat->serial;
    int old_instance = boat->instance;
    int fx = boat->fine_x, fz = boat->fine_z, angle = boat->angle;
    int facilities[TORIRSSERVER_VESSEL_FACILITY_SLOTS];
    memcpy(facilities, boat->facility, sizeof(facilities));
    struct ToriRSServerContainer* cargo = ToriRSServer_ContainerResolve(srv, player, 963);
    int ammunition = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_cannonball");
    SELFTEST_CHECK(cargo && ammunition >= 0, "native cargo and ammunition resolve");
    struct ToriRSServerItem old_item;
    memset(&old_item, 0, sizeof(old_item));
    if( cargo )
    {
        old_item = cargo->items[0];
        cargo->items[0].obj_id = ammunition;
        cargo->items[0].count = 23;
    }
    const char* path = "/tmp/torirs-sailing-lifecycle-selftest.ini";
    SELFTEST_CHECK(ToriRSServer_SavePlayer(player, path), "save while physically aboard");
    struct ToriRSServerSailingSave captured;
    ToriRSServer_VesselCapturePlayer(player, &captured);
    SELFTEST_CHECK(captured.aboard_slot == 1 && captured.boats[0].hp == 37,
                   "capture owned slot and actual damage");
    ToriRSServer_VesselLogout(srv, player);
    SELFTEST_CHECK(!ToriRSServer_VesselGet(srv, handle), "logout destroys its vessel identity");
    SELFTEST_CHECK(ToriRSServer_MapInstanceGeneration(old_instance) == 0,
                   "logout releases the deck reservation");
    SELFTEST_CHECK(abs(player->x - 3059) <= 12 && abs(player->z - 2979) <= 12 && player->level == 0 &&
                   !ToriRSServer_SceneWalkBlocked(0, player->x, player->z) &&
                   !ToriRSServer_VesselTileSailable(0, player->x, player->z),
                   "logout's fallback is the real recorded shore, got %d,%d,%d", player->x, player->z, player->level);
    SELFTEST_CHECK(!player->navigating_vessel && !player->navigating_vessel_serial,
                   "no helm identity survives logout");
    memset(&player->sailing, 0, sizeof(player->sailing));
    if( cargo ) cargo->items[0].count = 0;
    SELFTEST_CHECK(ToriRSServer_LoadPlayer(player, path), "read the durable save");
    SELFTEST_CHECK(player->x == 3059 && player->z == 2979 && player->level == 0,
                   "save never persisted the deck pool coordinates");
    SELFTEST_CHECK(player->sailing.aboard_slot == 1 &&
                   player->sailing.deck_x == captured.deck_x && player->sailing.deck_z == captured.deck_z,
                   "save records an owned slot and relative deck tile");
    ToriRSServer_VesselLogin(srv, player);
    boat = ToriRSServer_VesselAtTile(srv, player->x, player->z);
    SELFTEST_CHECK(boat && boat->serial != old_serial, "login rebuilds a fresh boat/deck identity and boards it");
    if( boat )
    {
        SELFTEST_CHECK(boat->fine_x == fx && boat->fine_z == fz && boat->angle == angle,
                       "fine ocean pose and heading survive restart exactly");
        SELFTEST_CHECK(boat->hp == 37 && boat->name_descriptor == 2 && boat->name_noun == 3,
                       "furnishing cannot replace saved HP/name with template defaults");
        SELFTEST_CHECK(memcmp(boat->facility, facilities, sizeof(facilities)) == 0,
                       "all native facility/cosmetic choices reconstruct");
        for( int part = 0; part < TORIRSSERVER_VESSEL_FACILITY_SLOTS; ++part )
            if( boat->facility[part] != facilities[part] )
                fprintf(stderr, "sailing lifecycle: facility%d before%d after%d\n",
                        part, facilities[part], boat->facility[part]);
        SELFTEST_CHECK(boat->anchored && !boat->sails_set && !boat->reversing && !player->navigating_vessel,
                       "rejoined boat is stopped, anchored and has no stale helm lease");
        SELFTEST_CHECK(ToriRSServer_MapInstanceVarGet(boat->instance, 0) == 7 &&
                       ToriRSServer_MapInstanceVarGet(boat->instance, 9) == 40 &&
                       ToriRSServer_MapInstanceVarGet(boat->instance, 10) == 31940,
                       "stored motes and facility resource pairs survive");
        SELFTEST_CHECK(ToriRSServer_MapInstanceVarGet(boat->instance, 8) == 0 &&
                       ToriRSServer_MapInstanceVarGet(boat->instance, 13) == 0,
                       "running work and reusable operator uids are not persisted");
        SELFTEST_CHECK(cargo && cargo->items[0].obj_id == ammunition && cargo->items[0].count == 23,
                       "native boat cargo survives the save roundtrip");
        /* The reconstructed hull reuses this handle with a new serial, which is
         * exactly the state every persisted callback from the previous
         * identity now arrives into.
         *
         * Its arrival control uses hotspot 0, and `distance` measures in the
         * ROOT frame -- both ends are projected off the deck reservation before
         * the Chebyshev. On this restarted hull (heading 128) the saved deck
         * tile is two deck tiles from that hotspot but rounds to three once
         * projected, so the queue would refuse to arrive for a reason that has
         * nothing to do with the stale serial it exists to test. Stand on the
         * plank beside the hotspot first, which is where real play arrives
         * from anyway (~sailing_facility_op walks there with map_findsquare),
         * and put the player back on their saved tile afterwards. */
        int arrival_base_x = 0;
        int arrival_base_z = 0;
        int saved_deck_x = player->x;
        int saved_deck_z = player->z;

        if( ToriRSServer_MapInstanceBase(boat->instance, &arrival_base_x, &arrival_base_z) )
        {
            player->x = arrival_base_x + 3;
            player->z = arrival_base_z + 4;
        }
        selftest_sailing_stale_queues(srv, player, boat, old_serial);
        player->x = saved_deck_x;
        player->z = saved_deck_z;
        /* A guest is evacuated independently when the owned hull is freed. */
        struct ToriRSServerPlayer* guest = ToriRSServer_WorldAddPlayer(srv, NULL);
        SELFTEST_CHECK(guest != NULL, "a second session can board as a guest");
        if( guest )
        {
            ToriRSServer_WorldPlayerInit(guest);
            /*
             * Stand the guest on the real Pandemonium shore BEFORE it boards.
             *
             * Writing `sailing.shore_*` here does not survive the next line:
             * boarding records where you boarded FROM over it, which is the
             * whole purpose of that field. A fresh slot stands on the home tile
             * (3222,3218), and whether that tile reads as a legitimate shore
             * depends only on what the PREVIOUS section left built in the root
             * scene window -- so the assignment happened to survive in the
             * sailing-only run, where Lumbridge is not built and the tile reads
             * blocked, and was correctly replaced by 3222,3218 in the full
             * suite, where an earlier section had built it. Evacuating a guest
             * to Lumbridge is right; boarding a boat off the Pandemonium coast
             * from Lumbridge is the fixture's fault. State the shore physically.
             */
            ToriRSServer_WorldSetActive(srv, guest);
            ToriRSServer_WorldTeleport(srv, 0, 3058, 2978);
            int guest_ashore = guest->x == 3058 && guest->z == 2978 && guest->level == 0 &&
                               !ToriRSServer_SceneWalkBlocked(0, guest->x, guest->z) &&
                               !ToriRSServer_VesselTileSailable(0, guest->x, guest->z);
            ToriRSServer_WorldSetActive(srv, player);
            SELFTEST_CHECK(guest_ashore,
                           "the guest waits on the real Pandemonium shore, got %d,%d,%d",
                           guest->x, guest->z, guest->level);
            SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, guest, boat), "guest boards retained deck");
            SELFTEST_CHECK(guest->sailing.shore_x == 3058 && guest->sailing.shore_z == 2978 &&
                           guest->sailing.shore_level == 0,
                           "boarding records the shore the guest actually left, got %d,%d,%d",
                           guest->sailing.shore_x, guest->sailing.shore_z, guest->sailing.shore_level);
            ToriRSServer_MapInstanceVarSet(boat->instance, 8, 1);
            ToriRSServer_MapInstanceVarSet(boat->instance, 13, guest->pid + 1);
            ToriRSServer_VesselLogout(srv, guest);
            SELFTEST_CHECK(boat->in_use && ToriRSServer_MapInstanceVarGet(boat->instance, 8) == 0 &&
                           ToriRSServer_MapInstanceVarGet(boat->instance, 13) == 0,
                           "guest logout retires its job lease without deleting the captain's boat");
            SELFTEST_CHECK(ToriRSServer_MapInstanceVarGet(boat->instance, 9) == 40,
                           "guest logout preserves the loaded facility resources");
            SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, guest, boat), "guest can board again");
            ToriRSServer_WorldMapInstanceFree(srv, boat->instance);
            SELFTEST_CHECK(abs(player->x - 3059) <= 12 && abs(player->z - 2979) <= 12 &&
                           abs(guest->x - 3059) <= 12 && abs(guest->z - 2979) <= 12 &&
                           !ToriRSServer_SceneWalkBlocked(0, player->x, player->z) &&
                           !ToriRSServer_SceneWalkBlocked(0, guest->x, guest->z) &&
                           !ToriRSServer_VesselTileSailable(0, player->x, player->z) &&
                           !ToriRSServer_VesselTileSailable(0, guest->x, guest->z),
                           "free evacuates both captain and guest to walkable shore");
            SELFTEST_CHECK(!ToriRSServer_VesselAtTile(srv, guest->x, guest->z) && !guest->navigating_vessel,
                           "guest keeps no reference to the freed instance");
            ToriRSServer_WorldRemovePlayer(srv, guest);
        }
        else ToriRSServer_VesselFree(srv, boat->index);
    }
    if( cargo ) cargo->items[0] = old_item;
    remove(path);
    SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 0 &&
                   ToriRSServer_MapInstanceLiveCount() == instances_before,
                   "no vessel or deck reservation leaks after reconnect/free");
    /*
     * A save written before [chat] existed still means all three modes ON.
     * Loaded into a scratch record rather than a live slot because the point is
     * the absent section, and nothing in this file may be left carrying it.
     */
    {
        struct ToriRSServerPlayer* legacy = calloc(1, sizeof(*legacy));
        const char* legacy_path = "/tmp/torirs-sailing-chatmodes-legacy.ini";
        FILE* legacy_file = fopen(legacy_path, "wb");

        assert(legacy);
        SELFTEST_CHECK(legacy_file != NULL, "write an old-format save");
        if( legacy_file )
        {
            fprintf(legacy_file, "[player]\nversion = 1\nname = sailcaptain\n"
                                 "x = 3222\nz = 3218\nlevel = 0\n");
            fclose(legacy_file);
            legacy->saved_chat_public_mode = 3;
            legacy->saved_chat_private_mode = TORIRSSERVER_CHAT_PRIVATE_OFF;
            legacy->saved_chat_trade_mode = 2;
            SELFTEST_CHECK(ToriRSServer_LoadPlayer(legacy, legacy_path) &&
                           legacy->saved_chat_public_mode == 0 &&
                           legacy->saved_chat_private_mode == TORIRSSERVER_CHAT_PRIVATE_ON &&
                           legacy->saved_chat_trade_mode == 0,
                           "a save with no [chat] section keeps the all-ON defaults, got %d/%d/%d",
                           legacy->saved_chat_public_mode, legacy->saved_chat_private_mode,
                           legacy->saved_chat_trade_mode);
            remove(legacy_path);
        }
        free(legacy);
    }

    /* Exercise the production disconnect/login entrypoints too: the generic
     * [logout] instance backstop must not free a deck before capture. */
    struct ToriRSServerPlayer* owner = ToriRSServer_WorldAddPlayer(srv, NULL);
    SELFTEST_CHECK(owner != NULL, "create an owner for the actual disconnect path");
    if( owner )
    {
        ToriRSServer_WorldPlayerInit(owner);
        /* A real name, through the one seam that writes both halves: base 37
         * packs 12 characters, and the friend service -- which is where the
         * chat filter modes live -- is keyed by the packed form. The old
         * 23-character fixture name left name37 at 0 and no roster entry. */
        ToriRSServer_WorldSetDisplayName(owner, "sailcaptain");
        SELFTEST_CHECK(owner->name37 != 0, "the disconnect fixture has a base-37 name");
        ToriRSServer_FriendsLogin(owner->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);
        /* Not the defaults, in all three: Board-friend reads the private mode,
         * and a login that resets it to ON is the defect this proves gone. */
        ToriRSServer_FriendsSetChatModes(owner->name37, /* public */ 2,
                                         TORIRSSERVER_CHAT_PRIVATE_FRIENDS, /* trade */ 1);
        ToriRSServer_WorldSetActive(srv, owner);
        ToriRSServer_WorldTeleport(srv, 0, 3072, 3160);
        int owned_handle = ToriRSServer_VesselSpawn(srv, 1, 1, 3, 0, 3072, 3160, 0);
        struct ToriRSServerVessel* owned = ToriRSServer_VesselGet(srv, owned_handle);
        SELFTEST_CHECK(owned != NULL, "spawn the disconnect owner's raft");
        if( owned )
        {
            owned->owner_uid = owner->pid + 1;
            owned->cargo_slot = 1;
            SELFTEST_CHECK(ToriRSServer_VesselBuildPlayerDeck(srv, owned_handle), "furnish disconnect raft");
            owner->sailing.shore_x = 3059;
            owner->sailing.shore_z = 2979;
            SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, owner, owned), "board before disconnect");
            int instance_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "map_instance_handle");
            if( instance_varp >= 0 ) owner->varps[instance_varp] = owned->instance;
            int departed_pid = owner->pid;
            ToriRSServer_WorldRemovePlayer(srv, owner);
            SELFTEST_CHECK(!owner->active && ToriRSServer_VesselLiveCount(srv) == 0,
                           "production logout frees both session and owned vessel");
            for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
                SELFTEST_CHECK(!srv->vessels[i].in_use || srv->vessels[i].owner_uid != departed_pid + 1,
                               "no boat remains bound to recyclable pid %d", departed_pid);
            struct ToriRSServerPlayer* returning = ToriRSServer_WorldAddPlayer(srv, NULL);
            SELFTEST_CHECK(returning != NULL, "new session logs in after disconnect");
            if( returning )
            {
                ToriRSServer_WorldPlayerInit(returning);
                ToriRSServer_WorldSetDisplayName(returning, "sailcaptain");
                ToriRSServer_WorldLogin(returning);
                {
                    int restored_public = -1;
                    int restored_private = -1;
                    int restored_trade = -1;

                    ToriRSServer_FriendsChatModes(returning->name37, &restored_public,
                                                  &restored_private, &restored_trade);
                    SELFTEST_CHECK(restored_public == 2 &&
                                   restored_private == TORIRSSERVER_CHAT_PRIVATE_FRIENDS &&
                                   restored_trade == 1,
                                   "chat filter modes survive logout/login, got %d/%d/%d",
                                   restored_public, restored_private, restored_trade);
                }
                /*
                 * And the private mode's SECOND carrier, which is the only one
                 * that reaches the client at this revision.
                 *
                 * CHAT_FILTER_SETTINGS (opcode 124) is two bytes here, public
                 * and trade. The private filter's own packet is opcode 5, which
                 * this lane does not speak; what it uses instead is the cache's
                 * own `torirs_chatbox_layout` (proc 113 line 86), which forces
                 * the private filter to varbit 13674 on every relayout and is
                 * registered as interface_162:0's onvartransmit hook with
                 * var1054 -- the varbit's carrier -- first in its list. So the
                 * varbit is both the value the client reads and the event that
                 * makes it read it, and a login that leaves it at 0 is a login
                 * that pins the filter bar to "Private On" whatever the save
                 * said. Board-friend reads the same mode.
                 */
                {
                    const int chat_private_varbit = 13674;
                    const int chat_private_varp = 1054;
                    const struct ToriRSServerVarpDef* carrier =
                        ToriRSServer_ContentVarp(chat_private_varp);

                    SELFTEST_CHECK(ToriRSServer_VarbitCarrierBits(chat_private_varp) > 0,
                                   "this cache packs varbits into varp %d (chat_filter_clan)",
                                   chat_private_varp);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(returning, chat_private_varbit) ==
                                       TORIRSSERVER_CHAT_PRIVATE_ON,
                                   "the varbit starts at the fresh-session default, got %d",
                                   ToriRSServer_VarbitGet(returning, chat_private_varbit));
                    /*
                     * Called here rather than left to `ToriRSServer_WorldLogin`
                     * above, which is where it runs in production: at revision
                     * 239 the login arms a scene barrier and defers
                     * `ToriRSServer_WorldLoginFinish` — and with it the social
                     * dump — until MAP_BUILD_COMPLETE arrives from a client this
                     * fixture does not have. The native run in
                     * `docs/sailing_validation/lifecycle-chat-results.json`
                     * covers the deferred path end to end.
                     */
                    ToriRSServer_WorldSocialLogin(returning);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(returning, chat_private_varbit) ==
                                       TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                   "the social login dump publishes the saved private mode into "
                                   "varbit %d, got %d",
                                   chat_private_varbit,
                                   ToriRSServer_VarbitGet(returning, chat_private_varbit));
                    /* Without `transmit=yes` on the carrier the write above
                     * never leaves the server: both `ToriRSServer_WorldMarkVarp`
                     * and the login varp flush refuse an undeclared varp, and
                     * proc 113 keeps reading 0. This row is the declaration's
                     * gate, not a restatement of the write. */
                    SELFTEST_CHECK(carrier && carrier->transmit,
                                   "content declares varp %d transmit=yes so the varbit reaches "
                                   "the client (server/scripts/**/chat_filter.varp)",
                                   chat_private_varp);

                    /* A public/trade-only change must not disturb it. This is
                     * the data-loss regression from the other side: the client
                     * used to feed a fabricated private 0 back through
                     * chat_set_filter_184's
                     * `chat_setfilter(public, chat_getfilter_private, $new)`,
                     * and one Trade change rewrote the persisted Private mode
                     * to 0. */
                    ToriRSServer_FriendsSetChatModes(returning->name37, /* public */ 0,
                                                     TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                                     /* trade */ 2);
                    {
                        int after_public = -1;
                        int after_private = -1;
                        int after_trade = -1;

                        ToriRSServer_FriendsChatModes(returning->name37, &after_public,
                                                      &after_private, &after_trade);
                        SELFTEST_CHECK(after_private == TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                       "a trade-only change leaves the private mode alone, got %d",
                                       after_private);
                        SELFTEST_CHECK(ToriRSServer_VarbitGet(returning, chat_private_varbit) ==
                                           TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                       "and leaves varbit %d alone, got %d", chat_private_varbit,
                                       ToriRSServer_VarbitGet(returning, chat_private_varbit));
                        SELFTEST_CHECK(after_public == 0 && after_trade == 2,
                                       "while the modes that did change are stored, got %d/%d",
                                       after_public, after_trade);
                    }
                    /* Saved from the friends service, so the round trip that
                     * matters is the file the next login reads. */
                    SELFTEST_CHECK(ToriRSServer_SavePlayer(returning,
                                                           ToriRSServer_SavePath("sailcaptain")),
                                   "the trade-only change is written back to the save");
                    {
                        FILE* saved = fopen(ToriRSServer_SavePath("sailcaptain"), "rb");
                        char line[256];
                        int found_private = -1;

                        SELFTEST_CHECK(saved != NULL, "the trade-only change rewrote the save");
                        while( saved && fgets(line, sizeof(line), saved) )
                            if( sscanf(line, " private = %d", &found_private) == 1 )
                                break;
                        if( saved )
                            fclose(saved);
                        SELFTEST_CHECK(found_private == TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                       "[chat] private survives a trade-only change, got %d",
                                       found_private);
                    }
                    /* Put the modes back so the rest of the block, and any
                     * later reader of this fixture's save, sees what the rows
                     * above established. */
                    ToriRSServer_FriendsSetChatModes(returning->name37, /* public */ 2,
                                                     TORIRSSERVER_CHAT_PRIVATE_FRIENDS,
                                                     /* trade */ 1);
                }
                struct ToriRSServerVessel* resumed = ToriRSServer_VesselAtTile(srv, returning->x, returning->z);
                SELFTEST_CHECK(resumed && resumed->config_id == 1 && resumed->owner_uid == returning->pid + 1,
                               "production login restores aboard its raft under the new session uid");
                SELFTEST_CHECK(resumed && resumed->fine_x >> 7 == 3072 && resumed->fine_z >> 7 == 3160,
                               "production logout/login keeps the ocean location");
                ToriRSServer_WorldRemovePlayer(srv, returning);
            }
        }
        else ToriRSServer_WorldRemovePlayer(srv, owner);
        remove(ToriRSServer_SavePath("sailcaptain"));
    }

    /* ---- scriptsplayer: Board-friend through the real content proc ----
     *
     * ~sailing_board_friend switches to the captain with p_findvisibleplayer
     * and then reads vessel_here / vessel_slot / ~sailing_owned_port. Those
     * reads are the whole proc: while the vessel_* opcodes answered for
     * srv->active_player -- the guest, standing on the dock -- the captain's
     * hull was invisible to them and the proc always ended in "That captain
     * has no boat at this dock." This row drives the production proc, parks
     * on the real p_namedialog and answers it with the captain's name, so a
     * regression of that binding fails here rather than in a screenshot.
     */
    {
        int dock_row = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW,
                                                  "sailing_dock_the_pandemonium");
        SELFTEST_CHECK(dock_row >= 0, "the native Pandemonium dock row resolves");
        struct ToriRSServerPlayer* skipper = ToriRSServer_WorldAddPlayer(srv, NULL);
        struct ToriRSServerPlayer* joiner = ToriRSServer_WorldAddPlayer(srv, NULL);
        struct ToriRSServerPlayer* stowaway = ToriRSServer_WorldAddPlayer(srv, NULL);
        SELFTEST_CHECK(skipper != NULL, "Board-friend needs a captain session");
        SELFTEST_CHECK(joiner != NULL, "Board-friend needs a guest session");
        SELFTEST_CHECK(stowaway != NULL, "Board-friend needs a third session for its control");
        if( dock_row >= 0 && skipper && joiner && stowaway )
        {
            ToriRSServer_WorldPlayerInit(skipper);
            ToriRSServer_WorldPlayerInit(joiner);
            ToriRSServer_WorldPlayerInit(stowaway);
            ToriRSServer_WorldSetDisplayName(skipper, "boatskipper");
            ToriRSServer_WorldSetDisplayName(joiner, "boatjoiner");
            ToriRSServer_WorldSetDisplayName(stowaway, "boatstowaway");
            /* p_findvisibleplayer asks the FOUND name's private mode, so the
             * captain is the one who has to be showing all. */
            ToriRSServer_FriendsLogin(skipper->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);
            ToriRSServer_FriendsLogin(joiner->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);
            ToriRSServer_FriendsLogin(stowaway->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);

            ToriRSServer_WorldSetActive(srv, skipper);
            ToriRSServer_WorldTeleport(srv, 0, 3072, 3160);
            int friend_handle = ToriRSServer_VesselSpawn(srv, 2, 2, 5, 0, 3072, 3160, 0);
            struct ToriRSServerVessel* friend_boat = ToriRSServer_VesselGet(srv, friend_handle);
            SELFTEST_CHECK(friend_boat != NULL, "the captain gets a real skiff");
            if( friend_boat )
            {
                friend_boat->owner_uid = skipper->pid + 1;
                friend_boat->cargo_slot = 1;
                SELFTEST_CHECK(ToriRSServer_VesselBuildPlayerDeck(srv, friend_handle),
                               "furnish the captain's skiff");
                ToriRSServer_WorldTeleport(srv, 0, 3059, 2979);
                SELFTEST_CHECK(ToriRSServer_VesselRecover(srv, friend_handle, 0, 3070, 2987),
                               "moor the captain's skiff at the real Pandemonium gangplank");
                /* The owned-slot registration the proc reads in the CAPTAIN's
                 * context: slot 1, moored at dock_id 1 (the Pandemonium). */
                int owned_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT,
                                                           "sailing_boat_1_owned");
                int port_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT,
                                                          "sailing_boat_1_port");
                SELFTEST_CHECK(owned_bit >= 0 && port_bit >= 0,
                               "the native owned/port varbits for slot 1 resolve");
                if( owned_bit >= 0 ) ToriRSServer_VarbitSet(srv, owned_bit, 1);
                if( port_bit >= 0 ) ToriRSServer_VarbitSet(srv, port_bit, 1);
                skipper->sailing.shore_x = 3059;
                skipper->sailing.shore_z = 2979;
                SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, skipper, friend_boat),
                               "the captain stands on their own deck");

                ToriRSServer_WorldSetActive(srv, joiner);
                ToriRSServer_WorldTeleport(srv, 0, 3069, 2987);
                SELFTEST_CHECK(!ToriRSServer_VesselAtTile(srv, joiner->x, joiner->z),
                               "the guest starts ashore at the dock");
                int32_t board_args[1] = {dock_row};
                static const uint8_t skipper_reply[] = "boatskipper";
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                                   srv, "[proc,sailing_board_friend]", board_args, 1),
                               "the guest runs the production Board-friend proc");
                SELFTEST_CHECK(joiner->active_script &&
                                   joiner->active_script->execution == SSVM_NAMEDIALOG,
                               "Board-friend parks on p_namedialog for the captain's name");
                SELFTEST_CHECK(ToriRSServer_ScriptsResumeNamedialog(
                                   srv, skipper_reply, (int)sizeof(skipper_reply) - 1),
                               "the name reply resumes the parked proc");
                SELFTEST_CHECK(!joiner->active_script, "Board-friend runs to completion");
                SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, joiner->x, joiner->z) == friend_boat,
                               "the guest boards the captain's hull found by p_findvisibleplayer");
                SELFTEST_CHECK(joiner->level == ToriRSServer_VesselDeckPlane(friend_boat),
                               "the guest stands on the deck plane, not the dock");
                SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, skipper->x, skipper->z) == friend_boat,
                               "the captain was never moved by the guest's boarding");
                SELFTEST_CHECK(friend_boat->owner_uid == skipper->pid + 1,
                               "boarding by name never reassigns the hull to the guest");
                SELFTEST_CHECK(!ToriRSServer_VesselCanNavigate(srv, joiner, friend_boat),
                               "a passenger receives no navigation permission by boarding");

                /* ---- the active binding, and the second plank -------------
                 *
                 * Two separate defects, both visible on this one fixture.
                 *
                 * 1. ~sailing_board_friend ends in `p_finduid($guest);
                 *    vessel_board($boat)`, so the op runs with the SSVM bound
                 *    to the guest while the WORLD is still bound to the player
                 *    whose tick is executing. ToriRSServer_VesselBoardPlayer
                 *    borrows that world binding to teleport, and used to walk
                 *    away leaving it pointed at whoever it just moved -- the
                 *    rest of the tick then addressed the wrong player. The op
                 *    wrapped it in a save/restore; the function now does it
                 *    itself. `joiner` is the ticking player here: it is what
                 *    was active when the proc was started, and the proc's own
                 *    p_findvisibleplayer / p_finduid switches move only the
                 *    SCRIPT's binding. This proc's p_finduid switches BACK to
                 *    its caller before boarding, so the row below states the
                 *    end-of-proc contract rather than discriminating on its
                 *    own -- the rows that fail without the fix are the two
                 *    C-level ones further down, where the world is bound to
                 *    somebody the call is not moving.
                 *
                 * 2. The deck search took the first walkable tile outward from
                 *    the pivot with no regard for who was standing on it, and
                 *    the captain boarded first -- so the guest landed on the
                 *    captain's exact tile. Players never block each other, so
                 *    the collision map cannot report the captain: the search
                 *    has to ask the player pool.
                 */
                SELFTEST_CHECK(srv->active_player == joiner,
                               "vessel_board under p_finduid leaves the ticking player bound, "
                               "got pid %d, wanted %d",
                               srv->active_player ? srv->active_player->pid : -1, joiner->pid);
                SELFTEST_CHECK(skipper->level == ToriRSServer_VesselDeckPlane(friend_boat),
                               "the captain is still on the deck plane too");
                SELFTEST_CHECK(joiner->x != skipper->x || joiner->z != skipper->z,
                               "the second boarder gets its own deck tile, not the captain's: "
                               "captain %d,%d guest %d,%d",
                               skipper->x, skipper->z, joiner->x, joiner->z);
                SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, joiner->x, joiner->z) == friend_boat,
                               "and that tile is still this hull's deck");

                /* The disembark half of the same binding rule. No content
                 * script disembarks anybody but its own caller, so this drives
                 * the entrypoint the op calls, with the world bound to a
                 * DIFFERENT player -- exactly the state a p_finduid'd
                 * vessel_disembark would arrive in. The hull is moored at the
                 * real Pandemonium gangplank, so there is a landing. */
                ToriRSServer_WorldSetActive(srv, skipper);
                SELFTEST_CHECK(ToriRSServer_VesselDisembarkPlayer(srv, joiner),
                               "the moored hull has a real gangplank landing for the guest");
                SELFTEST_CHECK(srv->active_player == skipper,
                               "vessel_disembark restores the binding it borrowed, got pid %d, "
                               "wanted %d",
                               srv->active_player ? srv->active_player->pid : -1, skipper->pid);
                SELFTEST_CHECK(!ToriRSServer_VesselAtTile(srv, joiner->x, joiner->z) &&
                               ToriRSServer_VesselAtTile(srv, skipper->x, skipper->z) == friend_boat,
                               "and moved only the named player, leaving the captain aboard");
                SELFTEST_CHECK(ToriRSServer_VesselBoardPlayer(srv, joiner, friend_boat),
                               "the guest returns to the deck for the control below");
                SELFTEST_CHECK(srv->active_player == skipper,
                               "the return trip restores the binding as well, got pid %d",
                               srv->active_player ? srv->active_player->pid : -1);
                joiner->sailing.shore_x = 3069;
                joiner->sailing.shore_z = 2987;
                joiner->sailing.shore_level = 0;
                /* ---- end of the binding/plank block ---------------------- */

                /* Control: the same proc, same dock, naming somebody who owns
                 * no boat. boatjoiner is now standing ON the captain's hull, so
                 * this only refuses if vessel_slot's ownership gate is read in
                 * the NAMED player's context -- the exact read the old binding
                 * got wrong in the other direction. */
                ToriRSServer_WorldSetActive(srv, stowaway);
                ToriRSServer_WorldTeleport(srv, 0, 3069, 2987);
                static const uint8_t joiner_reply[] = "boatjoiner";
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                                   srv, "[proc,sailing_board_friend]", board_args, 1),
                               "the control guest runs the same proc");
                SELFTEST_CHECK(ToriRSServer_ScriptsResumeNamedialog(
                                   srv, joiner_reply, (int)sizeof(joiner_reply) - 1),
                               "the control name reply resumes the parked proc");
                SELFTEST_CHECK(!ToriRSServer_VesselAtTile(srv, stowaway->x, stowaway->z) &&
                                   stowaway->x == 3069 && stowaway->z == 2987,
                               "naming a passenger who owns no boat boards nobody");
            }
            ToriRSServer_WorldRemovePlayer(srv, stowaway);
            ToriRSServer_WorldRemovePlayer(srv, joiner);
            ToriRSServer_WorldRemovePlayer(srv, skipper);
            remove(ToriRSServer_SavePath("boatskipper"));
            remove(ToriRSServer_SavePath("boatjoiner"));
            remove(ToriRSServer_SavePath("boatstowaway"));
        }
    }
done:
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
        if( srv->vessels[i].in_use ) ToriRSServer_VesselFree(srv, srv->vessels[i].index);
    ToriRSServer_WorldSetActive(srv, player);
    player->sailing = old_sailing;
    memcpy(player->varps, old_varps, sizeof(player->varps));
    free(old_varps);
    selftest_park_player(srv, 3222, 3218);
    player->rebuild_pending = 0;
    if( scripts_owned ) ToriRSServer_ScriptsFree(srv);
}
