/*
 * The data-driven OSRS revision tables, 227-241. See proto.h.
 *
 * Each table is built once, on first use, from 3rd/rsprot's generated prot
 * table for the revision. GameProtoRevTable's slots take no context, so the
 * per-revision functions they point at are stamped out below by PROTO_HOOKS
 * and do nothing but name their revision.
 */
#include "proto.h"

#include "gen/rsprot_tables.h"
#include "net/login/login_osrs.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/proto/proto_info.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static struct ProtoRev g_revs[PROTO_OSRS_COUNT];
static int g_built[PROTO_OSRS_COUNT];

static struct ProtoRev*
slot(int revision)
{
    assert(revision >= PROTO_OSRS_FIRST);
    assert(revision <= PROTO_OSRS_LAST);
    return &g_revs[revision - PROTO_OSRS_FIRST];
}

static int
size_of(RsprotProtDef const* d)
{
    if( d->size == RSPROT_VARU8 )
        return PKTIN_LENGTH_VARU8;
    if( d->size == RSPROT_VARU16 )
        return PKTIN_LENGTH_VARU16;
    return d->size;
}

/* --- the context-free slots, by revision -------------------------------- */

static int
in_size(int revision, int wire)
{
    struct ProtoRev const* r = slot(revision);

    if( wire < 0 || wire >= PROTO_MAX_OPCODE || r->server_by_wire[wire] < 0 )
        return 0;
    return size_of(&r->prots->server[r->server_by_wire[wire]]);
}

static int
in_code(int revision, int wire)
{
    struct ProtoRev const* r = slot(revision);

    if( wire < 0 || wire >= PROTO_MAX_OPCODE )
        return PKT_NAME_NONE;
    return r->server_name[wire];
}

static int
in_wire(int revision, int name)
{
    if( name <= 0 || name >= PKT_NAME_COUNT )
        return -1;
    return slot(revision)->name_wire[name];
}

static int
out_code(int revision, int name)
{
    if( name <= 0 || name >= PKTOUT_NAME_COUNT )
        return -1;
    return slot(revision)->outname_wire[name];
}

static int
out_size(int revision, int wire)
{
    struct ProtoRev const* r = slot(revision);

    if( wire < 0 || wire >= PROTO_MAX_OPCODE || r->client_by_wire[wire] < 0 )
        return 0;
    return size_of(&r->prots->client[r->client_by_wire[wire]]);
}

/*
 * The appearance block's shape: 241 made the obj customisation self-delimiting
 * by count (V6); 237 widened its model ids to four bytes (V5, the 239 form);
 * below that they are two (V4). RSProx PlayerInfoClient.decodeAppearance.
 */
static enum AppearanceEncoding
appearance_encoding(int revision)
{
    if( revision >= 241 )
        return APPEARANCE_ENC_V6;
    if( revision >= 237 )
        return APPEARANCE_ENC_V5;
    return APPEARANCE_ENC_V4;
}

/* V is the NPC_INFO prot version the revision's canonical arms decode; each
 * entry point (<prot>_osrs<N>, 3rd/rsprot/streams) calls its own version. */
