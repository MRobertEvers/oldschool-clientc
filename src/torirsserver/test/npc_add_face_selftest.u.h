/*
 * What an npc's [add] restates about its facing, read back through the client's
 * own NPC_INFO reader for the world's wire revision.
 *
 * The rule (RSProx rev 238+, counts in .claude/skills/rsprox-content/SKILL.md
 * section 9): an npc holds two facing latches, the entity one and
 * `face_add_latch`. An add sends the entity if one is held, else the coord
 * while a facesquare stands, else `reset` once a reset has cleared a coord,
 * else nothing. `TORIRSSERVER_SELFTEST_NPCFACEADD_ONLY=1` runs it alone; the
 * revision is `TORIRSSERVER_REV`.
 */

enum SelftestAddFace
{
    SELFTEST_ADDFACE_NOTADDED = -1,
    SELFTEST_ADDFACE_NONE = 0,
    SELFTEST_ADDFACE_RESET,
    SELFTEST_ADDFACE_COORD,
    SELFTEST_ADDFACE_ENTITY,
    SELFTEST_ADDFACE_ANGLE,
};

static enum SelftestAddFace
selftest_add_face_read(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int slot,
    int* out_x,
    int* out_z)
{
    static struct RsprotInfoOp ops[4096];
    static int active[2048];
    static struct ToriRSServerCapture capture;
    int const revision = srv->wire->revision;
    struct GameProtoRevTable const* rev =
        revision >= 242 ? GameProtoRev_OSRS242() : revision >= 241 ? GameProtoRev_OSRS241() : GameProtoRev_OSRS239();
    enum SelftestAddFace kind = SELFTEST_ADDFACE_NOTADDED;
    int active_count = 0;
    int at;

    assert(rev);
    ToriRSServer_WorldNpcRefreshObservation(srv, &srv->npcs[slot]);
    srv->npcs[slot].tele = 0;
    player->tracked_count = 0;
    memset(player->npc_tracked, 0, sizeof(player->npc_tracked));
    ToriRSServer_SlotMapReset(player);
    player->npc_view_tiles = TORIRSSERVER_NPC_VIEW_TILES;
    ToriRSServer_ZoneSyncNpcs(srv);
    ToriRSServer_PlayerzonemapMove(player);
    ToriRSServer_CaptureBegin(srv, &capture);
    ToriRSServer_SendNpcInfo(player);
    ToriRSServer_CaptureEnd(srv);

    at = ToriRSServer_CaptureFindNamed(&capture, PKT_NAME_NPC_INFO, 0);
    SELFTEST_CHECK(at >= 0, "the re-add writes an NPC_INFO packet");
    if( at < 0 )
        return SELFTEST_ADDFACE_NOTADDED;
    SELFTEST_CHECK(!capture.packets[at].truncated, "the NPC_INFO under test is whole");
    {
        RsprotInfoList list;
        int cur = -1;
        int count;

        rsprot_info_list_init(&list, ops, 4096, g_selftest_info_bytes, (int)sizeof(g_selftest_info_bytes));
        count = rev->npc_info_read(capture.packets[at].data, capture.packets[at].len, &list);
        for( int i = 0; i < count; i++ )
        {
            const struct RsprotInfoOp* op = &ops[i];

            if( op->kind == RSPROT_OP_ADD_NEW )
            {
                int world = ToriRSServer_SlotMapWorld(player, (int)op->value);

                if( active_count < 2048 )
                    active[active_count++] = world;
                if( world == slot )
                    kind = SELFTEST_ADDFACE_NONE;
                cur = -1;
            }
            else if( op->kind == RSPROT_OP_SET_EXT )
            {
                cur = (int)op->value < active_count ? active[op->value] : -1;
            }
            else if( cur == slot && op->kind == RSPROT_OP_FACE_ENTITY )
            {
                /* A reset reads back as 65535 on the classic header, -1 on
                 * the modern (rev 239+) facing block. */
                kind = (op->face_entity.entity_id == 65535 || op->face_entity.entity_id < 0)
                           ? SELFTEST_ADDFACE_RESET
                           : SELFTEST_ADDFACE_ENTITY;
            }
            else if( cur == slot && op->kind == RSPROT_OP_FACE_COORD )
            {
                kind = SELFTEST_ADDFACE_COORD;
                *out_x = op->face_coord.x;
                *out_z = op->face_coord.z;
            }
            else if( cur == slot && op->kind == RSPROT_OP_FACE_ANGLE )
            {
                kind = SELFTEST_ADDFACE_ANGLE;
            }
        }
    }
    return kind;
}

