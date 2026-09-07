/* Called from the lifecycle roundtrip while the real content pack is loaded.
 * Exercise old queued work against a new hull that reused the same handle. */
static void
selftest_sailing_stale_queues(struct ToriRSServer* srv,
                             struct ToriRSServerPlayer* player,
                             struct ToriRSServerVessel* boat, int old_serial)
{
    SELFTEST_CHECK(srv->scripts_ok && boat->serial != old_serial,
                   "stale callback fixture has real content and a replacement hull identity");
    if( !srv->scripts_ok || boat->serial == old_serial ) return;
    int saved_regs[7];
    for( int i=0; i<7; ++i ) saved_regs[i]=ToriRSServer_MapInstanceVarGet(boat->instance,8+i);
    int building=ToriRSServer_MapInstanceVarGet(boat->instance,110);
    struct ToriRSServerQueued queue[TORIRSSERVER_QUEUE_MAX];
    struct ToriRSServerQueued engine[TORIRSSERVER_ENGINE_QUEUE_MAX];
    memcpy(queue,player->queue,sizeof(queue));
    memcpy(engine,player->engine_queue,sizeof(engine));
    struct ToriRSServerPlayer* was=srv->active_player;
    ToriRSServer_WorldSetActive(srv,player);
    int xp=player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING];
    /* On the replacement hull, the same hotspot has new work with the same
     * per-instance token. A callback from its old identity must not stop it.
     * Building mode makes the current-identity operator check stop the work;
     * that positive control proves this is not a callback that always no-ops. */
    ToriRSServer_MapInstanceVarSet(boat->instance,110,1);
    static const struct {const char* name; int argc;} jobs[]={
        {"[queue,sailing_salvage_tick]",4},
        {"[queue,sailing_trawling_tick]",4},
        {"[queue,sailing_cannon_tick]",5},
    };
    for( unsigned job=0; job<sizeof(jobs)/sizeof(jobs[0]); ++job )
    {
        ToriRSServer_MapInstanceVarSet(boat->instance,8,1);
        ToriRSServer_MapInstanceVarSet(boat->instance,13,player->pid+1);
        ToriRSServer_MapInstanceVarSet(boat->instance,14,73);
        int32_t args[5]={boat->index,old_serial,0,73,0};
        if( jobs[job].argc==5 ) { args[3]=boat->instance; args[4]=73; }
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv,jobs[job].name,args,jobs[job].argc),
                       "%s accepts its persisted callback signature",jobs[job].name);
        SELFTEST_CHECK(ToriRSServer_MapInstanceVarGet(boat->instance,8)==1 &&
                       ToriRSServer_MapInstanceVarGet(boat->instance,14)==73 &&
                       !memcmp(queue,player->queue,sizeof(queue)) &&
                       !memcmp(engine,player->engine_queue,sizeof(engine)) &&
                       player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING]==xp,
                       "stale %s cannot stop replacement work, reschedule or award XP",jobs[job].name);
        args[1]=boat->serial;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv,jobs[job].name,args,jobs[job].argc) &&
                       ToriRSServer_MapInstanceVarGet(boat->instance,8)==0,
                       "current-identity %s executes its real operator stop control",jobs[job].name);
    }
    /*
     * The sort queue's own stale-serial refusal, made observable.
     *
     * `[queue,sailing_sort_tick]` opens with the same `vessel_stat(handle, 10)
     * ! serial` refusal as the three jobs above, but unlike them it records
     * nothing on the hull: its work is a sort of the OPERATOR's items. Left
     * alone, a stale and a current call are indistinguishable here, because
     * this reconstructed skiff carries no sorting station and both calls
     * return at the very next line for a reason that has nothing to do with
     * the serial.
     *
     * So give the deck the native station the skiff can actually carry --
     * hotspot 1's tenth option, `sailing_boat_facility_salvaging_station_2x5a`
     * -- which sits on the very tile the arrival fixture already stands the
     * player on, and one real salvage. Now the difference between refusal and
     * execution is a consumed item plus Sailing XP.
     */
    {
        const struct ToriRSServerIds* sort_ids = ToriRSServer_Ids();
        struct ToriRSServerContainer* pack =
            ToriRSServer_ContainerResolve(srv, player, sort_ids->inv_backpack);
        int salvage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ,
                                                 "sailing_small_shipwreck_salvage");
        int saved_facility = boat->facility[4];
        SELFTEST_CHECK(pack != NULL && pack->slots > 0 && salvage >= 0,
                       "the sort fixture has a backpack and a real native salvage");
        if( pack && pack->slots > 0 && salvage >= 0 )
        {
            struct ToriRSServerItem* saved_items =
                malloc((size_t)pack->slots * sizeof(*saved_items));
            assert(saved_items);
            memcpy(saved_items, pack->items, (size_t)pack->slots * sizeof(*saved_items));
            memset(pack->items, 0, (size_t)pack->slots * sizeof(*pack->items));
            pack->items[0].obj_id = salvage;
            pack->items[0].count = 1;
            /* 1-based pick into hotspot 1's option column: the station is its
             * index 9 (`sailing_boat_skiff` hotspot dbrow 8528). */
            boat->facility[4] = 10;
            int sort_xp = player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING];
            int32_t sort_args[2] = {boat->index, old_serial};
            SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[queue,sailing_sort_tick]", sort_args, 2) &&
                           pack->items[0].obj_id == salvage && pack->items[0].count == 1 &&
                           player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING] == sort_xp &&
                           !memcmp(queue, player->queue, sizeof(queue)),
                           "stale [queue,sailing_sort_tick] cannot sort the replacement hull's salvage");
            sort_args[1] = boat->serial;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[queue,sailing_sort_tick]", sort_args, 2) &&
                           pack->items[0].obj_id != salvage &&
                           player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING] > sort_xp,
                           "current-identity sort consumes the actual salvage and awards Sailing XP");
            memcpy(pack->items, saved_items, (size_t)pack->slots * sizeof(*pack->items));
            free(saved_items);
            player->stat_xp_tenths[TORIRSSERVER_STAT_SAILING] = sort_xp;
        }
        boat->facility[4] = saved_facility;
    }
    /* An arrival to the exact same handle/hotspot used to open the new hull's
     * customization panel. It must retain the old hull serial while walking. */
    int modal=player->mainmodal_group;
    int32_t arrive[5]={boat->index,old_serial,0,1,20};
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv,"[queue,sailing_facility_arrive]",arrive,5) &&
                   player->mainmodal_group==modal,
                   "stale arrival cannot open a replacement hull's customization interface");
    arrive[1]=boat->serial;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv,"[queue,sailing_facility_arrive]",arrive,5) &&
                   player->mainmodal_group==939,
                   "current arrival reaches the actual native customization interface");
    ToriRSServer_WorldCloseModal(srv);
    for( int i=0; i<7; ++i ) ToriRSServer_MapInstanceVarSet(boat->instance,8+i,saved_regs[i]);
    ToriRSServer_MapInstanceVarSet(boat->instance,110,building);
    memcpy(player->queue,queue,sizeof(queue));
    memcpy(player->engine_queue,engine,sizeof(engine));
    ToriRSServer_WorldSetActive(srv,was);
}
