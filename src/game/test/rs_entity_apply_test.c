/*
 * game/rs_entity_apply.h: the one copy of PLAYER_INFO / NPC_INFO's rules,
 * against a World, an entity sync state and a host that records what it was
 * asked. Asserts on.
 */
#include "game/rs_chat.h"
#include "game/rs_entity_apply.h"
#include "game/rs_entity_sync.h"
#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "streams/rsprot_info.h"
#include "world/entity_npc.h"
#include "world/entity_pathing.h"
#include "world/entity_player.h"
#include "world/world.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                         \
        }                                                                                          \
    } while( 0 )

/* ---- a recording host ---- */

#define NOTES_MAX 64

struct TestHost
{
    struct World* world;
    int next_element;
    /* npc_resolve: base -> rung; anything unlisted is itself. */
    int resolve_from[4];
    int resolve_to[4];
    int resolve_count;
    int origin_x, origin_z;
    int npc_spawns;
    int npc_retypes;
    int last_retype_type;
    struct RS_EntityNote notes[NOTES_MAX];
    int note_count;
    /* The entity's tile when NPC_ADDED was presented. */
    int added_x, added_z;
    /* public_chat_shown: the answer, and what it was last asked. */
    bool chat_hidden;
    int chat_asks;
    char chat_asked_sender[64];
    bool chat_asked_ignorable;
};

static bool
host_public_chat_shown(
    void* ctx,
    char const* sender,
    bool ignorable)
{
    struct TestHost* h = ctx;

    h->chat_asks++;
    snprintf(h->chat_asked_sender, sizeof(h->chat_asked_sender), "%s", sender ? sender : "(none)");
    h->chat_asked_ignorable = ignorable;
    return !h->chat_hidden;
}

static int
host_npc_resolve(
    void* ctx,
    int base)
{
    struct TestHost* h = ctx;
    for( int i = 0; i < h->resolve_count; i++ )
        if( h->resolve_from[i] == base )
            return h->resolve_to[i];
    return base;
}

/* Every type is known; type 7 has a stance set 7xx, the others 1xx. */
static int
host_npc_type(
    void* ctx,
    int npc_type,
    int base_type,
    struct RS_EntityNpcType* out)
{
    int base = npc_type == 7 ? 700 : 100;

    (void)ctx;
    (void)base_type;
    if( npc_type < 0 )
        return 0;
    memset(out, 0, sizeof(*out));
    out->size = npc_type == 7 ? 2 : 1;
    out->turn_speed = npc_type == 9 ? 0 : 32;
    memset(&out->idle, 0xff, sizeof(out->idle));
    out->idle.readyanim = base + 1;
    out->idle.walkanim = base + 2;
    out->name = npc_type == 7 ? "Seven" : "Other";
    for( int i = 0; i < 5; i++ )
        out->actions[i] = i == 0 ? "Talk-to" : "";
    out->combat_level = npc_type;
    out->minimap_visible = true;
    out->interactable = true;
    return 1;
}

static void
host_hitsplat(
    void* ctx,
    int type,
    int* duration,
    int* policy)
{
    (void)ctx;
    *duration = 50 + type;
    *policy = WORLD_HITMARK_POLICY_DISCARD;
}

static int
host_headbar_persist(
    void* ctx,
    int type)
{
    (void)ctx;
    return type * 10;
}

static int
host_home_view(
    void* ctx,
    int abs_x,
    int abs_z,
    int* out_x,
    int* out_z)
{
    (void)ctx;
    (void)abs_x;
    (void)abs_z;
    (void)out_x;
    (void)out_z;
    return 0;
}

static void
host_npc_origin(
    void* ctx,
    int view,
    int* x,
    int* z,
    int* level)
{
    struct TestHost* h = ctx;
    (void)view;
    *x = h->origin_x;
    *z = h->origin_z;
    *level = 0;
}

static int
host_player_spawn(
    void* ctx,
    int tile_x,
    int tile_z,
    int level)
{
    struct TestHost* h = ctx;
    return World_PlayerSpawn(h->world, h->next_element++, level, tile_x, tile_z, RS_EntityApply_PlayerSpawnIdle());
}

static int
host_npc_spawn(
    void* ctx,
    int npc_type,
    int base_type,
    int tile_x,
    int tile_z,
    int level)
{
    struct TestHost* h = ctx;
    struct RS_EntityNpcType type;
    int idx;

    h->npc_spawns++;
    host_npc_type(ctx, npc_type, base_type, &type);
    idx = World_NpcSpawn(h->world, h->next_element++, npc_type, level, tile_x, tile_z, type.size, type.idle);
    RS_EntityApply_NpcSpawned(h->world, idx, &type);
    return idx;
}

