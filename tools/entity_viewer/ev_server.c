/*
 * ev_server — the entity viewer's cache half.
 *
 * The browser cannot open a 216 MB cache, so this does: it holds the cache, the
 * catalog ev_catalog produced, and answers three kinds of request.
 *
 *   GET /api/npcs.json          every npc, for the picker
 *   GET /api/npc/<id>.json      one npc's animation lists (rig matches, name guesses)
 *   GET /api/npc/<id>.model     the built, merged, lit model in ev_wire format
 *   GET /api/seq/<id>.anim      one sequence as an animation in ev_wire format
 *   GET /api/player.model?wear=  a composited player, equipment and all
 *   GET /api/objs.json           every obj id, for the equipment picker
 *   GET /api/spotanims.json      every graphic id, for the graphic picker
 *   GET /api/spot/<id>.model     one graphic's model in ev_wire format
 *   GET /api/spot/<id>.json      that graphic's own sequence id
 *   GET /api/rig/<id>.json       every sequence on one framemap
 *   GET /<anything else>        static files from the web directory
 *
 * Single-threaded and blocking on purpose: one viewer, one browser tab, and a
 * request is a cache read that takes milliseconds. Concurrency here would buy
 * nothing and cost the ability to reason about the cache handle.
 *
 * Usage:
 *   ev_server --rev osrs239 <cache_dir> --catalog <dir> [--port 8099] [--web DIR]
 */

#include "ev_build.h"
#include "ev_player.h"
#include "ev_render.h"
#include "lc_pack.h"

#include "bmp.h"
#include "toridraw_model_sprite.h"
#include "ev_wire.h"
#include "tool_profile.h"

#include "toridraw.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ---- catalog in memory -------------------------------------------------- */

struct SeqRow
{
    int seq_id;
    int framemap_id;
    int frame_count;
    /** Reached through the sequence's Animaya curve set rather than a frame
     *  list. Same rig id space either way; playing one needs a model with an
     *  Animaya skin. */
    int skeletal;
    char* name;
};

struct NpcRow
{
    int npc_id;
    char* name;    /* display name from the cache record */
    char* gameval; /* content-team name */
    int framemaps[16];
    int framemap_count;
    int rig_match_seqs;
    int rig_match_skeletal;
    int animaya_skinned;
    int name_match_seqs;
};

struct NameMatchRow
{
    int npc_id;
    int seq_id;
    int score;
    int in_rig;
};

static struct SeqRow* g_seqs = NULL;
static int g_seq_count = 0;
static struct NpcRow* g_npcs = NULL;
static int g_npc_count = 0;
static struct NameMatchRow* g_name_matches = NULL;
static int g_name_match_count = 0;

static struct Tool_Dat2Cache g_cache;
static const char* g_web_dir = NULL;

/*
 * Gameval names for the player half's two pickers.
 *
 * The catalog CSVs name npcs and sequences, because that is what ev_catalog was
 * built to walk. A player is dressed in objs and wears graphics, and a picker
 * over four thousand unnamed spotanim ids is not a picker. These come from the
 * same `id=name` compacks ev_catalog reads, through the same lc_pack loader, and
 * are optional: without --names the lists still work and show ids alone.
 */
static struct LC_Pack g_obj_names = { 0 };
static struct LC_Pack g_spotanim_names = { 0 };
static int g_have_names = 0;

/**
 * One CSV field, unquoted in place.
 *
 * Only what these four files actually contain: comma separation, and quoted
 * fields with doubled quotes, which is what ev_catalog writes for npc names.
 */
static char*
csv_field(char** cursor)
{
    char* p = *cursor;
    if( !p )
        return NULL;

    if( *p == '"' )
    {
        p++;
        char* out = p;
        char* w = p;
        while( *p )
        {
            if( *p == '"' && p[1] == '"' )
            {
                *w++ = '"';
                p += 2;
                continue;
            }
            if( *p == '"' )
            {
                p++;
                break;
            }
            *w++ = *p++;
        }
        *w = '\0';
        if( *p == ',' )
            p++;
        *cursor = p;
        return out;
    }

    char* out = p;
    while( *p && *p != ',' && *p != '\n' && *p != '\r' )
        p++;
    if( *p )
    {
        *p = '\0';
        p++;
    }
    else
        p = NULL;
    *cursor = p;
    return out;
}

/*
 * A CSV row addressed by column *name*.
 *
 * The reader used to take fields in order, which meant adding a column to
 * ev_catalog silently shifted every later one: the day `kind` and
 * `rig_match_skeletal` appeared, the viewer showed sequence names where frame
 * counts belonged and zero name-matches for every npc. Nothing failed — the
 * numbers were just wrong, which is the worst way for a format change to land.
 */
#define CSV_MAX_COLS 32

struct CsvHeader
{
    char* name[CSV_MAX_COLS];
    int count;
};

static void
csv_header_parse(struct CsvHeader* h, char* line)
{
    h->count = 0;
    char* cur = line;
    while( cur && h->count < CSV_MAX_COLS )
    {
        char* field = csv_field(&cur);
        if( !field )
            break;
        h->name[h->count++] = field;
    }
}

static int
csv_col(const struct CsvHeader* h, const char* name)
{
    for( int i = 0; i < h->count; i++ )
        if( strcmp(h->name[i], name) == 0 )
            return i;
    return -1;
}

/** Split a row into fields, in place. Returns how many were found. */
static int
csv_split(char* line, char** out, int max)
{
    int n = 0;
    char* cur = line;
    while( cur && n < max )
    {
        char* field = csv_field(&cur);
        if( !field )
            break;
        out[n++] = field;
    }
    return n;
}

static const char*
csv_get(char** fields, int count, int col)
{
    return (col >= 0 && col < count) ? fields[col] : "";
}

