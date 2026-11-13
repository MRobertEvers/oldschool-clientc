/*
 * The tick table and the marker lanes of an RSProx recording (replay_timeline.h).
 *
 * One pass over the indexed frames. Only the packets a lane needs are decoded
 * (REBUILD_*, MIDI_SONG*, IF_OPENTOP, IF_OPENSUB, MESSAGE_GAME), with the
 * revision's own wire parser, the way proto_reconnect.c's camera_player reads
 * CAM_TARGET; everything else is classified by name alone.
 */
#include "replay/replay_timeline.h"

#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h" /* gameproto_free */
#include "net/rsprox_recording.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- building ------------------------------------------------------------ */

static void
lane_push(
    struct ReplayLane* lane,
    uint32_t tick,
    uint32_t kind,
    uint32_t arg)
{
    if( lane->count == lane->cap )
    {
        lane->cap = lane->cap ? lane->cap * 2 : 64;
        lane->marks = ToriMem_Realloc(ToriMem_Net(), lane->marks, (size_t)lane->cap * sizeof(*lane->marks));
        assert(lane->marks);
    }
    /* Frames are walked in order and a frame's tick never goes down. */
    assert(lane->count == 0 || lane->marks[lane->count - 1].tick <= tick);
    lane->marks[lane->count].tick = tick;
    lane->marks[lane->count].kind = kind;
    lane->marks[lane->count].arg = arg;
    lane->count++;
}

static uint32_t
chat_push(
    struct ReplayTimeline* tl,
    char const* text)
{
    size_t n = strlen(text);
    uint32_t const at = (uint32_t)tl->chat_len;

    if( n > REPLAY_CHAT_TEXT_MAX )
        n = REPLAY_CHAT_TEXT_MAX;
    if( tl->chat_len + n + 1 > tl->chat_cap )
    {
        size_t cap = tl->chat_cap ? tl->chat_cap : 4096;

        while( cap < tl->chat_len + n + 1 )
            cap *= 2;
        tl->chat_text = ToriMem_Realloc(ToriMem_Net(), tl->chat_text, cap);
        assert(tl->chat_text);
        tl->chat_cap = cap;
    }
    memcpy(tl->chat_text + tl->chat_len, text, n);
    tl->chat_text[tl->chat_len + n] = '\0';
    tl->chat_len += n + 1;
    return at;
}

/* Decode frame `fr` as `name`; 1 when it parsed as that packet. The caller
 * gameproto_free()s `pkt` either way. */
static int
parse_frame(
    struct GameProtoRevTable const* rev,
    struct RsproxRecording const* rec,
    struct RsproxRecordingFrame const* fr,
    int name,
    struct RevPacket* pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    return rev->parse_wire(rev, fr->opcode, rec->data + fr->offset, (int)fr->length, pkt) == 1 &&
           (int)pkt->packet_type == name;
}

static void
note_rebuild(
    struct ReplayTimeline* tl,
    struct GameProtoRevTable const* rev,
    struct RsproxRecording const* rec,
    int f,
    int name)
{
    struct RsproxRecordingFrame const* fr = &rec->frames[f];
    struct RevPacket pkt;
    struct ReplayScene* scene;

    tl->scenes = ToriMem_Realloc(ToriMem_Net(), tl->scenes, (size_t)(tl->scene_count + 1) * sizeof(*tl->scenes));
    assert(tl->scenes);
    scene = &tl->scenes[tl->scene_count];
    scene->frame = (uint32_t)f;
    scene->tick = (uint32_t)fr->tick;
    scene->base_x = -1;
    scene->base_z = -1;
    if( parse_frame(rev, rec, fr, name, &pkt) )
    {
        /* rev->scene_base, or the rule the table states for a NULL one. */
        if( rev->scene_base )
        {
            int x;
            int z;

            if( rev->scene_base(&pkt, &x, &z) )
            {
                scene->base_x = x;
                scene->base_z = z;
            }
        }
        else
        {
            scene->base_x = (pkt._map_rebuild.zonex - 6) * 8;
            scene->base_z = (pkt._map_rebuild.zonez - 6) * 8;
        }
    }
    gameproto_free(&pkt);
    lane_push(&tl->lanes[REPLAY_LANE_SCENE], (uint32_t)fr->tick, 0, (uint32_t)tl->scene_count);
    tl->scene_count++;
}

