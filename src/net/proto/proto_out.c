/* See proto_out.h -- client packets written by RSProt's `_in` codecs. */
#include "proto_out.h"

#include "net/proto/proto.h"
#include "net/proto/proto_in_adapt.h"

#include "rsprot_exec.h"
#include "rsprot_registry.h"
#include "rsprot_tables.h"

#include <assert.h>
#include <string.h>

#include "proto_out_writers.h"

/* The RSProt revision `rev` speaks, family NONE for one that writes no RSProt. */
static RsprotRev
out_rev(struct GameProtoRevTable const* rev)
{
    RsprotRev none = { 0 };

    if( rev->revision == GAMEPROTO_REVISION_RSPROX )
        return rsprot_osrs(proto_rev_state(rev)->revision);
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 || rev->revision == GAMEPROTO_REVISION_OSRS241 )
        return rsprot_osrs(proto_osrs_revision(rev));
    return none;
}

int
proto_out_speaks(struct GameProtoRevTable const* rev)
{
    assert(rev);
    return rsprot_rev_family(out_rev(rev)) != RSPROT_FAMILY_NONE;
}

int
proto_out_has(struct GameProtoRevTable const* rev, int out_name)
{
    assert(rev);
    return rev->packetout_code(out_name) >= 0;
}

static struct ProtoOutWriter const*
writer_for(char const* prot)
{
    for( size_t i = 0; i < sizeof(k_proto_out_writers) / sizeof(k_proto_out_writers[0]); i++ )
        if( strcmp(k_proto_out_writers[i].prot, prot) == 0 )
            return &k_proto_out_writers[i];
    return NULL;
}

int
proto_encode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct ProtoOutArgs const* args)
{
    static union
    {
        long double align;
        uint8_t bytes[64 * 1024];
    } msg;
    RsprotRev rsrev;
    RsprotRevTable const* t;
    RsprotProtDef const* row;
    struct ProtoOutWriter const* w;
    char const* file = NULL;
    RsprotCodecFn fn;
    RsprotExec x;
    RsprotBuf b;
    int wire;
    int head;
    int len;

    assert(rev);
    assert(random_out);
    assert(buf);
    assert(args);
    rsrev = out_rev(rev);
    assert(rsprot_rev_family(rsrev) != RSPROT_FAMILY_NONE);
    wire = rev->packetout_code(out_name);
    if( wire < 0 )
        return -1;
    t = rsprot_rev_table(rsrev);
    assert(t);
    /* The revision's table and RSProt's name the same client opcodes. */
    row = rsprot_find_client_by_opcode(t, wire);
    assert(row);
    fn = rsprot_registry_resolve_def(row, RSPROT_REG_IN, rsrev, &file);
    /* A prot the client sends has a codec at every revision it is sent at. */
    assert(fn);
    w = writer_for(file);
    /* ...and a writer (tools/proto_out_gen.py lists the files it has none for). */
    assert(w);

    memset(&msg, 0, sizeof(msg));
    w->fill(&msg, args);
    head = row->size == RSPROT_VARU8 ? 1 : row->size == RSPROT_VARU16 ? 2 : 0;
    if( cap < 1 + head )
        return -1;
    rsprot_buf_wrap(&b, buf + 1 + head, cap - 1 - head);
    rsprot_exec_encode(&x, &b);
    fn(&x, &msg);
    if( b.err )
        return -1;
    /* Arguments the layout cannot carry are the caller's bug. */
    assert(rsprot_exec_ok(&x));
    len = b.wpos;
    if( head == 0 )
        assert(len == row->size);
    else if( head == 1 )
    {
        if( len > 0xFF )
            return -1;
        buf[1] = (uint8_t)len;
    }
    else
    {
        if( len > 0xFFFF )
            return -1;
        buf[1] = (uint8_t)(len >> 8);
        buf[2] = (uint8_t)len;
    }
    buf[0] = (uint8_t)((wire + isaac_next(random_out)) & 0xff);
    return 1 + head + len;
}
