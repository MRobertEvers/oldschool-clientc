#ifndef SRC_NET_NET_CAPTURE_H
#define SRC_NET_NET_CAPTURE_H

/*
 * The server's packets as the client received them, written as an RSProx
 * recording (net/rsprox_recording.h) so the corpus the protocol tests read
 * (net/test/proto_diff.c) can be our own sessions as well as Jagex's.
 *
 *     TORIRS_NET_CAPTURE=<file>        net.c, every framed server packet
 *     TORIRS_SCRIPTRUN_CAPTURE=<file>  scriptrun, the same at its sink
 *
 * Only the server's half is written. The header's client name is
 * "torirs-capture:<revision table name>" (osrs239, lc254, ...): which table
 * framed the stream, so a reader frames it with the same one.
 *
 * Header-only: each translation unit that captures owns one writer, opened on
 * its first packet from its own variable. A test that wants both runs them
 * into two files.
 */

#include "net/rev/gameproto_revisions.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NetCapture
{
    FILE* f;
    int tried;
};

static inline void
net_capture_p1(FILE* f, uint32_t v)
{
    fputc((int)(v & 0xFFu), f);
}

static inline void
net_capture_p2(FILE* f, uint32_t v)
{
    net_capture_p1(f, v >> 8);
    net_capture_p1(f, v);
}

static inline void
net_capture_p4(FILE* f, uint32_t v)
{
    net_capture_p2(f, v >> 16);
    net_capture_p2(f, v);
}

static inline void
net_capture_jstr(FILE* f, char const* s)
{
    fputs(s, f);
    fputc(0, f);
}

/* The header RsproxRecording_Open reads (BinaryHeader, version 1). */
static inline void
net_capture_header(FILE* f, struct GameProtoRevTable const* rev)
{
    char name[96];

    snprintf(name, sizeof(name), "torirs-capture:%s", rev->name);
    net_capture_p4(f, 1);                   /* headerVersion */
    net_capture_p4(f, (uint32_t)rev->client_version);
    net_capture_p4(f, 0);                   /* subRevision */
    net_capture_p1(f, 0);                   /* clientType */
    net_capture_p1(f, 0);                   /* platformType */
    net_capture_p4(f, 0);                   /* timestamp, p8 */
    net_capture_p4(f, 0);
    net_capture_p2(f, 0);                   /* worldId */
    net_capture_p4(f, 0);                   /* worldFlags */
    net_capture_p1(f, 0);                   /* worldLocation */
    net_capture_jstr(f, "");                /* host */
    net_capture_jstr(f, "");                /* activity */
    net_capture_p2(f, 0);                   /* localPlayerIndex */
    net_capture_p2(f, 0);                   /* accountHash: none */
    net_capture_jstr(f, name);              /* clientName */
    net_capture_p4(f, 0);                   /* js5MasterIndex: none */
}

/**
 * One server packet: `wire` framed by `rev`'s table, `len` payload bytes.
 * Does nothing unless `env` names a file.
 */
static inline void
net_capture_server(
    struct NetCapture* cap,
    char const* env,
    struct GameProtoRevTable const* rev,
    int wire,
    uint8_t const* data,
    int len)
{
    int size;

    assert(cap);
    assert(env);
    assert(rev);
    if( !cap->tried )
    {
        char const* path = getenv(env);

        cap->tried = 1;
        if( path && path[0] )
        {
            cap->f = fopen(path, "wb");
            assert(cap->f);
            net_capture_header(cap->f, rev);
        }
    }
    if( !cap->f )
        return;
    assert(data || len == 0);
    assert(wire >= 0 && wire < 0x8000);
    size = rev->packetin_size(wire);
    /* A packet the table cannot frame as it arrived (an opcode it does not
     * know, read as the framer's guess) cannot be written so a reader frames
     * it: input, left out, not a contract violation. */
    if( size >= 0 ? size != len : (size != PKTIN_LENGTH_VARU8 && size != PKTIN_LENGTH_VARU16) )
        return;
    if( size == PKTIN_LENGTH_VARU8 && len > 0xFF )
        return;
    /* direction 1 (server), no time since the last packet: varlen 1. */
    net_capture_p1(cap->f, 1);
    if( wire >= 0x80 )
        net_capture_p2(cap->f, 0x8000u | (uint32_t)wire);
    else
        net_capture_p1(cap->f, (uint32_t)wire);
    if( size == PKTIN_LENGTH_VARU8 )
        net_capture_p1(cap->f, (uint32_t)len);
    else if( size == PKTIN_LENGTH_VARU16 )
        net_capture_p2(cap->f, (uint32_t)len);
    if( len > 0 )
        fwrite(data, 1, (size_t)len, cap->f);
    fflush(cap->f);
}

#endif