static int
load_catalog(const char* dir)
{
    char path[2048];
    char line[8192];
    FILE* f;

    /* framemap_seqs.csv: framemap_id,seq_id,frame_count,seq_name */
    snprintf(path, sizeof(path), "%s/framemap_seqs.csv", dir);
    f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    int cap = 1024;
    g_seqs = malloc((size_t)cap * sizeof(*g_seqs));

    char header_line[8192];
    struct CsvHeader h;
    char* fields[CSV_MAX_COLS];

    if( !fgets(header_line, sizeof(header_line), f) )
    {
        fclose(f);
        return 0;
    }
    csv_header_parse(&h, header_line);
    int c_fm = csv_col(&h, "framemap_id");
    int c_seq = csv_col(&h, "seq_id");
    int c_kind = csv_col(&h, "kind");
    int c_frames = csv_col(&h, "frame_count");
    int c_name = csv_col(&h, "seq_name");

    while( fgets(line, sizeof(line), f) )
    {
        int n = csv_split(line, fields, CSV_MAX_COLS);
        if( n == 0 )
            continue;
        if( g_seq_count == cap )
        {
            cap *= 2;
            g_seqs = realloc(g_seqs, (size_t)cap * sizeof(*g_seqs));
        }
        const char* name = csv_get(fields, n, c_name);
        g_seqs[g_seq_count].seq_id = atoi(csv_get(fields, n, c_seq));
        g_seqs[g_seq_count].framemap_id = atoi(csv_get(fields, n, c_fm));
        g_seqs[g_seq_count].frame_count = atoi(csv_get(fields, n, c_frames));
        g_seqs[g_seq_count].skeletal =
            strcmp(csv_get(fields, n, c_kind), "skeletal") == 0 ? 1 : 0;
        g_seqs[g_seq_count].name = *name ? strdup(name) : NULL;
        g_seq_count++;
    }
    fclose(f);

    /* npc_catalog.csv: id,name,gameval,models,seeds,framemaps,rig,strict,nm,nm_outside */
    snprintf(path, sizeof(path), "%s/npc_catalog.csv", dir);
    f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    cap = 1024;
    g_npcs = malloc((size_t)cap * sizeof(*g_npcs));
    if( !fgets(header_line, sizeof(header_line), f) )
    {
        fclose(f);
        return 0;
    }
    csv_header_parse(&h, header_line);
    int n_id = csv_col(&h, "npc_id");
    int n_name = csv_col(&h, "npc_name");
    int n_gv = csv_col(&h, "gameval");
    int n_fms = csv_col(&h, "framemaps");
    int n_rig = csv_col(&h, "rig_match_seqs");
    int n_skel = csv_col(&h, "rig_match_skeletal");
    int n_skin = csv_col(&h, "animaya_skinned");
    int n_nm = csv_col(&h, "name_match_seqs");

    while( fgets(line, sizeof(line), f) )
    {
        int n = csv_split(line, fields, CSV_MAX_COLS);
        if( n == 0 )
            continue;
        int id = atoi(csv_get(fields, n, n_id));
        const char* name = csv_get(fields, n, n_name);
        const char* gv = csv_get(fields, n, n_gv);
        const char* fms = csv_get(fields, n, n_fms);
        int rig = atoi(csv_get(fields, n, n_rig));
        int nm = atoi(csv_get(fields, n, n_nm));

        if( g_npc_count == cap )
        {
            cap *= 2;
            g_npcs = realloc(g_npcs, (size_t)cap * sizeof(*g_npcs));
        }
        struct NpcRow* row = &g_npcs[g_npc_count++];
        memset(row, 0, sizeof(*row));
        row->npc_id = id;
        row->name = *name ? strdup(name) : NULL;
        row->gameval = *gv ? strdup(gv) : NULL;
        row->rig_match_seqs = rig;
        row->rig_match_skeletal = atoi(csv_get(fields, n, n_skel));
        row->animaya_skinned = strcmp(csv_get(fields, n, n_skin), "true") == 0 ? 1 : 0;
        row->name_match_seqs = nm;
        for( const char* t = fms; t && *t && row->framemap_count < 16; )
        {
            while( *t == ' ' )
                t++;
            if( !*t )
                break;
            row->framemaps[row->framemap_count++] = atoi(t);
            while( *t && *t != ' ' )
                t++;
        }
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s/npc_name_matches.csv", dir);
    f = fopen(path, "rb");
    if( f && fgets(header_line, sizeof(header_line), f) )
    {
        cap = 4096;
        g_name_matches = malloc((size_t)cap * sizeof(*g_name_matches));
        csv_header_parse(&h, header_line);
        int m_npc = csv_col(&h, "npc_id");
        int m_seq = csv_col(&h, "seq_id");
        int m_score = csv_col(&h, "score");
        int m_rig = csv_col(&h, "in_rig_set");

        while( fgets(line, sizeof(line), f) )
        {
            int n = csv_split(line, fields, CSV_MAX_COLS);
            if( n == 0 )
                continue;
            if( g_name_match_count == cap )
            {
                cap *= 2;
                g_name_matches = realloc(g_name_matches, (size_t)cap * sizeof(*g_name_matches));
            }
            g_name_matches[g_name_match_count].npc_id = atoi(csv_get(fields, n, m_npc));
            g_name_matches[g_name_match_count].seq_id = atoi(csv_get(fields, n, m_seq));
            g_name_matches[g_name_match_count].score = atoi(csv_get(fields, n, m_score));
            g_name_matches[g_name_match_count].in_rig =
                strcmp(csv_get(fields, n, m_rig), "true") == 0 ? 1 : 0;
            g_name_match_count++;
        }
    }
    if( f )
        fclose(f);

    fprintf(
        stderr,
        "catalog: %d npcs, %d rigged sequences, %d name matches\n",
        g_npc_count,
        g_seq_count,
        g_name_match_count);
    return 1;
}

static const struct NpcRow*
npc_row(int id)
{
    for( int i = 0; i < g_npc_count; i++ )
        if( g_npcs[i].npc_id == id )
            return &g_npcs[i];
    return NULL;
}

static const char*
seq_name(int seq_id)
{
    for( int i = 0; i < g_seq_count; i++ )
        if( g_seqs[i].seq_id == seq_id )
            return g_seqs[i].name;
    return NULL;
}

/* ---- http --------------------------------------------------------------- */

static void
send_all(int fd, const void* data, size_t len)
{
    const char* p = data;
    while( len )
    {
        ssize_t n = write(fd, p, len);
        if( n <= 0 )
            return;
        p += n;
        len -= (size_t)n;
    }
}

static void
send_response(
    int fd,
    const char* status,
    const char* content_type,
    const void* body,
    size_t len)
{
    char head[512];
    int n = snprintf(
        head,
        sizeof(head),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        content_type,
        len);
    send_all(fd, head, (size_t)n);
    if( body && len )
        send_all(fd, body, len);
}

static void
send_404(int fd)
{
    static const char body[] = "not found";
    send_response(fd, "404 Not Found", "text/plain", body, sizeof(body) - 1);
}

/** A growable text buffer, for building JSON without a dependency. */
struct Str
{
    char* p;
    size_t len;
    size_t cap;
};

static void
str_add(struct Str* s, const char* fmt, ...)
{
    va_list ap;
    for( ;; )
    {
        size_t room = s->cap - s->len;
        va_start(ap, fmt);
        int n = vsnprintf(s->p + s->len, room, fmt, ap);
        va_end(ap);
        if( n >= 0 && (size_t)n < room )
        {
            s->len += (size_t)n;
            return;
        }
        size_t next = s->cap ? s->cap * 2 : 4096;
        while( next < s->len + (size_t)(n > 0 ? n : 64) + 1 )
            next *= 2;
        char* grown = realloc(s->p, next);
        if( !grown )
            return;
        s->p = grown;
        s->cap = next;
    }
}

/** JSON string escaping, enough for the names these files carry. */
static void
str_add_json(struct Str* s, const char* text)
{
    if( !text )
    {
        str_add(s, "null");
        return;
    }
    str_add(s, "\"");
    for( const unsigned char* p = (const unsigned char*)text; *p; p++ )
    {
        if( *p == '"' || *p == '\\' )
            str_add(s, "\\%c", *p);
        else if( *p < 0x20 )
            str_add(s, "\\u%04x", *p);
        else if( *p < 0x80 )
            str_add(s, "%c", *p);
        else
            /* Cache text is windows-1252; the high half is emitted as its
             * Latin-1 code point, which is what the browser will render. */
            str_add(s, "\\u%04x", *p);
    }
    str_add(s, "\"");
}

static void
handle_npcs_json(int fd)
{
    struct Str s = { 0 };
    str_add(&s, "[");
    for( int i = 0; i < g_npc_count; i++ )
    {
        const struct NpcRow* r = &g_npcs[i];
        str_add(&s, i ? ",{" : "{");
        str_add(&s, "\"id\":%d,\"name\":", r->npc_id);
        str_add_json(&s, r->name);
        str_add(&s, ",\"gameval\":");
        str_add_json(&s, r->gameval);
        str_add(
            &s,
            ",\"rig\":%d,\"skeletal\":%d,\"skinned\":%s,\"maybe\":%d}",
            r->rig_match_seqs,
            r->rig_match_skeletal,
            r->animaya_skinned ? "true" : "false",
            r->name_match_seqs);
    }
    str_add(&s, "]");
    send_response(fd, "200 OK", "application/json", s.p, s.len);
    free(s.p);
}

static void
handle_npc_json(int fd, int npc_id)
{
    const struct NpcRow* r = npc_row(npc_id);
    if( !r )
    {
        send_404(fd);
        return;
    }

    struct Str s = { 0 };
    str_add(&s, "{\"id\":%d,\"name\":", r->npc_id);
    str_add_json(&s, r->name);
    str_add(&s, ",\"gameval\":");
    str_add_json(&s, r->gameval);

    str_add(&s, ",\"skinned\":%s", r->animaya_skinned ? "true" : "false");
    str_add(&s, ",\"framemaps\":[");
    for( int i = 0; i < r->framemap_count; i++ )
        str_add(&s, i ? ",%d" : "%d", r->framemaps[i]);
    str_add(&s, "]");

    /* Rigging matches: every sequence built on any of this npc's rigs. */
    str_add(&s, ",\"rig\":[");
    int first = 1;
    for( int i = 0; i < g_seq_count; i++ )
    {
        int hit = 0;
        for( int k = 0; k < r->framemap_count; k++ )
            if( g_seqs[i].framemap_id == r->framemaps[k] )
                hit = 1;
        if( !hit )
            continue;
        str_add(&s, first ? "{" : ",{");
        first = 0;
        str_add(
            &s,
            "\"seq\":%d,\"frames\":%d,\"framemap\":%d,\"skeletal\":%s,\"name\":",
            g_seqs[i].seq_id,
            g_seqs[i].frame_count,
            g_seqs[i].framemap_id,
            g_seqs[i].skeletal ? "true" : "false");
        str_add_json(&s, g_seqs[i].name);
        str_add(&s, "}");
    }
    str_add(&s, "]");

    /* Name guesses, each carrying whether the rig walk already found it. */
    str_add(&s, ",\"maybe\":[");
    first = 1;
    for( int i = 0; i < g_name_match_count; i++ )
    {
        if( g_name_matches[i].npc_id != npc_id )
            continue;
        str_add(&s, first ? "{" : ",{");
        first = 0;
        str_add(
            &s,
            "\"seq\":%d,\"score\":%d,\"in_rig\":%s,\"name\":",
            g_name_matches[i].seq_id,
            g_name_matches[i].score,
            g_name_matches[i].in_rig ? "true" : "false");
        str_add_json(&s, seq_name(g_name_matches[i].seq_id));
        str_add(&s, "}");
    }
    str_add(&s, "]}");

    send_response(fd, "200 OK", "application/json", s.p, s.len);
    free(s.p);
}

static void
handle_npc_model(int fd, int npc_id)
{
    struct ToriDraw_Model* model = ev_build_npc_model(&g_cache, npc_id);
    if( !model )
    {
        send_404(fd);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model(&buf, model);
    ToriDraw_ModelFree(model);

    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        return;
    }
    send_response(fd, "200 OK", "application/octet-stream", buf.data, buf.len);
    ev_wire_free(&buf);
}

static void
handle_seq_anim(int fd, int seq_id)
{
    int framemap_id = -1;
    struct ToriDraw_Animation* anim = ev_build_seq_anim(&g_cache, seq_id, &framemap_id);
    if( !anim )
    {
        send_404(fd);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_anim(&buf, anim);
    ToriDraw_AnimationFree(anim);

    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        return;
    }
    send_response(fd, "200 OK", "application/octet-stream", buf.data, buf.len);
    ev_wire_free(&buf);
}

/* ---- the player half ----------------------------------------------------- */

/*
 * A player is not an npc with different models on it.
 *
 * An npc is one config carrying a model list. A player is a set of identity
 * kits plus the wear models of whatever is equipped, and — the part that makes
 * a separate half of the viewer worth having — a player-attached graphic is not
 * a second object in the scene at all: the client MERGES the posed graphic into
 * the player's own model. So "what does this weapon animation look like with
 * this graphic on it" cannot be answered by drawing two models near each other.
 * These endpoints hand the browser both halves and ev_render does the merge the
 * client's way.
 */

static const char*
pack_name(const struct LC_Pack* pack, int id)
{
    if( !g_have_names || id < 0 || id > pack->max || !pack->names )
        return NULL;
    return pack->names[id];
}

/** `?wear=22325,1163&gender=0` -> the built player model. */
static void
handle_player_model(int fd, const char* query)
{
    struct EV_PlayerSpec spec;
    ev_player_spec_init(&spec);

    if( query )
    {
        const char* w = strstr(query, "wear=");
        const char* g = strstr(query, "gender=");
        if( g )
            spec.gender = atoi(g + 7) ? 1 : 0;
        if( w )
        {
            const char* p = w + 5;
            while( *p && *p != '&' && spec.worn_count < EV_PLAYER_MAX_WORN )
            {
                if( isdigit((unsigned char)*p) )
                {
                    spec.worn[spec.worn_count++] = atoi(p);
                    while( isdigit((unsigned char)*p) )
                        p++;
                }
                else
                    p++;
            }
        }
    }

    struct ToriDraw_Model* model = ev_build_player_model(&g_cache, &spec, NULL);
    if( !model )
    {
        send_404(fd);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model(&buf, model);
    ToriDraw_ModelFree(model);
    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        return;
    }
    send_response(fd, "200 OK", "application/octet-stream", buf.data, buf.len);
    ev_wire_free(&buf);
}

static void
handle_spot_model(int fd, int spotanim_id)
{
    int seq = -1;
    struct ToriDraw_Model* model =
        ev_build_spotanim_model(&g_cache, spotanim_id, NULL, -1, &seq);
    if( !model )
    {
        send_404(fd);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model(&buf, model);
    ToriDraw_ModelFree(model);
    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        return;
    }
    send_response(fd, "200 OK", "application/octet-stream", buf.data, buf.len);
    ev_wire_free(&buf);
}

/** The graphic's own sequence id, which the page needs before it can ask for
 *  the animation that drives it. */
static void
handle_spot_json(int fd, int spotanim_id)
{
    int seq = -1;
    struct ToriDraw_Model* model =
        ev_build_spotanim_model(&g_cache, spotanim_id, NULL, -1, &seq);
    if( !model )
    {
        send_404(fd);
        return;
    }
    ToriDraw_ModelFree(model);

    struct Str s = { 0 };
    str_add(&s, "{\"id\":%d,\"seq\":%d,\"name\":", spotanim_id, seq);
    str_add_json(&s, pack_name(&g_spotanim_names, spotanim_id));
    str_add(&s, "}");
    send_response(fd, "200 OK", "application/json", s.p, s.len);
    free(s.p);
}

/*
 * Every id in a config group, with whatever name the compacks give it.
 *
 * The list is not filtered to "wearable" objs even though only those contribute
 * geometry: deciding that here would mean decoding all 30,000 obj records on
 * every page load, and the page can say "nothing to draw" for the handful
 * anybody tries. The spotanim list is small enough that the question does not
 * arise.
 */
static void
handle_config_ids_json(int fd, enum RSCache_Type type, int kind, const struct LC_Pack* names)
{
    int* ids = NULL;
    int count = 0;

    if( !tool_dat2_config_ids(&g_cache, type, kind, &ids, &count) )
    {
        send_404(fd);
        return;
    }

    struct Str s = { 0 };
    str_add(&s, "[");
    for( int i = 0; i < count; i++ )
    {
        str_add(&s, i ? ",{" : "{");
        str_add(&s, "\"id\":%d,\"name\":", ids[i]);
        str_add_json(&s, pack_name(names, ids[i]));
        str_add(&s, "}");
    }
    str_add(&s, "]");
    free(ids);
    send_response(fd, "200 OK", "application/json", s.p, s.len);
    free(s.p);
}

/*
 * Every sequence built on one rig.
 *
 * An npc gets its animation list from the catalog's rig walk. A player has no
 * npc row to walk from, but it has a rig — framemap 0, the shared human one —
 * and the catalog already knows every sequence on it. 3,905 of them, which is
 * why the page's search box is not a nicety.
 */
static void
handle_rig_json(int fd, int framemap_id)
{
    struct Str s = { 0 };
    str_add(&s, "{\"framemap\":%d,\"rig\":[", framemap_id);
    int n = 0;
    for( int i = 0; i < g_seq_count; i++ )
    {
        if( g_seqs[i].framemap_id != framemap_id )
            continue;
        str_add(&s, n++ ? ",{" : "{");
        str_add(&s, "\"seq\":%d,\"frames\":%d,\"skeletal\":%d,\"name\":", g_seqs[i].seq_id,
                g_seqs[i].frame_count, g_seqs[i].skeletal);
        str_add_json(&s, g_seqs[i].name);
        str_add(&s, "}");
    }
    str_add(&s, "],\"maybe\":[]}");
    send_response(fd, "200 OK", "application/json", s.p, s.len);
    free(s.p);
}

static const char*
mime_for(const char* path)
{
    const char* dot = strrchr(path, '.');
    if( !dot )
        return "application/octet-stream";
    if( strcmp(dot, ".html") == 0 )
        return "text/html; charset=utf-8";
    if( strcmp(dot, ".js") == 0 )
        return "text/javascript";
    if( strcmp(dot, ".css") == 0 )
        return "text/css";
    if( strcmp(dot, ".wasm") == 0 )
        return "application/wasm";
    if( strcmp(dot, ".json") == 0 )
        return "application/json";
    return "application/octet-stream";
}

static void
handle_static(int fd, const char* rel)
{
    if( strstr(rel, "..") )
    {
        send_404(fd);
        return;
    }

    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", g_web_dir, rel[0] ? rel : "index.html");

    FILE* f = fopen(path, "rb");
    if( !f )
    {
        send_404(fd);
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* body = malloc((size_t)(len > 0 ? len : 1));
    if( !body || fread(body, 1, (size_t)len, f) != (size_t)len )
    {
        fclose(f);
        free(body);
        send_404(fd);
        return;
    }
    fclose(f);

    send_response(fd, "200 OK", mime_for(path), body, (size_t)len);
    free(body);
}

/**
 * Match `/<prefix><id><suffix>` exactly, yielding the id.
 *
 * Not sscanf: `sscanf("/api/npc/2042.model", "/api/npc/%d.json", &id)` returns
 * 1, because the return counts *assignments made*, not whether the rest of the
 * format matched. Every .model request was answered with JSON until this
 * replaced it, and the browser reported it as a corrupt model rather than as a
 * routing bug.
 */
static int
route_id(const char* target, const char* prefix, const char* suffix, int* out_id)
{
    size_t plen = strlen(prefix);
    if( strncmp(target, prefix, plen) != 0 )
        return 0;

    const char* p = target + plen;
    if( !isdigit((unsigned char)*p) )
        return 0;

    char* end = NULL;
    long id = strtol(p, &end, 10);
    if( !end || strcmp(end, suffix) != 0 )
        return 0;

    *out_id = (int)id;
    return 1;
}

static void
handle_request(int fd, const char* target, const char* query)
{
    int id = 0;
    if( strcmp(target, "/api/npcs.json") == 0 )
        handle_npcs_json(fd);
    else if( route_id(target, "/api/npc/", ".json", &id) )
        handle_npc_json(fd, id);
    else if( route_id(target, "/api/npc/", ".model", &id) )
        handle_npc_model(fd, id);
    else if( route_id(target, "/api/seq/", ".anim", &id) )
        handle_seq_anim(fd, id);
    /* The player half. */
    else if( strcmp(target, "/api/player.model") == 0 )
        handle_player_model(fd, query);
    else if( strcmp(target, "/api/objs.json") == 0 )
        handle_config_ids_json(fd, RSCACHE_TYPE_OBJ, RSCACHE_DAT2_CONFIG_KIND_OBJECT, &g_obj_names);
    else if( strcmp(target, "/api/spotanims.json") == 0 )
        handle_config_ids_json(
            fd, RSCACHE_TYPE_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM, &g_spotanim_names);
    else if( route_id(target, "/api/spot/", ".model", &id) )
        handle_spot_model(fd, id);
    else if( route_id(target, "/api/spot/", ".json", &id) )
        handle_spot_json(fd, id);
    else if( route_id(target, "/api/rig/", ".json", &id) )
        handle_rig_json(fd, id);
    else
        handle_static(fd, target[0] == '/' ? target + 1 : target);
}

/*
 * Every field of a model, compared.
 *
 * The check this replaces rendered the model twice and compared the images —
 * but both renders went through ev_wire, so it compared wire(model) against
 * wire(model) and could only ever prove determinism. A field the format drops
 * is dropped identically on both sides and the images agree. This compares the
 * model this process *built* against the one rebuilt from the bytes, which is
 * the question that was meant to be asked.
 */
#define EV_CMP(what, cond)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  wire LOSES %s\n", (what));                                          \
            diffs++;                                                                               \
        }                                                                                          \
    } while( 0 )

