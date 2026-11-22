#include "torirs_server_recording.h"

#include "torirs_server.h"
#include "torirs_server_auth.h"
#include "torirs_server_claim.h"
#include "torirs_server_savefile.h"
#include "torirs_server_wire.h"
#include "net/rsprox_recording.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* RSProx's BinaryHeader.HEADER_VERSION (net/rsprox_recording.c reads only this). */
#define RECORDING_HEADER_VERSION 1
/* RSProx's own server opcode for a reconnect (RSPROX_RECORDING_RECONNECT). */
#define RECORDING_RECONNECT_OPCODE 0xFF
/* Recorded at once: well past any world's player cap that records. A login
 * beyond it plays unrecorded and says so. */
#define RECORDING_SLOTS 64
/* A tick's packets are written in one go; a buffer this full is written at
 * once instead (a REBUILD into a busy area is tens of KB). */
#define RECORDING_FLUSH_AT (256 * 1024)

struct ToriRSServerRecording
{
    int in_use;
    /** The account's save name (ToriRSServer_SaveFileName). */
    char key[64];
    char path[1024];
    /** NULL while parked. Unbuffered: every write is whole records. */
    FILE* file;
    /** What this tick has gathered, not yet written. */
    uint8_t* buf;
    size_t len;
    size_t cap;
    /** Wall-clock ms of the last packet, either direction: the next delta. */
    int64_t last_ms;
    /** Wall-clock ms the player left its slot; 0 while one holds it. */
    int64_t parked_ms;
    /** Set when a write failed: the file is not written to again. */
    int broken;
};

static struct ToriRSServerRecording g_recordings[RECORDING_SLOTS];
/* ToriRSServer_RecordingClockSet: 0 = the wall clock. */
static int64_t g_clock_ms;

void
ToriRSServer_RecordingClockSet(int64_t ms)
{
    assert(ms >= 0);
    g_clock_ms = ms;
}

