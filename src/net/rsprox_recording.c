#include "rsprox_recording.h"
#include "mem/torirs_mem.h"

#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"

/* 3rd/rscache: the cache's own gzip reader, which the index server's
 * `.bin.gz` downloads are an ordinary instance of. */
#include "compression.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* BinaryHeader.HEADER_VERSION: the only layout RSProx has written. */
#define RSPROX_HEADER_VERSION 1

static void
fail(
    char* err,
    int err_cap,
    char const* fmt,
    ...)
{
    va_list ap;

    assert(err);
    assert(err_cap > 0);
    va_start(ap, fmt);
    vsnprintf(err, (size_t)err_cap, fmt, ap);
    va_end(ap);
}

/* --- a bounds-checked reader over the file ------------------------------- */

struct RecReader
{
    uint8_t const* data;
    size_t len;
    size_t pos;
    int overrun;
};

static int
rd_has(
    struct RecReader* r,
    size_t n)
{
    if( r->overrun || r->len - r->pos < n )
    {
        r->overrun = 1;
        return 0;
    }
    return 1;
}

static uint32_t
rd_g1(struct RecReader* r)
{
    if( !rd_has(r, 1) )
        return 0;
    return r->data[r->pos++];
}

static uint32_t
rd_g2(struct RecReader* r)
{
    uint32_t v;

    if( !rd_has(r, 2) )
        return 0;
    v = ((uint32_t)r->data[r->pos] << 8) | r->data[r->pos + 1];
    r->pos += 2;
    return v;
}

static uint32_t
rd_g4(struct RecReader* r)
{
    uint32_t hi = rd_g2(r);

    return (hi << 16) | rd_g2(r);
}

static uint64_t
rd_g8(struct RecReader* r)
{
    uint64_t hi = rd_g4(r);

    return (hi << 32) | rd_g4(r);
}

static void
rd_skip(
    struct RecReader* r,
    size_t n)
{
    if( rd_has(r, n) )
        r->pos += n;
}

/* Jagex `pjstr`: bytes up to a 0 terminator. */
static void
rd_jstr(
    struct RecReader* r,
    char* out,
    size_t cap)
{
    size_t n = 0;

    for( ;; )
    {
        uint32_t c;

        if( !rd_has(r, 1) )
            break;
        c = r->data[r->pos++];
        if( c == 0 )
            break;
        if( n + 1 < cap )
            out[n++] = (char)c;
    }
    out[n] = '\0';
}

/*
 * RSProt's pMidiVarLen, which BinaryStream writes the direction and delta
 * with: seven bits a byte, most significant group first, the top bit set on
 * every byte but the last.
 */
static uint32_t
rd_varlen(struct RecReader* r)
{
    uint32_t v = 0;

    for( int i = 0; i < 5; i++ )
    {
        uint32_t c = rd_g1(r);

        v = (v << 7) | (c & 0x7f);
        if( !(c & 0x80) )
            return v;
    }
    r->overrun = 1;
    return 0;
}

/* --- open ---------------------------------------------------------------- */

static int
inflate_if_gzip(
    uint8_t** data,
    size_t* len,
    char* err,
    int err_cap)
{
    uint8_t* raw;
    uint32_t raw_len;
    uint32_t got;

    if( *len < 18 || (*data)[0] != 0x1f || (*data)[1] != 0x8b )
        return 0;
    raw_len = RSCache_CompressionGzipUncompressedSize(*data, (int)*len);
    if( raw_len == 0 )
    {
        fail(err, err_cap, "the gzip stream is empty");
        return -1;
    }
    raw = ToriMem_Alloc(ToriMem_Net(), raw_len);
    assert(raw);
    got = RSCache_CompressionGzipDecompress(raw, (int)raw_len, *data, (int)*len, 0);
    if( got != raw_len )
    {
        ToriMem_Free(ToriMem_Net(), raw);
        fail(err, err_cap, "the gzip stream inflated to %u bytes, its footer says %u", got,
             raw_len);
        return -1;
    }
    ToriMem_Free(ToriMem_Net(), *data);
    *data = raw;
    *len = raw_len;
    return 0;
}

int
RsproxRecording_Open(
    struct RsproxRecording* rec,
    uint8_t* data,
    size_t len,
    char* err,
    int err_cap)
{
    struct RecReader r;
    uint32_t n;

    assert(rec);
    assert(data);
    assert(err);
    memset(rec, 0, sizeof(*rec));
    if( inflate_if_gzip(&data, &len, err, err_cap) != 0 )
    {
        ToriMem_Free(ToriMem_Net(), data);
        return -1;
    }
    rec->data = data;
    rec->data_len = len;

    memset(&r, 0, sizeof(r));
    r.data = data;
    r.len = len;
    rec->header_version = (int)rd_g4(&r);
    if( rec->header_version != RSPROX_HEADER_VERSION )
    {
        fail(err, err_cap, "header version %d (this reader knows %d): not an RSProx recording",
             rec->header_version, RSPROX_HEADER_VERSION);
        RsproxRecording_Free(rec);
        return -1;
    }
    rec->revision = (int)rd_g4(&r);
    rec->sub_revision = (int)rd_g4(&r);
    rec->client_type = (int)rd_g1(&r);
    rec->platform_type = (int)rd_g1(&r);
    rec->timestamp_ms = (int64_t)rd_g8(&r);
    rec->world_id = (int)rd_g2(&r);
    rec->world_flags = (int)rd_g4(&r);
    rec->world_location = (int)rd_g1(&r);
    rd_jstr(&r, rec->world_host, sizeof(rec->world_host));
    rd_jstr(&r, rec->world_activity, sizeof(rec->world_activity));
    rec->local_player_index = (int)rd_g2(&r);
    n = rd_g2(&r);
    rd_skip(&r, n); /* accountHash */
    rd_jstr(&r, rec->client_name, sizeof(rec->client_name));
    n = rd_g4(&r);
    rd_skip(&r, n); /* js5MasterIndex: this client has its own cache */
    if( r.overrun )
    {
        fail(err, err_cap, "the header runs past the end of the file (%zu bytes)", len);
        RsproxRecording_Free(rec);
        return -1;
    }
    rec->stream_offset = r.pos;
    return 0;
}

