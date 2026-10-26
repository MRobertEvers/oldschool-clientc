/*
 * The protocol module against the parsers it replaces, packet for packet.
 *
 *     make -C src test-proto-diff [CORPUS="a.bin.gz b.bin ..."] [FUZZ=8]
 *
 * For each revision with a hand-written table, every server payload in the
 * corpus is decoded twice: by that table's chain (net.c's order before the
 * protocol module: proto_decode_name, rev->parse, gameproto_parse) and by the
 * module (GameProtoRev_Rsprox's parse_wire), and the two RevPackets are
 * compared arm by arm -- owned arrays and strings by what they hold.
 *
 * The corpus is
 *   - RSProx recordings at a revision a hand table speaks (239, 241),
 *   - our own captures (net/net_capture.h), framed by the table they name,
 *   - FUZZ seeds per prot: every server prot with a codec at the revision,
 *     its message FILLed and encoded by that codec. The module decodes it with
 *     the same codec, so this is the hand parsers checked against RSProt's
 *     transcription, prot by prot, including the ones no session sends.
 *
 * Every disagreement must be listed in net/test/proto_diff_expected.txt with
 * the reason it is the hand parser's (or the module's) known difference. An
 * unlisted one fails the run, and so does a listed one the corpus exercised
 * that no longer differs: the ledger is kept honest both ways. A listed one
 * the corpus never sent is reported, not failed.
 */
#include "net/net.h"
#include "net/proto/proto.h"
#include "net/proto/proto_in_adapt.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "net/rsprox_recording.h"

#include "rsprot_exec.h"
#include "rsprot_registry.h"
#include "rsprot_storage.h"
#include "rsprot_tables.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- comparing two decodes -------------------------------------------------- */

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

/*
 * The two packets with their owned data compared and their pointers then
 * blanked, so the byte compare that follows sees only values. Every arm
 * gameproto_free releases is here: one missing would compare two heap
 * addresses and call every packet of that kind a difference.
 */
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
    case PKT_NAME_RECONNECT:
        owned = a->_reconnect.length == b->_reconnect.length &&
                blob_eq(a->_reconnect.data, b->_reconnect.data, (size_t)a->_reconnect.length);
        x._reconnect.data = y._reconnect.data = NULL;
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
    case PKT_NAME_REBUILD_WORLDENTITY:
        owned = a->_rebuild_wev.length == b->_rebuild_wev.length &&
                blob_eq(a->_rebuild_wev.data, b->_rebuild_wev.data, (size_t)a->_rebuild_wev.length);
        x._rebuild_wev.data = y._rebuild_wev.data = NULL;
        break;
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL:
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER:
    case PKT_NAME_VARCLAN:
    case PKT_NAME_VARCLAN_ENABLE:
    case PKT_NAME_VARCLAN_DISABLE:
    case PKT_NAME_CLANCHANNEL_FULL:
    case PKT_NAME_CLANCHANNEL_DELTA:
    case PKT_NAME_CLANSETTINGS_FULL:
    case PKT_NAME_CLANSETTINGS_DELTA:
    case PKT_NAME_UPDATE_TRADINGPOST:
        owned = a->_raw_payload.length == b->_raw_payload.length &&
                blob_eq(a->_raw_payload.data, b->_raw_payload.data, (size_t)a->_raw_payload.length);
        x._raw_payload.data = y._raw_payload.data = NULL;
        break;
    case PKT_NAME_MESSAGE_CLANCHANNEL:
    case PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM:
        owned = str_eq(a->_message_clanchannel.text, b->_message_clanchannel.text);
        x._message_clanchannel.text = y._message_clanchannel.text = NULL;
        break;
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

/* --- the two decoders ----------------------------------------------------- */

/* net.c's chain before the protocol module, on a hand-written table. */
static int
decode_reference(
    struct GameProtoRevTable const* rev, int wire, uint8_t const* data, int len, struct RevPacket* out)
{
    static uint8_t copy[1 << 17];
    int name = rev->packetin_code(wire);
    int parsed;

    memset(out, 0, sizeof(*out));
    if( name == PKT_NAME_NONE || len > (int)sizeof(copy) )
        return 0;
    /* gameproto_parse reads from a mutable buffer. */
    memcpy(copy, data, (size_t)len);
    out->packet_type = name;
    parsed = proto_decode_name(rev, name, copy, len, out);
    if( parsed < 0 && rev->parse )
        parsed = rev->parse(rev, name, copy, len, out);
    if( parsed < 0 )
        parsed = gameproto_parse(rev, name, copy, len, out);
    return parsed > 0;
}

static int
decode_module(
    struct GameProtoRevTable const* rev, int wire, uint8_t const* data, int len, struct RevPacket* out)
{
    memset(out, 0, sizeof(*out));
    return rev->parse_wire(rev, wire, data, len, out) > 0;
}

/* --- the ledger ------------------------------------------------------------ */

#define MAX_KEYS 2048

struct Key
{
    char rev[16];
    char prot[64];
    int same;
    int diff;
    int listed;
    char reason[200];
    char example[600];
};