static void
note_packet(
    struct ReplayTimeline* tl,
    struct GameProtoRevTable const* rev,
    struct RsproxRecording const* rec,
    int f,
    uint16_t* flags)
{
    struct RsproxRecordingFrame const* fr = &rec->frames[f];
    uint32_t const tick = (uint32_t)fr->tick;
    int const name = rev->packetin_code(fr->opcode);
    struct RevPacket pkt;

    if( name == PKT_NAME_REBUILD_NORMAL || name == PKT_NAME_REBUILD_REGION )
    {
        note_rebuild(tl, rev, rec, f, name);
        *flags |= REPLAY_TICK_REBUILD;
    }
    else if( name == PKT_NAME_MIDI_SONG )
    {
        if( parse_frame(rev, rec, fr, name, &pkt) )
            lane_push(&tl->lanes[REPLAY_LANE_MUSIC], tick, 0, (uint32_t)pkt._midi_song.id);
        gameproto_free(&pkt);
    }
    else if( name == PKT_NAME_MIDI_SONG_WITHSECONDARY )
    {
        if( parse_frame(rev, rec, fr, name, &pkt) )
            lane_push(&tl->lanes[REPLAY_LANE_MUSIC], tick, 0,
                      (uint32_t)pkt._midi_song_with_secondary.primary_id);
        gameproto_free(&pkt);
    }
    else if( name == PKT_NAME_LOGOUT || name == PKT_NAME_LOGOUT_WITHREASON ||
             name == PKT_NAME_LOGOUT_TRANSFER )
        lane_push(&tl->lanes[REPLAY_LANE_SESSION], tick, REPLAY_SESSION_LOGOUT, 0);
    else if( name == PKT_NAME_IF_OPENTOP )
    {
        if( parse_frame(rev, rec, fr, name, &pkt) )
            lane_push(&tl->lanes[REPLAY_LANE_INTERFACE], tick, REPLAY_INTERFACE_TOP,
                      (uint32_t)pkt._if_opentop.interface_id);
        gameproto_free(&pkt);
        *flags |= REPLAY_TICK_TOP_OPENED;
    }
    else if( name == PKT_NAME_IF_OPENSUB )
    {
        /* Type 0 is a modal: a bank, a shop, a dialogue. Overlays and tabs
         * are furniture. */
        if( parse_frame(rev, rec, fr, name, &pkt) && pkt._if_opensub.type == 0 )
            lane_push(&tl->lanes[REPLAY_LANE_INTERFACE], tick, REPLAY_INTERFACE_MODAL,
                      (uint32_t)pkt._if_opensub.interface_id);
        gameproto_free(&pkt);
    }
    else if( name == PKT_NAME_MESSAGE_GAME )
    {
        if( parse_frame(rev, rec, fr, name, &pkt) && pkt._message_game.text )
            lane_push(&tl->lanes[REPLAY_LANE_CHAT], tick, 0, chat_push(tl, pkt._message_game.text));
        gameproto_free(&pkt);
    }
    else if( name == PKT_NAME_WORLDENTITY_INFO || name == PKT_NAME_REBUILD_WORLDENTITY ||
             name == PKT_NAME_SET_ACTIVE_WORLD )
        *flags |= REPLAY_TICK_WORLD_ENTITY;
}

static void
build_density(
    struct ReplayTimeline* tl,
    int lane_id)
{
    struct ReplayLane* lane = &tl->lanes[lane_id];

    for( int k = 0; k < REPLAY_LANE_LEVELS; k++ )
    {
        int const cells = (tl->tick_count >> k) + 1;

        lane->density_cells[k] = cells;
        lane->density[k] = ToriMem_Calloc(ToriMem_Net(), (size_t)cells, sizeof(uint32_t));
        assert(lane->density[k]);
        if( k == 0 )
        {
            if( lane_id == REPLAY_LANE_ACTIVITY )
            {
                for( int t = 0; t < tl->tick_count; t++ )
                    lane->density[0][t] = tl->activity[t];
            }
            for( int i = 0; i < lane->count; i++ )
                lane->density[0][lane->marks[i].tick]++;
            continue;
        }
        for( int i = 0; i < lane->density_cells[k - 1]; i++ )
            lane->density[k][i >> 1] += lane->density[k - 1][i];
    }
}