static void
host_npc_retype(
    void* ctx,
    int idx,
    int element_id,
    int npc_type,
    int base_type)
{
    struct TestHost* h = ctx;
    struct RS_EntityNpcType type;

    (void)element_id;
    h->npc_retypes++;
    h->last_retype_type = npc_type;
    if( host_npc_type(ctx, npc_type, base_type, &type) )
        RS_EntityApply_NpcRetyped(h->world, idx, npc_type, &type);
}

static void
host_present(
    void* ctx,
    struct RS_EntityNote const* n)
{
    struct TestHost* h = ctx;

    assert(h->note_count < NOTES_MAX);
    h->notes[h->note_count++] = *n;
    if( n->kind == RS_ENTITY_NOTE_NPC_ADDED )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(&h->world->entities.npc, n->idx);
        h->added_x = npc->pathing.route_x[0];
        h->added_z = npc->pathing.route_z[0];
    }
}

static int
notes_of(
    struct TestHost const* h,
    enum RS_EntityNoteKind kind)
{
    int n = 0;
    for( int i = 0; i < h->note_count; i++ )
        n += h->notes[i].kind == kind;
    return n;
}

struct Fixture
{
    struct World* world;
    struct RS_EntitySync esync;
    struct GameProtoRevTable rev;
    struct TestHost h;
    struct RS_EntityApplyHost host;
    struct RS_EntityApplyIn in;
};

static void
fixture_init(struct Fixture* f)
{
    memset(f, 0, sizeof(*f));
    f->world = World_New();
    assert(f->world);
    RS_EntitySync_Init(&f->esync);
    f->h.world = f->world;
    f->h.next_element = 1;
    f->h.origin_x = 50;
    f->h.origin_z = 50;
    f->host = (struct RS_EntityApplyHost){
        .ctx = &f->h,
        .npc_resolve = host_npc_resolve,
        .npc_type = host_npc_type,
        .hitsplat = host_hitsplat,
        .headbar_persist = host_headbar_persist,
        .home_view = host_home_view,
        .npc_origin = host_npc_origin,
        .public_chat_shown = host_public_chat_shown,
        .player_spawn = host_player_spawn,
        .npc_spawn = host_npc_spawn,
        .npc_retype = host_npc_retype,
        .present = host_present,
    };
    f->in = (struct RS_EntityApplyIn){
        .world = f->world,
        .esync = &f->esync,
        .rev = &f->rev,
        .host = &f->host,
    };
}

static void
fixture_free(struct Fixture* f)
{
    RS_EntitySync_Free(&f->esync);
    World_Free(f->world);
}

static struct RsprotInfoOp
op_value(
    int kind,
    uint64_t value)
{
    struct RsprotInfoOp op;
    memset(&op, 0, sizeof(op));
    op.kind = (uint8_t)kind;
    op.who = -1;
    op.value = value;
    return op;
}

static struct RsprotInfoOp
op_delta(
    int dx,
    int dz,
    bool jump)
{
    struct RsprotInfoOp op = op_value(RSPROT_OP_DELTA_XZ, 0);
    op.delta_xz.dx = (int16_t)dx;
    op.delta_xz.dz = (int16_t)dz;
    op.delta_xz.jump = jump;
    return op;
}

static int
npc_of(
    struct Fixture* f,
    int slot)
{
    int idx = -1;
    RS_EntitySync_FindNpc(&f->esync, slot, &idx, NULL);
    return idx;
}

/* An add record spawns the rung, registers it, and is announced once its
 * DELTA_XZ placed it (FAULTS E1). */
static void
test_npc_add_record(void)
{
    struct Fixture f;
    struct RsprotInfoOp ops[] = {
        op_value(RSPROT_OP_ADD_NEW, 5),
        op_value(RSPROT_OP_NPCTYPE, 7),
        op_delta(3, -2, true),
    };
    struct WorldEntity_NPC const* npc;
    int idx;

    fixture_init(&f);
    RS_EntityApply_NpcInfo(&f.in, 0, ops, 3);
    idx = npc_of(&f, 5);
    CHECK(idx >= 0, "the add registered the slot");
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc && npc->server_slot == 5, "the entity carries its server slot");
    CHECK(npc && npc->npc_id == 7 && npc->base_npc_id == 7, "the rung and the shell");
    CHECK(npc && npc->size == 2 && npc->idle_animations.readyanim == 701, "the type's size and stances");
    CHECK(npc && strcmp(npc->name, "Seven") == 0 && npc->combat_level == 7, "the type's name and level");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_NPC_SPAWNED) == 1, "one spawn note");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_NPC_ADDED) == 1, "announced once");
    CHECK(f.h.added_x == 53 && f.h.added_z == 48, "announced after the record's delta placed it");
    fixture_free(&f);
}