static struct Key g_keys[MAX_KEYS];
static int g_key_count;

static struct Key*
key(char const* rev, char const* prot)
{
    for( int i = 0; i < g_key_count; i++ )
        if( strcmp(g_keys[i].rev, rev) == 0 && strcmp(g_keys[i].prot, prot) == 0 )
            return &g_keys[i];
    assert(g_key_count < MAX_KEYS);
    snprintf(g_keys[g_key_count].rev, sizeof(g_keys[0].rev), "%s", rev);
    snprintf(g_keys[g_key_count].prot, sizeof(g_keys[0].prot), "%s", prot);
    return &g_keys[g_key_count++];
}

static void
load_expected(char const* path)
{
    FILE* f = fopen(path, "r");
    char line[512];

    if( !f )
    {
        printf("no ledger at %s: every difference is unexpected\n", path);
        return;
    }
    while( fgets(line, sizeof(line), f) )
    {
        char rev[16];
        char prot[64];
        int at = 0;
        struct Key* k;

        if( line[0] == '#' || line[0] == '\n' )
            continue;
        if( sscanf(line, "%15s %63s %n", rev, prot, &at) < 2 )
            continue;
        k = key(rev, prot);
        k->listed = 1;
        line[strcspn(line, "\n")] = '\0';
        snprintf(k->reason, sizeof(k->reason), "%s", line + at);
    }
    fclose(f);
}

static void
hexline(char* out, size_t cap, char const* label, struct RevPacket const* p)
{
    unsigned char const* b = (unsigned char const*)p;
    size_t n = (size_t)snprintf(out, cap, "%s type %d:", label, p->packet_type);

    for( size_t i = 0; i < 40 && n + 4 < cap; i++ )
        n += (size_t)snprintf(out + n, cap - n, " %02x", b[i]);
}

/* --- one payload through both ------------------------------------------- */

static void
compare(
    char const* rev_name,
    struct GameProtoRevTable const* ref,
    struct GameProtoRevTable const* mod,
    int wire,
    uint8_t const* data,
    int len,
    char const* where)
{
    static struct RevPacket a;
    static struct RevPacket b;
    RsprotProtDef const* d = proto_server_prot(proto_rev_state(mod), wire);
    char prot[64];
    struct Key* k;
    int pa = decode_reference(ref, wire, data, len, &a);
    int pb = decode_module(mod, wire, data, len, &b);

    snprintf(prot, sizeof(prot), "%s", d ? d->name : "?");
    if( !d )
        snprintf(prot, sizeof(prot), "?%d", wire);
    k = key(rev_name, prot);
    if( pa == pb && (!pa || same(&a, &b)) )
        k->same++;
    else
    {
        k->diff++;
        if( !k->example[0] )
        {
            char l1[200];
            char l2[200];

            l1[0] = l2[0] = '\0';
            if( pa )
                hexline(l1, sizeof(l1), "hand", &a);
            if( pb )
                hexline(l2, sizeof(l2), "module", &b);
            char wire_hex[100];
            size_t n = 0;

            wire_hex[0] = '\0';
            for( int i = 0; i < len && i < 30; i++ )
                n += (size_t)snprintf(wire_hex + n, sizeof(wire_hex) - n, " %02x", data[i]);
            snprintf(k->example, sizeof(k->example),
                     "%s, %d bytes: hand %s, module %s\n      wire%s\n      %s\n      %s", where, len,
                     pa ? "decoded" : "dropped", pb ? "decoded" : "dropped", wire_hex, l1, l2);
        }
    }
    if( pa )
        gameproto_free(&a);
    if( pb )
        gameproto_free(&b);
}

/* --- corpora ------------------------------------------------------------- */

struct Pair
{
    char const* name;
    int revision;
};

/* The hand-written tables and the module revision each is checked against. */
static struct Pair const k_pairs[] = {
    { "osrs239", 239 },
    { "osrs241", 241 },
};

static struct Pair const*
pair_named(char const* name)
{
    for( size_t i = 0; i < sizeof(k_pairs) / sizeof(k_pairs[0]); i++ )
        if( strcmp(k_pairs[i].name, name) == 0 )
            return &k_pairs[i];
    return NULL;
}

