/*
 * One recording parsed by two revision tables, packet for packet: a reference
 * table's name-keyed chain (proto_decode_name, rev->parse, gameproto_parse --
 * net.c's order) against a data-driven table's parse_wire. Where they agree
 * the data-driven table is doing what the reference does; every difference is
 * printed with the first bytes of both canonical arms.
 *
 *     make -C src rsprox-table-diff RECORDING=<file.bin.gz> [REF=osrs241]
 *
 * Arms that own heap data are compared by what they own: PLAYER_INFO and
 * NPC_INFO by their payload. The rest are compared byte for byte, so a
 * difference in an arm holding a pointer (the rebuilds' key arrays) shows as
 * a difference the line has to be read to judge. Test-only.
 */
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "net/proto/proto_in_adapt.h"
#include "net/rsprox_recording.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAMES 512

static int g_same[MAX_NAMES];
static int g_diff[MAX_NAMES];
static int g_first[MAX_NAMES];

static int
parse_reference(
    struct GameProtoRevTable const* rev,
    int wire,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    int name = rev->packetin_code(wire);
    int parsed = -1;

    memset(out, 0, sizeof(*out));
    if( name == PKT_NAME_NONE )
        return 0;
    out->packet_type = name;
    parsed = proto_decode_name(rev, name, data, len, out);
    if( parsed < 0 && rev->parse )
        parsed = rev->parse(rev, name, data, len, out);
    if( parsed < 0 )
        parsed = gameproto_parse(rev, name, (uint8_t*)data, len, out);
    return parsed > 0;
}

static int
blob_eq(void const* a, void const* b, size_t n)
{
    if( n == 0 )
        return 1;
    if( !a || !b )
        return a == b;
    return memcmp(a, b, n) == 0;
}

static int
str_eq(char const* a, char const* b)
{
    if( !a || !b )
        return a == b;
    return strcmp(a, b) == 0;
}

/* The bytes of a zone sub-packet its kind uses: the parsers fill one union
 * member and leave the rest as they found it. */
static size_t
sub_size(struct PktZoneSubPacket const* e)
{
    size_t head = offsetof(struct PktZoneSubPacket, _loc_add_change);

    switch( e->name )
    {
    case PKT_NAME_LOC_ADD_CHANGE: return head + sizeof(e->_loc_add_change);
    case PKT_NAME_LOC_DEL: return head + sizeof(e->_loc_del);
    case PKT_NAME_LOC_ANIM: return head + sizeof(e->_loc_anim);
    case PKT_NAME_OBJ_ADD: return head + sizeof(e->_obj_add);
    case PKT_NAME_OBJ_DEL: return head + sizeof(e->_obj_del);
    case PKT_NAME_OBJ_REVEAL: return head + sizeof(e->_obj_reveal);
    case PKT_NAME_OBJ_COUNT: return head + sizeof(e->_obj_count);
    case PKT_NAME_LOC_MERGE: return head + sizeof(e->_loc_merge);
    case PKT_NAME_MAP_PROJANIM: return head + sizeof(e->_map_projanim);
    case PKT_NAME_MAP_ANIM: return head + sizeof(e->_map_anim);
    case PKT_NAME_SOUND_AREA: return head + sizeof(e->_sound_area);
    case PKT_NAME_SCRIPTEDPROJ_ADD: return head + sizeof(e->_scriptedproj_add);
    default: return sizeof(*e);
    }
}

static int
sub_eq(struct PktZoneSubPacket const* a, struct PktZoneSubPacket const* b)
{
    return a->name == b->name && memcmp(a, b, sub_size(a)) == 0;
}

static int
enclosed_eq(struct PktUpdateZoneEnclosed const* a, struct PktUpdateZoneEnclosed const* b)
{
    if( a->count != b->count )
        return 0;
    for( int i = 0; i < a->count; i++ )
        if( !sub_eq(&a->entries[i], &b->entries[i]) )
            return 0;
    return 1;
}

/* The two packets with their owned data compared and their pointers then
 * blanked, so the byte compare that follows sees only values. */