struct ReplayTimeline*
ReplayTimeline_Build(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev)
{
    struct ReplayTimeline* tl;
    void* pi_state = NULL;
    int cur_tick = -1;
    uint16_t scene = REPLAY_SCENE_NONE;
    int input = 0;

    assert(rec);
    assert(rev);
    assert(rev->packetin_code);
    assert(rev->parse_wire);
    assert(rec->frame_count >= 0);
    assert(rec->tick_count >= 0);

    tl = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*tl));
    assert(tl);
    tl->tick_count = rec->tick_count;
    tl->duration_ms = rec->duration_ms;
    tl->timestamp_ms = rec->timestamp_ms;
    tl->ticks = ToriMem_Calloc(ToriMem_Net(), (size_t)(tl->tick_count + 1), sizeof(*tl->ticks));
    assert(tl->ticks);
    tl->activity = ToriMem_Calloc(ToriMem_Net(), (size_t)(tl->tick_count + 1), sizeof(*tl->activity));
    assert(tl->activity);

    /* A login REBUILD's decode seeds the session's player table
     * (proto_adapt.c adapt_rebuild_normal); the pass must leave it as it
     * found it. */
    if( rev->player_info_state_size )
    {
        pi_state = ToriMem_Alloc(ToriMem_Net(), rev->player_info_state_size());
        assert(pi_state);
        rev->player_info_state_save(pi_state);
    }

    for( int f = 0; f < rec->frame_count; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec->frames[f];
        uint16_t flags = 0;

        assert(fr->tick >= cur_tick);
        if( fr->tick < tl->tick_count && fr->tick != cur_tick )
        {
            /* Every tick ends with a SERVER_TICK_END, so none is empty; the
             * loop only guards the table against a recording where one is. */
            for( int t = cur_tick + 1; t <= fr->tick; t++ )
            {
                while( input < rec->client_packet_count && rec->inputs[input].frame < f )
                    input++;
                tl->ticks[t].first_frame = (uint32_t)f;
                tl->ticks[t].t_ms = (uint32_t)fr->t_ms;
                tl->ticks[t].first_input = (uint32_t)input;
                tl->ticks[t].scene = scene;
            }
            cur_tick = fr->tick;
        }
        if( fr->tick < tl->tick_count )
            tl->activity[fr->tick]++;

        if( f > 0 && fr->t_ms > rec->frames[f - 1].t_ms + REPLAY_MAX_GAP_MS )
        {
            lane_push(&tl->lanes[REPLAY_LANE_SESSION], (uint32_t)fr->tick, REPLAY_SESSION_STALL,
                      (uint32_t)(fr->t_ms - rec->frames[f - 1].t_ms - REPLAY_MAX_GAP_MS));
            flags |= REPLAY_TICK_STALL_BEFORE;
        }

        if( fr->opcode == RSPROX_RECORDING_RECONNECT )
        {
            lane_push(&tl->lanes[REPLAY_LANE_SESSION], (uint32_t)fr->tick, REPLAY_SESSION_RECONNECT, 0);
            flags |= REPLAY_TICK_RECONNECT;
        }
        else if( fr->opcode != RSPROX_RECORDING_PACKET_GROUP_END )
        {
            int const scenes_before = tl->scene_count;

            note_packet(tl, rev, rec, f, &flags);
            if( tl->scene_count != scenes_before )
            {
                scene = (uint16_t)(tl->scene_count - 1);
                if( fr->tick < tl->tick_count )
                    tl->ticks[fr->tick].scene = scene;
            }
        }
        if( fr->tick < tl->tick_count )
            tl->ticks[fr->tick].flags |= flags;
    }

    if( pi_state )
    {
        rev->player_info_state_load(pi_state);
        ToriMem_Free(ToriMem_Net(), pi_state);
    }

    for( int lane = 0; lane < REPLAY_LANE_COUNT; lane++ )
        build_density(tl, lane);
    return tl;
}

void
ReplayTimeline_Free(struct ReplayTimeline* tl)
{
    if( !tl )
        return;
    for( int lane = 0; lane < REPLAY_LANE_COUNT; lane++ )
    {
        ToriMem_Free(ToriMem_Net(), tl->lanes[lane].marks);
        for( int k = 0; k < REPLAY_LANE_LEVELS; k++ )
            ToriMem_Free(ToriMem_Net(), tl->lanes[lane].density[k]);
    }
    ToriMem_Free(ToriMem_Net(), tl->ticks);
    ToriMem_Free(ToriMem_Net(), tl->activity);
    ToriMem_Free(ToriMem_Net(), tl->scenes);
    ToriMem_Free(ToriMem_Net(), tl->chat_text);
    ToriMem_Free(ToriMem_Net(), tl);
}

/* --- reading ------------------------------------------------------------- */