/* Removed and added again in one packet: the same npc, moved, not a new one
 * (RSProx `teleport`); removed and not added: gone, with a note first. */
static void
test_npc_readd_is_a_move(void)
{
    struct Fixture f;
    struct RsprotInfoOp add[] = {
        op_value(RSPROT_OP_ADD_NEW, 5),
        op_value(RSPROT_OP_NPCTYPE, 7),
        op_delta(0, 0, true),
    };
    struct RsprotInfoOp readd[] = {
        op_value(RSPROT_OP_CLEAR_OLD, 0),
        op_value(RSPROT_OP_ADD_NEW, 5),
        op_value(RSPROT_OP_NPCTYPE, 7),
        op_delta(4, 0, false),
    };
    struct RsprotInfoOp gone[] = {
        op_value(RSPROT_OP_CLEAR_OLD, 0),
    };
    uint32_t serial;
    int idx;

    fixture_init(&f);
    RS_EntityApply_NpcInfo(&f.in, 0, add, 3);
    idx = npc_of(&f, 5);
    serial = ((struct WorldEntity_NPC*)World_EntityPoolGet(&f.world->entities.npc, idx))->spawn_serial;
    f.h.note_count = 0;
    RS_EntityApply_NpcInfo(&f.in, 0, readd, 4);
    CHECK(npc_of(&f, 5) == idx, "the same pool entry");
    CHECK(((struct WorldEntity_NPC*)World_EntityPoolGet(&f.world->entities.npc, idx))->spawn_serial == serial,
        "the same npc, not a respawn");
    CHECK(f.h.npc_spawns == 1, "no second spawn");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_NPC_GONE) == 0, "nothing was removed");
    f.h.note_count = 0;
    RS_EntityApply_NpcInfo(&f.in, 0, gone, 1);
    CHECK(npc_of(&f, 5) < 0, "removed at the packet's end");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_NPC_GONE) == 1 && f.h.notes[0].slot == 5, "gone noted with its slot");
    fixture_free(&f);
}

/* A server type change resets the server's overrides and retypes through the
 * maker; BAS_CHANGE with an empty mask is the reset to the type's stances. */