static int64_t
real_ms(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    /* 100 ns ticks since 1601 -> ms since 1970. */
    return (int64_t)(t.QuadPart / 10000ULL) - 11644473600000LL;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/* The recordings' clock: the one a caller set, else the wall's. */
static int64_t
wall_ms(void)
{
    return g_clock_ms ? g_clock_ms : real_ms();
}

/* --- the gather buffer ---------------------------------------------------- */

static void
put_bytes(
    struct ToriRSServerRecording* rec,
    const void* data,
    size_t n)
{
    if( rec->len + n > rec->cap )
    {
        size_t cap = rec->cap ? rec->cap : 64 * 1024;

        while( cap < rec->len + n )
            cap *= 2;
        rec->buf = realloc(rec->buf, cap);
        assert(rec->buf);
        rec->cap = cap;
    }
    if( n > 0 )
        memcpy(rec->buf + rec->len, data, n);
    rec->len += n;
}

static void
put_u8(
    struct ToriRSServerRecording* rec,
    uint32_t v)
{
    uint8_t b = (uint8_t)v;

    put_bytes(rec, &b, 1);
}

static void
put_u16(
    struct ToriRSServerRecording* rec,
    uint32_t v)
{
    put_u8(rec, v >> 8);
    put_u8(rec, v);
}

static void
put_u32(
    struct ToriRSServerRecording* rec,
    uint32_t v)
{
    put_u16(rec, v >> 16);
    put_u16(rec, v);
}

static void
put_u64(
    struct ToriRSServerRecording* rec,
    uint64_t v)
{
    put_u32(rec, (uint32_t)(v >> 32));
    put_u32(rec, (uint32_t)v);
}

/* Jagex pjstr: the bytes and a 0. */
static void
put_jstr(
    struct ToriRSServerRecording* rec,
    const char* s)
{
    put_bytes(rec, s, strlen(s) + 1);
}

/* RSProt's pMidiVarLen, the reader's rd_varlen: seven bits a byte, most
 * significant group first, the top bit on every byte but the last. */
static void
put_varlen(
    struct ToriRSServerRecording* rec,
    uint32_t v)
{
    uint8_t groups[5];
    int n = 0;

    do
    {
        groups[n++] = (uint8_t)(v & 0x7f);
        v >>= 7;
    } while( v && n < 5 );
    while( n-- > 0 )
        put_u8(rec, groups[n] | (n > 0 ? 0x80 : 0));
}

/* The varint that opens a packet record: direction bit, then the ms since
 * the previous packet. */
static void
put_record_head(
    struct ToriRSServerRecording* rec,
    int from_server)
{
    int64_t now = wall_ms();
    int64_t delta = now - rec->last_ms;

    if( delta < 0 )
        delta = 0;
    /* 31 bits of ms is 24 days: a longer gap is not a session. */
    if( delta > 0x3fffffff )
        delta = 0x3fffffff;
    rec->last_ms = now;
    put_varlen(rec, ((uint32_t)delta << 1) | (from_server ? 1u : 0u));
}

static void
write_out(struct ToriRSServerRecording* rec)
{
    if( rec->len == 0 )
        return;
    if( rec->file && !rec->broken )
    {
        if( fwrite(rec->buf, 1, rec->len, rec->file) != rec->len )
        {
            fprintf(stderr, "torirsserver: recording %s: write failed; no more is written\n",
                    rec->path);
            rec->broken = 1;
        }
    }
    rec->len = 0;
}

static void
close_file(struct ToriRSServerRecording* rec)
{
    write_out(rec);
    if( rec->file )
        fclose(rec->file);
    rec->file = NULL;
}

static void
forget(struct ToriRSServerRecording* rec)
{
    close_file(rec);
    free(rec->buf);
    memset(rec, 0, sizeof(*rec));
}

/* --- open ----------------------------------------------------------------- */

static FILE*
open_unbuffered(
    const char* path,
    const char* mode)
{
    FILE* file = fopen(path, mode);

    if( file )
        setvbuf(file, NULL, _IONBF, 0);
    return file;
}

/* BinaryHeader (net/rsprox_recording.c reads it field for field). */
static void
put_header(
    struct ToriRSServerRecording* rec,
    const struct ToriRSServerPlayer* player,
    int64_t timestamp_ms)
{
    const struct ToriRSServerWire* wire =
        (player->world && player->world->wire) ? player->world->wire : ToriRSServer_WireDefault();

    put_u32(rec, RECORDING_HEADER_VERSION);
    put_u32(rec, (uint32_t)wire->revision);
    put_u32(rec, 1);                                /* subRevision */
    put_u8(rec, (uint32_t)player->client_type);     /* clientType: picks osrs/osrsm */
    put_u8(rec, (uint32_t)player->platform_type);   /* platformType */
    put_u64(rec, (uint64_t)timestamp_ms);
    put_u16(rec, 1);                                /* worldId */
    put_u32(rec, 0);                                /* worldFlags */
    put_u8(rec, 0);                                 /* worldLocation */
    put_jstr(rec, "torirs");                        /* host */
    put_jstr(rec, player->display_name);            /* activity: who it is */
    put_u16(rec, (uint32_t)ToriRSServer_WirePlayerIndex(player->pid));
    put_u16(rec, 0);                                /* accountHash: none */
    /* clientName: who wrote it and the wire it speaks. The client type above
     * is the client's own (7 for the phone, which the replay's layout needs),
     * but this server answers every client in its one wire, so the replay
     * frames the file by this name (RsproxRecording_Rev). */
    {
        char client_name[64];

        snprintf(client_name, sizeof(client_name), "%s:%s", RSPROX_RECORDING_TORIRSSERVER,
                 wire->name);
        put_jstr(rec, client_name);
    }
    put_u32(rec, 0);                                /* js5MasterIndex: none */
}

static struct ToriRSServerRecording*
find_parked(const char* key)
{
    for( int i = 0; i < RECORDING_SLOTS; i++ )
    {
        if( g_recordings[i].in_use && g_recordings[i].parked_ms &&
            strcmp(g_recordings[i].key, key) == 0 )
            return &g_recordings[i];
    }
    return NULL;
}

static struct ToriRSServerRecording*
free_slot(void)
{
    for( int i = 0; i < RECORDING_SLOTS; i++ )
    {
        if( !g_recordings[i].in_use )
            return &g_recordings[i];
    }
    return NULL;
}

/* A new file at `path` in the free slot `rec`, its header written, and the
 * player's recording set: or nothing, said on stderr. */
static void
open_new(
    struct ToriRSServerRecording* rec,
    struct ToriRSServerPlayer* player,
    const char* key,
    const char* path,
    int64_t now)
{
    memset(rec, 0, sizeof(*rec));
    snprintf(rec->key, sizeof(rec->key), "%s", key);
    snprintf(rec->path, sizeof(rec->path), "%s", path);
    ToriRSServer_SaveMkdirParent(rec->path);
    rec->file = open_unbuffered(rec->path, "wb");
    if( !rec->file )
    {
        fprintf(stderr, "torirsserver: recording %s: cannot open; %s plays unrecorded\n",
                rec->path, player->display_name);
        memset(rec, 0, sizeof(*rec));
        return;
    }
    rec->in_use = 1;
    rec->last_ms = now;
    put_header(rec, player, now);
    write_out(rec);
    player->recording = rec;
    fprintf(stderr, "torirsserver: recording %s to %s\n", player->display_name, rec->path);
}

void
ToriRSServer_RecordingLogin(
    struct ToriRSServerPlayer* player,
    int reconnect)
{
    char key[64];
    char dir[1024];
    char path[1100];
    char stamp[32];
    struct ToriRSServerRecording* rec;
    int64_t now;
    time_t secs;
    struct tm tm_utc;

    assert(player);
    player->recording = NULL;
    ToriRSServer_SaveFileName(player->account_name, key, sizeof(key));
    /* The account's `record` flag came with the login's check
     * (ToriRSServerPlayer.account_record): no file read on the tick. */
    if( !key[0] || !player->account_record )
        return;

    rec = find_parked(key);
    if( rec && reconnect )
    {
        rec->file = open_unbuffered(rec->path, "ab");
        if( !rec->file )
        {
            fprintf(stderr, "torirsserver: recording %s: cannot reopen for the reconnect\n",
                    rec->path);
            forget(rec);
            return;
        }
        rec->parked_ms = 0;
        player->recording = rec;
        return;
    }
    /* A fresh login is a new session: whatever the last one left parked is
     * finished. */
    if( rec )
        forget(rec);

    rec = free_slot();
    if( !rec )
    {
        fprintf(stderr, "torirsserver: %d recordings already open; %s plays unrecorded\n",
                RECORDING_SLOTS, player->account_name);
        return;
    }
    now = wall_ms();
    secs = (time_t)(now / 1000);
#ifdef _WIN32
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_utc);
    snprintf(dir, sizeof(dir), "%s/recordings/%s", ToriRSServer_SaveDir(), key);
    snprintf(path, sizeof(path), "%s/%s.bin", dir, stamp);
    open_new(rec, player, key, path, now);
}

