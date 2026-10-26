/*
 * The LostCity revisions' generated executors (src/net/rev/rs<N>/, RSProt's
 * rs-family codecs) against the hand parser they replace (gameproto_parse.c
 * behind GameProtoRev_LC<N>), packet for packet.
 *
 *     make -C src test-lc-diff [CAPTURE=build/proto_diff_corpus/cap_embed_osrs230_hybrid.unframed] [FUZZ=8]
 *
 * For each of rs245.2, rs254 and rs289:
 *   - the two opcode tables are compared: each server opcode's name and framed
 *     size, each client name's opcode -- a wrong one kills a session at framing
 *     (and a ledger line for one needs its reason as much as a decode's);
 *   - every server prot with a codec has its message FILLed and encoded FUZZ
 *     times, and both sides decode the bytes;
 *   - CAPTURE, a session of the osrs230 wire (LostCity payloads under that
 *     wire's opcodes), is reframed by the server's osrs230 table and each packet
 *     decoded both ways under the LostCity opcode of its name.
 *
 * The two RevPackets are compared arm by arm, owned arrays and strings by what
 * they hold. Every disagreement must be listed in net/test/lc_diff_expected.txt
 * with its reason; an unlisted one fails, and so does a listed one that no
 * longer differs.
 *
 * Lives as long as the hand parser: it goes when gameproto_parse.c's body does.
 */
#include "net/net.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/rev_list.h"
#include "net/rev/revpacket.h"
#include "net/rsprox_recording.h"
#include "torirsserver/wire/osrs230/packetin.h"

#include "rsprot_exec.h"
#include "rsprot_registry.h"
#include "rsprot_rev.h"
#include "rsprot_storage.h"
#include "rsprot_tables.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
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
        /* The codec and its version compare as the arm's plain fields. */
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


/* --- the two decoders ----------------------------------------------------- */

static int
decode(struct GameProtoRevTable const* rev, int wire, uint8_t const* data, int len, struct RevPacket* out)
{
    int parsed = -1;

    /* net.c's chain: a hand table has no parse_wire, and gameproto_parse is its
     * parser. */
    memset(out, 0, sizeof(*out));
    out->packet_type = rev->packetin_code(wire);
    if( rev->parse_wire )
        parsed = rev->parse_wire(rev, wire, data, len, out);
    if( parsed < 0 && rev->parse )
        parsed = rev->parse(rev, out->packet_type, data, len, out);
    if( parsed < 0 )
        parsed = gameproto_parse(rev, out->packet_type, (uint8_t*)data, len, out);
    return parsed > 0;
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
    char const* prot,
    struct GameProtoRevTable const* ref,
    struct GameProtoRevTable const* mod,
    int wire,
    uint8_t const* data,
    int len,
    char const* where)
{
    static struct RevPacket a;
    static struct RevPacket b;
    struct Key* k = key(rev_name, prot);
    int pa = decode(ref, wire, data, len, &a);
    int pb = decode(mod, wire, data, len, &b);

    if( pa == pb && (!pa || same(&a, &b)) )
        k->same++;
    else
    {
        k->diff++;
        if( !k->example[0] )
        {
            char l1[200];
            char l2[200];
            char wire_hex[100];
            size_t n = 0;

            l1[0] = l2[0] = '\0';
            if( pa )
                hexline(l1, sizeof(l1), "hand", &a);
            if( pb )
                hexline(l2, sizeof(l2), "exec", &b);
            wire_hex[0] = '\0';
            for( int i = 0; i < len && i < 30; i++ )
                n += (size_t)snprintf(wire_hex + n, sizeof(wire_hex) - n, " %02x", data[i]);
            snprintf(k->example, sizeof(k->example),
                     "%s, %d bytes: hand %s, exec %s\n      wire%s\n      %s\n      %s", where, len,
                     pa ? "decoded" : "dropped", pb ? "decoded" : "dropped", wire_hex, l1, l2);
        }
    }
    if( pa )
        gameproto_free(&a);
    if( pb )
        gameproto_free(&b);
}

/* --- the revisions -------------------------------------------------------- */