static void
test_npc_change_type_and_bas_reset(void)
{
    struct Fixture f;
    struct RsprotInfoOp add[] = {
        op_value(RSPROT_OP_ADD_NEW, 2),
        op_value(RSPROT_OP_NPCTYPE, 3),
        op_delta(0, 0, true),
    };
    struct RsprotInfoOp change[] = {
        op_value(RSPROT_OP_ADD_OLD, 0),
        op_value(RSPROT_OP_SET_EXT, 0),
        op_value(RSPROT_OP_CHANGE_TYPE, 0),
    };
    struct RsprotInfoOp bas[] = {
        op_value(RSPROT_OP_ADD_OLD, 0),
        op_value(RSPROT_OP_SET_EXT, 0),
        op_value(RSPROT_OP_BAS_CHANGE, 0),
    };
    struct WorldEntity_NPC* npc;
    int idx;

    fixture_init(&f);
    RS_EntityApply_NpcInfo(&f.in, 0, add, 3);
    idx = npc_of(&f, 2);
    /* As the wire shapes it: the list is rebuilt, then SET_EXT addresses it. */
    {
        struct RsprotInfoOp r[] = { op_value(RSPROT_OP_ADD_OLD, 0), op_value(RSPROT_OP_SET_EXT, 0),
                                    op_value(RSPROT_OP_NAME_CHANGE, 0) };
        r[2].name_change.name = "Renamed";
        RS_EntityApply_NpcInfo(&f.in, 0, r, 3);
    }
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(strcmp(npc->name, "Renamed") == 0 && (npc->overrides & WORLD_NPC_OVERRIDE_NAME), "a name override");
    change[2].change_type.npc_type = 7;
    RS_EntityApply_NpcInfo(&f.in, 0, change, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(f.h.npc_retypes == 1 && f.h.last_retype_type == 7, "the maker retyped it");
    CHECK(npc->npc_id == 7 && npc->base_npc_id == 7, "the new type is the shell and the rung");
    CHECK(strcmp(npc->name, "Seven") == 0 && npc->overrides == 0, "a server type change drops the overrides");
    npc->idle_animations.readyanim = 9999;
    bas[2].bas_change.mask = 0;
    RS_EntityApply_NpcInfo(&f.in, 0, bas, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc->idle_animations.readyanim == 701, "an empty BAS_CHANGE is the type's own set");
    CHECK(!(npc->overrides & WORLD_NPC_OVERRIDE_STANCES), "and no stance override");
    fixture_free(&f);
}

/* A rung that hides the npc still makes a live entity: the wrapper, hidden. */
static void
test_npc_hidden_rung(void)
{
    struct Fixture f;
    struct RsprotInfoOp add[] = {
        op_value(RSPROT_OP_ADD_NEW, 1),
        op_value(RSPROT_OP_NPCTYPE, 40),
    };
    struct WorldEntity_NPC const* npc;

    fixture_init(&f);
    f.h.resolve_from[0] = 40;
    f.h.resolve_to[0] = -1;
    f.h.resolve_count = 1;
    RS_EntityApply_NpcInfo(&f.in, 0, add, 2);
    npc = World_EntityPoolGet(&f.world->entities.npc, npc_of(&f, 1));
    CHECK(npc && npc->multinpc_hidden && npc->npc_id == 40, "the hidden wrapper exists");
    fixture_free(&f);
}

/* The types a packet task awaits: each named type once, -1 included. */
static void
test_npc_types_listed_once(void)
{
    struct RsprotInfoOp ops[] = {
        op_value(RSPROT_OP_NPCTYPE, 4), op_value(RSPROT_OP_NPCTYPE, 4), op_value(RSPROT_OP_CHANGE_TYPE, 0),
        op_value(RSPROT_OP_SEQUENCE, 0),
    };
    int out[4];
    int n;

    ops[2].change_type.npc_type = -1;
    n = RS_EntityApply_NpcTypes(ops, 4, out, 4);
    CHECK(n == 2 && out[0] == 4 && out[1] == -1, "4 and -1, once each");
}

/* A player add spawns with the default kit's stances; a hit takes the host's
 * timing; forced chat is a chat line noted before the overhead. */
static void
test_player_add_hit_and_chat(void)
{
    struct Fixture f;
    struct RsprotInfoOp ops[4];
    struct WorldEntity_Player const* player;
    int idx = -1;

    fixture_init(&f);
    ops[0] = op_value(RSPROT_OP_ADD_NEW, 12);
    ops[1] = op_value(RSPROT_OP_SAY, 0);
    ops[1].say.text = "hello";
    ops[2] = op_value(RSPROT_OP_DAMAGE, 0);
    ops[2].damage.damage_type = 3;
    ops[2].damage.damage = 4;
    ops[2].damage.soak_type = -1;
    ops[3] = op_value(RSPROT_OP_SEQUENCE, 0);
    ops[3].sequence.sequence_id = 422;
    RS_EntityApply_PlayerInfo(&f.in, ops, 4);
    RS_EntitySync_FindPlayer(&f.esync, 12, &idx, NULL);
    CHECK(idx >= 0, "the player exists at once");
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(player && player->server_pid == 12, "with its pid");
    CHECK(player && player->idle_animations.readyanim == 808, "the default kit's ready stance");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_CHAT) == 1, "one chat line");
    CHECK(f.h.notes[0].kind == RS_ENTITY_NOTE_CHAT && strcmp(f.h.notes[0].sender, "Player") == 0 &&
              f.h.notes[0].sender_key == NULL,
        "a nameless speaker prints as Player and keys on nothing");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_PLAYER_SEQ) == 1, "the sequence noted");
    CHECK(player && player->animation.primary.anim_id == 422, "and set");
    fixture_free(&f);
}

/* The chat block's text as the fixture's "revision" packs it: the bytes are
 * the text (the real readers' Huffman and wordpack are tested elsewhere). */
static char*
test_chat_unpack(
    uint8_t const* data,
    int len)
{
    char* out = ToriMem_Alloc(ToriMem_Net(), (size_t)len + 1);

    assert(out);
    memcpy(out, data, (size_t)len);
    out[len] = '\0';
    return out;
}

