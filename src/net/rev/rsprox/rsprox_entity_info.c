/*
 * PLAYER_INFO / NPC_INFO for the data-driven revisions: a 3rd/rsprot/streams
 * reader (stream_<prot>_rev<N>_read, which gameproto_rev_rsprox.c binds per
 * revision) reads the stream into its op list, and this file maps that list
 * onto the op lists (pkt_player_info.h / pkt_npc_info.h) the entity-info
 * executor consumes.
 *
 * The ops come out as src/net/rev/osrs241/osrs241_entity_info.c emits them, in
 * the same order, so the executor cannot tell the two apart -- that equivalence
 * is what the recording decode check holds this file to at 239 and 241. The
 * blocks that revision never carried (227-237's face-pathing-entity, face
 * coord and face angle, the combined hits block) are mapped onto the same ops
 * here, and each says how.
 *
 * WORLDENTITY_INFO is converted at parse rather than exec: its readers are
 * stateless and its canonical arm is decoded data, as the hand revisions'.
 *
 * STATE. The player stream's 2048-slot table is library data
 * (RsprotPlayerInfo); this file adds the one thing the op list needs that the
 * protocol does not track, the persistent traversal per slot (class95.field1344
 * in the reference client). Both live in one struct so a host that decodes
 * several sessions can park and restore them together.
 */
#include "rsprox_rev.h"

#include "mem/torirs_mem.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_info.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

/* The library list, per packet. The client's own caps are 8192 ops; a list
 * entry here is at most one client op, plus the BLOCK / EXT_BEGIN markers. */
#define LIBRARY_OPS 16384

struct PlayerState
{
    RsprotPlayerInfo pi;
    int8_t move_speed[RSPROT_INFO_PLAYER_SLOTS];
};

static struct PlayerState g_state;
static RsprotInfoOp g_lib[LIBRARY_OPS];
/* The library's verdict on the last stream read: 1 when every byte read as the
 * revision says. The op lists are delivered either way. */
static int g_last_ok;

static char*
dup_text(char const* s)
{
    size_t n;
    char* out;

    assert(s);
    n = strlen(s);
    out = (char*)ToriMem_Alloc(ToriMem_Net(), n + 1);
    assert(out);
    memcpy(out, s, n + 1);
    return out;
}

/* A borrowed blob, copied out in natural order (the appearance block and chat
 * arrive obfuscated). */
static uint8_t*
take_data(uint8_t const* data, int len, int order)
{
    uint8_t* out = (uint8_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)(len > 0 ? len : 1));

    assert(out);
    if( len > 0 )
        rsprot_info_copy_data(out, data, len, order);
    return out;
}

/* The pre-238 face-pathing-entity index: npc index, or 0x10000 + player index
 * (RSProt FacePathingEntity), 0xFFFFFF for none. The op's convention is the
 * classic one -- npc index, or 32768 + player index. */
static int32_t
pathing_entity(int32_t v)
{
    if( v == 0xFFFFFF || v < 0 )
        return -1;
    if( v >= 0x10000 )
        return 32768 + (v - 0x10000);
    return v;
}

/* decodeFacing (238+), the same for both streams. */
struct Facing
{
    int kind; /* 0 entity, 1 loc, 2 angle, 3 reset */
    bool instant;
    int movement_mode;
    int32_t entity_id;
    int angle;
    int x;
    int z;
};

static struct Facing
facing_of(RsprotInfoBlockOp const* b)
{
    struct Facing f;
    int type = b->facing.entity_type;
    int index = b->facing.index;

    memset(&f, 0, sizeof(f));
    f.kind = (b->facing.flag >> 3) & 0x7;
    f.instant = ((b->facing.flag >> 6) & 1) != 0;
    f.movement_mode = b->facing.flag & 0x7;
    f.entity_id = type == 1 ? index : type == 2 && index >= 0 ? 32768 + index : -1;
    f.angle = f.kind == 0 ? b->facing.fallback_angle : b->facing.angle;
    f.x = b->facing.x * 2 + (b->facing.size & 0xf);
    f.z = b->facing.z * 2 + ((b->facing.size >> 4) & 0xf);
    /* An entity face at a world entity has no registry here: the gamepack's
     * missing-target path applies the fallback angle. */
    if( f.kind == 0 && type == 3 )
        f.kind = 2;
    return f;
}

/* --- players ------------------------------------------------------------ */

struct PlayerOut
{
    struct PktPlayerInfoOp* ops;
    int cap;
    int count;
    int movement_op[RSPROT_INFO_PLAYER_SLOTS];
    /* HEADBARS / HITS: one op per block, from its first bar -- the 239 / 241
     * readers' rule. */
    int bar_pending;
};