int
ToriRSServer_RecordingOpenPath(
    struct ToriRSServerPlayer* player,
    const char* path)
{
    struct ToriRSServerRecording* rec;
    char key[64];

    assert(player);
    assert(path);
    assert(path[0]);
    player->recording = NULL;
    rec = free_slot();
    if( !rec )
    {
        fprintf(stderr, "torirsserver: %d recordings already open; %s plays unrecorded\n",
                RECORDING_SLOTS, player->display_name);
        return 0;
    }
    /* Keyed by the player's own name, never an account's: a parked one is
     * claimed by no login. */
    ToriRSServer_SaveFileName(player->display_name, key, sizeof(key));
    open_new(rec, player, key, path, wall_ms());
    return player->recording != NULL;
}

/* --- packets -------------------------------------------------------------- */

static void
after_put(struct ToriRSServerRecording* rec)
{
    if( rec->len >= RECORDING_FLUSH_AT )
        write_out(rec);
}

void
ToriRSServer_RecordingServerPacket(
    struct ToriRSServerRecording* rec,
    int opcode,
    const uint8_t* payload,
    int len,
    int var)
{
    assert(rec);
    assert(opcode >= 0);
    assert(len >= 0);
    assert(var >= 0 && var <= 2);
    if( len > 0 )
        assert(payload);
    put_record_head(rec, 1);
    /* The reader takes a first byte at or above 0x80 as the high half of a
     * two-byte opcode, whatever the revision: so does this. */
    if( opcode >= 0x80 )
    {
        put_u8(rec, (uint32_t)((opcode >> 8) | 0x80));
        put_u8(rec, (uint32_t)(opcode & 0xff));
    }
    else
        put_u8(rec, (uint32_t)opcode);
    if( var == 1 )
        put_u8(rec, (uint32_t)len);
    else if( var == 2 )
        put_u16(rec, (uint32_t)len);
    put_bytes(rec, payload, (size_t)len);
    after_put(rec);
}

void
ToriRSServer_RecordingClientPacket(
    struct ToriRSServerRecording* rec,
    int opcode,
    const uint8_t* payload,
    int len,
    int len_bytes)
{
    assert(rec);
    assert(opcode >= 0 && opcode <= 0xff);
    assert(len >= 0);
    assert(len_bytes >= 0 && len_bytes <= 2);
    if( len > 0 )
        assert(payload);
    put_record_head(rec, 0);
    put_u8(rec, (uint32_t)opcode);
    if( len_bytes == 1 )
        put_u8(rec, (uint32_t)len);
    else if( len_bytes == 2 )
        put_u16(rec, (uint32_t)len);
    put_bytes(rec, payload, (size_t)len);
    after_put(rec);
}

void
ToriRSServer_RecordingReconnect(
    struct ToriRSServerRecording* rec,
    const uint8_t* body,
    int len)
{
    assert(rec);
    ToriRSServer_RecordingServerPacket(rec, RECORDING_RECONNECT_OPCODE, body, len, 2);
}

/* --- leaving -------------------------------------------------------------- */

void
ToriRSServer_RecordingPark(struct ToriRSServerPlayer* player)
{
    struct ToriRSServerRecording* rec;

    assert(player);
    rec = player->recording;
    if( !rec )
        return;
    player->recording = NULL;
    close_file(rec);
    rec->parked_ms = wall_ms();
}

void
ToriRSServer_RecordingTick(void)
{
    int64_t now = 0;

    for( int i = 0; i < RECORDING_SLOTS; i++ )
    {
        struct ToriRSServerRecording* rec = &g_recordings[i];

        if( !rec->in_use )
            continue;
        if( !rec->parked_ms )
        {
            write_out(rec);
            continue;
        }
        if( !now )
            now = wall_ms();
        /* Past the claim window no reconnect can resume it. */
        if( now - rec->parked_ms > TORIRSSERVER_CLAIM_WINDOW_MS )
            forget(rec);
    }
}