#define PROTO_HOOKS(N, V)                                                                         \
    static int in_size_##N(int w) { return in_size(N, w); }                                        \
    static int in_code_##N(int w) { return in_code(N, w); }                                        \
    static int in_wire_##N(int n) { return in_wire(N, n); }                                        \
    static int out_code_##N(int n) { return out_code(N, n); }                                      \
    static int out_size_##N(int w) { return out_size(N, w); }                                      \
    static int pinfo_##N(uint8_t const* d, int l, struct RsprotInfoList* o)                       \
    {                                                                                              \
        return proto_info_player_read(player_info_osrs##N, d, l, o);                            \
    }                                                                                              \
    static int ninfo_##N(uint8_t const* d, int l, struct RsprotInfoList* o)                        \
    {                                                                                              \
        return proto_info_npc_read(npc_info_small_##V##_osrs##N, d, l, o);                      \
    }                                                                                              \
    static int ninfo_large_##N(uint8_t const* d, int l, struct RsprotInfoList* o)                  \
    {                                                                                              \
        return proto_info_npc_read(npc_info_large_##V##_osrs##N, d, l, o);                      \
    }                                                                                              \
    static int appear_##N(uint8_t const* d, int l, struct PktPlayerAppearance* o)                  \
    {                                                                                              \
        return PktPlayerAppearance_DecodeAs(o, appearance_encoding(N), d, l);                      \
    }

PROTO_HOOKS(227, v5)
PROTO_HOOKS(228, v5)
PROTO_HOOKS(229, v5)
PROTO_HOOKS(230, v5)
PROTO_HOOKS(231, v5)
PROTO_HOOKS(232, v5)
PROTO_HOOKS(233, v5)
PROTO_HOOKS(234, v5)
PROTO_HOOKS(235, v5)
PROTO_HOOKS(236, v5)
PROTO_HOOKS(237, v5)
PROTO_HOOKS(238, v5)
PROTO_HOOKS(239, v5)
PROTO_HOOKS(240, v5)
PROTO_HOOKS(241, v6)

struct Hooks
{
    int (*in_size)(int);
    int (*in_code)(int);
    int (*in_wire)(int);
    int (*out_code)(int);
    int (*out_size)(int);
    int (*pinfo)(uint8_t const*, int, struct RsprotInfoList*);
    int (*ninfo)(uint8_t const*, int, struct RsprotInfoList*);
    int (*ninfo_large)(uint8_t const*, int, struct RsprotInfoList*);
    int (*appear)(uint8_t const*, int, struct PktPlayerAppearance*);
    /** The prots the canonical NPC_INFO / NPC_INFO_LARGE arms decode. */
    char const* npc_small;
    char const* npc_large;
};

#define PROTO_HOOK_ROW(N, V)                                                                      \
    { in_size_##N, in_code_##N, in_wire_##N, out_code_##N, out_size_##N, pinfo_##N,             \
      ninfo_##N,   ninfo_large_##N, appear_##N, "NPC_INFO_SMALL_" #V, "NPC_INFO_LARGE_" #V }

static struct Hooks const k_hooks[PROTO_OSRS_COUNT] = {
    PROTO_HOOK_ROW(227, V5), PROTO_HOOK_ROW(228, V5), PROTO_HOOK_ROW(229, V5), PROTO_HOOK_ROW(230, V5),
    PROTO_HOOK_ROW(231, V5), PROTO_HOOK_ROW(232, V5), PROTO_HOOK_ROW(233, V5), PROTO_HOOK_ROW(234, V5),
    PROTO_HOOK_ROW(235, V5), PROTO_HOOK_ROW(236, V5), PROTO_HOOK_ROW(237, V5), PROTO_HOOK_ROW(238, V5),
    PROTO_HOOK_ROW(239, V5), PROTO_HOOK_ROW(240, V5), PROTO_HOOK_ROW(241, V6),
};

/* --- building a table ---------------------------------------------------- */

/* Every WORLDENTITY_INFO version's revision -> entry point lookup. */
static struct
{
    int version;
    RsprotWorldEntityInfoReadFn (*osrs)(RsprotRev rev);
} const k_worldentity_streams[] = {
    { 3, worldentity_info_v3_osrs }, { 4, worldentity_info_v4_osrs },
    { 5, worldentity_info_v5_osrs }, { 6, worldentity_info_v6_osrs },
    { 7, worldentity_info_v7_osrs },
};

static void
build(int revision)
{
    struct ProtoRev* r = slot(revision);
    struct Hooks const* h = &k_hooks[revision - PROTO_OSRS_FIRST];
    struct GameProtoRevTable* t = &r->table;
    int max_server = 0;

    memset(r, 0, sizeof(*r));
    r->revision = revision;
    r->prots = rsprot_rev_table(rsprot_osrs(revision));
    /* Generated for exactly this span (3rd/rsprot/gen/rsprot_tables.h). */
    assert(r->prots);

    for( int i = 0; i < PROTO_MAX_OPCODE; i++ )
    {
        r->server_by_wire[i] = -1;
        r->client_by_wire[i] = -1;
    }
    for( int i = 0; i < PKT_NAME_COUNT; i++ )
        r->name_wire[i] = -1;
    for( int i = 0; i < PKTOUT_NAME_COUNT; i++ )
        r->outname_wire[i] = -1;

    for( int i = 0; i < r->prots->server_count; i++ )
    {
        RsprotProtDef const* d = &r->prots->server[i];
        int name;

        if( d->code < 0 )
            continue; /* a zone sub-prot: no top-level opcode */
        assert(d->code < PROTO_MAX_OPCODE);
        r->server_by_wire[d->code] = (int16_t)i;
        name = proto_canon_server(d->name);
        /* 240 carries V5 and V6 of both npc prots; the canonical arms decode
         * the pair the hooks are bound to, and the other has no arm. */
        if( (name == PKT_NAME_NPC_INFO && strcmp(d->name, h->npc_small) != 0) ||
            (name == PKT_NAME_NPC_INFO_LARGE && strcmp(d->name, h->npc_large) != 0) )
            name = PKT_NAME_NONE;
        r->server_name[d->code] = (int16_t)name;
        if( name > 0 && r->name_wire[name] < 0 )
            r->name_wire[name] = (int16_t)d->code;
        if( d->code > max_server )
            max_server = d->code;
    }
    for( int i = 0; i < r->prots->client_count; i++ )
    {
        RsprotProtDef const* d = &r->prots->client[i];
        int name;

        if( d->code < 0 )
            continue;
        assert(d->code < PROTO_MAX_OPCODE);
        r->client_by_wire[d->code] = (int16_t)i;
        name = proto_canon_client(d->name);
        if( name > 0 && r->outname_wire[name] < 0 )
            r->outname_wire[name] = (int16_t)d->code;
    }

    for( size_t i = 0; i < sizeof(k_worldentity_streams) / sizeof(k_worldentity_streams[0]); i++ )
        r->worldentity_read[k_worldentity_streams[i].version] =
            k_worldentity_streams[i].osrs(rsprot_osrs(revision));

    snprintf(r->name, sizeof(r->name), "rsprox%d", revision);
    t->revision = GAMEPROTO_REVISION_RSPROX;
    t->name = r->name;
    t->client_version = revision;
    t->packetin_size = h->in_size;
    t->packetin_code = h->in_code;
    t->packetin_wire = h->in_wire;
    t->packetout_code = h->out_code;
    t->packetout_size = h->out_size;
    t->transport_kind = NET_TRANSPORT_TCP;
    t->opcode_plaintext = 0;
    /* RSProt writes the server opcode as pSmart1Or2: two bytes once it passes
     * 0x7F. A revision whose table never gets there reads one either way. */
    t->opcode_smart2 = max_server >= 0x80;
    t->server_tick_ms = 600;
    t->component_id_bytes = 4;
    /* The login block is RSProx's ClientLoginHandler at every revision here
     * (no branch after 232's serverVersion field), with the client version
     * this table states. */
    t->login = &g_login_osrs_vtable;
    t->reconnect_kind = NET_RECONNECT_SEED;
    t->parse_wire = proto_decode_wire;
    t->player_info_read = h->pinfo;
    t->npc_info_read = h->ninfo;
    t->npc_info_read_large = h->ninfo_large;
    t->player_info_set_local = proto_info_set_local;
    t->player_info_init = proto_info_init;
    t->player_info_state_size = proto_info_state_size;
    t->player_info_state_save = proto_info_state_save;
    t->player_info_state_load = proto_info_state_load;
    t->appearance_decode = h->appear;
}

struct GameProtoRevTable const*
GameProtoRev_Rsprox(int revision)
{
    if( revision < PROTO_OSRS_FIRST || revision > PROTO_OSRS_LAST )
        return NULL;
    if( !g_built[revision - PROTO_OSRS_FIRST] )
    {
        build(revision);
        g_built[revision - PROTO_OSRS_FIRST] = 1;
    }
    return &slot(revision)->table;
}

/*
 * By ADDRESS, not by client_version: a boot manifest may restate the version
 * the login sends (GameProtoRev_SetClientVersion), and that must not move the
 * table a packet is decoded with.
 */
struct ProtoRev const*
proto_rev_state(struct GameProtoRevTable const* rev)
{
    struct ProtoRev const* r;

    assert(rev);
    assert(rev->revision == GAMEPROTO_REVISION_RSPROX);
    r = (struct ProtoRev const*)(void const*)((char const*)rev - offsetof(struct ProtoRev, table));
    assert(r >= g_revs && r < g_revs + PROTO_OSRS_COUNT);
    return r;
}

RsprotProtDef const*
proto_server_prot(struct ProtoRev const* r, int wire)
{
    if( wire < 0 || wire >= PROTO_MAX_OPCODE || r->server_by_wire[wire] < 0 )
        return NULL;
    return &r->prots->server[r->server_by_wire[wire]];
}