static struct PktPlayerInfoOp*
pop(struct PlayerOut* s, enum PktPlayerInfoOpKind kind)
{
    struct PktPlayerInfoOp* op;

    if( s->count >= s->cap )
        return NULL;
    op = &s->ops[s->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

static void
ptarget(struct PlayerOut* s, int idx)
{
    struct PktPlayerInfoOp* op;

    if( idx == g_state.pi.local_index )
    {
        pop(s, PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER);
        return;
    }
    op = pop(s, PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID);
    if( op )
        op->_bitvalue = (uint64_t)idx;
}

static void
p_move(struct PlayerOut* s, int idx, int32_t coord, int how)
{
    int op_index;
    struct PktPlayerInfoOp* op;

    ptarget(s, idx);
    op_index = s->count;
    op = pop(s, PKT_PLAYER_INFO_OP_ABS_XZLEVEL);
    if( !op )
        return;
    op->_local_xz_level.x = (int16_t)((coord >> 14) & 0x3fff);
    op->_local_xz_level.z = (int16_t)(coord & 0x3fff);
    op->_local_xz_level.level = (uint8_t)((coord >> 28) & 0x3);
    /* Entering view and teleporting re-place; walking and running step. */
    op->_local_xz_level.jump = how == 0 || how == 3;
    op->_local_xz_level.has_move_speed = true;
    op->_local_xz_level.move_speed = g_state.move_speed[idx];
    s->movement_op[idx] = op_index;
}

static void
p_move_speed(struct PlayerOut* s, int idx, int speed, int persistent)
{
    int op_index;

    if( persistent )
        g_state.move_speed[idx] = (int8_t)speed;
    op_index = s->movement_op[idx];
    if( op_index < 0 || op_index >= s->count )
        return;
    /* The traversal block patches the movement already staged this cycle, as
     * class109.field1570 does; a temporary one is authoritative even over a
     * teleport-width move. */
    s->ops[op_index]._local_xz_level.has_move_speed = true;
    s->ops[op_index]._local_xz_level.move_speed = (int8_t)speed;
    if( !persistent || speed == PKT_PLAYER_TRAVERSAL_SNAP )
        s->ops[op_index]._local_xz_level.jump = speed == PKT_PLAYER_TRAVERSAL_SNAP;
}

static void
p_facing(struct PlayerOut* s, RsprotInfoBlockOp const* b)
{
    struct Facing f = facing_of(b);
    struct PktPlayerInfoOp* op;

    if( f.kind == 1 )
    {
        op = pop(s, PKT_PLAYER_INFO_OP_FACE_COORD);
        if( op )
        {
            op->_face_coord.x = (int16_t)f.x;
            op->_face_coord.z = (int16_t)f.z;
            op->_face_coord.instant = f.instant;
            op->_face_coord.movement_mode = (uint8_t)f.movement_mode;
            op->_face_coord.modern = true;
        }
    }
    else if( f.kind == 2 )
    {
        op = pop(s, PKT_PLAYER_INFO_OP_FACE_ANGLE);
        if( op )
        {
            op->_face_angle.angle = (uint16_t)f.angle;
            op->_face_angle.instant = f.instant;
            op->_face_angle.movement_mode = (uint8_t)f.movement_mode;
            op->_face_angle.modern = true;
        }
    }
    else
    {
        op = pop(s, PKT_PLAYER_INFO_OP_FACE_ENTITY);
        if( op )
        {
            op->_face_entity.entity_id = f.kind == 3 ? -1 : f.entity_id;
            op->_face_entity.fallback_angle = (uint16_t)(f.kind == 3 ? 0 : f.angle);
            op->_face_entity.has_fallback_angle = f.kind != 3;
            op->_face_entity.instant = f.kind != 3 && f.instant;
            op->_face_entity.movement_mode = (uint8_t)f.movement_mode;
            op->_face_entity.modern = true;
        }
    }
}

static void
p_block(struct PlayerOut* s, int idx, RsprotInfoBlockOp const* b)
{
    struct PktPlayerInfoOp* op;

    s->bar_pending = b->kind == RSPROT_IK_HEADBARS || b->kind == RSPROT_IK_HITS;
    switch( b->kind )
    {
    case RSPROT_IK_SAY:
        op = pop(s, PKT_PLAYER_INFO_OP_SAY);
        if( op )
            op->_say.text = dup_text(b->say.text);
        break;
    case RSPROT_IK_MOVE_SPEED:
        p_move_speed(s, idx, b->value.value, 1);
        break;
    case RSPROT_IK_TEMP_MOVE_SPEED:
        p_move_speed(s, idx, b->value.value, 0);
        break;
    case RSPROT_IK_FACING:
        p_facing(s, b);
        break;
    /* 227-237: face-lock a pathing entity, the classic op. */
    case RSPROT_IK_FACE_PATHING_ENTITY:
        op = pop(s, PKT_PLAYER_INFO_OP_FACE_ENTITY);
        if( op )
            op->_face_entity.entity_id = pathing_entity(b->face_pathing_entity.index);
        break;
    /* 227-238 (decodeFaceAngle): one g2 angle, the classic op. */
    case RSPROT_IK_FACE_ANGLE:
        op = pop(s, PKT_PLAYER_INFO_OP_FACE_ANGLE);
        if( op )
        {
            op->_face_angle.angle = (uint16_t)b->face_angle.angle;
            op->_face_angle.instant = b->face_angle.instant == 1;
        }
        break;
    case RSPROT_IK_APPEARANCE:
        op = pop(s, PKT_PLAYER_INFO_OP_APPEARANCE);
        if( op )
        {
            op->_appearance.appearance =
                take_data(b->appearance.data, b->appearance.data_len, b->appearance.data_order);
            op->_appearance.len = b->appearance.data_len;
        }
        break;
    case RSPROT_IK_CHAT:
        op = pop(s, PKT_PLAYER_INFO_OP_CHAT);
        if( op )
        {
            op->_chat.colour_effect = (uint16_t)b->chat.colour_effects;
            op->_chat.type = (uint8_t)b->chat.mod_icon;
            op->_chat.length = (uint8_t)b->chat.data_len;
            op->_chat.data = b->chat.data_len > 0
                                 ? take_data(b->chat.data, b->chat.data_len, b->chat.data_order)
                                 : NULL;
        }
        break;
    case RSPROT_IK_TINTING:
        op = pop(s, PKT_PLAYER_INFO_OP_TINT);
        if( op )
        {
            op->_tint.start_cycle_delta = (uint16_t)b->tinting.start;
            op->_tint.end_cycle_delta = (uint16_t)b->tinting.end;
            op->_tint.hue = (int8_t)b->tinting.hue;
            op->_tint.saturation = (int8_t)b->tinting.saturation;
            op->_tint.lightness = (int8_t)b->tinting.lightness;
            op->_tint.weight = (int8_t)b->tinting.weight;
        }
        break;
    case RSPROT_IK_SEQUENCE:
        op = pop(s, PKT_PLAYER_INFO_OP_SEQUENCE);
        if( op )
        {
            op->_sequence.sequence_id = b->sequence.id == 65535 ? -1 : b->sequence.id;
            op->_sequence.delay = (uint8_t)b->sequence.delay;
        }
        break;
    case RSPROT_IK_EXACT_MOVE:
        op = pop(s, PKT_PLAYER_INFO_OP_EXACT_MOVE);
        if( op )
        {
            op->_exactmove.start_x = (int16_t)b->exact_move.dx1;
            op->_exactmove.start_z = (int16_t)b->exact_move.dz1;
            op->_exactmove.end_x = (int16_t)b->exact_move.dx2;
            op->_exactmove.end_z = (int16_t)b->exact_move.dz2;
            op->_exactmove.end_cycle_delta = (uint16_t)b->exact_move.delay1;
            op->_exactmove.start_cycle_delta = (uint16_t)b->exact_move.delay2;
            op->_exactmove.facing = (uint16_t)b->exact_move.direction;
            op->_exactmove.facing_is_yaw = true;
            op->_exactmove.relative = true;
        }
        /* This cycle's staged move is never applied (class109.method3804
         * clears field1582 after method3132). */
        if( s->movement_op[idx] >= 0 && s->movement_op[idx] < s->count )
            s->ops[s->movement_op[idx]]._local_xz_level.route_only = true;
        break;
    case RSPROT_IK_TRANSPARENCY:
        op = pop(s, PKT_PLAYER_INFO_OP_TRANSPARENCY);
        if( op )
        {
            op->_transparency.start_cycle_delta = (int16_t)b->transparency.start;
            op->_transparency.end_cycle_delta = (int16_t)b->transparency.end;
            op->_transparency.from = (uint8_t)b->transparency.alpha_start;
            op->_transparency.to = (uint8_t)b->transparency.alpha_end;
            op->_transparency.use_start = b->transparency.use_start != 0;
        }
        break;
    /* The list blocks speak through their entries; name extras, reset and
     * freeze have no home in the op list, as at 239 and 241. */
    default:
        break;
    }
}

static void
p_hit(struct PlayerOut* s, RsprotInfoHit const* hit)
{
    struct PktPlayerInfoOp* op;

    /* 227-237's 0x7FFE is a delay with no splat. */
    if( hit->type == 0x7FFE )
        return;
    op = pop(s, PKT_PLAYER_INFO_OP_DAMAGE);
    if( op )
    {
        op->_damage.damage_type = hit->type;
        op->_damage.damage = hit->value;
        op->_damage.delay = (uint16_t)hit->delay;
        op->_damage.slots = (uint16_t)(hit->limit < 0 ? 0 : hit->limit);
    }
    /* A soak hit is two splats on one delay: the main and the soaked. */
    if( hit->soak_type >= 0 && (op = pop(s, PKT_PLAYER_INFO_OP_DAMAGE)) != NULL )
    {
        op->_damage.damage_type = hit->soak_type;
        op->_damage.damage = hit->soak_value;
        op->_damage.delay = (uint16_t)hit->delay;
    }
}

static void
p_bar(struct PlayerOut* s, RsprotInfoHeadbar const* bar)
{
    struct PktPlayerInfoOp* op;

    if( !s->bar_pending )
        return;
    s->bar_pending = 0;
    op = pop(s, PKT_PLAYER_INFO_OP_HEADBAR);
    if( !op )
        return;
    op->_headbar.type = bar->type;
    if( bar->end_time == 0x7FFF )
    {
        op->_headbar.remove = true;
        return;
    }
    op->_headbar.duration = bar->end_time;
    op->_headbar.start_delay = bar->start_time;
    op->_headbar.start_fill = (uint8_t)bar->start_fill;
    op->_headbar.end_fill = (uint8_t)bar->end_fill;
}

int
rsprox_entity_info_last_ok(void)
{
    return g_last_ok;
}

int
rsprox_player_info_convert(
    RsprotPlayerInfoReadFn read, uint8_t const* data, int len, struct PktPlayerInfoOp* ops,
    int cap)
{
    static struct PlayerOut s;
    int n;

    assert(read);
    assert(ops);
    g_last_ok = 0;
    /* The ops up to a malformed block are delivered, as the hand readers
     * deliver them; the table has advanced either way. An unseeded table
     * reads nothing. */
    n = read(&g_state.pi, data, len, g_lib, LIBRARY_OPS, &g_last_ok);

    memset(&s, 0, sizeof(s));
    for( int i = 0; i < RSPROT_INFO_PLAYER_SLOTS; i++ )
        s.movement_op[i] = -1;
    s.ops = ops;
    s.cap = cap;
    for( int i = 0; i < n; i++ )
    {
        RsprotInfoOp const* l = &g_lib[i];
        struct PktPlayerInfoOp* op;

        switch( l->kind )
        {
        case RSPROT_IO_PLAYER_MOVE:
            p_move(&s, l->who, l->move.coord, l->move.how);
            break;
        case RSPROT_IO_PLAYER_STILL:
        case RSPROT_IO_EXT_BEGIN:
            ptarget(&s, l->who);
            break;
        case RSPROT_IO_PLAYER_REMOVE:
            g_state.move_speed[l->who] = PKT_PLAYER_TRAVERSAL_WALK;
            op = pop(&s, PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID);
            if( op )
                op->_bitvalue = (uint64_t)l->who;
            break;
        case RSPROT_IO_BLOCK:
            p_block(&s, l->who, &l->block);
            break;
        case RSPROT_IO_SPOT:
            op = pop(&s, PKT_PLAYER_INFO_OP_SPOTANIM);
            if( op )
            {
                op->_spotanim.slot = (uint8_t)l->spot.slot;
                op->_spotanim.spotanim_id = l->spot.id == 65535 ? -1 : l->spot.id;
                op->_spotanim.height_delay = l->spot.height_delay;
                op->_spotanim.loop = l->spot.loop == 1;
            }
            break;
        case RSPROT_IO_HIT:
            p_hit(&s, &l->hit);
            break;
        case RSPROT_IO_BAR:
            p_bar(&s, &l->bar);
            break;
        default:
            break;
        }
    }
    return s.count;
}

void
rsprox_playerinfo_set_local(int local_index)
{
    rsprot_player_info_reset(&g_state.pi, local_index);
    memset(g_state.move_speed, PKT_PLAYER_TRAVERSAL_WALK, sizeof(g_state.move_speed));
}

void
rsprox_playerinfo_init(uint8_t const* data, int len)
{
    rsprot_player_info_init(&g_state.pi, data, len);
    memset(g_state.move_speed, PKT_PLAYER_TRAVERSAL_WALK, sizeof(g_state.move_speed));
}

size_t
rsprox_playerinfo_state_size(void)
{
    return sizeof(g_state);
}

void
rsprox_playerinfo_state_save(void* out)
{
    assert(out);
    memcpy(out, &g_state, sizeof(g_state));
}

void
rsprox_playerinfo_state_load(void const* in)
{
    assert(in);
    memcpy(&g_state, in, sizeof(g_state));
}

/* --- npcs ---------------------------------------------------------------- */

struct NpcOut
{
    struct PktNpcInfoOp* ops;
    int cap;
    int count;
    int bar_pending;
    /* The BODY_CUSTOM op the model entries that follow append to. */
    struct PktNpcInfoOp* body;
};

static struct PktNpcInfoOp*
nop(struct NpcOut* s, enum PktNpcInfoOpKind kind)
{
    struct PktNpcInfoOp* op;

    if( s->count >= s->cap )
        return NULL;
    op = &s->ops[s->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

static void
nop_value(struct NpcOut* s, enum PktNpcInfoOpKind kind, uint64_t v)
{
    struct PktNpcInfoOp* op = nop(s, kind);

    if( op )
        op->_bitvalue = v;
}

static void
n_add(struct NpcOut* s, RsprotInfoOp const* l)
{
    /* NpcInfoClient.NPC_TURN_ANGLES. */
    static uint16_t const k_spawn_facing[8] = { 768, 1024, 1280, 512, 1536, 256, 0, 1792 };
    struct PktNpcInfoOp* op;

    nop_value(s, PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID, (uint64_t)l->add.slot);
    nop_value(s, PKT_NPC_INFO_OPBITS_NPCTYPE, (uint64_t)l->add.type);
    op = nop(s, PKT_NPC_INFO_OP_DELTA_XZ);
    if( op )
    {
        op->_delta_xz.dx = (int16_t)l->add.dx;
        op->_delta_xz.dz = (int16_t)l->add.dz;
        op->_delta_xz.jump = l->add.jump != 0;
    }
    op = nop(s, PKT_NPC_INFO_OP_FACE_ANGLE);
    if( op )
    {
        op->_face_angle.angle = k_spawn_facing[l->add.facing & 7];
        op->_face_angle.instant = true;
        op->_face_angle.spawn = true;
    }
    if( l->add.has_spawn_cycle )
        nop_value(s, PKT_NPC_INFO_OP_SPAWN_CYCLE, l->add.spawn_cycle);
}

static void
n_facing(struct NpcOut* s, RsprotInfoBlockOp const* b)
{
    struct Facing f = facing_of(b);
    struct PktNpcInfoOp* op;

    if( f.kind == 1 )
    {
        op = nop(s, PKT_NPC_INFO_OP_FACE_COORD);
        if( op )
        {
            op->_face_coord.x = (int16_t)f.x;
            op->_face_coord.z = (int16_t)f.z;
            op->_face_coord.instant = f.instant;
            op->_face_coord.movement_mode = (uint8_t)f.movement_mode;
            op->_face_coord.modern = true;
        }
    }
    else if( f.kind == 2 )
    {
        op = nop(s, PKT_NPC_INFO_OP_FACE_ANGLE);
        if( op )
        {
            op->_face_angle.angle = (uint16_t)f.angle;
            op->_face_angle.instant = f.instant;
            op->_face_angle.movement_mode = (uint8_t)f.movement_mode;
            op->_face_angle.modern = true;
        }
    }
    else
    {
        op = nop(s, PKT_NPC_INFO_OP_FACE_ENTITY);
        if( op )
        {
            op->_face_entity.entity_id = f.kind == 3 ? -1 : f.entity_id;
            op->_face_entity.fallback_angle = (uint16_t)(f.kind == 3 ? 0 : f.angle);
            op->_face_entity.has_fallback_angle = f.kind != 3;
            op->_face_entity.instant = f.kind != 3 && f.instant;
            op->_face_entity.movement_mode = (uint8_t)f.movement_mode;
            op->_face_entity.modern = true;
        }
    }
}

static void
n_block(struct NpcOut* s, RsprotInfoBlockOp const* b)
{
    struct PktNpcInfoOp* op;

    s->bar_pending = b->kind == RSPROT_IK_HEADBARS || b->kind == RSPROT_IK_HITS;
    s->body = NULL;
    switch( b->kind )
    {
    case RSPROT_IK_EXACT_MOVE:
        op = nop(s, PKT_NPC_INFO_OP_EXACT_MOVE);
        if( op )
        {
            op->_exactmove.start_x = (int16_t)b->exact_move.dx1;
            op->_exactmove.start_z = (int16_t)b->exact_move.dz1;
            op->_exactmove.end_x = (int16_t)b->exact_move.dx2;
            op->_exactmove.end_z = (int16_t)b->exact_move.dz2;
            op->_exactmove.end_cycle_delta = (uint16_t)b->exact_move.delay1;
            op->_exactmove.start_cycle_delta = (uint16_t)b->exact_move.delay2;
            op->_exactmove.facing = (uint16_t)b->exact_move.direction;
            op->_exactmove.facing_is_yaw = true;
            op->_exactmove.relative = true;
        }
        break;
    /* The body forms all carry a model list, the only part the op holds:
     * 0x1 resets to the type's models, and a block with no list leaves them. */
    case RSPROT_IK_CUSTOM_TYPED:
    case RSPROT_IK_CUSTOM_V3:
    case RSPROT_IK_CUSTOM_V4:
        op = nop(s, PKT_NPC_INFO_OP_BODY_CUSTOM);
        if( op )
            op->_body.reset = (b->custom.flag & 1) != 0 || (b->custom.flag & 2) == 0;
        s->body = op;
        break;
    case RSPROT_IK_LEVEL_CHANGE:
        nop_value(s, PKT_NPC_INFO_OP_LEVEL_CHANGE, (uint32_t)b->value.value);
        break;
    case RSPROT_IK_SEQUENCE:
        op = nop(s, PKT_NPC_INFO_OP_SEQUENCE);
        if( op )
        {
            op->_sequence.sequence_id = b->sequence.id == 65535 ? -1 : b->sequence.id;
            op->_sequence.delay = (uint8_t)b->sequence.delay;
        }
        break;
    case RSPROT_IK_TINTING:
        op = nop(s, PKT_NPC_INFO_OP_TINT);
        if( op )
        {
            op->_tint.start_cycle_delta = (uint16_t)b->tinting.start;
            op->_tint.end_cycle_delta = (uint16_t)b->tinting.end;
            op->_tint.hue = (int8_t)b->tinting.hue;
            op->_tint.saturation = (int8_t)b->tinting.saturation;
            op->_tint.lightness = (int8_t)b->tinting.lightness;
            op->_tint.weight = (int8_t)b->tinting.weight;
        }
        break;
    case RSPROT_IK_FACING:
        n_facing(s, b);
        break;
    /* 227-238: face-lock a pathing entity. */
    case RSPROT_IK_FACE_PATHING_ENTITY:
        op = nop(s, PKT_NPC_INFO_OP_FACE_ENTITY);
        if( op )
            op->_face_entity.entity_id = pathing_entity(b->face_pathing_entity.index);
        break;
    /* 227-231: face the centre of an absolute tile -- half-tiles for the op,
     * whose 0,0 means "no target", as 0xFFFF does on the wire. */
    case RSPROT_IK_FACE_COORD:
        op = nop(s, PKT_NPC_INFO_OP_FACE_COORD);
        if( op && b->face_coord.x != 0xFFFF )
        {
            op->_face_coord.x = (int16_t)(b->face_coord.x * 2 + 1);
            op->_face_coord.z = (int16_t)(b->face_coord.z * 2 + 1);
            op->_face_coord.instant = b->face_coord.instant == 1;
        }
        break;
    /* 231-238: an absolute angle, optionally instant. */
    case RSPROT_IK_FACE_ANGLE:
        op = nop(s, PKT_NPC_INFO_OP_FACE_ANGLE);
        if( op )
        {
            op->_face_angle.angle = (uint16_t)b->face_angle.angle;
            op->_face_angle.instant = b->face_angle.instant == 1;
        }
        break;
    case RSPROT_IK_HEADICON_CUSTOM:
        op = nop(s, PKT_NPC_INFO_OP_HEADICON_CUSTOM);
        if( op )
        {
            op->_headicon.mask = (uint8_t)b->headicon.mask;
            for( int i = 0; i < 8; i++ )
            {
                op->_headicon.group[i] = -1;
                op->_headicon.index[i] = -1;
            }
        }
        s->body = op; /* the HEADICON entries that follow fill it */
        break;
    case RSPROT_IK_SAY:
        op = nop(s, PKT_NPC_INFO_OP_SAY);
        if( op )
            op->_say.text = dup_text(b->say.text);
        break;
    case RSPROT_IK_TRANSPARENCY:
        op = nop(s, PKT_NPC_INFO_OP_TRANSPARENCY);
        if( op )
        {
            op->_transparency.start_cycle_delta = (int16_t)b->transparency.start;
            op->_transparency.end_cycle_delta = (int16_t)b->transparency.end;
            op->_transparency.from = (uint8_t)b->transparency.alpha_start;
            op->_transparency.to = (uint8_t)b->transparency.alpha_end;
            op->_transparency.use_start = b->transparency.use_start == 1;
        }
        break;
    case RSPROT_IK_NAME_CHANGE:
        op = nop(s, PKT_NPC_INFO_OP_NAME_CHANGE);
        if( op )
            op->_name_change.name = dup_text(b->say.text);
        break;
    case RSPROT_IK_ENABLED_OPS:
        nop_value(s, PKT_NPC_INFO_OP_VISIBLE_OPS, (uint64_t)b->value.value);
        break;
    case RSPROT_IK_TRANSFORMATION:
        op = nop(s, PKT_NPC_INFO_OP_CHANGE_TYPE);
        if( op )
            op->_change_type.npc_type = b->value.value == 65535 ? -1 : b->value.value;
        break;
    case RSPROT_IK_BAS_CHANGE:
        op = nop(s, PKT_NPC_INFO_OP_BAS_CHANGE);
        if( op )
        {
            int32_t const* a = b->bas.anims;
#define BAS(i) (a[i] == 65535 ? -1 : a[i])
            op->_bas_change.mask = (uint32_t)b->bas.mask;
            op->_bas_change.turnanim = BAS(0);
            op->_bas_change.turnanim_r = BAS(1);
            op->_bas_change.walkanim = BAS(2);
            op->_bas_change.walkanim_b = BAS(3);
            op->_bas_change.walkanim_l = BAS(4);
            op->_bas_change.walkanim_r = BAS(5);
            op->_bas_change.runanim = BAS(6);
            op->_bas_change.runanim_b = BAS(7);
            op->_bas_change.runanim_l = BAS(8);
            op->_bas_change.runanim_r = BAS(9);
            op->_bas_change.crawlanim = BAS(10);
            op->_bas_change.crawlanim_b = BAS(11);
            op->_bas_change.crawlanim_l = BAS(12);
            op->_bas_change.crawlanim_r = BAS(13);
            op->_bas_change.readyanim = BAS(14);
#undef BAS
        }
        break;
    /* The list blocks speak through their entries; the head customisation and
     * freeze have no home in the op list, as at 239 and 241. */
    default:
        break;
    }
}

static void
n_hit(struct NpcOut* s, RsprotInfoHit const* hit)
{
    struct PktNpcInfoOp* op;

    if( hit->type == 0x7FFE )
        return;
    op = nop(s, PKT_NPC_INFO_OP_DAMAGE);
    if( op )
    {
        op->_damage.damage_type = hit->type;
        op->_damage.damage = hit->value;
        op->_damage.delay = (uint16_t)hit->delay;
        op->_damage.slots = (uint16_t)(hit->limit < 0 ? 0 : hit->limit);
    }
    if( hit->soak_type >= 0 && (op = nop(s, PKT_NPC_INFO_OP_DAMAGE)) != NULL )
    {
        op->_damage.damage_type = hit->soak_type;
        op->_damage.damage = hit->soak_value;
        op->_damage.delay = (uint16_t)hit->delay;
    }
}

static void
n_bar(struct NpcOut* s, RsprotInfoHeadbar const* bar)
{
    struct PktNpcInfoOp* op;

    if( !s->bar_pending )
        return;
    s->bar_pending = 0;
    op = nop(s, PKT_NPC_INFO_OP_HEADBAR);
    if( !op )
        return;
    op->_headbar.type = bar->type;
    if( bar->end_time == 0x7FFF )
    {
        op->_headbar.remove = true;
        return;
    }
    op->_headbar.duration = bar->end_time;
    op->_headbar.start_delay = bar->start_time;
    op->_headbar.start_fill = (uint8_t)bar->start_fill;
    op->_headbar.end_fill = (uint8_t)bar->end_fill;
}

int
rsprox_npc_info_convert(
    RsprotNpcInfoReadFn read, uint8_t const* data, int len, struct PktNpcInfoOp* ops, int cap)
{
    static struct NpcOut s;
    int n;

    assert(read);
    assert(ops);
    /* No type lookup yet: a CUSTOM_TYPED block with recolours or retextures
     * ends the tail, as it does in the hand readers. */
    n = read(data, len, NULL, NULL, g_lib, LIBRARY_OPS, &g_last_ok);

    memset(&s, 0, sizeof(s));
    s.ops = ops;
    s.cap = cap;
    for( int i = 0; i < n; i++ )
    {
        RsprotInfoOp const* l = &g_lib[i];

        switch( l->kind )
        {
        case RSPROT_IO_NPC_COUNT:
            nop_value(&s, PKT_NPC_INFO_OPBITS_COUNT_RESET, (uint64_t)l->count);
            break;
        case RSPROT_IO_NPC_KEEP:
            nop_value(&s, PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX, (uint64_t)l->who);
            nop_value(&s, PKT_NPC_INFO_OPBITS_INFO, (uint64_t)l->moved);
            break;
        case RSPROT_IO_NPC_REMOVE:
            nop_value(&s, PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX, (uint64_t)l->who);
            break;
        case RSPROT_IO_NPC_STEP:
            nop_value(&s,
                      l->step.speed == 1   ? PKT_NPC_INFO_OPBITS_WALKDIR
                      : l->step.speed == 2 ? PKT_NPC_INFO_OPBITS_RUNDIR
                                           : PKT_NPC_INFO_OPBITS_CRAWLDIR,
                      (uint64_t)l->step.direction);
            break;
        case RSPROT_IO_NPC_ADD:
            n_add(&s, l);
            break;
        case RSPROT_IO_EXT_BEGIN:
            nop_value(&s, PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX, (uint64_t)l->who);
            break;
        case RSPROT_IO_BLOCK:
            n_block(&s, &l->block);
            break;
        case RSPROT_IO_SPOT:
        {
            struct PktNpcInfoOp* op = nop(&s, PKT_NPC_INFO_OP_SPOTANIM);

            if( op )
            {
                op->_spotanim.spotanim_id = l->spot.id == 65535 ? -1 : l->spot.id;
                op->_spotanim.height_delay = l->spot.height_delay;
                op->_spotanim.slot = (uint8_t)l->spot.slot;
                op->_spotanim.loop = l->spot.loop == 1;
            }
            break;
        }
        case RSPROT_IO_HIT:
            n_hit(&s, &l->hit);
            break;
        case RSPROT_IO_BAR:
            n_bar(&s, &l->bar);
            break;
        case RSPROT_IO_MODEL:
            if( s.body && s.body->kind == PKT_NPC_INFO_OP_BODY_CUSTOM &&
                s.body->_body.model_count < PKT_NPC_BODY_CUSTOM_MODELS_MAX )
                s.body->_body.models[s.body->_body.model_count++] = l->item;
            break;
        case RSPROT_IO_HEADICON:
            if( s.body && s.body->kind == PKT_NPC_INFO_OP_HEADICON_CUSTOM )
            {
                s.body->_headicon.group[l->headicon.slot] = l->headicon.group;
                s.body->_headicon.index[l->headicon.slot] = (int16_t)l->headicon.index;
            }
            break;
        default:
            break;
        }
    }
    return s.count;
}

/* --- world entities ------------------------------------------------------ */

static void
we_ext(
    RsprotInfoWeExt const* e,
    unsigned* flags,
    int* has_op_mask,
    int* op_mask,
    int* has_seq,
    int* seq_id,
    int* seq_delay)
{
    *flags = (unsigned)e->flags;
    *has_op_mask = e->has_ops;
    *op_mask = e->has_ops ? e->ops : PKT_WEV_OP_MASK_ALL;
    *has_seq = e->has_sequence;
    *seq_id = !e->has_sequence || e->sequence_id == 65535 ? -1 : e->sequence_id;
    *seq_delay = e->has_sequence ? e->sequence_delay : 0;
}

/*
 * The arm was shaped by 239+ (struct PktWorldEntityInfo): V3 / V4 spawns carry
 * no config id or priority (-1 here, which the executor refuses to spawn) and
 * their level and V4's centre offsets have no field.
 */
int
rsprox_worldentity_info_convert(
    RsprotWorldEntityInfoReadFn read, uint8_t const* data, int len, struct PktWorldEntityInfo* out)
{
    int complete;
    int n;

    assert(read);
    assert(out);
    memset(out, 0, sizeof(*out));
    n = read(data, len, g_lib, LIBRARY_OPS, &complete);
    for( int i = 0; i < n; i++ )
    {
        RsprotInfoOp const* l = &g_lib[i];
        struct PktWevUpdate* u;
        struct PktWevSpawn* sp;

        switch( l->kind )
        {
        case RSPROT_IO_WE_COUNT:
            /* At most 15 entities exist client-side: a longer list is a
             * malformed frame (struct PktWorldEntityInfo). */
            if( l->count > PKT_WEV_INFO_MAX )
                return 0;
            out->count = l->count;
            break;
        case RSPROT_IO_WE_REMOVE:
            out->updates[l->who].op = PKT_WEV_OP_DESPAWN;
            out->updates[l->who].op_mask = PKT_WEV_OP_MASK_ALL;
            out->updates[l->who].seq_id = -1;
            break;
        case RSPROT_IO_WE_IDLE:
        case RSPROT_IO_WE_MOVE:
            u = &out->updates[l->who];
            u->op = l->kind == RSPROT_IO_WE_IDLE ? PKT_WEV_OP_FLAGS
                    : l->we_move.teleport        ? PKT_WEV_OP_SNAP
                                                 : PKT_WEV_OP_ENQUEUE;
            u->dx = l->we_move.dx;
            u->dy = l->we_move.dy;
            u->dz = l->we_move.dz;
            u->dangle = l->we_move.dangle;
            we_ext(&l->we_move.ext, &u->update_flags, &u->has_op_mask, &u->op_mask, &u->has_seq,
                   &u->seq_id, &u->seq_delay);
            break;
        case RSPROT_IO_WE_ADD:
            if( out->spawn_count >= PKT_WEV_INFO_MAX )
                return 0;
            sp = &out->spawns[out->spawn_count++];
            sp->id = l->who;
            sp->config_id = l->we_add.id;
            sp->priority_group = l->we_add.priority;
            sp->size_x_tiles = l->we_add.size_x;
            sp->size_z_tiles = l->we_add.size_z;
            sp->x = l->we_add.x;
            sp->y = l->we_add.y;
            sp->z = l->we_add.z;
            sp->angle = l->we_add.angle & 0x7ff;
            we_ext(&l->we_add.ext, &sp->update_flags, &sp->has_op_mask, &sp->op_mask, &sp->has_seq,
                   &sp->seq_id, &sp->seq_delay);
            break;
        default:
            break;
        }
    }
    return complete;
}