static int
same(struct RevPacket const* a, struct RevPacket const* b)
{
    static struct RevPacket x;
    static struct RevPacket y;
    int owned = 1;

    if( a->packet_type != b->packet_type )
        return 0;
    x = *a;
    y = *b;
    switch( a->packet_type )
    {
    case PKT_NAME_PLAYER_INFO:
        owned = a->_player_info.length == b->_player_info.length &&
                blob_eq(a->_player_info.data, b->_player_info.data, (size_t)a->_player_info.length);
        x._player_info.data = y._player_info.data = NULL;
        break;
    case PKT_NAME_NPC_INFO:
    case PKT_NAME_NPC_INFO_LARGE:
        owned = a->_npc_info.length == b->_npc_info.length &&
                blob_eq(a->_npc_info.data, b->_npc_info.data, (size_t)a->_npc_info.length);
        x._npc_info.data = y._npc_info.data = NULL;
        break;
    case PKT_NAME_RUNCLIENTSCRIPT:
        owned = blob_eq(a->_runclientscript, b->_runclientscript, sizeof(*a->_runclientscript));
        x._runclientscript = y._runclientscript = NULL;
        break;
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
    {
        struct PktMapRebuild const* m = &a->_map_rebuild;
        struct PktMapRebuild const* n = &b->_map_rebuild;

        owned = m->region_count == n->region_count &&
                blob_eq(m->region_ids, n->region_ids, (size_t)m->region_count * sizeof(int)) &&
                blob_eq(m->region_keys, n->region_keys, (size_t)m->region_count * 4 * sizeof(int32_t)) &&
                (!m->zones) == (!n->zones) &&
                blob_eq(m->zones, n->zones, m->zones ? 4 * 13 * 13 * sizeof(int32_t) : 0);
        x._map_rebuild.region_ids = y._map_rebuild.region_ids = NULL;
        x._map_rebuild.region_keys = y._map_rebuild.region_keys = NULL;
        x._map_rebuild.zones = y._map_rebuild.zones = NULL;
        break;
    }
    case PKT_NAME_UPDATE_INV_FULL:
    {
        size_t n = (size_t)a->_update_inv_full.size * sizeof(int);

        owned = a->_update_inv_full.size == b->_update_inv_full.size &&
                blob_eq(a->_update_inv_full.obj_ids, b->_update_inv_full.obj_ids, n) &&
                blob_eq(a->_update_inv_full.obj_counts, b->_update_inv_full.obj_counts, n);
        x._update_inv_full.obj_ids = y._update_inv_full.obj_ids = NULL;
        x._update_inv_full.obj_counts = y._update_inv_full.obj_counts = NULL;
        break;
    }
    case PKT_NAME_UPDATE_INV_PARTIAL:
        owned = a->_update_inv_partial.count == b->_update_inv_partial.count &&
                blob_eq(a->_update_inv_partial.entries, b->_update_inv_partial.entries,
                        (size_t)a->_update_inv_partial.count * sizeof(*a->_update_inv_partial.entries));
        x._update_inv_partial.entries = y._update_inv_partial.entries = NULL;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        owned = enclosed_eq(&a->_update_zone_enclosed, &b->_update_zone_enclosed);
        x._update_zone_enclosed.entries = y._update_zone_enclosed.entries = NULL;
        break;
    case PKT_NAME_UPDATE_IGNORELIST:
        owned = a->_update_ignorelist.count == b->_update_ignorelist.count &&
                blob_eq(a->_update_ignorelist.names37, b->_update_ignorelist.names37,
                        (size_t)a->_update_ignorelist.count * sizeof(int64_t));
        x._update_ignorelist.names37 = y._update_ignorelist.names37 = NULL;
        break;
    case PKT_NAME_IF_RESYNC_V2:
        owned = a->_if_resync.mount_count == b->_if_resync.mount_count &&
                a->_if_resync.event_count == b->_if_resync.event_count &&
                blob_eq(a->_if_resync.mounts, b->_if_resync.mounts,
                        (size_t)a->_if_resync.mount_count * sizeof(*a->_if_resync.mounts)) &&
                blob_eq(a->_if_resync.events, b->_if_resync.events,
                        (size_t)a->_if_resync.event_count * sizeof(*a->_if_resync.events));
        x._if_resync.mounts = y._if_resync.mounts = NULL;
        x._if_resync.events = y._if_resync.events = NULL;
        break;
    case PKT_NAME_IF_SETTEXT:
        owned = str_eq(a->_if_settext.text, b->_if_settext.text);
        x._if_settext.text = y._if_settext.text = NULL;
        break;
    case PKT_NAME_MESSAGE_GAME:
        owned = str_eq(a->_message_game.name, b->_message_game.name) &&
                str_eq(a->_message_game.text, b->_message_game.text);
        x._message_game.name = y._message_game.name = NULL;
        x._message_game.text = y._message_game.text = NULL;
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        owned = str_eq(a->_message_private.text, b->_message_private.text);
        x._message_private.text = y._message_private.text = NULL;
        break;
    case PKT_NAME_SET_PLAYER_OP:
        owned = str_eq(a->_set_player_op.text, b->_set_player_op.text);
        x._set_player_op.text = y._set_player_op.text = NULL;
        break;
    default:
        break;
    }
    return owned && memcmp(&x, &y, sizeof(x)) == 0;
}

