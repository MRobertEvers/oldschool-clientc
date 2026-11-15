#ifndef SRC_TORIRSSERVER_WIRE_CORE_WIRE_PROT_H
#define SRC_TORIRSSERVER_WIRE_CORE_WIRE_PROT_H

/*
 * A revision's prots as this server uses them.
 *
 * Every opcode, size and prot name is RSProt's own table
 * (3rd/rsprot/gen/<rev>_prot.h, written by 3rd/rsprot/tools/rsprot_gen.py from
 * spec/tables/<rev>.ini). What this server adds is the one thing that is not
 * protocol: which canonical name (net/rev/pktnames.h) it writes each
 * server->client prot as, and reads each client->server prot as. That map is
 * each revision's <rev>_prots.h, written by tools/rsprot_gen_rev.py, and it
 * names prots only by RSProt's constants -- it holds no number of its own.
 *
 * The one exception is `client_extra`: client->server prots the client sends
 * and RSProt does not table (the rev-239 anticheat packets). The server only
 * frames them, so it states their sizes itself.
 */

#include "3rd/rsprot/src/rsprot_tables_def.h"
#include "net/rev/pktnames.h"

#include <stddef.h>

/** One prot this server writes or reads under a canonical name. */
struct WireProtName
{
    int code; /* RSPROT_<REV>_SERVER_/_CLIENT_<prot>; -1 = a zone sub-packet only */
    int name; /* PKT_NAME_* (server->client) or PKTOUT_NAME_* (client->server) */
};

/** A client->server prot RSProt does not table: framed, never decoded. */
struct WireProtFrame
{
    int code;
    int size;
    char const* prot;
};

struct WireProtTable
{
    RsprotProtDef const* server;
    int server_count;
    RsprotProtDef const* client;
    int client_count;
    struct WireProtName const* server_names;
    int server_name_count;
    struct WireProtName const* client_names;
    int client_name_count;
    struct WireProtFrame const* client_extra;
    int client_extra_count;
};

static inline RsprotProtDef const*
wire_prot_by_code(
    RsprotProtDef const* defs,
    int count,
    int code)
{
    for( int i = 0; i < count; i++ )
        if( defs[i].code == code )
            return &defs[i];
    return NULL;
}

static inline struct WireProtFrame const*
wire_prot_extra_by_code(
    struct WireProtTable const* t,
    int code)
{
    for( int i = 0; i < t->client_extra_count; i++ )
        if( t->client_extra[i].code == code )
            return &t->client_extra[i];
    return NULL;
}

static inline int
wire_prot_name_of(
    struct WireProtName const* names,
    int count,
    int code,
    int none)
{
    for( int i = 0; i < count; i++ )
        if( names[i].code == code )
            return names[i].name;
    return none;
}

static inline int
wire_prot_code_of(
    struct WireProtName const* names,
    int count,
    int name,
    int none)
{
    if( name == none )
        return -1;
    for( int i = 0; i < count; i++ )
        if( names[i].name == name )
            return names[i].code;
    return -1;
}

/* ---- server->client ---------------------------------------------------- */

/** Payload size for a wire opcode: n, RSPROT_VARU8 or RSPROT_VARU16; 0 for an
 *  unknown opcode, which frames as empty so the stream resynchronises. */
static inline int
wire_prot_in_size(
    struct WireProtTable const* t,
    int code)
{
    RsprotProtDef const* def = wire_prot_by_code(t->server, t->server_count, code);
    return def ? def->size : 0;
}

/** The canonical PKT_NAME_* for a wire opcode, PKT_NAME_NONE if unmapped. */
static inline int
wire_prot_in_name(
    struct WireProtTable const* t,
    int code)
{
    return wire_prot_name_of(t->server_names, t->server_name_count, code, PKT_NAME_NONE);
}

/** The wire opcode this server writes `pkt_name` as; -1 if this revision has
 *  no such prot (or carries it only inside a zone). */
static inline int
wire_prot_in_code(
    struct WireProtTable const* t,
    int pkt_name)
{
    return wire_prot_code_of(t->server_names, t->server_name_count, pkt_name, PKT_NAME_NONE);
}

/** RSProt's own prot name for a wire opcode; NULL when unknown. Logs read as
 *  the revision spells them, so a trace can be grepped straight into RSProt. */
static inline char const*
wire_prot_in_protname(
    struct WireProtTable const* t,
    int code)
{
    RsprotProtDef const* def = wire_prot_by_code(t->server, t->server_count, code);
    return def ? def->name : NULL;
}

/* ---- client->server ---------------------------------------------------- */

static inline int
wire_prot_out_size(
    struct WireProtTable const* t,
    int code)
{
    RsprotProtDef const* def = wire_prot_by_code(t->client, t->client_count, code);
    struct WireProtFrame const* extra;

    if( def )
        return def->size;
    extra = wire_prot_extra_by_code(t, code);
    return extra ? extra->size : 0;
}

static inline int
wire_prot_out_name(
    struct WireProtTable const* t,
    int code)
{
    return wire_prot_name_of(t->client_names, t->client_name_count, code, PKTOUT_NAME_NONE);
}

static inline int
wire_prot_out_code(
    struct WireProtTable const* t,
    int pkt_out_name)
{
    return wire_prot_code_of(t->client_names, t->client_name_count, pkt_out_name, PKTOUT_NAME_NONE);
}

static inline char const*
wire_prot_out_protname(
    struct WireProtTable const* t,
    int code)
{
    RsprotProtDef const* def = wire_prot_by_code(t->client, t->client_count, code);
    struct WireProtFrame const* extra;

    if( def )
        return def->name;
    extra = wire_prot_extra_by_code(t, code);
    return extra ? extra->prot : NULL;
}

#endif