static int
compare_models(
    const struct ToriDraw_Model* a,
    const struct ToriDraw_Model* b)
{
    int diffs = 0;

    EV_CMP("vertex_count", a->vertex_count == b->vertex_count);
    EV_CMP("face_count", a->face_count == b->face_count);
    EV_CMP("flags", a->flags == b->flags);
    EV_CMP("model_priority", a->model_priority == b->model_priority);
    EV_CMP("textured_face_count", a->textured_face_count == b->textured_face_count);
    if( diffs )
        return diffs;

    for( int i = 0; i < a->vertex_count; i++ )
        EV_CMP(
            "vertices",
            a->vertices_x[i] == b->vertices_x[i] && a->vertices_y[i] == b->vertices_y[i] &&
                a->vertices_z[i] == b->vertices_z[i]);

    for( int i = 0; i < a->face_count; i++ )
        EV_CMP(
            "face indices",
            a->face_indices_a[i] == b->face_indices_a[i] &&
                a->face_indices_b[i] == b->face_indices_b[i] &&
                a->face_indices_c[i] == b->face_indices_c[i]);

#define EV_CMP_OPT(name, field, count)                                                             \
    do                                                                                             \
    {                                                                                              \
        EV_CMP(name " presence", (a->field == NULL) == (b->field == NULL));                        \
        if( a->field && b->field )                                                                 \
            for( int i = 0; i < (count); i++ )                                                     \
                EV_CMP(name, a->field[i] == b->field[i]);                                          \
    } while( 0 )

    EV_CMP_OPT("face_colors", face_colors, a->face_count);
    EV_CMP_OPT("face_alphas", face_alphas, a->face_count);
    EV_CMP_OPT("face_infos", face_infos, a->face_count);
    EV_CMP_OPT("face_textures", face_textures, a->face_count);
    EV_CMP_OPT("face_priorities", face_priorities, (a->face_count + 1) / 2);
    EV_CMP_OPT("face_colors_a", face_colors_a, a->face_count);
    EV_CMP_OPT("face_colors_b", face_colors_b, a->face_count);
    EV_CMP_OPT("face_colors_c", face_colors_c, a->face_count);
    EV_CMP_OPT("face_texture_coords", face_texture_coords, a->face_count);
    EV_CMP_OPT("textured_p", textured_p_coordinate, a->textured_face_count);
    EV_CMP_OPT("textured_m", textured_m_coordinate, a->textured_face_count);
    EV_CMP_OPT("textured_n", textured_n_coordinate, a->textured_face_count);
    EV_CMP_OPT("animaya_group_counts", animaya_group_counts, a->animaya_vertex_count);
#undef EV_CMP_OPT

    EV_CMP("vertex_bones presence", (a->vertex_bones == NULL) == (b->vertex_bones == NULL));
    if( a->vertex_bones && b->vertex_bones )
    {
        EV_CMP("vertex_bones count", a->vertex_bones->bones_count == b->vertex_bones->bones_count);
        for( int i = 0; i < a->vertex_bones->bones_count && !diffs; i++ )
        {
            EV_CMP("vertex_bones size", a->vertex_bones->bones_sizes[i] ==
                                            b->vertex_bones->bones_sizes[i]);
            for( int j = 0; j < a->vertex_bones->bones_sizes[i]; j++ )
                EV_CMP("vertex_bones", a->vertex_bones->bones[i][j] ==
                                           b->vertex_bones->bones[i][j]);
        }
    }

    EV_CMP("animaya_vertex_count", a->animaya_vertex_count == b->animaya_vertex_count);
    for( int i = 0; i < a->animaya_vertex_count && a->animaya_groups && b->animaya_groups; i++ )
    {
        int n = a->animaya_group_counts[i];
        for( int j = 0; j < n; j++ )
            EV_CMP(
                "animaya skin",
                a->animaya_groups[i][j] == b->animaya_groups[i][j] &&
                    a->animaya_scales[i][j] == b->animaya_scales[i][j]);
    }

    return diffs;
}

