/*
 * See proto_adapt.h -- one adapter per RSProt message, from the decoded message
 * to the canonical RevPacket arm. The revision's executor has already run the
 * codec its revision carries; nothing here decodes a byte or looks a codec up.
 *
 * Each adapter is written out rather than generated because each states a
 * mapping decision -- which canonical field a wire field lands in, a unit
 * change, a sentinel -- and stating them here keeps them where someone reading
 * the packet looks.
 */
#include "proto_adapt.h"

#include "mem/torirs_mem.h"
#include "net/jbase37.h"
#include "net/proto/proto_info.h"
#include "net/wordpack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RevPacket owns its strings; a message's are borrows into the payload. */
static char*
dup_borrowed(char const* s)
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

static int
sign24(int v)
{
    return (v & 0x800000) ? v - 0x1000000 : v;
}

/* The huffman text a chat message ends with. */
static char*
unpack_text(uint8_t const* huffman, int32_t length)
{
    struct RSCache_Buffer text;

    RSCache_BufferInit(&text, (uint8_t*)huffman, (uint32_t)length);
    return wordpack_unpack(&text, length);
}

/* A packet the store decodes against client state, carried as its bytes. */
static int
take_raw(uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktRawPayload* p = &out->_raw_payload;

    memset(p, 0, sizeof(*p));
    if( len == 0 )
        return 1;
    p->length = len;
    p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
    assert(p->data);
    memcpy(p->data, data, (size_t)len);
    return 1;
}

/* A stream the exec layer decodes later, against session state. */
static uint8_t*
take_stream(uint8_t const* data, int len)
{
    uint8_t* copy = ToriMem_Alloc(ToriMem_Net(), (size_t)(len > 0 ? len : 1));

    assert(copy);
    memcpy(copy, data, (size_t)len);
    return copy;
}

/* ------------------------------------------------------------------ */
/* The info streams                                                    */
/* ------------------------------------------------------------------ */

int
adapt_player_info(uint8_t const* data, int len, struct RevPacket* out)
{
    out->_player_info.length = len;
    out->_player_info.data = take_stream(data, len);
    return 1;
}

int
adapt_npc_info(uint8_t const* data, int len, struct RevPacket* out)
{
    out->_npc_info.length = len;
    out->_npc_info.large = out->packet_type == PKT_NAME_NPC_INFO_LARGE;
    out->_npc_info.data = take_stream(data, len);
    return 1;
}

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
 * The stream's list into the canonical arm, which was shaped by 239+ (struct
 * PktWorldEntityInfo): V3 / V4 spawns carry no config id or priority (-1 here,
 * which the executor refuses to spawn) and their level and V4's centre offsets
 * have no field. 1 when the stream read whole and fits the arm.
 */