struct Pair
{
    char const* name;
    struct GameProtoRevTable const* (*hand)(void);
    struct GameProtoRevTable const* (*exec)(void);
    int major;
    int minor;
};

static struct Pair const k_pairs[] = {
    { "rs245.2", GameProtoRev_LC245_2, GameProtoRev_RS245_2, 245, 2 },
    { "rs254", GameProtoRev_LC254, GameProtoRev_RS254, 254, 0 },
    { "rs289", GameProtoRev_LC289, GameProtoRev_RS289, 289, 0 },
};

/* The two tables, opcode by opcode. The hand client table has no sizes (net_out
 * writes each length itself), so the client side is compared by opcode only;
 * its bytes are net-out-dump's. Each disagreement is the ledger's
 * "table:server:<opcode>" or "table:client:<name>". */
static void
table_row(char const* rev_name, char const* what, int differs, char const* example)
{
    struct Key* k = key(rev_name, what);

    if( !differs )
    {
        k->same++;
        return;
    }
    k->diff++;
    snprintf(k->example, sizeof(k->example), "%s", example);
}

static void
run_tables(struct Pair const* p)
{
    struct GameProtoRevTable const* ref = p->hand();
    struct GameProtoRevTable const* mod = p->exec();
    char what[64];
    char example[200];

    for( int op = 0; op < 256; op++ )
    {
        int rn = ref->packetin_code(op);
        int mn = mod->packetin_code(op);
        int rs = ref->packetin_size(op);
        int ms = mod->packetin_size(op);

        if( rn == PKT_NAME_NONE && mn == PKT_NAME_NONE )
            continue;
        snprintf(what, sizeof(what), "table:server:%d", op);
        snprintf(example, sizeof(example), "hand name %d size %d, exec name %d size %d", rn, rs, mn, ms);
        table_row(p->name, what, rn != mn || rs != ms, example);
    }
    for( int n = 1; n < PKTOUT_NAME_COUNT; n++ )
    {
        int rc = ref->packetout_code(n);
        int mc = mod->packetout_code(n);

        if( rc < 0 && mc < 0 )
            continue;
        snprintf(what, sizeof(what), "table:client:%d", n);
        snprintf(example, sizeof(example), "hand opcode %d, exec opcode %d", rc, mc);
        table_row(p->name, what, rc != mc, example);
    }
}

/* Everything else the table states: framing, timing, login, reconnect, the
 * checksums, and which stream, appearance and scene hooks it names -- the
 * same functions on both sides, so compared by address. Ledger key
 * "settings:<field>". */
static void
run_settings(struct Pair const* p)
{
    struct GameProtoRevTable const* r = p->hand();
    struct GameProtoRevTable const* m = p->exec();
    char example[200];

#define SETTING(field)                                                                             \
    snprintf(example, sizeof(example), "hand %lld, exec %lld", (long long)(intptr_t)r->field,      \
             (long long)(intptr_t)m->field);                                                       \
    table_row(p->name, "settings:" #field, r->field != m->field, example)
    SETTING(number);
    SETTING(client_version);
    SETTING(transport_kind);
    SETTING(opcode_plaintext);
    SETTING(opcode_smart2);
    SETTING(server_tick_ms);
    SETTING(component_id_bytes);
    SETTING(login);
    SETTING(reconnect_kind);
    SETTING(player_info_read);
    SETTING(npc_info_read);
    SETTING(npc_info_read_large);
    SETTING(player_info_set_local);
    SETTING(player_info_init);
    SETTING(player_info_state_size);
    SETTING(player_info_state_save);
    SETTING(player_info_state_load);
    SETTING(appearance_decode);
    SETTING(scene_base);
    SETTING(out_vt);
#undef SETTING
    snprintf(example, sizeof(example), "the nine jag checksums differ");
    table_row(p->name, "settings:jag_checksum",
              memcmp(r->jag_checksum, m->jag_checksum, sizeof(r->jag_checksum)) != 0, example);
}

static union
{
    long double align;
    uint8_t bytes[256 * 1024];
} g_msg;

static uint8_t g_wire[256 * 1024];

/* Every server prot with a codec, FILLed and encoded. */
static void
run_fuzz(struct Pair const* p, int seeds)
{
    struct GameProtoRevTable const* ref = p->hand();
    struct GameProtoRevTable const* mod = p->exec();
    RsprotRev rev = rsprot_rs(p->major, p->minor);
    RsprotRevTable const* t = rsprot_rev_table(rev);
    int fed = 0;

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
        {
            printf("no codec %s %s\n", p->name, d->name);
            continue;
        }
        for( uint32_t seed = 1; seed <= (uint32_t)seeds; seed++ )
        {
            RsprotExec x;
            RsprotBuf b;
            char where[96];

            if( getenv("LC_DIFF_TRACE") )
                printf("fuzz %s %s seed %u\n", p->name, d->name, seed);
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
            compare(p->name, d->name, ref, mod, d->code, g_wire, b.wpos, where);
            fed++;
        }
    }
    printf("fuzz %s: %d payloads\n", p->name, fed);
}