static int
run_recording(char const* path)
{
    struct RsproxRecording rec;
    struct GameProtoRevTable const* ref;
    struct GameProtoRevTable const* mod;
    struct Pair const* pair;
    char err[256];
    char where[300];
    char ref_name[32];

    memset(&rec, 0, sizeof(rec));
    if( RsproxRecording_OpenFile(&rec, path, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        return 1;
    }
    if( strncmp(rec.client_name, "torirs-capture:", 15) == 0 )
        snprintf(ref_name, sizeof(ref_name), "%s", rec.client_name + 15);
    else
        snprintf(ref_name, sizeof(ref_name), "osrs%d", rec.revision);
    pair = pair_named(ref_name);
    if( !pair )
    {
        printf("skip %s: no hand-written table for %s\n", path, ref_name);
        RsproxRecording_Free(&rec);
        return 0;
    }
    ref = GameProtoRev_ByName(pair->name);
    mod = GameProtoRev_Rsprox(pair->revision);
    assert(ref);
    assert(mod);
    if( RsproxRecording_Index(&rec, mod, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        RsproxRecording_Free(&rec);
        return 1;
    }
    printf("%s: %s, %d server packets\n", path, pair->name, rec.frame_count);
    for( int f = 0; f < rec.frame_count; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec.frames[f];

        if( fr->opcode == RSPROX_RECORDING_RECONNECT || fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            continue;
        snprintf(where, sizeof(where), "%s frame %d", path, f);
        compare(pair->name, ref, mod, fr->opcode, rec.data + fr->offset, (int)fr->length, where);
    }
    RsproxRecording_Free(&rec);
    return 0;
}

static union
{
    long double align;
    uint8_t bytes[256 * 1024];
} g_msg;

static uint8_t g_wire[256 * 1024];

/* Every server prot with a codec at the pair's revision, FILLed and encoded. */
static void
run_fuzz(struct Pair const* pair, int seeds)
{
    struct GameProtoRevTable const* ref = GameProtoRev_ByName(pair->name);
    struct GameProtoRevTable const* mod = GameProtoRev_Rsprox(pair->revision);
    RsprotRev rev = rsprot_osrs(pair->revision);
    RsprotRevTable const* t = rsprot_rev_table(rev);
    int fed = 0;

    assert(ref);
    assert(mod);
    assert(t);
    for( int i = 0; i < t->server_count; i++ )
    {
        RsprotProtDef const* d = &t->server[i];
        char const* file = d->name;
        RsprotCodecFn fn;

        if( d->code < 0 )
            continue;
        fn = rsprot_registry_resolve_def(d, RSPROT_REG_OUT, rev, &file);
        if( !fn )
            continue;
        for( uint32_t seed = 1; seed <= (uint32_t)seeds; seed++ )
        {
            RsprotExec x;
            RsprotBuf b;
            char where[96];

            memset(&g_msg, 0, sizeof(g_msg));
            rsprot_storage_provide(file, &g_msg, 0);
            rsprot_exec_fill(&x, seed);
            fn(&x, &g_msg);
            if( !rsprot_exec_ok(&x) )
                continue;
            rsprot_buf_wrap(&b, g_wire, sizeof(g_wire));
            rsprot_exec_encode(&x, &b);
            fn(&x, &g_msg);
            if( !rsprot_exec_ok(&x) || b.err )
                continue;
            if( d->size >= 0 && b.wpos != d->size )
                continue;
            snprintf(where, sizeof(where), "fuzz seed %u", seed);
            compare(pair->name, ref, mod, d->code, g_wire, b.wpos, where);
            fed++;
        }
    }
    printf("fuzz %s: %d payloads\n", pair->name, fed);
}

/* --- the report ------------------------------------------------------------ */

static int
cmp_key(void const* a, void const* b)
{
    struct Key const* x = a;
    struct Key const* y = b;
    int c = strcmp(x->rev, y->rev);

    return c ? c : strcmp(x->prot, y->prot);
}

int
main(int argc, char** argv)
{
    char const* expected = getenv("PROTO_DIFF_EXPECTED");
    int seeds = getenv("PROTO_DIFF_FUZZ") ? atoi(getenv("PROTO_DIFF_FUZZ")) : 8;
    int failures = 0;
    int unexpected = 0;
    int stale = 0;
    int unseen = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    load_expected(expected ? expected : "net/test/proto_diff_expected.txt");
    for( int i = 1; i < argc; i++ )
        failures += run_recording(argv[i]);
    if( seeds > 0 )
        for( size_t i = 0; i < sizeof(k_pairs) / sizeof(k_pairs[0]); i++ )
            run_fuzz(&k_pairs[i], seeds);

    qsort(g_keys, (size_t)g_key_count, sizeof(g_keys[0]), cmp_key);
    printf("\n");
    for( int i = 0; i < g_key_count; i++ )
    {
        struct Key const* k = &g_keys[i];

        if( k->diff && !k->listed )
        {
            printf("UNEXPECTED %s %-34s %d differ, %d agree\n    %s\n", k->rev, k->prot, k->diff, k->same,
                   k->example);
            unexpected++;
        }
        else if( k->diff )
            printf("expected   %s %-34s %d differ, %d agree -- %s\n", k->rev, k->prot, k->diff, k->same,
                   k->reason);
        else if( k->listed && k->same )
        {
            printf("STALE      %s %-34s listed, but %d agree and none differ -- %s\n", k->rev, k->prot,
                   k->same, k->reason);
            stale++;
        }
        else if( k->listed )
        {
            printf("unseen     %s %-34s listed; the corpus sent none\n", k->rev, k->prot);
            unseen++;
        }
    }
    printf("\nproto-diff: %d unexpected, %d stale, %d listed but unseen, %d corpus failure(s)\n",
           unexpected, stale, unseen, failures);
    return unexpected || stale || failures ? 1 : 0;
}