int
adapt_worldentity_info(RsprotWorldEntityInfoReadFn read, uint8_t const* data, int len, struct RevPacket* out)
{
    /* A g1 count of entries and spawns of at least ten bytes each: the arm's
     * fifteen run out long before these do. */
    static RsprotInfoOp ops[512];
    static uint8_t bytes[1];
    struct PktWorldEntityInfo* p = &out->_worldentity_info;
    RsprotInfoList list;

    assert(read);
    memset(p, 0, sizeof(*p));
    rsprot_info_list_init(&list, ops, (int)(sizeof(ops) / sizeof(ops[0])), bytes, sizeof(bytes));
    read(data, len, &list);
    for( int i = 0; i < list.count; i++ )
    {
        RsprotInfoOp const* l = &ops[i];
        struct PktWevUpdate* u;
        struct PktWevSpawn* sp;

        switch( l->kind )
        {
        case RSPROT_OP_WE_COUNT:
            /* At most 15 entities exist client-side: a longer list is a
             * malformed frame (struct PktWorldEntityInfo). */
            if( l->value > PKT_WEV_INFO_MAX )
                return 0;
            p->count = (int)l->value;
            break;
        case RSPROT_OP_WE_REMOVE:
            p->updates[l->who].op = PKT_WEV_OP_DESPAWN;
            p->updates[l->who].op_mask = PKT_WEV_OP_MASK_ALL;
            p->updates[l->who].seq_id = -1;
            break;
        case RSPROT_OP_WE_IDLE:
        case RSPROT_OP_WE_MOVE:
            u = &p->updates[l->who];
            u->op = l->kind == RSPROT_OP_WE_IDLE ? PKT_WEV_OP_FLAGS
                    : l->we_move.teleport        ? PKT_WEV_OP_SNAP
                                                 : PKT_WEV_OP_ENQUEUE;
            u->dx = l->we_move.dx;
            u->dy = l->we_move.dy;
            u->dz = l->we_move.dz;
            u->dangle = l->we_move.dangle;
            we_ext(&l->we_move.ext, &u->update_flags, &u->has_op_mask, &u->op_mask, &u->has_seq,
                   &u->seq_id, &u->seq_delay);
            break;
        case RSPROT_OP_WE_ADD:
            if( p->spawn_count >= PKT_WEV_INFO_MAX )
                return 0;
            /* An id or size this client cannot host is still the wire's:
             * the spawn is refused one entity at a time where the view is
             * built (App_WevSpawn), not by dropping the frame -- Jagex sends
             * world-entity ids past our 15-view registry. */
            sp = &p->spawns[p->spawn_count++];
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
    return rsprot_info_list_complete(&list);
}

/* ------------------------------------------------------------------ */
/* Lists read into caller storage                                      */
/* ------------------------------------------------------------------ */

/*
 * The codecs write these lists through pointers in the message (RSProt never
 * allocates). One static pool per message, sized at the codec's own caps, so
 * a malformed payload fails the exec rather than writing past them.
 */

void
prep_update_inv_full(MsgUpdateInvFull* m)
{
    static MsgUpdateInvFullSlot slots[MSG_UPDATE_INV_FULL_MAX_SLOTS];

    m->slots = slots;
}

int
adapt_update_inv_full(MsgUpdateInvFull const* m, struct RevPacket* out)
{
    struct PktUpdateInvFull* p = &out->_update_inv_full;

    memset(p, 0, sizeof(*p));
    p->component_id = m->combined_id;
    p->inv_id = m->inventory_id;
    p->size = m->capacity;
    p->obj_ids = (int*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->capacity * sizeof(int) + 1);
    assert(p->obj_ids);
    p->obj_counts = (int*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->capacity * sizeof(int) + 1);
    assert(p->obj_counts);
    for( int i = 0; i < m->capacity; i++ )
    {
        p->obj_ids[i] = m->slots[i].id;
        p->obj_counts[i] = m->slots[i].count;
    }
    return 1;
}

void
prep_update_inv_partial(MsgUpdateInvPartial* m)
{
    static MsgUpdateInvPartialSlot slots[MSG_UPDATE_INV_PARTIAL_MAX_SLOTS];

    m->slots = slots;
}

int
adapt_update_inv_partial(MsgUpdateInvPartial const* m, struct RevPacket* out)
{
    struct PktUpdateInvPartial* p = &out->_update_inv_partial;

    memset(p, 0, sizeof(*p));
    p->component_id = m->combined_id;
    p->inv_id = m->inventory_id;
    p->count = m->count;
    p->entries = (struct PktUpdateInvPartialEntry*)ToriMem_Alloc(
        ToriMem_Net(), (size_t)m->count * sizeof(*p->entries) + 1);
    assert(p->entries);
    for( int i = 0; i < m->count; i++ )
    {
        p->entries[i].slot = m->slots[i].slot;
        p->entries[i].obj_id = m->slots[i].id;
        /* An emptied slot carries no count on the wire; the canonical struct
         * wants a number, and 0 is what "nothing here" means to the consumer. */
        p->entries[i].count = m->slots[i].id < 0 ? 0 : m->slots[i].count;
    }
    return 1;
}

/* Two bytes, and they are the CONTAINER id, not a component. This revision
 * addresses the container directly; component_id is stated -1 to say the
 * packet carries none, which is what the consumer tests. */
int
adapt_update_inv_stoptransmit(MsgUpdateInvStopTransmit const* m, struct RevPacket* out)
{
    out->_update_inv_stop_transmit.component_id = -1;
    out->_update_inv_stop_transmit.inv_id = m->inventory_id;
    return 1;
}

void
prep_update_ignorelist(MsgUpdateIgnoreList* m)
{
    static MsgUpdateIgnoreListEntry entries[MSG_UPDATE_IGNORELIST_MAX_ENTRIES];

    m->entries = entries;
}

int
adapt_update_ignorelist(MsgUpdateIgnoreList const* m, struct RevPacket* out)
{
    struct PktUpdateIgnoreList* p = &out->_update_ignorelist;

    memset(p, 0, sizeof(*p));
    p->count = m->count;
    p->names37 = (int64_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->count * sizeof(int64_t) + 1);
    assert(p->names37);
    for( int i = 0; i < m->count; i++ )
        p->names37[i] = m->entries[i].name ? (int64_t)strtobase37(m->entries[i].name) : 0;
    return 1;
}

void
prep_update_friendlist(MsgUpdateFriendList* m)
{
    static MsgUpdateFriendListEntry entries[MSG_UPDATE_FRIENDLIST_MAX_ENTRIES];

    m->entries = entries;
}

/*
 * PktUpdateFriendList holds ONE friend, not a list, and the hand-written arm
 * required the payload to hold exactly one entry. Keeping that: the first entry
 * wins and `present` says whether there was one, which is what the consumer
 * already reads. A multi-entry payload decodes, and the extra entries are
 * dropped -- a gap in the canonical struct, not in this decode.
 */
int
adapt_update_friendlist(MsgUpdateFriendList const* m, struct RevPacket* out)
{
    struct PktUpdateFriendList* p = &out->_update_friendlist;

    memset(p, 0, sizeof(*p));
    /* An empty payload is legal and completes initial sync. */
    if( m->count <= 0 )
        return 1;
    if( !m->entries[0].name )
        return 0;
    p->name37 = (int64_t)strtobase37(m->entries[0].name);
    p->world = m->entries[0].world_id;
    p->rank = m->entries[0].rank;
    snprintf(p->previous_name, sizeof(p->previous_name), "%s",
        m->entries[0].previous_name ? m->entries[0].previous_name : "");
    p->present = 1;
    return 1;
}

void
prep_runclientscript(MsgRunClientScript* m)
{
    static MsgRunClientScriptArg args[MSG_RUNCLIENTSCRIPT_MAX_ARGS];
    static int32_t ints[MSG_RUNCLIENTSCRIPT_MAX_ELEMS];
    static const char* strs[MSG_RUNCLIENTSCRIPT_MAX_ELEMS];

    memset(args, 0, sizeof(args));
    for( int i = 0; i < MSG_RUNCLIENTSCRIPT_MAX_ARGS; i++ )
    {
        args[i].ints = ints;
        args[i].strs = strs;
    }
    m->args = args;
}

int
adapt_runclientscript(MsgRunClientScript const* m, struct RevPacket* out)
{
    struct PktRunClientScript* p;
    int argc;

    if( !m->types )
        return 0;
    p = pkt_runclientscript_reset(out);
    p->script_id = m->id;
    argc = (int)strlen(m->types);
    if( argc > PKT_RUNCLIENTSCRIPT_ARG_MAX )
        return 0;
    p->argc = argc;
    for( int i = 0; i < argc; i++ )
    {
        if( m->types[i] == 's' )
        {
            /* The canonical struct owns its string arena; the codec hands
             * back a borrow into the payload, so this copies. */
            char const* s = m->args[i].sval ? m->args[i].sval : "";

            pkt_runclientscript_set_str(p, i, s, strlen(s));
        }
        else if( m->types[i] == (char)MSG_RUNCLIENTSCRIPT_TYPE_LONG )
        {
            p->longv[i] = m->args[i].lval;
            p->long_mask |= 1u << i;
        }
        else
        {
            /*
             * The array forms have no home in PktRunClientScript, which carries
             * one scalar per argument. They decode correctly and are then
             * narrowed; the same narrowing the hand-written arm did by only
             * ever reading p4 and pjstr.
             */
            p->intv[i] = m->args[i].ival;
        }
    }
    return 1;
}

/*
 * IF_RESYNC: the root, the mounted sub-interfaces, the event ranges. The
 * sub-interface list is prefixed by a two-byte count on the wire; `events`
 * runs to the end of the payload, which is why the codec loops it with
 * RSPROT_MORE and decoding learns its length rather than being told. Before
 * V2 (227-231) an event range carries one events word.
 */
#define RESYNC_PREP(MSG, SUB, EVT)                                                                 \
    {                                                                                              \
        static SUB subs[4096];                                                                     \
        static EVT evts[256];                                                                      \
        m->sub_interfaces = subs;                                                                  \
        m->events = evts;                                                                          \
    }

#define RESYNC_ADAPT(EVENTS1, EVENTS2)                                                             \
    {                                                                                              \
        struct PktIfResync* p = &out->_if_resync;                                                  \
        memset(p, 0, sizeof(*p));                                                                  \
        if( m->sub_interfaces_count < 0 || m->sub_interfaces_count > 4096 )                        \
            return 0;                                                                              \
        /* 65535 is "no root"; the canonical struct says that with -1. */                         \
        p->root_interface_id = m->top_level_interface == 65535 ? -1 : m->top_level_interface;      \
        p->mount_count = m->sub_interfaces_count;                                                  \
        p->event_count = m->events_count;                                                          \
        if( m->sub_interfaces_count > 0 )                                                          \
        {                                                                                          \
            p->mounts = (struct PktIfResyncMount*)ToriMem_Alloc(                                   \
                ToriMem_Net(), (size_t)m->sub_interfaces_count * sizeof(*p->mounts));              \
            assert(p->mounts);                                                                     \
            for( int i = 0; i < m->sub_interfaces_count; i++ )                                     \
            {                                                                                      \
                p->mounts[i].target_uid = m->sub_interfaces[i].destination_combined_id;            \
                p->mounts[i].interface_id = m->sub_interfaces[i].interface_id;                     \
                p->mounts[i].type = m->sub_interfaces[i].type;                                     \
            }                                                                                      \
        }                                                                                          \
        if( m->events_count > 0 )                                                                  \
        {                                                                                          \
            p->events = (struct PktIfResyncEvent*)ToriMem_Alloc(                                   \
                ToriMem_Net(), (size_t)m->events_count * sizeof(*p->events));                      \
            assert(p->events);                                                                     \
            for( int i = 0; i < m->events_count; i++ )                                             \
            {                                                                                      \
                p->events[i].component_id = m->events[i].combined_id;                             \
                p->events[i].from = (int16_t)m->events[i].start;                                   \
                p->events[i].to = (int16_t)m->events[i].end;                                       \
                p->events[i].events1 = (uint32_t)(EVENTS1);                                        \
                p->events[i].events2 = (uint32_t)(EVENTS2);                                        \
            }                                                                                      \
        }                                                                                          \
        return 1;                                                                                  \
    }

void
prep_if_resync(MsgIfResync* m)
RESYNC_PREP(MsgIfResync, Rsprot_MsgIfResync_sub_interfacesElem, Rsprot_MsgIfResync_eventsElem)

int
adapt_if_resync(MsgIfResync const* m, struct RevPacket* out)
RESYNC_ADAPT(m->events[i].events, 0)

void
prep_if_resync_v1(MsgIfResyncV1* m)
RESYNC_PREP(MsgIfResyncV1, Rsprot_MsgIfResyncV1_sub_interfacesElem, Rsprot_MsgIfResyncV1_eventsElem)

int
adapt_if_resync_v1(MsgIfResyncV1 const* m, struct RevPacket* out)
RESYNC_ADAPT(m->events[i].events, 0)

void
prep_if_resync_v2(MsgIfResyncV2* m)
RESYNC_PREP(MsgIfResyncV2, Rsprot_MsgIfResyncV2_sub_interfacesElem, Rsprot_MsgIfResyncV2_eventsElem)

int
adapt_if_resync_v2(MsgIfResyncV2 const* m, struct RevPacket* out)
RESYNC_ADAPT(m->events[i].events1, m->events[i].events2)

/* ------------------------------------------------------------------ */
/* Interfaces before V2                                                */
/* ------------------------------------------------------------------ */

/* IF_SETEVENTS before V2 (227-231): one 32-bit events word. */
int
adapt_if_setevents(MsgIfSetEvents const* m, struct RevPacket* out)
{
    struct PktIfSetEvents* p = &out->_if_setevents;

    p->component_id = m->combined_id;
    p->from = (int16_t)m->start;
    p->to = (int16_t)m->end;
    p->events = m->events;
    p->events1 = (uint32_t)p->events;
    p->events2 = 0;
    return 1;
}

int
adapt_if_setevents_v1(MsgIfSetEventsV1 const* m, struct RevPacket* out)
{
    struct PktIfSetEvents* p = &out->_if_setevents;

    p->component_id = m->combined_id;
    p->from = (int16_t)m->start;
    p->to = (int16_t)m->end;
    p->events = m->events;
    p->events1 = (uint32_t)p->events;
    p->events2 = 0;
    return 1;
}

int
adapt_if_setmodel(MsgIfSetModel const* m, struct RevPacket* out)
{
    out->_if_setmodel.component_id = m->combined_id;
    out->_if_setmodel.model_id = m->model;
    return 1;
}

int
adapt_if_setmodel_v1(MsgIfSetModelV1 const* m, struct RevPacket* out)
{
    out->_if_setmodel.component_id = m->combined_id;
    out->_if_setmodel.model_id = m->model;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Vars, timers, the world                                             */
/* ------------------------------------------------------------------ */

/* RSProx's VarpLongDecoder: the value in two halves, high first. */
int
adapt_varp_long(MsgVarpLong const* m, struct RevPacket* out)
{
    out->_varp_long.variable = m->id;
    out->_varp_long.value = (int64_t)(((uint64_t)(uint32_t)m->high << 32) | (uint64_t)(uint32_t)m->low);
    return 1;
}

int
adapt_update_reboot_timer(MsgUpdateRebootTimer const* m, struct RevPacket* out)
{
    out->_update_reboot_timer.ticks = m->game_cycles;
    return 1;
}

int
adapt_update_reboot_timer_v1(MsgUpdateRebootTimerV1 const* m, struct RevPacket* out)
{
    out->_update_reboot_timer.ticks = m->game_cycles;
    return 1;
}

int
adapt_update_reboot_timer_v2(MsgUpdateRebootTimerV2 const* m, struct RevPacket* out)
{
    out->_update_reboot_timer.ticks = m->game_cycles;
    return 1;
}

int
adapt_update_stockmarket_slot_v1(MsgUpdateStockmarketSlotV1 const* m, struct RevPacket* out)
{
    struct PktUpdateStockmarketSlot* p = &out->_update_stockmarket_slot;

    memset(p, 0, sizeof(*p));
    p->slot = m->slot;
    p->status = (int8_t)m->status;
    /* An empty slot's remaining 18 bytes are padding. */
    if( p->status != 0 )
    {
        p->obj = m->obj;
        p->price = m->price;
        p->count = m->count;
        p->completed_count = m->completed_count;
        p->completed_gold = m->completed_gold;
    }
    return 1;
}

/* 227-228's form names the root by a type byte. The canonical struct says
 * root with 0. */
int
adapt_set_active_world_v1(MsgSetActiveWorldV1 const* m, struct RevPacket* out)
{
    out->_set_active_world.world_id = m->type == 0 ? 0 : m->index;
    out->_set_active_world.level = m->active_level;
    return 1;
}

/* 229+ name it by index: -1 for the root below 237 and 0 from it (RSProx
 * SetActiveWorldV2 folds 0 to -1). Both fold to the canonical 0. */
int
adapt_set_active_world_v2(int revision, MsgSetActiveWorldV2 const* m, struct RevPacket* out)
{
    int index = (int16_t)m->index;

    (void)revision;
    out->_set_active_world.world_id = index < 0 ? 0 : index;
    out->_set_active_world.level = m->active_level;
    return 1;
}

/* SET_MAP_FLAG before V2 (227-236): a build-area tile; 255,255 clears. */
int
adapt_set_map_flag(MsgSetMapFlag const* m, struct RevPacket* out)
{
    out->_set_map_flag.x = m->x_in_build_area;
    out->_set_map_flag.z = m->z_in_build_area;
    out->_set_map_flag.clear = m->x_in_build_area == 255 && m->z_in_build_area == 255;
    out->_set_map_flag.absolute = 0;
    return 1;
}

int
adapt_set_map_flag_v1(MsgSetMapFlagV1 const* m, struct RevPacket* out)
{
    out->_set_map_flag.x = m->x_in_build_area;
    out->_set_map_flag.z = m->z_in_build_area;
    out->_set_map_flag.clear = m->x_in_build_area == 255 && m->z_in_build_area == 255;
    out->_set_map_flag.absolute = 0;
    return 1;
}

int
adapt_set_player_op(MsgSetPlayerOp const* m, struct RevPacket* out)
{
    struct PktSetPlayerOp* p = &out->_set_player_op;

    memset(p, 0, sizeof(*p));
    p->slot = m->id;
    p->primary = m->priority;
    p->text = dup_borrowed(m->op ? m->op : "");
    return 1;
}

/* HintArrowDecoder: types 1 / 10 / 11 name an entity, 2..6 a tile. */
int
adapt_hint_arrow(MsgHintArrow const* m, struct RevPacket* out)
{
    out->_hint_arrow.type = m->type;
    if( m->type >= 2 && m->type <= 6 )
    {
        out->_hint_arrow.id = m->x;
        out->_hint_arrow.z = m->z;
    }
    else
    {
        out->_hint_arrow.id = m->index;
    }
    out->_hint_arrow.height = m->height;
    return 1;
}

int
adapt_update_runweight(MsgUpdateRunWeight const* m, struct RevPacket* out)
{
    out->_update_runweight.run_weight = (int16_t)m->runweight;
    return 1;
}

/* MapAnimSpecific: the build-area packed coord is zone << 8 | (in-zone) halves
 * (deob client.java, prot 90). */
int
adapt_map_anim_specific(MsgMapAnimSpecific const* m, struct RevPacket* out)
{
    int packed = m->coord_in_build_area_packed;

    out->_map_anim_specific.x = ((packed >> 4) & 0x7) + (packed >> 16);
    out->_map_anim_specific.z = (packed & 0x7) + ((packed >> 8) & 0xff);
    out->_map_anim_specific.id = m->id;
    out->_map_anim_specific.height = m->height;
    out->_map_anim_specific.delay = m->delay;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */

int
adapt_midi_song_withsecondary(MsgMidiSongWithSecondary const* m, struct RevPacket* out)
{
    struct PktMidiSongWithSecondary* p = &out->_midi_song_with_secondary;

    p->primary_id = m->primary_id == 65535 ? -1 : m->primary_id;
    p->secondary_id = m->secondary_id == 65535 ? -1 : m->secondary_id;
    /* Speeds are client cycles; the player works in milliseconds. */
    p->fade_in_ms = m->fade_in_speed * 20;
    p->fade_out_ms = m->fade_out_speed * 20;
    return 1;
}

int
adapt_midi_swap(MsgMidiSwap const* m, struct RevPacket* out)
{
    out->_midi_swap.fade_out_ms = m->fade_out_speed * 20;
    out->_midi_swap.fade_in_ms = m->fade_in_speed * 20;
    return 1;
}

int
adapt_midi_song_stop(MsgMidiSongStop const* m, struct RevPacket* out)
{
    out->_midi_song_stop.fade_out_ms = m->fade_out_speed * 20;
    return 1;
}

int
adapt_ambientsound_start(MsgAmbientSoundStart const* m, struct RevPacket* out)
{
    out->_ambientsound_start.id = m->id;
    out->_ambientsound_start.fade_ms = m->fade * 20;
    return 1;
}

int
adapt_ambientsound_stop(MsgAmbientSoundStop const* m, struct RevPacket* out)
{
    out->_ambientsound_stop.fade_ms = m->fade != 0 ? 500 : 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Camera before V3                                                    */
/* ------------------------------------------------------------------ */

int
adapt_cam_lookat(MsgCamLookAt const* m, struct RevPacket* out)
{
    out->_cam_lookat.local_x = m->destination_x_in_build_area;
    out->_cam_lookat.local_z = m->destination_z_in_build_area;
    out->_cam_lookat.height = m->height;
    out->_cam_lookat.rate = m->rate;
    out->_cam_lookat.rate2 = m->rate2;
    return 1;
}

int
adapt_cam_lookat_v1(MsgCamLookAtV1 const* m, struct RevPacket* out)
{
    out->_cam_lookat.local_x = m->destination_x_in_build_area;
    out->_cam_lookat.local_z = m->destination_z_in_build_area;
    out->_cam_lookat.height = m->height;
    out->_cam_lookat.rate = m->rate;
    out->_cam_lookat.rate2 = m->rate2;
    return 1;
}

int
adapt_cam_lookat_v2(MsgCamLookAtV2 const* m, struct RevPacket* out)
{
    out->_cam_lookat.local_x = m->x;
    out->_cam_lookat.local_z = m->z;
    out->_cam_lookat.height = m->height;
    out->_cam_lookat.rate = m->rate;
    out->_cam_lookat.rate2 = m->rate2;
    out->_cam_lookat.absolute = 1;
    return 1;
}

int
adapt_cam_moveto(MsgCamMoveTo const* m, struct RevPacket* out)
{
    out->_cam_moveto.local_x = m->destination_x_in_build_area;
    out->_cam_moveto.local_z = m->destination_z_in_build_area;
    out->_cam_moveto.height = m->height;
    out->_cam_moveto.rate = m->rate;
    out->_cam_moveto.rate2 = m->rate2;
    return 1;
}

int
adapt_cam_moveto_v1(MsgCamMoveToV1 const* m, struct RevPacket* out)
{
    out->_cam_moveto.local_x = m->destination_x_in_build_area;
    out->_cam_moveto.local_z = m->destination_z_in_build_area;
    out->_cam_moveto.height = m->height;
    out->_cam_moveto.rate = m->rate;
    out->_cam_moveto.rate2 = m->rate2;
    return 1;
}

int
adapt_cam_moveto_v2(MsgCamMoveToV2 const* m, struct RevPacket* out)
{
    out->_cam_moveto.local_x = m->x;
    out->_cam_moveto.local_z = m->z;
    out->_cam_moveto.height = m->height;
    out->_cam_moveto.rate = m->rate;
    out->_cam_moveto.rate2 = m->rate2;
    out->_cam_moveto.absolute = 1;
    return 1;
}

/*
 * The cycle-timed camera before V3 (227-240) addressed a BUILD-AREA tile; the
 * canonical struct's coordinates are absolute. PktCamMoveToCycles carries no
 * flag, so the build-area forms are reported as they are and the exec layer,
 * which knows the base, converts (see the struct).
 */
int
adapt_cam_moveto_cycles(MsgCamMoveToCycles const* m, struct RevPacket* out)
{
    out->_cam_moveto_cycles.x = m->destination_x_in_build_area;
    out->_cam_moveto_cycles.z = m->destination_z_in_build_area;
    out->_cam_moveto_cycles.height = m->height;
    out->_cam_moveto_cycles.cycles = m->cycles;
    out->_cam_moveto_cycles.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_cycles.easing = m->easing_id;
    out->_cam_moveto_cycles.absolute = 0;
    return 1;
}

int
adapt_cam_moveto_cycles_v1(MsgCamMoveToCyclesV1 const* m, struct RevPacket* out)
{
    out->_cam_moveto_cycles.x = m->destination_x_in_build_area;
    out->_cam_moveto_cycles.z = m->destination_z_in_build_area;
    out->_cam_moveto_cycles.height = m->height;
    out->_cam_moveto_cycles.cycles = m->cycles;
    out->_cam_moveto_cycles.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_cycles.easing = m->easing_id;
    out->_cam_moveto_cycles.absolute = 0;
    return 1;
}

int
adapt_cam_moveto_cycles_v2(MsgCamMoveToCyclesV2 const* m, struct RevPacket* out)
{
    out->_cam_moveto_cycles.x = m->x;
    out->_cam_moveto_cycles.z = m->z;
    out->_cam_moveto_cycles.height = m->height;
    out->_cam_moveto_cycles.cycles = m->cycles;
    out->_cam_moveto_cycles.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_cycles.easing = m->easing_id;
    out->_cam_moveto_cycles.absolute = 1;
    return 1;
}

/* CAM_MOVETO_ARC: v1 (222-236) carries build-area tiles in one byte each, v2
 * and v3 world tiles; v3 adds a height relative to the previous move's. */
int
adapt_cam_moveto_arc_v1(MsgCamMoveToArcV1 const* m, struct RevPacket* out)
{
    out->_cam_moveto_arc.x = m->destination_x_in_build_area;
    out->_cam_moveto_arc.z = m->destination_z_in_build_area;
    out->_cam_moveto_arc.via_x = m->center_x_in_build_area;
    out->_cam_moveto_arc.via_z = m->center_z_in_build_area;
    out->_cam_moveto_arc.height = m->height;
    out->_cam_moveto_arc.cycles = m->cycles;
    out->_cam_moveto_arc.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_arc.easing = m->easing_id;
    out->_cam_moveto_arc.absolute = 0;
    return 1;
}

int
adapt_cam_moveto_arc_v2(MsgCamMoveToArcV2 const* m, struct RevPacket* out)
{
    out->_cam_moveto_arc.x = m->destination_x;
    out->_cam_moveto_arc.z = m->destination_z;
    out->_cam_moveto_arc.via_x = m->center_x;
    out->_cam_moveto_arc.via_z = m->center_z;
    out->_cam_moveto_arc.height = m->height;
    out->_cam_moveto_arc.cycles = m->cycles;
    out->_cam_moveto_arc.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_arc.easing = m->easing_id;
    out->_cam_moveto_arc.absolute = 1;
    return 1;
}

int
adapt_cam_moveto_arc_v3(MsgCamMoveToArcV3 const* m, struct RevPacket* out)
{
    out->_cam_moveto_arc.x = m->destination_x;
    out->_cam_moveto_arc.z = m->destination_z;
    out->_cam_moveto_arc.via_x = m->center_x;
    out->_cam_moveto_arc.via_z = m->center_z;
    out->_cam_moveto_arc.height = m->height;
    out->_cam_moveto_arc.cycles = m->cycles;
    out->_cam_moveto_arc.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_arc.easing = m->easing_id;
    out->_cam_moveto_arc.height_relative = m->height_relative;
    out->_cam_moveto_arc.absolute = 1;
    return 1;
}

int
adapt_cam_rotateto_coordinate(MsgCamRotateToCoordinate const* m, struct RevPacket* out)
{
    out->_cam_rotateto_coordinate.x = m->destination_x_in_build_area;
    out->_cam_rotateto_coordinate.z = m->destination_z_in_build_area;
    out->_cam_rotateto_coordinate.height = m->height;
    out->_cam_rotateto_coordinate.cycles = m->cycles;
    out->_cam_rotateto_coordinate.easing = m->easing_id;
    out->_cam_rotateto_coordinate.absolute = 0;
    return 1;
}

int
adapt_cam_rotateto_coordinate_v1(MsgCamRotateToCoordinateV1 const* m, struct RevPacket* out)
{
    out->_cam_rotateto_coordinate.x = m->destination_x_in_build_area;
    out->_cam_rotateto_coordinate.z = m->destination_z_in_build_area;
    out->_cam_rotateto_coordinate.height = m->height;
    out->_cam_rotateto_coordinate.cycles = m->cycles;
    out->_cam_rotateto_coordinate.easing = m->easing_id;
    out->_cam_rotateto_coordinate.absolute = 0;
    return 1;
}

int
adapt_cam_rotateto_coordinate_v2(MsgCamRotateToCoordinateV2 const* m, struct RevPacket* out)
{
    out->_cam_rotateto_coordinate.x = m->x;
    out->_cam_rotateto_coordinate.z = m->z;
    out->_cam_rotateto_coordinate.height = m->height;
    out->_cam_rotateto_coordinate.cycles = m->cycles;
    out->_cam_rotateto_coordinate.easing = m->easing_id;
    out->_cam_rotateto_coordinate.absolute = 1;
    return 1;
}

int
adapt_cam_target_v1(MsgCamTargetV1 const* m, struct RevPacket* out)
{
    out->_cam_target.type = m->type;
    out->_cam_target.index = m->index;
    return 1;
}

int
adapt_cam_target_v2(MsgCamTargetV2 const* m, struct RevPacket* out)
{
    out->_cam_target.type = m->type;
    out->_cam_target.index = m->index;
    return 1;
}

int
adapt_cam_target_v3(MsgCamTargetV3 const* m, struct RevPacket* out)
{
    out->_cam_target.type = m->type;
    out->_cam_target.index = m->target_index == 65535 ? -1 : m->target_index;
    return 1;
}

int
adapt_cam_target_v4(MsgCamTargetV4 const* m, struct RevPacket* out)
{
    out->_cam_target.type = m->type;
    out->_cam_target.index = m->value == 65535 ? -1 : m->value;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Social                                                              */
/* ------------------------------------------------------------------ */

int
adapt_message_private(MsgMessagePrivate const* m, struct RevPacket* out)
{
    struct PktMessagePrivate* p = &out->_message_private;

    memset(p, 0, sizeof(*p));
    if( !m->sender )
        return 0;
    p->from = (int64_t)strtobase37(m->sender);
    p->message_id = m->world_message_counter;
    p->staff_mod = m->chat_crown_type;
    p->text = unpack_text(m->huffman, m->huffman_length);
    return p->text != NULL;
}

int
adapt_message_clanchannel(MsgMessageClanchannel const* m, struct RevPacket* out)
{
    struct PktMessageClanChannel* p = &out->_message_clanchannel;
    size_t n;

    memset(p, 0, sizeof(*p));
    if( !m->name )
        return 0;
    n = strlen(m->name);
    if( n >= sizeof(p->sender) )
        return 0;
    memcpy(p->sender, m->name, n + 1);
    p->clan_type = (int8_t)m->clan_type;
    p->world = m->world_id;
    p->counter = m->world_message_counter;
    p->crown = m->chat_crown_type;
    p->text = unpack_text(m->huffman, m->huffman_length);
    return p->text != NULL;
}

int
adapt_message_clanchannel_system(MsgMessageClanchannelSystem const* m, struct RevPacket* out)
{
    struct PktMessageClanChannel* p = &out->_message_clanchannel;

    memset(p, 0, sizeof(*p));
    p->crown = -1;
    p->clan_type = (int8_t)m->clan_type;
    p->world = m->world_id;
    p->counter = m->world_message_counter;
    p->text = unpack_text(m->huffman, m->huffman_length);
    return p->text != NULL;
}

int
adapt_update_friendchat_channel_full_v2(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_update_friendchat_channel_singleuser(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_varclan(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_varclan_enable(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_varclan_disable(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_clanchannel_full(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_clanchannel_delta(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_clansettings_full(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_clansettings_delta(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

int
adapt_update_tradingpost_v1(uint8_t const* data, int len, struct RevPacket* out)
{
    return take_raw(data, len, out);
}

/* ------------------------------------------------------------------ */
/* Map rebuilds                                                        */
/* ------------------------------------------------------------------ */

/* A rebuild's XTEA keys into the canonical arm. */
static void
take_keys(int32_t const* keys, int count, struct PktMapRebuild* p)
{
    assert(count >= 0);
    assert(count <= PKT_MAP_REBUILD_ZONES);
    p->region_count = count;
    if( count == 0 )
        return;
    p->region_keys =
        (int32_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)count * 4 * sizeof(*p->region_keys));
    assert(p->region_keys);
    memcpy(p->region_keys, keys, (size_t)count * 4 * sizeof(*p->region_keys));
}

/* A region grid (-1: no zone) into the canonical arm (0). */
static void
take_grid(int32_t const* zones, struct PktMapRebuild* p)
{
    p->zones = ToriMem_Calloc(ToriMem_Net(), PKT_MAP_REBUILD_ZONES, sizeof(*p->zones));
    assert(p->zones);
    for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
        p->zones[i] = zones[i] == -1 ? 0 : zones[i];
}

/*
 * REBUILD_NORMAL (227-236), _V1 (237), _V2 (237+). The login variant puts the
 * player-info init block first and shares the opcode; the codec tells the two
 * apart by length (rsprot_tail_gpi_init) and hands the block back, which seeds
 * the player table here as REBUILD_LOGIN does in the deob.
 */
int
adapt_rebuild_normal(MsgRebuildNormal const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    if( m->gpi_init_length > 0 )
        proto_info_init(m->gpi_init, m->gpi_init_length);
    p->world_area = (int16_t)m->world_area;
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    take_keys(&m->keys[0][0], m->xtea_count, p);
    return 1;
}

int
adapt_rebuild_normal_v1(MsgRebuildNormalV1 const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    if( m->gpi_init_length > 0 )
        proto_info_init(m->gpi_init, m->gpi_init_length);
    p->world_area = (int16_t)m->world_area;
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    take_keys(&m->keys[0][0], m->xtea_count, p);
    return 1;
}

/* _V2 carries no keys: the client has them (the cache's xteas). */
int
adapt_rebuild_normal_v2(MsgRebuildNormalV2 const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    if( m->gpi_init_length > 0 )
        proto_info_init(m->gpi_init, m->gpi_init_length);
    p->world_area = (int16_t)m->world_area;
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    return 1;
}

/*
 * REBUILD_REGION (227-236) / _V1 (237): header, g2 XTEA count, the 4x13x13
 * descriptor grid, then the keys. _V2 (237+): header, g2 source-square count
 * (a loading hint), the grid, no keys.
 */
int
adapt_rebuild_region(MsgRebuildRegion const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    take_grid(m->zones, p);
    take_keys(m->keys, m->xtea_count, p);
    return 1;
}

int
adapt_rebuild_region_v1(MsgRebuildRegionV1 const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    take_grid(m->zones, p);
    take_keys(m->keys, m->xtea_count, p);
    return 1;
}

/* The distinct-mapsquare count is a loading hint this client does not use. */
int
adapt_rebuild_region_v2(MsgRebuildRegionV2 const* m, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    p->zonex = m->zone_x;
    p->zonez = m->zone_z;
    take_grid(m->zones, p);
    return 1;
}

/*
 * REBUILD_WORLDENTITY_V2 (227-228: a view index leads), _V3 (229-237: keys
 * trail the grid), _V4 (237+). The grid's size is the ACTIVE view's, which this
 * layer does not know, so the whole payload is kept with the codec that reads
 * it, for the exec layer to rerun once it knows (PktRebuildWev_DecodeZones).
 * Run with the size unknown, the V3 / V4 codecs hand the grid back unread
 * (grid_raw); V2's is a fixed 13x13.
 */
static int
take_wev(int version, RsprotCodecFn codec, int base_x, int base_z, uint8_t const* data, int len,
         struct RevPacket* out)
{
    struct PktRebuildWev* p = &out->_rebuild_wev;

    memset(p, 0, sizeof(*p));
    p->base_x = base_x;
    p->base_z = base_z;
    p->version = version;
    p->codec = codec;
    p->length = len;
    p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
    assert(p->data);
    memcpy(p->data, data, (size_t)len);
    return 1;
}

int
adapt_rebuild_worldentity_v2(
    MsgRebuildWorldEntityV2 const* m, RsprotCodecFn codec, uint8_t const* data, int len, struct RevPacket* out)
{
    return take_wev(2, codec, m->base_x, m->base_z, data, len, out);
}

int
adapt_rebuild_worldentity_v3(
    MsgRebuildWorldEntityV3 const* m, RsprotCodecFn codec, uint8_t const* data, int len, struct RevPacket* out)
{
    if( m->grid_raw_length <= 0 )
        return 0;
    return take_wev(3, codec, m->base_x, m->base_z, data, len, out);
}

int
adapt_rebuild_worldentity_v4(
    MsgRebuildWorldEntityV4 const* m, RsprotCodecFn codec, uint8_t const* data, int len, struct RevPacket* out)
{
    if( m->grid_raw_length <= 0 )
        return 0;
    return take_wev(4, codec, m->base_x, m->base_z, data, len, out);
}

int
PktRebuildWev_DecodeZones(
    struct PktRebuildWev const* p,
    int zones_x,
    int zones_z,
    int32_t* out_zones)
{
    static union
    {
        long double align;
        MsgRebuildWorldEntityV2 v2;
        MsgRebuildWorldEntityV3 v3;
        MsgRebuildWorldEntityV4 v4;
    } m;
    RsprotExec x;
    RsprotBuf b;
    int32_t const* zones;

    assert(p);
    assert(p->data);
    assert(p->codec);
    assert(out_zones);
    /* 13 is the PKT_MAP_REBUILD_ZONES stride (revpacket.h): a view wider than
     * the full-instance grid cannot be expressed in the array the world-load
     * path consumes. */
    assert(zones_x > 0);
    assert(zones_x <= 13);
    assert(zones_z > 0);
    assert(zones_z <= 13);

    memset(out_zones, 0, PKT_MAP_REBUILD_ZONES * sizeof(*out_zones));
    memset(&m, 0, sizeof(m));
    rsprot_buf_wrap_read(&b, p->data, p->length);
    rsprot_exec_decode(&x, &b);
    if( p->version == 2 )
    {
        /* V2's grid is the full 13x13 instance grid, already at the stride. */
        p->codec(&x, &m.v2);
        if( !rsprot_exec_ok(&x) || b.err || b.rpos != p->length )
            return 0;
        for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
            out_zones[i] = m.v2.zones[i] < 0 ? 0 : m.v2.zones[i];
        return 1;
    }
    if( p->version == 3 )
    {
        m.v3.size_x_zones = zones_x;
        m.v3.size_z_zones = zones_z;
        p->codec(&x, &m.v3);
        zones = m.v3.zones;
    }
    else
    {
        assert(p->version == 4);
        m.v4.size_x_zones = zones_x;
        m.v4.size_z_zones = zones_z;
        p->codec(&x, &m.v4);
        zones = m.v4.zones;
    }
    if( !rsprot_exec_ok(&x) || b.err || b.rpos != p->length )
        return 0;
    /* The codec reads [4 levels][zones_x][zones_z] in wire order (deob
     * class592.method12654); restride onto the 13x13 instance array, with
     * the wire's -1 (no zone) as the array's 0 (void). */
    for( int level = 0; level < 4; level++ )
        for( int zx = 0; zx < zones_x; zx++ )
            for( int zz = 0; zz < zones_z; zz++ )
            {
                int32_t z = zones[(level * zones_x + zx) * zones_z + zz];

                out_zones[level * 13 * 13 + zx * 13 + zz] = z < 0 ? 0 : z;
            }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Zone sub-packets                                                    */
/* ------------------------------------------------------------------ */

int
adapt_update_zone_partial_enclosed(
    MsgDesktopUpdateZonePartialEnclosed const* m, ProtoZoneSubFn zone, struct RevPacket* out)
{
    struct PktUpdateZoneEnclosed* enc = &out->_update_zone_enclosed;
    RsprotBuf subs;
    int cap = 16;

    enc->level = m->level;
    enc->base_z = m->zone_z;
    enc->base_x = m->zone_x;
    rsprot_buf_wrap_read(&subs, m->payload, m->payload_length);
    enc->count = 0;
    enc->entries = (struct PktZoneSubPacket*)ToriMem_Alloc(
        ToriMem_Net(), (size_t)cap * sizeof(*enc->entries));
    assert(enc->entries);
    for( ;; )
    {
        int got;

        if( enc->count == cap )
        {
            cap *= 2;
            enc->entries = (struct PktZoneSubPacket*)ToriMem_Realloc(
                ToriMem_Net(), enc->entries, (size_t)cap * sizeof(*enc->entries));
            assert(enc->entries);
        }
        got = zone(&subs, &enc->entries[enc->count]);
        if( got < 0 )
            break;
        /* Records carry no length: past an unreadable one nothing can be
         * found, so the walk keeps what it has. */
        if( got == 0 )
            break;
        if( got == 1 )
            enc->count++;
    }
    return 1;
}

void
proto_adapt_zone_to_packet(struct PktZoneSubPacket const* sub, struct RevPacket* out)
{
    assert(sub);
    assert(out);
    out->packet_type = sub->name;
    switch( sub->name )
    {
    case PKT_NAME_LOC_ADD_CHANGE: out->_loc_add_change = sub->_loc_add_change; break;
    case PKT_NAME_LOC_DEL: out->_loc_del = sub->_loc_del; break;
    case PKT_NAME_LOC_ANIM: out->_loc_anim = sub->_loc_anim; break;
    case PKT_NAME_LOC_MERGE: out->_loc_merge = sub->_loc_merge; break;
    case PKT_NAME_OBJ_ADD: out->_obj_add = sub->_obj_add; break;
    case PKT_NAME_OBJ_DEL: out->_obj_del = sub->_obj_del; break;
    case PKT_NAME_OBJ_REVEAL: out->_obj_reveal = sub->_obj_reveal; break;
    case PKT_NAME_OBJ_COUNT: out->_obj_count = sub->_obj_count; break;
    case PKT_NAME_MAP_ANIM: out->_map_anim = sub->_map_anim; break;
    case PKT_NAME_SOUND_AREA: out->_sound_area = sub->_sound_area; break;
    case PKT_NAME_SCRIPTEDPROJ_ADD: out->_scriptedproj_add = sub->_scriptedproj_add; break;
    case PKT_NAME_MAP_PROJANIM: out->_map_projanim = sub->_map_projanim; break;
    default: assert(!"a zone sub-packet with no top-level arm"); break;
    }
}

int
adapt_zone_loc_add_change(MsgLocAddChange const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_ADD_CHANGE;
    out->_loc_add_change.info = m->loc_properties_packed;
    out->_loc_add_change.pos = m->coord_in_zone_packed;
    out->_loc_add_change.loc_id = m->id;
    out->_loc_add_change.op_flags = m->op_flags;
    return 1;
}

int
adapt_zone_loc_add_change_v1(MsgLocAddChangeV1 const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_ADD_CHANGE;
    out->_loc_add_change.info = m->loc_properties_packed;
    out->_loc_add_change.pos = m->coord_in_zone_packed;
    out->_loc_add_change.loc_id = m->id;
    out->_loc_add_change.op_flags = m->op_flags;
    return 1;
}

void
prep_loc_add_change_v2(MsgLocAddChangeV2* m)
{
    /* One-byte op count, so 256 entries cannot overflow. */
    static Rsprot_MsgLocAddChangeV2_opsElem ops[256];

    m->ops = ops;
}

int
adapt_zone_loc_add_change_v2(MsgLocAddChangeV2 const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_ADD_CHANGE;
    out->_loc_add_change.info = m->loc_properties_packed;
    out->_loc_add_change.pos = m->coord_in_zone_packed;
    out->_loc_add_change.loc_id = m->id;
    out->_loc_add_change.op_flags = m->op_flags;
    /* `key` is the one-based op number (the codec's XFORM -1). */
    for( int i = 0; i < m->ops_count; i++ )
    {
        int slot = m->ops[i].key - 1;

        if( slot < 0 || slot >= 5 || !m->ops[i].value )
            continue;
        snprintf(out->_loc_add_change.ops[slot], sizeof(out->_loc_add_change.ops[slot]), "%s",
                 m->ops[i].value);
    }
    return 1;
}

int
adapt_zone_loc_del(MsgLocDel const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_DEL;
    out->_loc_del.info = m->loc_properties_packed;
    out->_loc_del.pos = m->coord_in_zone_packed;
    return 1;
}

int
adapt_zone_loc_anim(MsgLocAnim const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_ANIM;
    out->_loc_anim.info = m->loc_properties_packed;
    out->_loc_anim.pos = m->coord_in_zone_packed;
    out->_loc_anim.seq_id = m->id;
    return 1;
}

int
adapt_zone_loc_merge(MsgLocMerge const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_MERGE;
    out->_loc_merge.west = (int8_t)m->min_x;
    out->_loc_merge.pid = m->index;
    out->_loc_merge.info = m->loc_properties_packed;
    out->_loc_merge.south = (int8_t)m->min_z;
    out->_loc_merge.loc_id = m->id;
    out->_loc_merge.end = m->end;
    out->_loc_merge.start = m->start;
    out->_loc_merge.east = (int8_t)m->max_x;
    out->_loc_merge.pos = m->coord_in_zone_packed;
    out->_loc_merge.north = (int8_t)m->max_z;
    return 1;
}

int
adapt_zone_map_anim(MsgMapAnim const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_MAP_ANIM;
    out->_map_anim.height = m->height;
    out->_map_anim.id = m->id;
    out->_map_anim.delay = m->delay;
    out->_map_anim.pos = m->coord_in_zone_packed;
    return 1;
}

int
adapt_zone_sound_area(MsgSoundArea const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_SOUND_AREA;
    out->_sound_area.pos = m->coord_in_zone_packed;
    out->_sound_area.radius = m->range;
    out->_sound_area.inner = m->drop_off_range;
    out->_sound_area.loops = m->loops;
    out->_sound_area.id = m->id;
    out->_sound_area.delay = m->delay;
    return 1;
}

/*
 * 227-231's forms: a signed per-axis offset from the source tile and one-byte
 * heights at the classic scale -- what PktMapProjAnim was written for. Indices
 * keep V2's 24-bit signed encoding.
 */
#define PROJANIM_OLD                                                                               \
    {                                                                                              \
        struct PktMapProjAnim* p = &out->_map_projanim;                                            \
        out->name = PKT_NAME_MAP_PROJANIM;                                                         \
        p->start_delay = m->start_time;                                                            \
        p->pos = m->coord_in_zone_packed;                                                          \
        p->arc = m->progress;                                                                      \
        p->spotanim = m->id;                                                                       \
        p->end_delay = m->end_time;                                                                \
        p->dst_height = m->end_height;                                                             \
        p->peak = m->angle;                                                                        \
        p->src_height = m->start_height;                                                           \
        p->target = sign24(m->target_index);                                                       \
        p->source = sign24(m->source_index);                                                       \
        p->dx_offset = (int8_t)m->delta_x;                                                         \
        p->dz_offset = (int8_t)m->delta_z;                                                         \
        return 1;                                                                                  \
    }

int
adapt_zone_map_projanim(MsgMapProjAnim const* m, struct PktZoneSubPacket* out)
PROJANIM_OLD

int
adapt_zone_map_projanim_v1(MsgMapProjAnimV1 const* m, struct PktZoneSubPacket* out)
PROJANIM_OLD

/*
 * 231+: an absolute destination and heights four times the classic scale
 * (World_ProjectileSpawn still applies the classic *4, so they are divided
 * back).
 */
int
adapt_zone_map_projanim_v2(MsgMapProjAnimV2 const* m, struct PktZoneSubPacket* out)
{
    struct PktMapProjAnim* p = &out->_map_projanim;

    out->name = PKT_NAME_MAP_PROJANIM;
    p->start_delay = m->start_time;
    p->pos = m->coord_in_zone_packed;
    p->arc = m->progress;
    p->spotanim = m->id;
    p->end_delay = m->end_time;
    p->dst_height = m->end_height / 4;
    p->peak = m->angle;
    p->src_height = m->start_height / 4;
    p->target = sign24(m->target_index);
    p->source = sign24(m->source_index);
    p->dst_abs = 1;
    p->dst_abs_level = (m->end_packed >> 28) & 0x3;
    p->dst_abs_x = (m->end_packed >> 14) & 0x3fff;
    p->dst_abs_z = m->end_packed & 0x3fff;
    return 1;
}

int
adapt_zone_scriptedproj_add(MsgScriptedProjAdd const* m, struct PktZoneSubPacket* out)
{
    struct PktScriptedProjAdd* p = &out->_scriptedproj_add;

    out->name = PKT_NAME_SCRIPTEDPROJ_ADD;
    p->pos = m->coord_in_zone_packed;
    p->source = sign24(m->source_index);
    p->source_offset_x = (int16_t)m->source_offset_x;
    p->source_offset_z = (int16_t)m->source_offset_z;
    p->source_height = (int16_t)m->source_height;
    p->target = sign24(m->target_index);
    p->target_level = (m->target_coord_packed >> 28) & 0x3;
    p->target_x = (m->target_coord_packed >> 14) & 0x3fff;
    p->target_z = m->target_coord_packed & 0x3fff;
    p->target_offset_x = (int16_t)m->target_offset_x;
    p->target_offset_z = (int16_t)m->target_offset_z;
    p->target_height = (int16_t)m->target_height;
    p->spotanim = m->id;
    p->slot = m->slot;
    p->start_delay = m->start_time;
    p->end_delay = m->end_time;
    p->curve_h = m->curve_script_h;
    p->curve_a = m->curve_script_a;
    p->curve_t = m->curve_script_t;
    return 1;
}

int
adapt_zone_obj_enabled_ops(MsgObjEnabledOps const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_OBJ_REVEAL;
    out->_obj_reveal.pos = m->coord_in_zone_packed;
    out->_obj_reveal.obj_id = m->id;
    out->_obj_reveal.receiver = -1;
    return 1;
}

int
adapt_zone_obj_del(MsgObjDel const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_OBJ_DEL;
    out->_obj_del.obj_id = m->id;
    out->_obj_del.pos = m->coord_in_zone_packed;
    return 1;
}

int
adapt_zone_obj_count(MsgObjCount const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_OBJ_COUNT;
    out->_obj_count.old_count = m->old_quantity;
    out->_obj_count.pos = m->coord_in_zone_packed;
    out->_obj_count.new_count = m->new_quantity;
    out->_obj_count.obj_id = m->id;
    return 1;
}

int
adapt_zone_obj_add(MsgObjAdd const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_OBJ_ADD;
    out->_obj_add.pos = m->coord_in_zone_packed;
    out->_obj_add.obj_id = m->id;
    out->_obj_add.count = m->quantity;
    out->_obj_add.time_until_public = m->time_until_public;
    out->_obj_add.time_until_despawn = m->time_until_despawn;
    out->_obj_add.ownership_type = m->ownership_type;
    out->_obj_add.never_becomes_public = m->never_becomes_public;
    return 1;
}

/* ------------------------------------------------------------------ */
/* One field to one field                                              */
/* ------------------------------------------------------------------ */

int
adapt_varp_small(MsgVarpSmall const* m, struct RevPacket* out)
{
    out->_varp_small.variable = m->id;
    /*
     * Sign-extend. The wire field is one byte and the canonical struct's
     * comment already says "g1b signed byte": varps carry negative values and
     * reading this unsigned turns -1 into 255, which is a valid different
     * setting rather than an error.
     */
    out->_varp_small.value = (int8_t)m->value;
    return 1;
}

int
adapt_varp_large(MsgVarpLarge const* m, struct RevPacket* out)
{
    out->_varp_large.variable = m->id;
    out->_varp_large.value = m->value;
    return 1;
}

int
adapt_update_runenergy(MsgUpdateRunEnergy const* m, struct RevPacket* out)
{
    /*
     * UNIT CHANGE, and it is the whole content of this adapter.
     *
     * The wire carries HUNDREDTHS of a percent in two bytes; the canonical
     * `run_energy` field is a percent, 0-100, and osrs230_parse.c has always
     * divided by 100 on the way in. Handing the raw value over instead reads as
     * a working decode -- the bytes are right, the packet frames, nothing
     * errors -- and pegs the run-energy orb at 100x.
     *
     * Caught by src/net/rev/test/rsprot_bridge_test.c, which is the argument
     * for that test existing: a codec can be byte-perfect and the ADAPTER still
     * wrong, and only running both parsers over the same payload sees it.
     */
    out->_update_run_energy.run_energy = m->runenergy / 100;
    out->_update_run_energy.run_energy_raw = m->runenergy;
    return 1;
}

int
adapt_if_opensub(MsgIfOpenSub const* m, struct RevPacket* out)
{
    out->_if_opensub.target_uid = m->destination_combined_id;
    out->_if_opensub.interface_id = m->interface_id;
    out->_if_opensub.type = m->type;
    return 1;
}

int
adapt_if_closesub(MsgIfCloseSub const* m, struct RevPacket* out)
{
    out->_if_closesub.target_uid = m->combined_id;
    return 1;
}

int
adapt_if_settext(MsgIfSetText const* m, struct RevPacket* out)
{
    out->_if_settext.component_id = m->combined_id;
    out->_if_settext.text = dup_borrowed(m->text);
    if( !out->_if_settext.text )
        return 0;
    return 1;
}

int
adapt_if_setanim(MsgIfSetAnim const* m, struct RevPacket* out)
{
    out->_if_setanim.component_id = m->combined_id;
    out->_if_setanim.anim_id = m->anim;
    return 1;
}

int
adapt_message_game(MsgMessageGame const* m, struct RevPacket* out)
{
    out->_message_game.type = m->type;
    /* `name` rides on its own present flag: a game message has no speaker, and
     * a zero-length name is not the same thing as no name. */
    if( m->name_present && m->name )
    {
        out->_message_game.name = dup_borrowed(m->name);
        if( !out->_message_game.name )
            return 0;
    }
    out->_message_game.text = dup_borrowed(m->message);
    if( !out->_message_game.text )
        return 0;
    return 1;
}

int
adapt_if_opentop(MsgIfOpenTop const* m, struct RevPacket* out)
{
    out->_if_opentop.interface_id = m->interface_id;
    return 1;
}

int
adapt_if_clearinv(MsgIfClearInv const* m, struct RevPacket* out)
{
    out->_if_clearinv.component_id = m->combined_id;
    return 1;
}

int
adapt_if_movesub(MsgIfMoveSub const* m, struct RevPacket* out)
{
    out->_if_movesub.dest_uid = m->destination_combined_id;
    out->_if_movesub.source_uid = m->source_combined_id;
    return 1;
}

int
adapt_if_setangle(MsgIfSetAngle const* m, struct RevPacket* out)
{
    out->_if_setangle.component_id = m->combined_id;
    out->_if_setangle.zoom = m->zoom;
    out->_if_setangle.angle_x = m->angle_x;
    out->_if_setangle.angle_y = m->angle_y;
    return 1;
}

int
adapt_if_setcolour(MsgIfSetColour const* m, struct RevPacket* out)
{
    out->_if_setcolour.component_id = m->combined_id;
    out->_if_setcolour.colour = m->colour15_bit_packed;
    return 1;
}

int
adapt_if_setmodel_v2(MsgIfSetModelV2 const* m, struct RevPacket* out)
{
    out->_if_setmodel.component_id = m->combined_id;
    out->_if_setmodel.model_id = m->model;
    return 1;
}

int
adapt_if_setnpchead(MsgIfSetNpcHead const* m, struct RevPacket* out)
{
    out->_if_setnpchead.component_id = m->combined_id;
    out->_if_setnpchead.npc_id = m->npc;
    return 1;
}

int
adapt_if_setnpchead_active(MsgIfSetNpcHeadActive const* m, struct RevPacket* out)
{
    out->_if_setnpchead_active.component_id = m->combined_id;
    out->_if_setnpchead_active.index = m->index;
    return 1;
}

/*
 * `count` on the wire is the model ZOOM, not a quantity. RSProt names the field
 * after the classic obj-count slot it reuses; the canonical struct names it for
 * what the client does with it.
 */
int
adapt_if_setobject(MsgIfSetObject const* m, struct RevPacket* out)
{
    out->_if_setobject.component_id = m->combined_id;
    out->_if_setobject.obj_id = m->obj;
    out->_if_setobject.zoom = m->count;
    return 1;
}

int
adapt_if_setplayerhead(MsgIfSetPlayerHead const* m, struct RevPacket* out)
{
    out->_if_setplayerhead.component_id = m->combined_id;
    return 1;
}

int
adapt_if_setposition(MsgIfSetPosition const* m, struct RevPacket* out)
{
    out->_if_setposition.component_id = m->combined_id;
    /* Signed, for the same reason as IF_SETROTATESPEED: a position offset is a
     * displacement and goes both ways. */
    out->_if_setposition.x = (int16_t)m->x;
    /* RSProt's `y` is a screen offset on the vertical axis; the canonical
     * struct calls that axis z, matching the rest of this tree's 2D naming. */
    out->_if_setposition.z = (int16_t)m->y;
    return 1;
}

int
adapt_if_setrotatespeed(MsgIfSetRotateSpeed const* m, struct RevPacket* out)
{
    out->_if_setrotatespeed.component_id = m->combined_id;
    /*
     * SIGNED. A rotate speed is a per-tick delta and a negative one spins the
     * model the other way; read unsigned, -1 becomes 65535 and the model
     * whips around instead of drifting back. The codec hands back the raw
     * 16-bit field, so the sign is this adapter's business.
     *
     * Caught by the differential test — it was the one packet of eighteen
     * where the mapping was right and the type was not.
     */
    out->_if_setrotatespeed.x_speed = (int16_t)m->x_speed;
    out->_if_setrotatespeed.y_speed = (int16_t)m->y_speed;
    return 1;
}

int
adapt_if_setscrollpos(MsgIfSetScrollPos const* m, struct RevPacket* out)
{
    out->_if_setscrollpos.component_id = m->combined_id;
    out->_if_setscrollpos.pos = m->scroll_pos;
    return 1;
}

/*
 * V2 carries TWO 32-bit masks where the older packet carried one, and the
 * button ops MOVED between them: events1 no longer includes ifbutton, and
 * events2 holds one bit per button. `events` is the classic single mask the
 * menu and input code still read, reconstructed exactly as the 239 client does.
 * That reconstruction is client behaviour, not wire layout, which is why it
 * lives here and not in the codec.
 */
int
adapt_if_setevents_v2(MsgIfSetEventsV2 const* m, struct RevPacket* out)
{
    struct PktIfSetEvents* p = &out->_if_setevents;

    p->component_id = m->combined_id;
    p->from = (int16_t)m->start;
    p->to = (int16_t)m->end;
    p->events1 = (uint32_t)m->events1;
    p->events2 = (uint32_t)m->events2;
    p->events = (int)(p->events1 | ((p->events2 & UINT32_C(0x3ff)) << 1));
    return 1;
}

int
adapt_update_zone_full_follows(MsgUpdateZoneFullFollows const* m, struct RevPacket* out)
{
    out->_update_zone_full_follows.base_x = m->zone_x;
    out->_update_zone_full_follows.base_z = m->zone_z;
    out->_update_zone_full_follows.level = m->level;
    return 1;
}

int
adapt_update_zone_partial_follows(MsgUpdateZonePartialFollows const* m, struct RevPacket* out)
{
    out->_update_zone_partial_follows.base_x = m->zone_x;
    out->_update_zone_partial_follows.base_z = m->zone_z;
    out->_update_zone_partial_follows.level = m->level;
    return 1;
}

/*
 * `invisibleBoostedLevel` is dropped: the canonical struct carries one level,
 * and the client wants the real one. The hand-written arm dropped the same
 * field, so this is not a regression -- but a boosted level the UI could show
 * is being thrown away at both revisions, which is worth someone deciding
 * rather than inheriting.
 */
/* 221-225's layout of the same message (UPDATE_STAT before RSProt's _V2). */
int
adapt_update_stat(MsgUpdateStat const* m, struct RevPacket* out)
{
    out->_update_stat.stat = m->stat;
    out->_update_stat.level = m->current_level;
    out->_update_stat.xp = m->experience;
    return 1;
}

int
adapt_update_stat_v2(MsgUpdateStatV2 const* m, struct RevPacket* out)
{
    out->_update_stat.stat = m->stat;
    out->_update_stat.level = m->current_level;
    out->_update_stat.xp = m->experience;
    return 1;
}

int
adapt_synth_sound(MsgSynthSound const* m, struct RevPacket* out)
{
    out->_synth_sound.id = m->id;
    out->_synth_sound.loops = m->loops;
    out->_synth_sound.delay = m->delay;
    return 1;
}

/* Only the id survives: the canonical struct has no home for the four fade
 * timings V2 added, and the client's music player does not use them. */
int
adapt_midi_song_v2(MsgMidiSongV2 const* m, struct RevPacket* out)
{
    out->_midi_song.id = m->id;
    /* Speeds are client cycles; the player wants milliseconds. */
    out->_midi_song.fade_out_ms = m->fade_out_speed * 20;
    out->_midi_song.fade_in_ms = m->fade_in_speed * 20;
    return 1;
}

/*
 * MIDI_JINGLE. The wire layout genuinely differs by revision -- rev 239 is a
 * 5-byte `p3 length_in_millis / p2Alt3 id`, where LostCity's is a 4-byte
 * `g2 id / g2 delay`. The hand-written reader this replaced used the 4-byte
 * shape for every revision, which desynchronised the very next packet against
 * a real 239 server.
 *
 * `delay` carries the millisecond length; the canonical struct has no separate
 * field for it and the client does not act on it once decoded either way
 * (rs_audio.c's RS_Audio_Jingle ignores its own length argument).
 */
int
adapt_midi_jingle(MsgMidiJingle const* m, struct RevPacket* out)
{
    out->_midi_jingle.id = m->id;
    out->_midi_jingle.delay = m->length_in_millis;
    return 1;
}

int
adapt_friendlist_loaded(MsgFriendListLoaded const* m, struct RevPacket* out)
{
    (void)m;
    /* 1, matching the hand-written arm. An earlier version of this adapter
     * said 2 and nothing caught it: FRIENDLIST_LOADED is zero-payload, so it
     * was never added to the differential test's generic cases. It is now. */
    out->_friendlist_loaded.status = 1;
    return 1;
}

int
adapt_cam_shake(MsgCamShake const* m, struct RevPacket* out)
{
    /*
     * MsgCamShake holds EIGHT fields for a four-byte packet, because the struct
     * is the union across versions and RSProt renamed all four between them:
     * older revisions call them type/randomAmount/sineAmount/sineFrequency and
     * 239 calls them axis/random/amplitude/rate. Mapping the wrong set compiles
     * fine and reads four zeroes.
     *
     * `grep rev239 3rd/rsprot/packets/cam_shake.h` says which codec 239 picks,
     * and that codec names the fields it uses. The differential test caught
     * this one; the union is exactly where that mistake is easy to make.
     */
    out->_cam_shake.axis = m->axis;
    out->_cam_shake.amplitude = m->random;
    out->_cam_shake.frequency = m->amplitude;
    out->_cam_shake.speed = m->rate;
    return 1;
}

/*
 * `chat_private_mode` is stated as ABSENT, not as 0.
 *
 * The rev-239 packet carries only the public and trade filters -- private chat
 * moved to its own CHAT_FILTER_SETTINGS_PRIVATECHAT packet (opcode 5, whose
 * handler at client.java:3830 is the only writer of Statics.field5072, the
 * object CS2 5005 chat_getfilter_private reads) -- so there is no value on the
 * wire for it. This used to say 0, and the hand-written arm in
 * `osrs239/osrs239_parse.c` said 0 beside it; both now say -1, because a
 * fabricated 0 is not "no value", it is the value "Show all" written over
 * whatever the player chose.
 *
 * What that cost: the filter bar read "Private On" the instant after the
 * player picked Show friends, because this echo landed on top of the choice;
 * and their NEXT filter change of any kind sent the fabricated 0 back to the
 * server, through chat_set_filter_184's
 * `chat_setfilter(chat_getfilter_public, chat_getfilter_private, $new)`,
 * destroying the persisted Private setting Board-friend reads. The executor
 * (`src/game/rs_gameproto_exec.c`, PKT_NAME_CHAT_FILTER_SETTINGS) applies the
 * private mode only when it is non-negative, so -1 leaves the client's own copy
 * standing -- the same absent convention the zone headers use for a plane a
 * revision does not send.
 */
int
adapt_chat_filter_settings(MsgChatFilterSettings const* m, struct RevPacket* out)
{
    out->_chat_filter_settings.chat_public_mode = m->public_chat_filter;
    out->_chat_filter_settings.chat_trade_mode = m->trade_chat_filter;
    out->_chat_filter_settings.chat_private_mode = -1;
    return 1;
}

int
adapt_minimap_toggle(MsgMinimapToggle const* m, struct RevPacket* out)
{
    out->_minimap_toggle.state = m->minimap_state;
    return 1;
}

int
adapt_cam_unlock(MsgCamUnlock const* m, struct RevPacket* out)
{
    out->_cam_unlock.unlock = m->unlock == 1;
    return 1;
}

int
adapt_hidenpcops(MsgHideNpcOps const* m, struct RevPacket* out)
{
    out->_hide_ops.hidden = m->hidden;
    return 1;
}

int
adapt_hidelocops(MsgHideLocOps const* m, struct RevPacket* out)
{
    out->_hide_ops.hidden = m->hidden;
    return 1;
}

int
adapt_hideobjops(MsgHideObjOps const* m, struct RevPacket* out)
{
    out->_hide_ops.hidden = m->hidden;
    return 1;
}

int
adapt_if_sethide(MsgIfSetHide const* m, struct RevPacket* out)
{
    out->_if_sethide.component_id = m->combined_id;
    out->_if_sethide.hide = m->hidden;
    return 1;
}

/*
 * Four packets, one canonical arm, and `index` is the discriminator.
 *
 * PktIfSetPlayerModel holds (component_id, index, value). BASECOLOUR names a
 * colour slot and so carries a real index; the other three set a single
 * property and use -1 to say "no slot". That -1 is load-bearing -- it is how
 * the consumer tells a base-colour write from a bodytype write -- so each
 * adapter states it rather than leaving the field at whatever memset left.
 */
int
adapt_if_setplayermodel_basecolour(MsgIfSetPlayerModelBaseColour const* m, struct RevPacket* out)
{
    out->_if_setplayermodel.component_id = m->combined_id;
    out->_if_setplayermodel.index = m->index;
    out->_if_setplayermodel.value = m->colour;
    return 1;
}

int
adapt_if_setplayermodel_bodytype(MsgIfSetPlayerModelBodyType const* m, struct RevPacket* out)
{
    out->_if_setplayermodel.component_id = m->combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = m->body_type;
    return 1;
}

int
adapt_if_setplayermodel_obj(MsgIfSetPlayerModelObj const* m, struct RevPacket* out)
{
    out->_if_setplayermodel.component_id = m->combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = m->obj;
    return 1;
}

/* `copy_objs` is a flag; the canonical arm carries it in `value` as 0/1, which
 * is what the hand-written arm's `!= 0` produced. */
int
adapt_if_setplayermodel_self(MsgIfSetPlayerModelSelf const* m, struct RevPacket* out)
{
    out->_if_setplayermodel.component_id = m->combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = m->copy_objs != 0;
    return 1;
}

int
adapt_set_npc_update_origin(MsgSetNpcUpdateOrigin const* m, struct RevPacket* out)
{
    out->_set_npc_update_origin.x = m->origin_x;
    out->_set_npc_update_origin.z = m->origin_z;
    return 1;
}

/*
 * Four bytes carrying an ABSOLUTE packed CoordGrid, where the classic packet
 * sends two scene-local tile bytes with 255,255 meaning "clear". Three of the
 * four canonical fields are unpacked here rather than read off the wire:
 *
 *   - x/z come out of the packed coord;
 *   - `clear` has no sentinel at this revision -- a cleared flag is coord 0;
 *   - `absolute` tells the executor to subtract the scene origin, which this
 *     layer does not know.
 *
 * RSProt calls this SET_MAP_FLAG_V2; the client's canonical name is
 * UNSET_MAP_FLAG, and src/torirsserver/wire/osrs239/packetin.h has recorded the mapping
 * all along. It was reported as "not vendored in RSProt" twice before anyone
 * read that row.
 */
int
adapt_set_map_flag_v2(MsgSetMapFlagV2 const* m, struct RevPacket* out)
{
    out->_set_map_flag.x = (m->coord_grid_packed >> 14) & 0x3fff;
    out->_set_map_flag.z = m->coord_grid_packed & 0x3fff;
    out->_set_map_flag.clear = m->coord_grid_packed == 0;
    out->_set_map_flag.absolute = 1;
    return 1;
}

/*
 * CAM_LOOKAT_V3 / CAM_MOVETO_V3: the only camera prots left at 241, where the
 * V2 pair was deleted. One extra byte, `heightRelative` -- per RSProx, the
 * height is relative to the previous camera move's height rather than the
 * ground. The canonical struct has no home for it and is DROPPED here, so a
 * relative move applies its height as an absolute one; this server never sets
 * it.
 */
int
adapt_cam_lookat_v3(MsgCamLookAtV3 const* m, struct RevPacket* out)
{
    out->_cam_lookat.local_x = m->x;
    out->_cam_lookat.local_z = m->z;
    out->_cam_lookat.height = m->height;
    out->_cam_lookat.rate = m->rate;
    out->_cam_lookat.rate2 = m->rate2;
    out->_cam_lookat.absolute = 1;
    return 1;
}

int
adapt_cam_moveto_v3(MsgCamMoveToV3 const* m, struct RevPacket* out)
{
    out->_cam_moveto.local_x = m->x;
    out->_cam_moveto.local_z = m->z;
    out->_cam_moveto.height = m->height;
    out->_cam_moveto.rate = m->rate;
    out->_cam_moveto.rate2 = m->rate2;
    out->_cam_moveto.absolute = 1;
    return 1;
}

/* The eased camera pair. Both carry absolute world tiles, like the V3 pair. */
int
adapt_cam_moveto_cycles_v3(MsgCamMoveToCyclesV3 const* m, struct RevPacket* out)
{
    out->_cam_moveto_cycles.x = m->x;
    out->_cam_moveto_cycles.z = m->z;
    out->_cam_moveto_cycles.height = m->height;
    out->_cam_moveto_cycles.cycles = m->cycles;
    out->_cam_moveto_cycles.ignore_terrain = m->ignore_terrain;
    out->_cam_moveto_cycles.easing = m->easing_id;
    out->_cam_moveto_cycles.height_relative = m->height_relative;
    out->_cam_moveto_cycles.absolute = 1;
    return 1;
}

int
adapt_cam_rotateto_coordinate_v3(MsgCamRotateToCoordinateV3 const* m, struct RevPacket* out)
{
    out->_cam_rotateto_coordinate.x = m->x;
    out->_cam_rotateto_coordinate.z = m->z;
    out->_cam_rotateto_coordinate.height = m->height;
    out->_cam_rotateto_coordinate.cycles = m->cycles;
    out->_cam_rotateto_coordinate.easing = m->easing_id;
    out->_cam_rotateto_coordinate.height_relative = m->height_relative;
    out->_cam_rotateto_coordinate.track_target = m->track_target;
    out->_cam_rotateto_coordinate.absolute = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* LostCity (rs245.2, rs254, rs289)                                    */
/* ------------------------------------------------------------------ */

/*
 * The 2004 protocol's messages, as the client's own reader mapped them
 * (gameproto_parse.c, which these replaced: git show 448b3cdf0) -- field for
 * field, so a LostCity revision decodes into exactly the arm it always did.
 */

/* A newline-terminated string the codec borrowed: owned, NUL-terminated. */
static char*
dup_bytes(uint8_t const* bytes, int32_t len)
{
    char* out;

    assert(len >= 0);
    out = (char*)ToriMem_Alloc(ToriMem_Net(), (size_t)len + 1);
    assert(out);
    if( len > 0 )
        memcpy(out, bytes, (size_t)len);
    out[len] = '\0';
    return out;
}

int
adapt_if_openchat(MsgIfOpenchat const* m, struct RevPacket* out)
{
    out->_if_openchat.component_id = m->combined_id;
    return 1;
}

int
adapt_if_openmain(MsgIfOpenmain const* m, struct RevPacket* out)
{
    out->_if_openmain.component_id = m->combined_id;
    return 1;
}

int
adapt_if_openside(MsgIfOpenside const* m, struct RevPacket* out)
{
    out->_if_openside.component_id = m->combined_id;
    return 1;
}

int
adapt_if_openoverlay(MsgIfOpenoverlay const* m, struct RevPacket* out)
{
    out->_if_openoverlay.component_id = m->combined_id;
    return 1;
}

int
adapt_if_openmain_side(MsgIfOpenmainSide const* m, struct RevPacket* out)
{
    out->_if_openmain_side.main_component_id = m->main_combined_id;
    out->_if_openmain_side.side_component_id = m->side_combined_id;
    return 1;
}

int
adapt_if_settab(MsgIfSettab const* m, struct RevPacket* out)
{
    out->_if_settab.component_id = m->combined_id;
    out->_if_settab.tab_id = m->tab;
    return 1;
}

int
adapt_if_settab_active(MsgIfSettabActive const* m, struct RevPacket* out)
{
    out->_if_settab_active.tab_id = m->tab;
    return 1;
}

int
adapt_if_settext_rs(MsgIfSettextRs const* m, struct RevPacket* out)
{
    out->_if_settext.component_id = m->combined_id;
    out->_if_settext.text = dup_bytes(m->text, m->text_length);
    return 1;
}

int
adapt_tut_flash(MsgTutFlash const* m, struct RevPacket* out)
{
    out->_tut_flash.tab = m->tab;
    return 1;
}

int
adapt_tut_open(MsgTutOpen const* m, struct RevPacket* out)
{
    out->_tut_open.component_id = m->combined_id;
    return 1;
}

int
adapt_update_pid(MsgUpdatePid const* m, struct RevPacket* out)
{
    out->_update_pid.local_player_index = m->index;
    out->_update_pid.unused = m->members;
    return 1;
}

int
adapt_set_multiway(MsgSetMultiway const* m, struct RevPacket* out)
{
    out->_set_multiway.multiway = m->multiway;
    return 1;
}

int
adapt_last_login_info(MsgLastLoginInfo const* m, struct RevPacket* out)
{
    out->_last_login_info.last_ip = m->last_ip;
    out->_last_login_info.days_since_login = m->days_since_login;
    out->_last_login_info.days_since_recovery = m->days_since_recovery;
    out->_last_login_info.unread_messages = m->unread_messages;
    out->_last_login_info.member_warning = m->member_warning;
    return 1;
}

/* No payload: it always clears (osrs230's SET_MAP_FLAG says 255,255). */
int
adapt_unset_map_flag(struct RevPacket* out)
{
    out->_set_map_flag.x = 255;
    out->_set_map_flag.z = 255;
    out->_set_map_flag.clear = 1;
    return 1;
}

int
adapt_update_stat_rs(MsgUpdateStatRs const* m, struct RevPacket* out)
{
    out->_update_stat.stat = m->stat;
    out->_update_stat.xp = m->experience;
    out->_update_stat.level = m->level;
    return 1;
}

/* A whole percent; the raw field is OSRS's hundredths. */
int
adapt_update_runenergy_rs(MsgUpdateRunenergyRs const* m, struct RevPacket* out)
{
    out->_update_run_energy.run_energy = m->percent;
    out->_update_run_energy.run_energy_raw = m->percent * 100;
    return 1;
}

int
adapt_chat_filter_settings_rs(MsgChatFilterSettingsRs const* m, struct RevPacket* out)
{
    out->_chat_filter_settings.chat_public_mode = m->public_chat_filter;
    out->_chat_filter_settings.chat_private_mode = m->private_chat_filter;
    out->_chat_filter_settings.chat_trade_mode = m->trade_chat_filter;
    return 1;
}

int
adapt_message_game_rs(MsgMessageGameRs const* m, struct RevPacket* out)
{
    out->_message_game.text = dup_bytes(m->text, m->text_length);
    return 1;
}

int
adapt_message_private_rs(MsgMessagePrivateRs const* m, struct RevPacket* out)
{
    out->_message_private.from = m->from37;
    out->_message_private.message_id = m->message_id;
    out->_message_private.staff_mod = m->staff_mod;
    out->_message_private.text = unpack_text(m->text, m->text_length);
    return out->_message_private.text != NULL;
}

/* The pre-V2 form: an id and nothing else, so the fades read zero ("switch
 * immediately"). */
int
adapt_midi_song(MsgMidiSong const* m, struct RevPacket* out)
{
    out->_midi_song.id = m->id;
    out->_midi_song.fade_out_ms = m->fade_out_speed * 20;
    out->_midi_song.fade_in_ms = m->fade_in_speed * 20;
    return 1;
}

int
adapt_hint_arrow_rs(MsgHintArrowRs const* m, struct RevPacket* out)
{
    out->_hint_arrow.type = m->type;
    out->_hint_arrow.id = m->id;
    out->_hint_arrow.z = m->z;
    out->_hint_arrow.height = m->height;
    return 1;
}

int
adapt_set_player_op_rs(MsgSetPlayerOpRs const* m, struct RevPacket* out)
{
    out->_set_player_op.slot = m->id;
    out->_set_player_op.primary = m->priority;
    out->_set_player_op.text = dup_bytes(m->op, m->op_length);
    return 1;
}

int
adapt_rebuild_normal_rs(MsgRebuildNormalRs const* m, struct RevPacket* out)
{
    out->_map_rebuild.zonex = m->zone_x;
    out->_map_rebuild.zonez = m->zone_z;
    return 1;
}

int
adapt_friendlist_loaded_rs(MsgFriendlistLoadedRs const* m, struct RevPacket* out)
{
    out->_friendlist_loaded.status = m->status;
    return 1;
}

int
adapt_update_friendlist_rs(MsgUpdateFriendlistRs const* m, struct RevPacket* out)
{
    out->_update_friendlist.name37 = m->name37;
    out->_update_friendlist.world = m->world;
    return 1;
}

void
prep_update_ignorelist_rs(MsgUpdateIgnoreListRs* m)
{
    static int64_t names[MSG_UPDATE_IGNORELIST_RS_MAX_NAMES];

    m->names37 = names;
}

int
adapt_update_ignorelist_rs(MsgUpdateIgnoreListRs const* m, struct RevPacket* out)
{
    struct PktUpdateIgnoreList* p = &out->_update_ignorelist;

    p->count = m->count;
    p->names37 = NULL;
    if( m->count > 0 )
    {
        p->names37 = (int64_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->count * sizeof(int64_t));
        assert(p->names37);
        memcpy(p->names37, m->names37, (size_t)m->count * sizeof(int64_t));
    }
    return 1;
}

void
prep_update_inv_full_rs(MsgUpdateInvFullRs* m)
{
    static Rsprot_MsgUpdateInvFullRs_slotsElem slots[MSG_UPDATE_INV_FULL_RS_MAX_SLOTS];

    m->slots = slots;
}

/* The component is the container's key; the wire's obj is id + 1, 0 empty, which
 * the codec's transform has already undone. */
int
adapt_update_inv_full_rs(MsgUpdateInvFullRs const* m, struct RevPacket* out)
{
    struct PktUpdateInvFull* p = &out->_update_inv_full;

    p->component_id = m->combined_id;
    p->inv_id = -1;
    p->size = m->capacity;
    p->obj_ids = (int*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->capacity * sizeof(int) + 1);
    assert(p->obj_ids);
    p->obj_counts = (int*)ToriMem_Alloc(ToriMem_Net(), (size_t)m->capacity * sizeof(int) + 1);
    assert(p->obj_counts);
    for( int i = 0; i < m->capacity; i++ )
    {
        p->obj_ids[i] = m->slots[i].id;
        p->obj_counts[i] = m->slots[i].count;
    }
    return 1;
}

void
prep_update_inv_partial_rs(MsgUpdateInvPartialRs* m)
{
    static Rsprot_MsgUpdateInvPartialRs_slotsElem slots[MSG_UPDATE_INV_PARTIAL_RS_MAX_SLOTS];

    m->slots = slots;
}

int
adapt_update_inv_partial_rs(MsgUpdateInvPartialRs const* m, struct RevPacket* out)
{
    struct PktUpdateInvPartial* p = &out->_update_inv_partial;

    p->component_id = m->combined_id;
    p->inv_id = -1;
    p->count = m->count;
    p->entries = (struct PktUpdateInvPartialEntry*)ToriMem_Alloc(
        ToriMem_Net(), (size_t)m->count * sizeof(*p->entries) + 1);
    assert(p->entries);
    for( int i = 0; i < m->count; i++ )
    {
        p->entries[i].slot = m->slots[i].slot;
        p->entries[i].obj_id = m->slots[i].id;
        p->entries[i].count = m->slots[i].count;
    }
    return 1;
}

int
adapt_update_inv_stoptransmit_rs(MsgUpdateInvStoptransmitRs const* m, struct RevPacket* out)
{
    out->_update_inv_stop_transmit.component_id = m->combined_id;
    out->_update_inv_stop_transmit.inv_id = -1;
    return 1;
}

/* LostCity's zone packets carry no level: -1 is "the player's current one". */
int
adapt_update_zone_partial_follows_rs(MsgUpdateZonePartialFollowsRs const* m, struct RevPacket* out)
{
    out->_update_zone_partial_follows.base_x = m->zone_x;
    out->_update_zone_partial_follows.base_z = m->zone_z;
    out->_update_zone_partial_follows.level = -1;
    return 1;
}

int
adapt_update_zone_full_follows_rs(MsgUpdateZoneFullFollowsRs const* m, struct RevPacket* out)
{
    out->_update_zone_full_follows.base_x = m->zone_x;
    out->_update_zone_full_follows.base_z = m->zone_z;
    out->_update_zone_full_follows.level = -1;
    return 1;
}

int
adapt_update_zone_partial_enclosed_rs(
    MsgUpdateZonePartialEnclosedRs const* m, ProtoZoneSubFn zone, struct RevPacket* out)
{
    MsgDesktopUpdateZonePartialEnclosed header;

    memset(&header, 0, sizeof(header));
    header.zone_x = m->zone_x;
    header.zone_z = m->zone_z;
    header.payload = m->payload;
    header.payload_length = m->payload_length;
    if( !adapt_update_zone_partial_enclosed(&header, zone, out) )
        return 0;
    out->_update_zone_enclosed.level = -1;
    return 1;
}

/* No op flags: all five set says the loctype's own menu stands. */
int
adapt_zone_loc_add_change_rs(MsgLocAddChangeRs const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_LOC_ADD_CHANGE;
    out->_loc_add_change.op_flags = 0x1f;
    out->_loc_add_change.pos = m->coord_in_zone_packed;
    out->_loc_add_change.info = m->loc_properties_packed;
    out->_loc_add_change.loc_id = m->id;
    return 1;
}

/* Offsets from the source tile and a signed 16-bit target -- what
 * PktMapProjAnim was written for. `dst_abs` stays 0 (the relative branch). */
int
adapt_zone_map_projanim_rs(MsgMapProjanimRs const* m, struct PktZoneSubPacket* out)
{
    struct PktMapProjAnim* p = &out->_map_projanim;

    out->name = PKT_NAME_MAP_PROJANIM;
    p->pos = m->coord_in_zone_packed;
    p->dx_offset = m->delta_x;
    p->dz_offset = m->delta_z;
    p->target = m->target_index;
    p->spotanim = m->id;
    p->src_height = m->start_height;
    p->dst_height = m->end_height;
    p->start_delay = m->start_time;
    p->end_delay = m->end_time;
    p->peak = m->angle;
    p->arc = m->progress;
    return 1;
}

int
adapt_zone_obj_reveal(MsgObjReveal const* m, struct PktZoneSubPacket* out)
{
    out->name = PKT_NAME_OBJ_REVEAL;
    out->_obj_reveal.pos = m->coord_in_zone_packed;
    out->_obj_reveal.obj_id = m->id;
    out->_obj_reveal.count = m->quantity;
    out->_obj_reveal.receiver = m->receiver;
    return 1;
}