/* The osrs230 wire's session: LostCity payloads framed by that wire's table,
 * decoded under the LostCity opcode of each packet's name. */
static struct GameProtoRevTable g_framer;

static int
run_capture(struct Pair const* p, char const* path)
{
    struct GameProtoRevTable const* ref = p->hand();
    struct GameProtoRevTable const* mod = p->exec();
    struct RsproxRecording rec;
    char err[256];
    char where[300];
    char prot[64];
    RsprotRevTable const* t = rsprot_rev_table(rsprot_rs(p->major, p->minor));
    int fed = 0;
    int unnamed = 0;

    g_framer = *ref;
    g_framer.packetin_size = packetin_size_osrs230;
    g_framer.packetout_size = mod->packetout_size; /* the hand table has none */
    g_framer.opcode_smart2 = 0;
    memset(&rec, 0, sizeof(rec));
    if( RsproxRecording_OpenFile(&rec, path, err, sizeof(err)) != 0 ||
        RsproxRecording_Index(&rec, &g_framer, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        RsproxRecording_Free(&rec);
        return 1;
    }
    for( int f = 0; f < rec.frame_count; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec.frames[f];
        int name;
        int wire;

        if( fr->opcode == RSPROX_RECORDING_RECONNECT || fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            continue;
        name = packetin_code_osrs230(fr->opcode);
        wire = name == PKT_NAME_NONE ? -1 : ref->packetin_wire(name);
        if( wire < 0 )
        {
            unnamed++;
            continue;
        }
        snprintf(prot, sizeof(prot), "cap:%d", name);
        for( int i = 0; i < t->server_count; i++ )
            if( t->server[i].code == wire )
                snprintf(prot, sizeof(prot), "cap:%s", t->server[i].name);
        snprintf(where, sizeof(where), "%s frame %d", path, f);
        compare(p->name, prot, ref, mod, wire, rec.data + fr->offset, (int)fr->length, where);
        fed++;
    }
    printf("capture %s: %d packets, %d with no %s opcode\n", p->name, fed, unnamed, p->name);
    RsproxRecording_Free(&rec);
    return 0;
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
    char const* expected = getenv("LC_DIFF_EXPECTED");
    int seeds = getenv("LC_DIFF_FUZZ") ? atoi(getenv("LC_DIFF_FUZZ")) : 8;
    int failures = 0;
    int unexpected = 0;
    int stale = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    load_expected(expected ? expected : "net/test/lc_diff_expected.txt");
    for( size_t i = 0; i < sizeof(k_pairs) / sizeof(k_pairs[0]); i++ )
    {
        run_tables(&k_pairs[i]);
        run_settings(&k_pairs[i]);
        run_fuzz(&k_pairs[i], seeds);
        for( int a = 1; a < argc; a++ )
            failures += run_capture(&k_pairs[i], argv[a]);
    }

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
    }
    printf("\nlc-diff: %d unexpected, %d stale, %d corpus failure(s)\n", unexpected, stale, failures);
    return unexpected || stale || failures ? 1 : 0;
}