int
RsproxRecording_OpenFile(
    struct RsproxRecording* rec,
    char const* path,
    char* err,
    int err_cap)
{
    FILE* f;
    long size;
    uint8_t* data;

    assert(rec);
    assert(path);
    memset(rec, 0, sizeof(*rec));
    f = fopen(path, "rb");
    if( !f )
    {
        fail(err, err_cap, "cannot open %s", path);
        return -1;
    }
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        fail(err, err_cap, "%s is empty or unreadable", path);
        return -1;
    }
    data = ToriMem_Alloc(ToriMem_Net(), (size_t)size);
    assert(data);
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        ToriMem_Free(ToriMem_Net(), data);
        fail(err, err_cap, "short read on %s", path);
        return -1;
    }
    fclose(f);
    return RsproxRecording_Open(rec, data, (size_t)size, err, err_cap);
}

/* --- index --------------------------------------------------------------- */

int
RsproxRecording_Index(
    struct RsproxRecording* rec,
    struct GameProtoRevTable const* rev,
    char* err,
    int err_cap)
{
    struct RecReader r;
    uint64_t t_ms = 0;
    int cap = 0;
    int tick = 0;

    assert(rec);
    assert(rec->data);
    assert(rev);
    assert(rev->packetin_size);
    assert(rev->packetout_size);
    assert(err);

    memset(&r, 0, sizeof(r));
    r.data = rec->data;
    r.len = rec->data_len;
    r.pos = rec->stream_offset;
    rec->frame_count = 0;
    rec->client_packet_count = 0;

    while( r.pos < r.len )
    {
        size_t const at = r.pos;
        uint32_t const packed = rd_varlen(&r);
        int const from_server = (int)(packed & 1u);
        int opcode;
        int size;

        t_ms += packed >> 1;
        opcode = (int)rd_g1(&r);
        if( from_server && opcode >= 0x80 )
            opcode = ((opcode - 0x80) << 8) | (int)rd_g1(&r);
        /* An opcode neither table knows reads as 0 bytes (the live framer's
         * resynchronising guess). Here that shifts the rest of the log, and
         * the walk then fails to land on the end of the file: refused below. */
        if( from_server && opcode == RSPROX_RECORDING_RECONNECT )
            size = PKTIN_LENGTH_VARU16;
        else if( from_server && opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            size = 0;
        else
            size = from_server ? rev->packetin_size(opcode) : rev->packetout_size(opcode);
        if( size == PKTIN_LENGTH_VARU8 )
            size = (int)rd_g1(&r);
        else if( size == PKTIN_LENGTH_VARU16 )
            size = (int)rd_g2(&r);
        if( r.overrun || size < 0 || !rd_has(&r, (size_t)size) )
        {
            fail(err, err_cap,
                 "%s opcode %d at byte %zu: its payload runs past the end of the file "
                 "(revision %d's prot table does not frame this recording)",
                 from_server ? "server" : "client", opcode, at, rec->revision);
            return -1;
        }
        if( from_server )
        {
            struct RsproxRecordingFrame* f;

            if( rec->frame_count == cap )
            {
                cap = cap ? cap * 2 : 4096;
                rec->frames = ToriMem_Realloc(ToriMem_Net(), rec->frames,
                                              (size_t)cap * sizeof(*rec->frames));
                assert(rec->frames);
            }
            f = &rec->frames[rec->frame_count++];
            f->t_ms = t_ms;
            f->tick = tick;
            f->opcode = opcode;
            f->offset = (uint32_t)r.pos;
            f->length = (uint32_t)size;
            f->reconnect_index = opcode == RSPROX_RECORDING_RECONNECT ? rec->local_player_index : -1;
            if( rev->packetin_code(opcode) == PKT_NAME_SERVER_TICK_END )
                tick++;
        }
        else
            rec->client_packet_count++;
        r.pos += (size_t)size;
    }
    rec->tick_count = tick;
    rec->duration_ms = t_ms;
    return 0;
}

char const*
RsproxRecording_RevName(struct RsproxRecording const* rec)
{
    static char names[RSPROX_RECORDING_REV_LAST - RSPROX_RECORDING_REV_FIRST + 1][16];

    assert(rec);
    /* Every revision plays on the data-driven family's table (net/rev/rsprox/),
     * which is generated from the same RSProx that wrote the recording. The
     * cache is this tree's rev 241, so an older recording's ids can name
     * different things than they did when it was recorded (seq 579 is an axe
     * special here): the packets decode exactly, the content they point at may
     * not. */
    if( rec->revision < RSPROX_RECORDING_REV_FIRST || rec->revision > RSPROX_RECORDING_REV_LAST )
        return NULL;
    {
        char* name = names[rec->revision - RSPROX_RECORDING_REV_FIRST];

        snprintf(name, sizeof(names[0]), "rsprox%d", rec->revision);
        return name;
    }
}

void
RsproxRecording_Free(struct RsproxRecording* rec)
{
    if( !rec )
        return;
    if( rec->frames )
        ToriMem_Free(ToriMem_Net(), rec->frames);
    if( rec->data )
        ToriMem_Free(ToriMem_Net(), rec->data);
    memset(rec, 0, sizeof(*rec));
}