/**
 * Bake one npc and one sequence, round-trip both through the wire format, and
 * report. `--selftest` runs it and exits.
 *
 * The point is that it exercises the exact path a request takes with no socket
 * in the way: when the server died on its first .model request, the socket made
 * the failure look like a networking problem, and this said in one line that it
 * was the bake.
 */
static int
selftest(int npc_id, int seq_id)
{
    fprintf(stderr, "selftest: npc %d, seq %d\n", npc_id, seq_id);

    struct ToriDraw_Model* model = ev_build_npc_model(&g_cache, npc_id);
    if( !model )
    {
        fprintf(stderr, "  npc %d: no model\n", npc_id);
        return 1;
    }
    /*
     * Face priority as the raster will actually see it.
     *
     * Priority is the primary key of the painter's sort, and the way it goes
     * wrong is not an error — it is a histogram that collapses to one bucket,
     * at which point every part of a merged npc sorts by depth alone and arms
     * fall behind torsos. So print the distribution rather than asserting
     * anything about it.
     */
    {
        int hist[16] = { 0 };
        if( model->face_priorities )
            for( int i = 0; i < model->face_count; i++ )
                hist[ToriDraw_ModelGetFacePriority(model->face_priorities, i) & 15]++;
        fprintf(stderr, "  face priorities:");
        if( !model->face_priorities )
            fprintf(stderr, " none (uniform model_priority %d)", model->model_priority);
        else
            for( int p = 0; p < 16; p++ )
                if( hist[p] )
                    fprintf(stderr, " p%d=%d", p, hist[p]);
        fprintf(stderr, "\n");
    }

    int textured = 0;
    if( model->face_textures )
        for( int i = 0; i < model->face_count; i++ )
            if( model->face_textures[i] >= 0 )
                textured++;
    fprintf(
        stderr,
        "  model: %d vertices, %d faces (%d textured), classic rig %s, "
        "animaya skin %s, face_priorities %s\n",
        model->vertex_count,
        model->face_count,
        textured,
        model->vertex_bones ? "yes" : "no",
        model->animaya_vertex_count > 0 ? "yes" : "no",
        model->face_priorities ? "yes" : "no");

    struct EV_WireBuf mb = { 0 };
    if( !ev_wire_write_model(&mb, model) )
    {
        fprintf(stderr, "  model: encode failed\n");
        return 1;
    }
    struct ToriDraw_Model* back = ev_wire_read_model(mb.data, mb.len);
    if( !back )
    {
        fprintf(stderr, "  model: %zu bytes written, decode failed\n", mb.len);
        return 1;
    }
    fprintf(
        stderr,
        "  model: %zu bytes, round-trip %d/%d vertices, %d/%d faces\n",
        mb.len,
        back->vertex_count,
        model->vertex_count,
        back->face_count,
        model->face_count);

    int framemap_id = -1;
    struct ToriDraw_Animation* anim = ev_build_seq_anim(&g_cache, seq_id, &framemap_id);
    if( !anim )
    {
        fprintf(stderr, "  seq %d: no animation\n", seq_id);
        return 1;
    }
    if( anim->skeletal )
        fprintf(
            stderr,
            "  anim: skeletal, %d frames on rig %d (%d bones, %d baked)\n",
            anim->frame_count,
            framemap_id,
            anim->skeletal->bone_count,
            anim->skeletal->frame_count);
    else
        fprintf(
            stderr,
            "  anim: classic, %d frames on rig %d, base length %d\n",
            anim->frame_count,
            framemap_id,
            anim->base->length);

    struct EV_WireBuf ab = { 0 };
    if( !ev_wire_write_anim(&ab, anim) )
    {
        fprintf(stderr, "  anim: encode failed\n");
        return 1;
    }
    struct ToriDraw_Animation* aback = ev_wire_read_anim(ab.data, ab.len);
    if( !aback )
    {
        fprintf(stderr, "  anim: %zu bytes written, decode failed\n", ab.len);
        return 1;
    }
    fprintf(stderr, "  anim: %zu bytes, round-trip %d frames\n", ab.len, aback->frame_count);

    const char* wire_dump = getenv("EV_DUMP_WIRE");
    if( wire_dump )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s_model.bin", wire_dump);
        FILE* out = fopen(path, "wb");
        if( out )
        {
            fwrite(mb.data, 1, mb.len, out);
            fclose(out);
        }
        snprintf(path, sizeof(path), "%s_anim.bin", wire_dump);
        out = fopen(path, "wb");
        if( out )
        {
            fwrite(ab.data, 1, ab.len, out);
            fclose(out);
        }
    }
    /* Applying a frame is where a wrong rig binding shows: if the pose does not
     * move a single vertex the model and the animation do not agree. */

    /*
     * Run the browser's exact render path natively.
     *
     * "Nothing on the canvas" has two very different causes — the module never
     * ran, or it ran and the projection culled the model — and only this tells
     * them apart. The count is of pixels that are not the background.
     */
    {
        int diffs = compare_models(model, back);
        fprintf(
            stderr,
            "  wire fidelity: %d field difference(s) between the built model and "
            "the one the browser rebuilds\n",
            diffs);
    }

    ev_init();
    ev_set_model(mb.data, (int)mb.len);
    ev_set_anim(ab.data, (int)ab.len);

    /* The pose the renderer would apply, through the renderer's own branch. */
    fprintf(
        stderr,
        "  frame 0 (%s) moves %d of %d vertices%s\n",
        ev_anim_is_skeletal() ? "skeletal" : "classic",
        ev_pose_moved_vertices(0),
        ev_model_vertex_count(),
        ev_anim_is_skeletal() && !ev_model_has_animaya()
            ? "  — model has no Animaya skin, so it cannot play this"
            : "");

    {
        /* Every frame's delay, so a bad one cannot hide: the player stalls on
         * whichever frame reports a huge length, and that is invisible in a
         * still. */
        int min_delay = 1 << 30;
        int max_delay = 0;
        for( int i = 0; i < ev_frame_count(); i++ )
        {
            int d = ev_frame_delay(i);
            if( d < min_delay )
                min_delay = d;
            if( d > max_delay )
                max_delay = d;
        }
        if( ev_frame_count() > 0 )
            fprintf(
                stderr,
                "  frame delays: min %d, max %d ticks over %d frames\n",
                min_delay,
                max_delay,
                ev_frame_count());
    }

    /*
     * Does the priority sort change anything?
     *
     * Render once as-is, then again with the priority array removed, and count
     * differing pixels. A model whose faces span several priorities must look
     * different without them — if it does not, the sort is not consulting them
     * and the layering the merge worked out is being thrown away downstream.
     */
    {
        uint8_t* with = ev_render(256, 256, 0, 200, ev_model_height() * 3, 0);
        uint8_t* copy = malloc(256 * 256 * 4);
        if( with && copy )
            memcpy(copy, with, 256 * 256 * 4);

        struct EV_WireBuf flat = { 0 };
        struct ToriDraw_Model* stripped = ev_wire_read_model(mb.data, mb.len);
        if( stripped )
        {
            free(stripped->face_priorities);
            stripped->face_priorities = NULL;
            stripped->model_priority = 0;
            ev_wire_write_model(&flat, stripped);
            ToriDraw_ModelFree(stripped);
            ev_set_model(flat.data, (int)flat.len);
            ev_set_anim(ab.data, (int)ab.len);

            uint8_t* without = ev_render(256, 256, 0, 200, ev_model_height() * 3, 0);
            int differ = 0;
            if( copy && without )
                for( int i = 0; i < 256 * 256 * 4; i++ )
                    if( copy[i] != without[i] )
                        differ++;
            fprintf(
                stderr,
                "  priority effect: %d byte(s) differ between sorted-with and "
                "sorted-without face priorities\n",
                differ);
            ev_wire_free(&flat);
        }
        free(copy);

        /* Put the real model back for the render report below. */
        ev_set_model(mb.data, (int)mb.len);
        ev_set_anim(ab.data, (int)ab.len);
    }

    /*
     * The library's own model rasteriser, as an oracle.
     *
     * ToriDraw_SpriteNewFromModelRaster is what the client uses for chatheads
     * and interface model previews — same scene, same sort, same raster, and a
     * framing this viewer's was copied from. Dumping both means "does my render
     * differ from the library's" is a picture rather than an argument.
     *
     * EV_DUMP_BMP=<prefix> writes <prefix>_ev.bmp and <prefix>_lib.bmp.
     */
    const char* dump = getenv("EV_DUMP_BMP");
    if( dump )
    {
        char path[1024];
        int side = 512;
        int zoom_px = ev_model_height() * 3 > 400 ? ev_model_height() * 3 : 400;

        /* A strip of yaws at the viewer's own framing. One angle proves
         * nothing about a depth sort: the layering that goes wrong does so at
         * the angles where two parts overlap. */
        static const int yaws[4] = { 0, 512, 1024, 1536 };
        int strip_w = side * 4;
        int* strip = calloc((size_t)strip_w * side, sizeof(int));
        for( int v = 0; v < 4; v++ )
        {
            uint8_t* mine = ev_render(side, side, yaws[v], 200, zoom_px, -1);
            if( !mine )
                continue;
            for( int y = 0; y < side; y++ )
                for( int x = 0; x < side; x++ )
                {
                    int i = y * side + x;
                    strip[y * strip_w + v * side + x] =
                        (int)(0xFF000000u | ((uint32_t)mine[i * 4] << 16) |
                              ((uint32_t)mine[i * 4 + 1] << 8) | (uint32_t)mine[i * 4 + 2]);
                }
        }
        snprintf(path, sizeof(path), "%s_ev.bmp", dump);
        bmp_write_file(path, strip, strip_w, side);
        free(strip);

        struct ToriDraw_Scene* scene = ToriDraw_SceneNew(
            TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = back;

        int* lib_strip = calloc((size_t)strip_w * side, sizeof(int));
        for( int v = 0; v < 4; v++ )
        {
            struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromModelRaster(
                scene, hnd, zoom_px, 200, yaws[v], side, side, false);
            if( !sprite )
                continue;
            for( int y = 0; y < side && y < sprite->height; y++ )
                for( int x = 0; x < side && x < sprite->width; x++ )
                    lib_strip[y * strip_w + v * side + x] =
                        (int)sprite->pixels_argb[y * sprite->width + x];
            ToriDraw_SpriteFree(sprite);
        }
        snprintf(path, sizeof(path), "%s_lib.bmp", dump);
        bmp_write_file(path, lib_strip, strip_w, side);
        free(lib_strip);
        ToriDraw_SceneFree(scene);

        /* The same yaws with the priority array removed, so "is the priority
         * sort helping or hurting" is a picture too. */
        struct ToriDraw_Model* flat_model = ev_wire_read_model(mb.data, mb.len);
        if( flat_model )
        {
            free(flat_model->face_priorities);
            flat_model->face_priorities = NULL;
            flat_model->model_priority = 0;
            struct EV_WireBuf fb = { 0 };
            ev_wire_write_model(&fb, flat_model);
            ToriDraw_ModelFree(flat_model);
            ev_set_model(fb.data, (int)fb.len);

            int* nop = calloc((size_t)strip_w * side, sizeof(int));
            for( int v = 0; v < 4; v++ )
            {
                uint8_t* r = ev_render(side, side, yaws[v], 200, zoom_px, -1);
                if( !r )
                    continue;
                for( int y = 0; y < side; y++ )
                    for( int x = 0; x < side; x++ )
                    {
                        int i = y * side + x;
                        nop[y * strip_w + v * side + x] =
                            (int)(0xFF000000u | ((uint32_t)r[i * 4] << 16) |
                                  ((uint32_t)r[i * 4 + 1] << 8) | (uint32_t)r[i * 4 + 2]);
                    }
            }
            snprintf(path, sizeof(path), "%s_noprio.bmp", dump);
            bmp_write_file(path, nop, strip_w, side);
            free(nop);
            ev_wire_free(&fb);
            ev_set_model(mb.data, (int)mb.len);
        }
        fprintf(stderr, "  dumped %s_ev.bmp and %s_lib.bmp\n", dump, dump);
    }

    int h = ev_model_height();
    int zoom = h * 3 > 400 ? h * 3 : 400;
    uint8_t* rgba = ev_render(256, 256, 0, 200, zoom, 0);
    int lit = 0;
    if( rgba )
        for( int i = 0; i < 256 * 256; i++ )
            if( rgba[i * 4] != 0x14 || rgba[i * 4 + 1] != 0x18 || rgba[i * 4 + 2] != 0x21 )
                lit++;
    fprintf(
        stderr,
        "  render: height %d, zoom %d, cull %d, %d of %d pixels drawn\n",
        h,
        zoom,
        ev_last_cull(),
        lit,
        256 * 256);

    ToriDraw_ModelFree(model);
    ToriDraw_ModelFree(back);
    ToriDraw_AnimationFree(anim);
    ToriDraw_AnimationFree(aback);
    ev_wire_free(&mb);
    ev_wire_free(&ab);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* cache_dir = NULL;
    const char* catalog_dir = NULL;
    const char* names_dir = NULL;
    int port = 8099;
    int selftest_npc = -1;
    int selftest_seq = -1;

    /* Relative to the binary's directory by default, so the usual invocation
     * from the repo root needs no --web. */
    static char web_default[2048];
    snprintf(web_default, sizeof(web_default), "tools/entity_viewer/web");
    g_web_dir = web_default;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--catalog") == 0 && i + 1 < argc )
            catalog_dir = argv[++i];
        else if( strcmp(argv[i], "--web") == 0 && i + 1 < argc )
            g_web_dir = argv[++i];
        else if( strcmp(argv[i], "--names") == 0 && i + 1 < argc )
            names_dir = argv[++i];
        else if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else if( strcmp(argv[i], "--selftest") == 0 && i + 2 < argc )
        {
            selftest_npc = atoi(argv[++i]);
            selftest_seq = atoi(argv[++i]);
        }
        else if( argv[i][0] != '-' )
            cache_dir = argv[i];
    }

    if( !rev_name || !cache_dir || !catalog_dir )
    {
        fprintf(
            stderr,
            "Usage: %s --rev osrs239 <cache_dir> --catalog <dir> "
            "[--port 8099] [--web DIR] [--names CONTENT_DIR]\n",
            argv[0]);
        return 1;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(rev_name, NULL, NULL, NULL, NULL, &profile) )
        return 1;
    if( !tool_dat2_open(cache_dir, &profile, &g_cache) )
    {
        fprintf(stderr, "cannot open cache at %s\n", cache_dir);
        return 1;
    }
    if( !load_catalog(catalog_dir) )
        return 1;

    if( names_dir )
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/configs/all.obj.compack", names_dir);
        lc_pack_load(&g_obj_names, path, "obj", 1);
        snprintf(path, sizeof(path), "%s/configs/all.spotanim.compack", names_dir);
        lc_pack_load(&g_spotanim_names, path, "spotanim", 1);
        g_have_names = 1;
        fprintf(stderr, "gameval names: %d obj, %d spotanim\n",
                lc_pack_named_count(&g_obj_names), lc_pack_named_count(&g_spotanim_names));
    }

    ToriDraw_Init();

    if( selftest_npc >= 0 )
        return selftest(selftest_npc, selftest_seq);

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 16) != 0 )
    {
        fprintf(stderr, "cannot listen on port %d: %s\n", port, strerror(errno));
        return 1;
    }

    fprintf(stderr, "entity viewer on http://127.0.0.1:%d/\n", port);

    for( ;; )
    {
        int fd = accept(srv, NULL, NULL);
        if( fd < 0 )
            continue;

        char req[4096];
        ssize_t n = read(fd, req, sizeof(req) - 1);
        if( n > 0 )
        {
            req[n] = '\0';
            char method[16] = { 0 };
            char target[1024] = { 0 };
            if( sscanf(req, "%15s %1023s", method, target) == 2 &&
                strcmp(method, "GET") == 0 )
            {
                /* The query is kept rather than discarded: the player build is
                 * parameterised by what is equipped, and that is a query. */
                char* q = strchr(target, '?');
                if( q )
                    *q = '\0';
                handle_request(fd, target, q ? q + 1 : NULL);
            }
            else
                send_404(fd);
        }
        close(fd);
    }
}