/* One chat block from pid 12 (added by the first call). */
static void
send_public_chat(
    struct Fixture* f,
    char const* text,
    int crown,
    int autotyper)
{
    struct RsprotInfoOp ops[2];

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 12);
    ops[1] = op_value(RSPROT_OP_CHAT, 0);
    ops[1].chat.data = (uint8_t const*)text;
    ops[1].chat.length = (uint8_t)strlen(text);
    ops[1].chat.type = (uint8_t)crown;
    ops[1].chat.autotyper = (uint8_t)autotyper;
    ops[1].chat.colour_effect = 0x0102;
    RS_EntityApply_PlayerInfo(&f->in, ops, 2);
}

/* B1: a public line as deob class95.method3131 files it. The host decides
 * whether it is heard (the real client: ignore list and Tutorial Island; the
 * headless: always); a hidden line sets no overhead either. The text is
 * sentence-cased (Statics.method10915) and its tags escaped
 * (class671.method14451); the type follows the crown and the autotyper (1 /
 * 2, 91 / 90); the overhead is the same text, trimmed. */
static void
test_public_chat(void)
{
    struct Fixture f;
    struct RsprotInfoOp add = op_value(RSPROT_OP_ADD_NEW, 12);
    struct WorldEntity_Player* player;
    struct RS_EntityNote const* line;
    int idx = -1;

    fixture_init(&f);
    f.rev.chat_unpack = test_chat_unpack;
    RS_EntityApply_PlayerInfo(&f.in, &add, 1);
    RS_EntitySync_FindPlayer(&f.esync, 12, &idx, NULL);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    assert(player);
    snprintf(player->name, sizeof(player->name), "%s", "Spam\xa0" "Bot");

    /* An ignored speaker (crown 0 can be ignored): asked, and silent. */
    f.h.chat_hidden = true;
    send_public_chat(&f, "buy gold", 0, 0);
    CHECK(f.h.chat_asks == 1 && strcmp(f.h.chat_asked_sender, "Spam\xa0" "Bot") == 0 && f.h.chat_asked_ignorable,
        "the host is asked about the speaker, with an ignorable crown");
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_CHAT) == 0, "a hidden line is not filed");
    CHECK(player->chat.message[0] == '\0', "nor shown overhead");

    /* Heard: sentence case, escaped tags, type 2, the trimmed overhead. */
    f.h.chat_hidden = false;
    send_public_chat(&f, "  hELLO wORLD. <col=ff0000>red!  ", 0, 0);
    CHECK(notes_of(&f.h, RS_ENTITY_NOTE_CHAT) == 1, "a heard line is filed");
    line = &f.h.notes[f.h.note_count - 1];
    CHECK(line->kind == RS_ENTITY_NOTE_CHAT && line->chat_type == RS_CHAT_TYPE_PUBLIC, "typed 2 for a plain crown");
    CHECK(line->kind == RS_ENTITY_NOTE_CHAT &&
              strcmp(line->text, "  Hello world. <lt>col=ff0000<gt>red!  ") == 0,
        "sentence-cased with its tags escaped");
    CHECK(strcmp(player->chat.message, "Hello world. <lt>col=ff0000<gt>red!") == 0,
        "the overhead is the same text, trimmed");

    /* A Jagex moderator's crown (2): staff, and not ignorable. */
    send_public_chat(&f, "hi", 2, 0);
    line = &f.h.notes[f.h.note_count - 1];
    CHECK(!f.h.chat_asked_ignorable, "a moderator's crown is not ignorable");
    CHECK(line->chat_type == RS_CHAT_TYPE_PUBLIC_MOD && strcmp(line->sender, "<img=1>Spam\xa0" "Bot") == 0,
        "typed 1 and named with the crown's image");
    send_public_chat(&f, "hi", 2, 1);
    CHECK(f.h.notes[f.h.note_count - 1].chat_type == 91, "the autotyper's staff line is 91");
    send_public_chat(&f, "hi", 0, 1);
    CHECK(f.h.notes[f.h.note_count - 1].chat_type == 90, "and anyone else's 90");
    fixture_free(&f);
}

/* ---- wants raised on change (World_SeqSource.want) ---- */

static int g_wants[16];
static int g_want_count;

static void
test_want(
    void* userdata,
    int seq_id)
{
    (void)userdata;
    if( g_want_count < 16 )
        g_wants[g_want_count] = seq_id;
    g_want_count++;
}

/* A sequence is asked for where a track changes to it -- a SEQUENCE sent,
 * the movement step choosing a stance -- and a steady world asks for none;
 * binding a source asks once for every track already set. */