/* The end of a tick, for the facing blocks only: what `phase_cleanup` does. */
static void
selftest_add_face_tick(struct ToriRSServerNpc* npc)
{
    ToriRSServer_NpcFaceAddLatch(npc);
    npc->masks = 0;
}

static void
selftest_npc_add_face(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int slot;
    struct ToriRSServerNpc* npc;
    int x = 0;
    int z = 0;
    enum SelftestAddFace got;

    if( srv->wire->revision < 239 )
    {
        fprintf(stderr, "ToriRSServer selftest: npc add facing skipped on rev %d\n", srv->wire->revision);
        return;
    }
    slot = selftest_require_npc(srv, 3028, g_home_x + 3, g_home_z + 5, 0);
    SELFTEST_CHECK(slot >= 0, "the fixture goblin should exist");
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    /* A selftest world's zone map fills on its first ticks. */
    selftest_tick(srv);
    selftest_tick(srv);

    /* A fresh npc: nothing. The common case, and what must stay nothing. */
    npc->face_entity = -1;
    npc->face_add_latch = TORIRSSERVER_NPCFACEADD_NONE;
    npc->masks = 0;
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_NONE, "a fresh npc's add carries no facing, got %d", (int)got);

    /* An entity face then its reset, with no coord behind it: nothing again
     * (4448 slot 28451: player, reset, every later add empty). */
    ToriRSServer_NpcFacePlayer(npc, player->pid);
    SELFTEST_CHECK(npc->face_entity >= 0, "the goblin latches the player");
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_ENTITY, "an add restates a held entity, got %d", (int)got);
    npc->face_entity = -1;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_NONE,
                   "a reset that only cleared an entity leaves the add empty, got %d", (int)got);

    /* A coord stands: the add restates it. */
    npc->face_x = ToriRSServer_CoordFine(g_home_x + 7, 1);
    npc->face_z = ToriRSServer_CoordFine(g_home_z + 9, 1);
    npc->masks |= TORIRSSERVER_NMASK_FACE_COORD;
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_COORD, "an add restates a standing coord, got %d", (int)got);
    SELFTEST_CHECK(x == npc->face_x, "the restated coord's x is the facesquare's, got %d", x);
    SELFTEST_CHECK(z == npc->face_z, "the restated coord's z is the facesquare's, got %d", z);

    /* An entity over the coord wins; its reset resolves the coord too
     * (4451 slot 28400: coord, player, reset, add reset). */
    ToriRSServer_NpcFacePlayer(npc, player->pid);
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_ENTITY, "the entity wins over a coord, got %d", (int)got);
    npc->face_entity = -1;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_RESET, "a reset with a coord behind it is the add's, got %d", (int)got);

    /* And stays: the next add carries it again, with no new block between
     * (Klenter, 4487: 13 adds, all reset). */
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_RESET, "the reset latch holds across adds, got %d", (int)got);

    /* An angle ends it, and a stale facesquare must not come back with it
     * (`face_x` is still set). */
    npc->face_angle = 512;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ANGLE;
    selftest_add_face_tick(npc);
    got = selftest_add_face_read(srv, player, slot, &x, &z);
    SELFTEST_CHECK(got == SELFTEST_ADDFACE_NONE, "an angle clears the add's facing, got %d", (int)got);

    /* Leave the fixture goblin as a fresh npc for whatever runs next. */
    npc->face_x = 0;
    npc->face_z = 0;
    npc->face_add_latch = TORIRSSERVER_NPCFACEADD_NONE;
    npc->face_entity = -1;
    npc->masks = 0;
}