int
ReplayTimeline_TickAtMs(
    struct ReplayTimeline const* tl,
    uint32_t t_ms)
{
    int n;
    int lo;
    int hi;
    int guess;

    assert(tl);
    n = tl->tick_count;
    if( n == 0 )
        return 0;
    if( (uint64_t)t_ms >= tl->duration_ms )
        return n;
    /* Ticks are 600 ms apart except across a stall, so the guess is close and
     * a gallop from it lands in a few steps. Invariant afterwards:
     * ticks[lo].t_ms <= t_ms, and hi == n or ticks[hi].t_ms > t_ms. */
    guess = (int)(t_ms / 600u);
    if( guess > n - 1 )
        guess = n - 1;
    if( tl->ticks[guess].t_ms <= t_ms )
    {
        int step = 1;

        lo = guess;
        hi = guess + 1;
        while( hi < n && tl->ticks[hi].t_ms <= t_ms )
        {
            lo = hi;
            step *= 2;
            hi = guess + step;
        }
        if( hi > n )
            hi = n;
    }
    else
    {
        int step = 1;

        hi = guess;
        lo = guess - 1;
        while( lo > 0 && tl->ticks[lo].t_ms > t_ms )
        {
            hi = lo;
            step *= 2;
            lo = guess - step;
        }
        if( lo < 0 )
            lo = 0;
        if( tl->ticks[lo].t_ms > t_ms )
            return 0;
    }
    while( hi - lo > 1 )
    {
        int const mid = lo + (hi - lo) / 2;

        if( tl->ticks[mid].t_ms <= t_ms )
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

uint32_t
ReplayTimeline_MsAtTick(
    struct ReplayTimeline const* tl,
    int tick)
{
    assert(tl);
    assert(tick >= 0);
    assert(tick <= tl->tick_count);
    if( tick == tl->tick_count )
        return (uint32_t)tl->duration_ms;
    return tl->ticks[tick].t_ms;
}

/* The first mark whose tick is > `tick` (count when none). */
static int
lane_upper(
    struct ReplayLane const* lane,
    int tick)
{
    int lo = 0;
    int hi = lane->count;

    while( lo < hi )
    {
        int const mid = lo + (hi - lo) / 2;

        if( (int)lane->marks[mid].tick <= tick )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* The first mark whose tick is >= `tick` (count when none). */
static int
lane_lower(
    struct ReplayLane const* lane,
    int tick)
{
    int lo = 0;
    int hi = lane->count;

    while( lo < hi )
    {
        int const mid = lo + (hi - lo) / 2;

        if( (int)lane->marks[mid].tick < tick )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int
ReplayTimeline_NextMark(
    struct ReplayTimeline const* tl,
    int lane,
    int tick)
{
    int i;

    assert(tl);
    assert(lane >= 0);
    assert(lane < REPLAY_LANE_COUNT);
    i = lane_upper(&tl->lanes[lane], tick);
    return i < tl->lanes[lane].count ? (int)tl->lanes[lane].marks[i].tick : -1;
}

int
ReplayTimeline_PrevMark(
    struct ReplayTimeline const* tl,
    int lane,
    int tick)
{
    int i;

    assert(tl);
    assert(lane >= 0);
    assert(lane < REPLAY_LANE_COUNT);
    i = lane_lower(&tl->lanes[lane], tick);
    return i > 0 ? (int)tl->lanes[lane].marks[i - 1].tick : -1;
}

char const*
ReplayTimeline_ChatText(
    struct ReplayTimeline const* tl,
    struct ReplayMark const* mark)
{
    assert(tl);
    assert(mark);
    assert(mark->arg < tl->chat_len);
    return tl->chat_text + mark->arg;
}

/* --- JSON ---------------------------------------------------------------- */

struct JsonOut
{
    char* buf;
    size_t len;
    size_t cap;
};

static void
json_reserve(
    struct JsonOut* out,
    size_t more)
{
    if( out->len + more + 1 <= out->cap )
        return;
    while( out->len + more + 1 > out->cap )
        out->cap = out->cap ? out->cap * 2 : 65536;
    out->buf = realloc(out->buf, out->cap);
    assert(out->buf);
}

static void
json_raw(
    struct JsonOut* out,
    char const* s)
{
    size_t const n = strlen(s);

    json_reserve(out, n);
    memcpy(out->buf + out->len, s, n);
    out->len += n;
    out->buf[out->len] = '\0';
}

static void
json_fmt(
    struct JsonOut* out,
    char const* fmt,
    long long a,
    long long b,
    long long c)
{
    char tmp[128];

    snprintf(tmp, sizeof(tmp), fmt, a, b, c);
    json_raw(out, tmp);
}

/* A string. The game's text is cp1252-ish bytes, not UTF-8: a byte from 0x80
 * is written as the Latin-1 code point it names, so the JSON is valid
 * whatever the bytes are. */
static void
json_string(
    struct JsonOut* out,
    char const* s)
{
    json_raw(out, "\"");
    for( unsigned char const* p = (unsigned char const*)s; *p; p++ )
    {
        char tmp[8];

        if( *p == '"' || *p == '\\' )
        {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            tmp[2] = '\0';
        }
        else if( *p < 0x20 || *p >= 0x7f )
            snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
        else
        {
            tmp[0] = (char)*p;
            tmp[1] = '\0';
        }
        json_raw(out, tmp);
    }
    json_raw(out, "\"");
}

char*
ReplayTimeline_Json(struct ReplayTimeline const* tl)
{
    static char const* const k_session[] = { "reconnect", "logout", "stall" };
    static char const* const k_interface[] = { "top", "modal" };
    struct JsonOut out;
    struct ReplayLane const* lane;

    assert(tl);
    memset(&out, 0, sizeof(out));
    json_fmt(&out, "{\"ticks\":%lld,\"duration_ms\":%lld,\"timestamp_ms\":%lld", tl->tick_count,
             (long long)tl->duration_ms, (long long)tl->timestamp_ms);

    json_raw(&out, ",\"tick_ms\":[");
    for( int t = 0; t <= tl->tick_count; t++ )
        json_fmt(&out, t ? ",%lld" : "%lld", (long long)ReplayTimeline_MsAtTick(tl, t), 0, 0);
    json_raw(&out, "],\"activity\":[");
    for( int t = 0; t < tl->tick_count; t++ )
        json_fmt(&out, t ? ",%lld" : "%lld", (long long)tl->activity[t], 0, 0);
    json_raw(&out, "],\"lanes\":{");

    json_raw(&out, "\"scene\":[");
    lane = &tl->lanes[REPLAY_LANE_SCENE];
    for( int i = 0; i < lane->count; i++ )
    {
        struct ReplayScene const* scene = &tl->scenes[lane->marks[i].arg];

        json_fmt(&out, i ? ",{\"tick\":%lld,\"x\":%lld,\"z\":%lld}" : "{\"tick\":%lld,\"x\":%lld,\"z\":%lld}",
                 (long long)lane->marks[i].tick, (long long)scene->base_x, (long long)scene->base_z);
    }

    json_raw(&out, "],\"music\":[");
    lane = &tl->lanes[REPLAY_LANE_MUSIC];
    for( int i = 0; i < lane->count; i++ )
        json_fmt(&out, i ? ",{\"tick\":%lld,\"id\":%lld}" : "{\"tick\":%lld,\"id\":%lld}",
                 (long long)lane->marks[i].tick, (long long)lane->marks[i].arg, 0);

    json_raw(&out, "],\"session\":[");
    lane = &tl->lanes[REPLAY_LANE_SESSION];
    for( int i = 0; i < lane->count; i++ )
    {
        struct ReplayMark const* m = &lane->marks[i];

        assert(m->kind < sizeof(k_session) / sizeof(k_session[0]));
        json_fmt(&out, i ? ",{\"tick\":%lld,\"kind\":" : "{\"tick\":%lld,\"kind\":", (long long)m->tick, 0, 0);
        json_string(&out, k_session[m->kind]);
        if( m->kind == REPLAY_SESSION_STALL )
            json_fmt(&out, ",\"ms\":%lld", (long long)m->arg, 0, 0);
        json_raw(&out, "}");
    }

    json_raw(&out, "],\"interface\":[");
    lane = &tl->lanes[REPLAY_LANE_INTERFACE];
    for( int i = 0; i < lane->count; i++ )
    {
        struct ReplayMark const* m = &lane->marks[i];

        assert(m->kind < sizeof(k_interface) / sizeof(k_interface[0]));
        json_fmt(&out, i ? ",{\"tick\":%lld,\"kind\":" : "{\"tick\":%lld,\"kind\":", (long long)m->tick, 0, 0);
        json_string(&out, k_interface[m->kind]);
        json_fmt(&out, ",\"id\":%lld}", (long long)m->arg, 0, 0);
    }

    json_raw(&out, "],\"chat\":[");
    lane = &tl->lanes[REPLAY_LANE_CHAT];
    for( int i = 0; i < lane->count; i++ )
    {
        json_fmt(&out, i ? ",{\"tick\":%lld,\"text\":" : "{\"tick\":%lld,\"text\":",
                 (long long)lane->marks[i].tick, 0, 0);
        json_string(&out, ReplayTimeline_ChatText(tl, &lane->marks[i]));
        json_raw(&out, "}");
    }
    json_raw(&out, "]}}");
    return out.buf;
}