/* The first byte at which two packets' value bytes differ, or -1. */
static int
first_diff(struct RevPacket const* a, struct RevPacket const* b)
{
    unsigned char const* p = (unsigned char const*)a;
    unsigned char const* q = (unsigned char const*)b;

    for( size_t i = 0; i < sizeof(*a); i++ )
        if( p[i] != q[i] )
            return (int)i;
    return -1;
}

static void
show(char const* label, struct RevPacket const* p)
{
    unsigned char const* bytes = (unsigned char const*)p;

    printf("    %s type %d:", label, p->packet_type);
    for( size_t i = 0; i < 48 && i < sizeof(*p); i++ )
        printf(" %02x", bytes[i]);
    printf("\n");
}

/* The first sub-packet two ENCLOSED arms disagree on. */
static void
show_enclosed(struct PktUpdateZoneEnclosed const* a, struct PktUpdateZoneEnclosed const* b)
{
    printf("    entries: ref %d, gen %d\n", a->count, b->count);
    for( int i = 0; i < a->count && i < b->count; i++ )
    {
        unsigned char const* p = (unsigned char const*)&a->entries[i];
        unsigned char const* q = (unsigned char const*)&b->entries[i];

        if( sub_eq(&a->entries[i], &b->entries[i]) )
            continue;
        printf("    entry %d: ref sub %d, gen sub %d\n      ref:", i, a->entries[i].name, b->entries[i].name);
        for( size_t k = 0; k < 40; k++ )
            printf(" %02x", p[k]);
        printf("\n      gen:");
        for( size_t k = 0; k < 40; k++ )
            printf(" %02x", q[k]);
        printf("\n");
        return;
    }
}

int
main(int argc, char** argv)
{
    struct RsproxRecording rec;
    struct GameProtoRevTable const* ref;
    struct GameProtoRevTable const* gen;
    static struct RevPacket a;
    static struct RevPacket b;
    char err[256];
    int shown = 0;
    int limit = getenv("TABLE_DIFF_SHOW") ? atoi(getenv("TABLE_DIFF_SHOW")) : 3;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <recording> [reference table name]\n", argv[0]);
        return 2;
    }
    memset(&rec, 0, sizeof(rec));
    if( RsproxRecording_OpenFile(&rec, argv[1], err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", argv[1], err);
        return 1;
    }
    ref = GameProtoRev_ByName(argc > 2 ? argv[2] : "osrs241");
    gen = GameProtoRev_Rsprox(rec.revision);
    assert(ref);
    assert(gen);
    assert(gen->parse_wire);
    if( RsproxRecording_Index(&rec, gen, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", argv[1], err);
        return 1;
    }
    for( int i = 0; i < MAX_NAMES; i++ )
        g_first[i] = -1;
    for( int f = 0; f < rec.frame_count; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec.frames[f];
        uint8_t const* data = rec.data + fr->offset;
        int len = (int)fr->length;
        int pa;
        int pb;
        int name;

        if( fr->opcode == RSPROX_RECORDING_RECONNECT ||
            fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            continue;
        pa = parse_reference(ref, fr->opcode, data, len, &a);
        memset(&b, 0, sizeof(b));
        pb = gen->parse_wire(gen, fr->opcode, data, len, &b) > 0;
        name = pa ? a.packet_type : pb ? b.packet_type : 0;
        if( name <= 0 || name >= MAX_NAMES )
            goto next;
        if( pa == pb && (!pa || same(&a, &b)) )
            g_same[name]++;
        else
        {
            g_diff[name]++;
            if( g_first[name] < 0 )
                g_first[name] = f;
            if( g_diff[name] <= limit && shown < 400 )
            {
                printf("  frame %d tick %d wire %d name %d: reference %s, rsprox%d %s\n", f, fr->tick,
                       fr->opcode, name, pa ? "parsed" : "dropped", rec.revision, pb ? "parsed" : "dropped");
                if( pa && pb )
                    printf("    first differing byte %d\n", first_diff(&a, &b));
                if( pa && pb && name == PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED )
                    show_enclosed(&a._update_zone_enclosed, &b._update_zone_enclosed);
                if( pa )
                    show("ref", &a);
                if( pb )
                    show("gen", &b);
                shown++;
            }
        }
    next:
        if( pa )
            gameproto_free(&a);
        if( pb )
            gameproto_free(&b);
    }
    printf("\n%-6s %8s %8s %8s\n", "name", "same", "DIFF", "first");
    for( int i = 0; i < MAX_NAMES; i++ )
        if( g_diff[i] )
            printf("%-6d %8d %8d %8d\n", i, g_same[i], g_diff[i], g_first[i]);
    RsproxRecording_Free(&rec);
    return 0;
}