static void
test_wants_raised_on_change(void)
{
    struct Fixture f;
    struct RsprotInfoOp add[] = {
        op_value(RSPROT_OP_ADD_NEW, 3),
        op_value(RSPROT_OP_NPCTYPE, 7),
        op_delta(0, 0, true),
    };
    struct RsprotInfoOp seq[3];
    struct World_SeqSource source;
    int wants_after_settle;

    fixture_init(&f);
    memset(&source, 0, sizeof(source));
    source.want = test_want;
    World_SetSeqSource(f.world, &source);
    g_want_count = 0;
    /* A loaded world has a scene: cycling an actor asserts it. */
    World_ResetScene(f.world, 50, 50, 104);
    World_SetLoadComplete(f.world, true);
    RS_EntityApply_NpcInfo(&f.in, 0, add, 3);
    World_Cycle(f.world, 1);
    CHECK(g_want_count == 1 && g_wants[0] == 701, "the spawn's first step asks for its ready stance");
    for( int i = 0; i < 50; i++ )
        World_Cycle(f.world, 1);
    wants_after_settle = g_want_count;
    CHECK(wants_after_settle == 1, "a steady world asks for nothing more");
    seq[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    seq[1] = op_value(RSPROT_OP_SET_EXT, 0);
    seq[2] = op_value(RSPROT_OP_SEQUENCE, 0);
    seq[2].sequence.sequence_id = 1234;
    RS_EntityApply_NpcInfo(&f.in, 0, seq, 3);
    CHECK(g_want_count == 2 && g_wants[1] == 1234, "a sequence sent is asked for once");
    g_want_count = 0;
    World_SetSeqSource(f.world, &source);
    CHECK(g_want_count >= 1, "a bound source is asked for the tracks already set");
    fixture_free(&f);
}

/* NAME_EXTRAS decorates the player; a RESET in the same record runs before
 * the record's other blocks (deob class109.method3804 tests 0x2 first), so
 * the decorations and a sequence sent beside it survive, while what came
 * before is cleared. */
static void
test_player_name_extras_and_reset(void)
{
    struct Fixture f;
    struct RsprotInfoOp first[3];
    struct RsprotInfoOp second[5];
    struct RsprotInfoOp third[2];
    struct WorldEntity_Player const* player;
    int idx = -1;

    fixture_init(&f);
    first[0] = op_value(RSPROT_OP_ADD_NEW, 4);
    first[1] = op_value(RSPROT_OP_NAME_EXTRAS, 0);
    first[1].name_extras.before_name = "<img=1>";
    first[1].name_extras.after_name = "";
    first[1].name_extras.after_combat_level = " (x)";
    first[2] = op_value(RSPROT_OP_SEQUENCE, 0);
    first[2].sequence.sequence_id = 500;
    RS_EntityApply_PlayerInfo(&f.in, first, 3);
    RS_EntitySync_FindPlayer(&f.esync, 4, &idx, NULL);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(strcmp(player->name_extras[0], "<img=1>") == 0 && strcmp(player->name_extras[2], " (x)") == 0,
        "the decorations are kept");
    /* Reset with a new decoration and a new sequence in the same record. */
    second[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    second[1] = op_value(RSPROT_OP_NAME_EXTRAS, 0);
    second[1].name_extras.before_name = "a";
    second[1].name_extras.after_name = "b";
    second[1].name_extras.after_combat_level = "c";
    second[2] = op_value(RSPROT_OP_SEQUENCE, 0);
    second[2].sequence.sequence_id = 600;
    second[3] = op_value(RSPROT_OP_RESET, 0);
    second[4] = op_value(RSPROT_OP_INFO, 0);
    RS_EntityApply_PlayerInfo(&f.in, second, 5);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(strcmp(player->name_extras[0], "a") == 0 && strcmp(player->name_extras[1], "b") == 0,
        "a reset runs before the record's own decorations");
    CHECK(player->animation.primary.anim_id == 600, "and before its sequence");
    third[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    third[1] = op_value(RSPROT_OP_RESET, 0);
    RS_EntityApply_PlayerInfo(&f.in, third, 2);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(player->name_extras[0][0] == '\0' && player->name_extras[2][0] == '\0', "a reset alone clears them");
    CHECK(player->animation.primary.anim_id == (uint16_t)-1, "and stops the sequence");
    fixture_free(&f);
}

/* BODY_CUSTOM / HEAD_CUSTOM (FAULTS E12): a block without 0x2 keeps the
 * type's models and still carries its recolours and retextures; 0x1 alone
 * clears the override; the chathead's block is its own override; a count
 * whose list the op arena lost keeps the override the npc had. */
static void
test_npc_body_and_head_custom(void)
{
    static int32_t const colours[3] = { 11, 22, 33 };
    static int32_t const textures[1] = { 44 };
    static int32_t const models[2] = { 5000, 5001 };
    struct Fixture f;
    struct RsprotInfoOp add[] = {
        op_value(RSPROT_OP_ADD_NEW, 6),
        op_value(RSPROT_OP_NPCTYPE, 3),
        op_delta(0, 0, true),
    };
    struct RsprotInfoOp ext[3] = {
        op_value(RSPROT_OP_ADD_OLD, 0),
        op_value(RSPROT_OP_SET_EXT, 0),
        op_value(RSPROT_OP_BODY_CUSTOM, 0),
    };
    struct WorldEntity_NPC const* npc;
    int idx;
    int serial;

    fixture_init(&f);
    RS_EntityApply_NpcInfo(&f.in, 0, add, 3);
    idx = npc_of(&f, 6);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    serial = npc->body_serial;

    /* 0x4 | 0x8: recolours and retextures, no models. */
    ext[2].body.flag = 0x4 | 0x8;
    ext[2].body.recolour_count = 3;
    ext[2].body.recolours = colours;
    ext[2].body.retexture_count = 1;
    ext[2].body.retextures = textures;
    ext[2].body.mirror = -1;
    ext[2].body.body_type = -1;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc->body_custom.active, "a recolour-only block sets an override");
    CHECK(!npc->body_custom.models_sent, "over the type's own models");
    CHECK(npc->body_custom.recolour_count == 3 && npc->body_custom.recolours &&
              npc->body_custom.recolours[2] == 33,
        "with every recolour");
    CHECK(npc->body_custom.retexture_count == 1 && npc->body_custom.retextures[0] == 44, "and the retexture");
    CHECK(npc->body_serial == serial + 1, "the body is remounted");

    /* 0x2 | 0x10 in the counted form (no mirror byte): models and the mirror. */
    memset(&ext[2].body, 0, sizeof(ext[2].body));
    ext[2].body.flag = 0x2 | 0x10;
    ext[2].body.model_count = 2;
    ext[2].body.models = models;
    ext[2].body.mirror = -1;
    ext[2].body.body_type = -1;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc->body_custom.models_sent && npc->body_custom.model_count == 2 && npc->body_custom.models[1] == 5001,
        "a new block replaces the override whole: its models");
    CHECK(npc->body_custom.recolour_count == 0, "and not the earlier recolours");
    CHECK(npc->body_custom.mirror, "the counted form's 0x10 is the mirror");

    /* The op arena overflowed: the count is there, the list is not. */
    memset(&ext[2].body, 0, sizeof(ext[2].body));
    ext[2].body.flag = 0x4;
    ext[2].body.recolour_count = 3;
    ext[2].body.mirror = -1;
    ext[2].body.body_type = -1;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc->body_custom.model_count == 2 && npc->body_custom.recolour_count == 0,
        "a lost list keeps the override the npc had");

    /* HEAD_CUSTOM, typed form: models and a mirror byte of 1. */
    memset(&ext[2].body, 0, sizeof(ext[2].body));
    ext[2].kind = RSPROT_OP_HEAD_CUSTOM;
    ext[2].body.flag = 0x2 | 0x10;
    ext[2].body.model_count = 1;
    ext[2].body.models = models;
    ext[2].body.mirror = 1;
    ext[2].body.body_type = -1;
    serial = npc->body_serial;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(npc->head_custom.active && npc->head_custom.model_count == 1 && npc->head_custom.mirror,
        "the chathead override is kept");
    CHECK(npc->body_serial == serial && npc->body_custom.model_count == 2, "and is not the body's");

    /* 0x1 alone: the reset, of each separately. */
    memset(&ext[2].body, 0, sizeof(ext[2].body));
    ext[2].kind = RSPROT_OP_BODY_CUSTOM;
    ext[2].body.flag = 0x1;
    ext[2].body.reset = true;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(!npc->body_custom.active && npc->body_custom.model_count == 0, "0x1 clears the body's override");
    CHECK(npc->head_custom.active, "and keeps the chathead's");
    ext[2].kind = RSPROT_OP_HEAD_CUSTOM;
    RS_EntityApply_NpcInfo(&f.in, 0, ext, 3);
    npc = World_EntityPoolGet(&f.world->entities.npc, idx);
    CHECK(!npc->head_custom.active, "the chathead's 0x1 clears it");
    fixture_free(&f);
}

/* The rest of RESET (deob class95.method3110): the tint, the transparency,
 * the facing block's target and the newest route step's traversal go back to
 * their defaults, before the record's own blocks. */
static void
test_player_reset_clears_tint_transparency_facing(void)
{
    struct Fixture f;
    struct RsprotInfoOp first[4];
    struct RsprotInfoOp second[2];
    struct WorldEntity_Player* player;
    int idx = -1;

    fixture_init(&f);
    first[0] = op_value(RSPROT_OP_ADD_NEW, 9);
    first[1] = op_value(RSPROT_OP_TINT, 0);
    first[1].tint.start_cycle_delta = 0;
    first[1].tint.end_cycle_delta = 500;
    first[1].tint.hue = 10;
    first[1].tint.saturation = 20;
    first[1].tint.lightness = 30;
    first[1].tint.weight = 64;
    first[2] = op_value(RSPROT_OP_TRANSPARENCY, 0);
    first[2].transparency.start_cycle_delta = 0;
    first[2].transparency.end_cycle_delta = 100;
    first[2].transparency.from = 0;
    first[2].transparency.to = 200;
    first[3] = op_value(RSPROT_OP_FACE_ENTITY, 0);
    first[3].face_entity.entity_id = 12;
    RS_EntityApply_PlayerInfo(&f.in, first, 4);
    RS_EntitySync_FindPlayer(&f.esync, 9, &idx, NULL);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(player && WorldEntityTint_Active(&player->tint, f.world->cycle), "the tint is on");
    CHECK(player && player->transparency.set, "the transparency is set");
    CHECK(player && player->facing.entity_id == 12, "the face target is set");
    player->pathing.route_step[0] = WORLD_PATHSTEP_RUN;

    second[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    second[1] = op_value(RSPROT_OP_RESET, 0);
    RS_EntityApply_PlayerInfo(&f.in, second, 2);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(!WorldEntityTint_Active(&player->tint, f.world->cycle), "a reset clears the tint");
    CHECK(!player->transparency.set, "and the transparency");
    CHECK(player->facing.entity_id == WORLD_FACING_ENTITY_NONE && player->facing.square_x == 0 &&
              player->facing.fallback_angle == -1,
        "and the face target");
    CHECK(player->pathing.route_step[0] == WORLD_PATHSTEP_WALK, "and the newest step walks");
    fixture_free(&f);
}

/* PLAYER_INFO FREEZE (0x80000 at rev 239): a window on the player; 65535 is
 * the cancel, and its reset stops the sequence. */
static void
test_player_freeze(void)
{
    struct Fixture f;
    struct RsprotInfoOp first[3];
    struct RsprotInfoOp second[2];
    struct WorldEntity_Player* player;
    int idx = -1;

    fixture_init(&f);
    first[0] = op_value(RSPROT_OP_ADD_NEW, 11);
    first[1] = op_value(RSPROT_OP_SEQUENCE, 0);
    first[1].sequence.sequence_id = 700;
    first[2] = op_value(RSPROT_OP_FREEZE, 0);
    first[2].freeze.delay = 5;
    first[2].freeze.duration = 20;
    first[2].freeze.cancel = 0;
    RS_EntityApply_PlayerInfo(&f.in, first, 3);
    RS_EntitySync_FindPlayer(&f.esync, 11, &idx, NULL);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(player && player->freeze.count == 1 && player->freeze.windows[0].start_cycle == (int)f.world->cycle + 5 &&
              player->freeze.windows[0].end_cycle == (int)f.world->cycle + 25 && !player->freeze.windows[0].reset_seq,
        "a freeze window on the player");
    second[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    second[1] = op_value(RSPROT_OP_FREEZE, 0);
    second[1].freeze.delay = 65535;
    second[1].freeze.duration = 65535;
    second[1].freeze.cancel = 1;
    RS_EntityApply_PlayerInfo(&f.in, second, 2);
    player = World_EntityPoolGet(&f.world->entities.player, idx);
    CHECK(player->freeze.count == 0, "65535 cancels it");
    CHECK(player->animation.primary.anim_id == (uint16_t)-1, "and its reset stops the sequence");
    fixture_free(&f);
}

int
main(void)
{
    ToriMem_Init();
    test_npc_add_record();
    test_npc_readd_is_a_move();
    test_npc_change_type_and_bas_reset();
    test_npc_hidden_rung();
    test_npc_types_listed_once();
    test_player_add_hit_and_chat();
    test_public_chat();
    test_wants_raised_on_change();
    test_player_name_extras_and_reset();
    test_npc_body_and_head_custom();
    test_player_reset_clears_tint_transparency_facing();
    test_player_freeze();
    printf("rs_entity_apply_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
