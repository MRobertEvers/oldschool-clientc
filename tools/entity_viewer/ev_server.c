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
#include <assert.h>
#include "ev_caches.h"
#include "ev_textures.h"

/* The client's own RSCache_Model -> ToriDraw_Model pair, for POST /api/modelfile.
 * Hand-rolling that conversion is what ev_build.c's header comment warns about. */
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
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
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
 * The cache registry, and the index of whichever cache is active.
 *
 * `g_cache` is still the one open cache every handler uses — switching
 * rebinds it rather than adding a cache parameter to forty call sites. The
 * index is rebuilt on each switch and is what the search endpoints read; the
 * CSV catalog, when the active cache happens to have one, is layered on top and
 * is entirely optional (see ev_caches.h on why the two are separate).
 */
static struct EV_CacheList g_caches;
static struct EV_Index g_index;

/*
 * The active cache's baked textures.
 *
 * Built once per cache switch rather than per request: a procedural cache takes
 * about three seconds for the whole table because the dependency closures
 * overlap, and doing it per model would pay that repeatedly for the same
 * handful of materials.
 */
static struct EV_TextureSet g_texture_set;

/* Model building asks this before keeping a face's texture id — see
 * ev_build_set_texture_available. */
static int
server_texture_available(int id, void* user)
{
    (void)user;
    return ev_textures_get(&g_texture_set, id) != NULL;
}
static char g_caches_file[1024];
static int g_cache_open = 0;

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

/* The content tree, when one is named: its `pack/7_models.pack` says which
 * file each model id was exported to, and an exported file that has since been
 * edited is what the game draws. See content_model_path. */
static const char* g_content_dir = NULL;
static struct LC_Pack g_model_paths = { 0 };

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
        assert(grown);
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

    /*
     * No catalog: answer from the index instead of an empty list.
     *
     * The catalog is what supplies the rig matching, and it takes minutes to
     * build — so a cache the user just added has none. Returning [] there makes
     * a perfectly good cache look empty. The index has every npc's id and name,
     * which is what the list is; the rig columns simply read as zero.
     */
    if( g_npc_count == 0 && g_index.npc_count > 0 )
    {
        str_add(&s, "[");
        for( int i = 0; i < g_index.npc_count; i++ )
        {
            str_add(&s, i ? ",{" : "{");
            str_add(&s, "\"id\":%d,\"name\":", g_index.npcs[i].id);
            str_add_json(&s, g_index.npcs[i].name);
            str_add(&s, ",\"gameval\":");
            str_add_json(&s, NULL);
            str_add(&s, ",\"rig\":0,\"skeletal\":0,\"skinned\":false,\"maybe\":0}");
        }
        str_add(&s, "]");
        send_response(fd, "200 OK", "application/json", s.p, s.len);
        free(s.p);
        return;
    }

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
        /*
         * No catalog row. Answer from the index rather than 404 — the page
         * fetches this before drawing anything, so a 404 here aborts the boot
         * chain and leaves an empty canvas for a cache that is perfectly fine.
         *
         * The animation lists are empty because they are exactly what the
         * catalog computes; everything else the page needs to render the npc is
         * in the model endpoint, not here.
         */
        const char* name = NULL;
        for( int i = 0; i < g_index.npc_count; i++ )
            if( g_index.npcs[i].id == npc_id )
            {
                name = g_index.npcs[i].name;
                break;
            }
        if( !name && g_index.npc_count == 0 )
        {
            send_404(fd);
            return;
        }

        struct Str fallback = { 0 };
        str_add(&fallback, "{\"id\":%d,\"name\":", npc_id);
        str_add_json(&fallback, name);
        str_add(&fallback,
                ",\"gameval\":null,\"skinned\":false,\"framemaps\":[],"
                "\"rig\":[],\"maybe\":[],\"no_catalog\":true}");
        send_response(fd, "200 OK", "application/json", fallback.p, fallback.len);
        free(fallback.p);
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

/**
 * The distinct texture ids a model's faces name, as a comma list.
 *
 * Sent as a header on every model response so the page knows what to ask for.
 * Shipping the whole table instead is not an option: an RS727 set is 2315
 * textures at 128x128x4, or 151 MB, and a model names a couple of dozen.
 */
static void
model_texture_ids(const struct ToriDraw_Model* model, char* out, size_t cap)
{
    size_t at = 0;
    int seen[256];
    int seen_count = 0;

    out[0] = '\0';
    assert(model);
    if( !model->face_textures )
        return;

    for( int f = 0; f < model->face_count; f++ )
    {
        int id = model->face_textures[f];
        int dup = 0;
        if( id < 0 )
            continue;
        for( int i = 0; i < seen_count; i++ )
            if( seen[i] == id )
            {
                dup = 1;
                break;
            }
        if( dup || seen_count >= (int)(sizeof(seen) / sizeof(seen[0])) )
            continue;
        seen[seen_count++] = id;

        int n = snprintf(out + at, cap - at, "%s%d", at ? "," : "", id);
        if( n <= 0 || (size_t)n >= cap - at )
            break;
        at += (size_t)n;
    }
}

/**
 * A model blob plus the texture ids its faces name.
 *
 * The ids ride as a header rather than inside the blob so the wire format stays
 * the model's own — the page reads the header, fetches those textures once, and
 * both render paths draw with them.
 */
static void
send_model_with_textures(int fd, const void* body, size_t len, const char* tex_ids)
{
    char header[1536];
    int hlen = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Texture-Ids: %s\r\n"
        "Access-Control-Expose-Headers: X-Texture-Ids\r\n"
        "Connection: close\r\n\r\n",
        len, tex_ids);
    send_all(fd, header, (size_t)hlen);
    if( body && len )
        send_all(fd, body, len);
}

static void
handle_npc_model(int fd, int npc_id)
{
    /*
     * Try the HD build first.
     *
     * An RS2-era npc's textured faces are mostly cylinder- and cube-mapped, and
     * the classic raster can only plane-map — so it *skips* every one of them
     * and the model comes out untextured or, when nearly all its faces are
     * mapped, invisible. ev_build_npc_model_hd returns NULL for the models that
     * do not need it, which is every OldSchool npc.
     */
    struct ToriDraw_ModelHD* hd = ev_build_npc_model_hd(&g_cache, npc_id);
    if( hd )
    {
        struct EV_WireBuf hbuf = { 0 };
        int hok = ev_wire_write_model_hd(&hbuf, hd);
        char hd_ids[1024];
        model_texture_ids(&hd->base, hd_ids, sizeof(hd_ids));
        ToriDraw_ModelHDFree(hd);
        if( hok )
        {
            send_model_with_textures(fd, hbuf.data, hbuf.len, hd_ids);
            ev_wire_free(&hbuf);
            return;
        }
        ev_wire_free(&hbuf);
        /* Fall through to the plain build rather than 404: a wire failure is
         * not a reason to show nothing. */
    }

    struct ToriDraw_Model* model = ev_build_npc_model(&g_cache, npc_id);
    if( !model )
    {
        send_404(fd);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model(&buf, model);
    char tex_ids[1024];
    model_texture_ids(model, tex_ids, sizeof(tex_ids));
    ToriDraw_ModelFree(model);

    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        return;
    }
    send_model_with_textures(fd, buf.data, buf.len, tex_ids);
    ev_wire_free(&buf);
}

/**
 * One `key=value` out of a query string, percent-decoded.
 *
 * The existing handlers reach for `strstr(query, "wear=")` directly, which is
 * fine for a parameter that is always first and never contains a space. A
 * search box is neither.
 */
static void
query_param(const char* query, const char* key, char* out, size_t out_len)
{
    out[0] = '\0';
    if( !query )
        return;

    size_t klen = strlen(key);
    const char* p = query;
    while( p && *p )
    {
        if( strncmp(p, key, klen) == 0 && p[klen] == '=' )
        {
            const char* v = p + klen + 1;
            size_t n = 0;
            while( *v && *v != '&' && n + 1 < out_len )
            {
                if( *v == '%' && v[1] && v[2] )
                {
                    char hex[3] = { v[1], v[2], 0 };
                    out[n++] = (char)strtol(hex, NULL, 16);
                    v += 3;
                }
                else if( *v == '+' )
                {
                    out[n++] = ' ';
                    v++;
                }
                else
                {
                    out[n++] = *v++;
                }
            }
            out[n] = '\0';
            return;
        }
        p = strchr(p, '&');
        if( p )
            p++;
    }
}

/* ---- textures ------------------------------------------------------------ */

/* A plain byte sink. `struct Str` is text — it is grown by snprintf and would
 * stop at the first zero byte, and a texture blob is full of them. */
struct Bytes
{
    uint8_t* p;
    size_t len;
    size_t cap;
};

static int
bytes_put(struct Bytes* b, const void* data, size_t n)
{
    if( b->len + n > b->cap )
    {
        size_t want = b->cap ? b->cap * 2 : 4096;
        while( want < b->len + n )
            want *= 2;
        uint8_t* grown = realloc(b->p, want);
        assert(grown);
        b->p = grown;
        b->cap = want;
    }
    memcpy(b->p + b->len, data, n);
    b->len += n;
    return 1;
}

static int
bytes_put_u32(struct Bytes* b, uint32_t v)
{
    uint8_t enc[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                       (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
    return bytes_put(b, enc, sizeof(enc));
}

/**
 * `GET /api/textures.bin?ids=1,2,3` — those textures as an EVT1 blob.
 *
 * An id the cache does not have is skipped rather than erroring: a model can
 * name a texture that its own cache lacks, and the right result is that face
 * falling back to flat colour, not a failed page load.
 */
static void
handle_textures_bin(int fd, const char* query)
{
    char ids[4096] = { 0 };
    struct Bytes body = { 0 };
    struct Bytes out = { 0 };
    int count = 0;

    query_param(query, "ids", ids, sizeof(ids));

    for( const char* p = ids; p && *p; )
    {
        int id = atoi(p);
        const struct EV_Texture* tex = ev_textures_get(&g_texture_set, id);
        if( tex && tex->texels )
        {
            uint8_t hdr[4] = { (uint8_t)(tex->size & 0xFF), (uint8_t)((tex->size >> 8) & 0xFF),
                               (uint8_t)(tex->opaque ? 1 : 0), 0 };
            bytes_put_u32(&body, (uint32_t)id);
            bytes_put(&body, hdr, sizeof(hdr));
            for( int i = 0; i < tex->size * tex->size; i++ )
                bytes_put_u32(&body, (uint32_t)tex->texels[i]);
            count++;
        }
        p = strchr(p, ',');
        if( p )
            p++;
    }

    bytes_put_u32(&out, EV_WIRE_TEXTURES_MAGIC);
    bytes_put_u32(&out, (uint32_t)count);
    if( body.p )
        bytes_put(&out, body.p, body.len);
    free(body.p);

    send_response(fd, "200 OK", "application/octet-stream", (const char*)out.p, out.len);
    free(out.p);
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

/*
 * Where a spotanim's model actually lives, when the content tree has its own.
 *
 * `pack/7_models.pack` maps a model id to a path under `models/`, and the
 * porter writes the record out there byte for byte. Once someone edits that
 * file the cache's copy and the game's copy are different models, and a viewer
 * reading the cache shows an arc nobody sees in play. Returns 0 when there is
 * no content file for this id, in which case the cache's model is right.
 */
static int
content_model_path(int model_id, char* out, size_t cap)
{
    const char* rel;
    struct stat st;

    if( !g_content_dir || model_id < 0 || model_id > g_model_paths.max || !g_model_paths.names )
        return 0;
    rel = g_model_paths.names[model_id];
    if( !rel )
        return 0;
    snprintf(out, cap, "%s/models/%s.model", g_content_dir, rel);
    return stat(out, &st) == 0;
}

/** Which model file a graphic would be built from, and its rotation, both
 *  overridable: `?orient=3` forces a quarter-turn count so a rotation can be
 *  judged in the viewer before any asset or config is edited. */
static void
handle_spot_model(int fd, int spotanim_id, const char* query)
{
    int seq = -1;
    int orient = -1;
    char path[2048];
    const char* file = NULL;
    const char* o = query ? strstr(query, "orient=") : NULL;

    if( o )
        orient = atoi(o + 7);
    {
        int model_id = ev_spotanim_model_id(&g_cache, spotanim_id);
        if( content_model_path(model_id, path, sizeof(path)) )
            file = path;
    }

    struct ToriDraw_Model* model =
        ev_build_spotanim_model(&g_cache, spotanim_id, file, orient, &seq);
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
    assert(body);
    if( fread(body, 1, (size_t)len, f) != (size_t)len )
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


/* ------------------------------------------------------- a model file ---
 *
 * POST /api/modelfile  with the raw bytes of a model archive as the body.
 *
 * The point is to look at a model the cache pickers cannot reach — one dumped
 * to disk, or produced by a tool — and to see how the HD path routes it. So the
 * response says what the file *is* as well as returning it:
 *
 *   X-Model-Format   OB3 | V2 | V3 | OB2 | unknown
 *   X-Model-Faces    face count
 *   X-Model-Textured textured face count
 *   X-Model-HD       1 when the blob carries texture mappings
 *
 * A non-OB3 file is still decoded and returned when the decoder handles it; the
 * header says which layout it was, and the page reports that rather than
 * pretending everything is OB3.
 */

static const char*
model_format_name(const uint8_t* data, size_t len)
{
    if( len < 2 )
        return "unknown";
    uint8_t last = data[len - 1];
    uint8_t prev = data[len - 2];
    if( prev == 0xFF && last == 0xFF )
        return "OB3";
    if( prev == 0xFF && last == 0xFE )
        return "V2";
    if( prev == 0xFF && last == 0xFD )
        return "V3";
    return "OB2";
}

static void
handle_model_file(int fd, const uint8_t* body, size_t body_len)
{
    if( !body || body_len < 3 )
    {
        send_404(fd);
        return;
    }

    const char* format = model_format_name(body, body_len);

    struct RSCache_Model* rs = RSCache_ModelNewDecode((uint8_t*)body, (int)body_len);
    if( !rs )
    {
        send_404(fd);
        return;
    }

    /*
     * Copy the complex mapping parameters BEFORE converting: the conversion
     * moves arrays out of `rs` and frees the rest, and these are the fields it
     * does not carry across. They are one entry per textured face, so the copy
     * is small.
     */
    int tfc = rs->textured_face_count;
    int32_t* sx = NULL;
    int32_t* sy = NULL;
    int32_t* sz = NULL;
    int8_t* rot = NULL;
    int8_t* dir = NULL;
    int8_t* spd = NULL;
    int8_t* tu = NULL;
    int8_t* tv = NULL;
#define EV_DUP(dstv, srcv, type)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if( (srcv) && tfc > 0 )                                                                    \
        {                                                                                          \
            (dstv) = (type*)malloc((size_t)tfc * sizeof(type));                                    \
            if( (dstv) )                                                                           \
                memcpy((dstv), (srcv), (size_t)tfc * sizeof(type));                                \
        }                                                                                          \
    } while( 0 )
    EV_DUP(sx, rs->texture_scale_x, int32_t);
    EV_DUP(sy, rs->texture_scale_y, int32_t);
    EV_DUP(sz, rs->texture_scale_z, int32_t);
    EV_DUP(rot, rs->texture_rotation, int8_t);
    EV_DUP(dir, rs->texture_direction, int8_t);
    EV_DUP(spd, rs->texture_speed, int8_t);
    EV_DUP(tu, rs->texture_trans_u, int8_t);
    EV_DUP(tv, rs->texture_trans_v, int8_t);
#undef EV_DUP

    struct ToriRS_Model* mid = ToriRS_ModelFromRSCache(rs);
    RSCache_ModelFree(rs);
    if( !mid )
    {
        send_404(fd);
        goto done_params;
    }

    struct ToriDraw_Model* model = ToriDraw_ModelFromToriRS(mid);
    ToriRS_ModelFree(mid);
    if( !model )
    {
        send_404(fd);
        goto done_params;
    }

    /* An HD model embeds its base by value, so the contents move across and the
     * original shell is released without disturbing the arrays. */
    struct ToriDraw_ModelHD* hd =
        (struct ToriDraw_ModelHD*)calloc(1, sizeof(struct ToriDraw_ModelHD));
    if( !hd )
    {
        ToriDraw_ModelFree(model);
        send_404(fd);
        goto done_params;
    }
    hd->base = *model;
    free(model);

    /*
     * Light it, or every face draws at its authored colour and the shape is
     * unreadable — and, more sharply, face_colors_a/b/c do not exist until the
     * lighting pass creates them.
     *
     * Through a PLAIN handle: lighting is a base-model operation and
     * ToriDraw_LightModelActor only accepts TORIDRAWMK_MODEL, by design (the
     * scene and lighting paths were deliberately not widened to the HD kind).
     * Passing an HD handle here silently skips lighting and leaves those arrays
     * NULL for the raster to dereference.
     */
    struct ToriDraw_ModelHandle lit;
    memset(&lit, 0, sizeof(lit));
    lit.kind = TORIDRAWMK_MODEL;
    lit.u.model.model = &hd->base;
    ToriDraw_LightModelActor(lit, 768, 64);

    /* The mappings are derived from the bind pose, so this must happen before
     * anything animates the model. */
    ToriDraw_ModelBuildTextureMappings(hd, sx, sy, sz, rot, dir, spd, tu, tv);

    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model_hd(&buf, hd);

    int faces = hd->base.face_count;
    int textured = hd->base.textured_face_count;
    int has_mappings = hd->texture_mappings ? 1 : 0;
    char tex_ids[1024];
    model_texture_ids(&hd->base, tex_ids, sizeof(tex_ids));
    ToriDraw_ModelHDFree(hd);

    if( !ok )
    {
        ev_wire_free(&buf);
        send_404(fd);
        goto done_params;
    }

    /* Not send_response: the format and the tallies ride as headers so the page
     * can report what the file was without a second request, and that needs a
     * bespoke header block. */
    char header[512];
    int hlen = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Model-Format: %s\r\n"
        "X-Model-Faces: %d\r\n"
        "X-Model-Textured: %d\r\n"
        "X-Model-HD: %d\r\n"
        "X-Texture-Ids: %s\r\n"
        "Access-Control-Expose-Headers: X-Model-Format, X-Model-Faces, "
        "X-Model-Textured, X-Model-HD, X-Texture-Ids\r\n"
        "Connection: close\r\n\r\n",
        buf.len, format, faces, textured, has_mappings, tex_ids);
    send_all(fd, header, (size_t)hlen);
    send_all(fd, buf.data, buf.len);
    ev_wire_free(&buf);

done_params:
    free(sx);
    free(sy);
    free(sz);
    free(rot);
    free(dir);
    free(spd);
    free(tu);
    free(tv);
}

/* ---- the cache registry -------------------------------------------------- */


/**
 * Make `index` the active cache: reopen, reindex, and pick up a catalog if one
 * happens to sit beside it.
 *
 * The catalog is looked for at `<cache>.anims` and `out/<basename>_anims`,
 * which are where ev_catalog's documented invocations put it. Not finding one
 * is normal and not an error — the index alone answers every search; the
 * catalog only adds rig matching.
 */
static int
select_cache(int index)
{
    if( index < 0 || index >= g_caches.count )
        return 0;

    struct EV_CacheEntry* e = &g_caches.items[index];

    struct RSCache profile;
    if( !tool_resolve_profile(e->rev, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "select_cache: unknown revision '%s'\n", e->rev);
        return 0;
    }

    struct Tool_Dat2Cache next;
    if( !tool_dat2_open(e->path, &profile, &next) )
    {
        fprintf(stderr, "select_cache: cannot open %s\n", e->path);
        return 0;
    }

    if( g_cache_open )
        tool_dat2_close(&g_cache);
    g_cache = next;
    g_cache_open = 1;

    /*
     * Drop the catalog. It describes the cache that WAS open — its npc rows,
     * its sequence ids, its rig matches — and none of that transfers. Keeping
     * it means the list still shows the old cache's npcs under the new cache's
     * name, and clicking one asks for an id that means something else.
     *
     * The rows themselves are not freed here: a catalog is loaded once at
     * startup from --catalog and there is no per-cache one to load in its
     * place, so zeroing the counts is what makes every reader fall through to
     * the index. See handle_npcs_json.
     */
    g_npc_count = 0;
    g_seq_count = 0;
    g_name_match_count = 0;

    ev_index_free(&g_index);
    clock_t index_t0 = clock();
    ev_index_build(&g_cache, &profile, e->path, e->rev, &g_index);
    int index_ms = (int)((clock() - index_t0) * 1000 / CLOCKS_PER_SEC);
    ev_textures_free(&g_texture_set);
    ev_textures_load(&g_cache, &g_texture_set);
    ev_build_set_texture_available(server_texture_available, NULL);
    e->npc_count = g_index.npc_count;
    e->seq_count = g_index.seq_count;
    e->model_count = g_index.model_count;
    e->indexed = true;

    g_caches.active = index;
    fprintf(stderr, "cache: %s (%s) — %d npcs, %d sequences, %d models [index %d ms]\n",
            e->label, e->rev, e->npc_count, e->seq_count, e->model_count, index_ms);
    return 1;
}

static void
handle_caches_json(int fd)
{
    struct Str out = { 0 };
    str_add(&out, "{\"active\":%d,\"caches\":[", g_caches.active);
    for( int i = 0; i < g_caches.count; i++ )
    {
        const struct EV_CacheEntry* e = &g_caches.items[i];
        str_add(&out, "%s{\"index\":%d,\"label\":", i ? "," : "", i);
        str_add_json(&out, e->label);
        str_add(&out, ",\"path\":");
        str_add_json(&out, e->path);
        str_add(&out, ",\"rev\":");
        str_add_json(&out, e->rev);
        str_add(&out, ",\"indexed\":%d,\"npcs\":%d,\"seqs\":%d,\"models\":%d}",
                e->indexed ? 1 : 0, e->npc_count, e->seq_count, e->model_count);
    }
    str_add(&out, "]}");
    send_response(fd, "200 OK", "application/json", out.p ? out.p : "{}", out.len);
    free(out.p);
}

/** `q` is matched against the name AND the id, so "2745" and "jad" both work. */
static int
matches(const char* name, int id, const char* q)
{
    if( !q || !*q )
        return 1;
    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "%d", id);
    if( strstr(idbuf, q) )
        return 1;
    return name && strcasestr(name, q) != NULL;
}

#define EV_SEARCH_LIMIT 400

static void
handle_search_npcs(int fd, const char* query)
{
    char q[128] = { 0 };
    query_param(query, "q", q, sizeof(q));

    struct Str out = { 0 };
    str_add(&out, "[");
    int n = 0;
    for( int i = 0; i < g_index.npc_count && n < EV_SEARCH_LIMIT; i++ )
    {
        if( !matches(g_index.npcs[i].name, g_index.npcs[i].id, q) )
            continue;
        str_add(&out, "%s{\"id\":%d,\"name\":", n ? "," : "", g_index.npcs[i].id);
        str_add_json(&out, g_index.npcs[i].name);
        str_add(&out, "}");
        n++;
    }
    str_add(&out, "]");
    send_response(fd, "200 OK", "application/json", out.p ? out.p : "[]", out.len);
    free(out.p);
}

/** Sequences and models have no names in the cache, so this is an id filter. */
static void
handle_search_ids(int fd, const char* query, const int* ids, int count)
{
    char q[128] = { 0 };
    query_param(query, "q", q, sizeof(q));

    struct Str out = { 0 };
    str_add(&out, "[");
    int n = 0;
    for( int i = 0; i < count && n < EV_SEARCH_LIMIT; i++ )
    {
        if( !matches(NULL, ids[i], q) )
            continue;
        str_add(&out, "%s%d", n ? "," : "", ids[i]);
        n++;
    }
    str_add(&out, "]");
    send_response(fd, "200 OK", "application/json", out.p ? out.p : "[]", out.len);
    free(out.p);
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
        handle_spot_model(fd, id, query);
    else if( route_id(target, "/api/spot/", ".json", &id) )
        handle_spot_json(fd, id);
    else if( route_id(target, "/api/rig/", ".json", &id) )
        handle_rig_json(fd, id);
    /* The cache registry and the searches over the active cache's index. */
    else if( strcmp(target, "/api/caches.json") == 0 )
        handle_caches_json(fd);
    else if( strcmp(target, "/api/caches/select") == 0 )
    {
        char v[32] = { 0 };
        query_param(query, "index", v, sizeof(v));
        int ok = select_cache(atoi(v));
        if( ok )
            ev_caches_save(&g_caches, g_caches_file);
        handle_caches_json(fd);
    }
    else if( strcmp(target, "/api/caches/add") == 0 )
    {
        char path[EV_CACHE_PATH_MAX] = { 0 };
        char rev[32] = { 0 };
        query_param(query, "path", path, sizeof(path));
        query_param(query, "rev", rev, sizeof(rev));
        if( path[0] )
        {
            int added = ev_caches_add(&g_caches, path, rev[0] ? rev : NULL);
            if( added >= 0 )
                ev_caches_save(&g_caches, g_caches_file);
        }
        handle_caches_json(fd);
    }
    else if( strcmp(target, "/api/caches/rev") == 0 )
    {
        char v[32] = { 0 };
        char rev[32] = { 0 };
        query_param(query, "index", v, sizeof(v));
        query_param(query, "rev", rev, sizeof(rev));
        int i = atoi(v);
        if( ev_caches_set_rev(&g_caches, i, rev) )
        {
            ev_caches_save(&g_caches, g_caches_file);
            /* Reopen when it is the active one: the profile decides how every
             * record decodes, so leaving the old one open would keep answering
             * with the widths the user just rejected. */
            if( i == g_caches.active )
                select_cache(i);
        }
        handle_caches_json(fd);
    }
    else if( strcmp(target, "/api/caches/remove") == 0 )
    {
        char v[32] = { 0 };
        query_param(query, "index", v, sizeof(v));
        if( ev_caches_remove(&g_caches, atoi(v)) )
            ev_caches_save(&g_caches, g_caches_file);
        handle_caches_json(fd);
    }
    else if( strcmp(target, "/api/caches/discover") == 0 )
    {
        char root[EV_CACHE_PATH_MAX] = { 0 };
        query_param(query, "root", root, sizeof(root));
        if( ev_caches_discover(&g_caches, root[0] ? root : ".") > 0 )
            ev_caches_save(&g_caches, g_caches_file);
        handle_caches_json(fd);
    }
    else if( strcmp(target, "/api/textures.bin") == 0 )
        handle_textures_bin(fd, query);
    else if( strcmp(target, "/api/search/npcs.json") == 0 )
        handle_search_npcs(fd, query);
    else if( strcmp(target, "/api/search/seqs.json") == 0 )
        handle_search_ids(fd, query, g_index.seq_ids, g_index.seq_count);
    else if( strcmp(target, "/api/search/models.json") == 0 )
        handle_search_ids(fd, query, g_index.model_ids, g_index.model_count);
    /* POST /api/modelfile is routed in the accept loop, where the body is. */
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
    /* Where --caches discovery looks; run.sh passes the repo root. */
    const char* cache_root = ".";
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
        else if( strcmp(argv[i], "--cache-root") == 0 && i + 1 < argc )
            cache_root = argv[++i];
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

    /*
     * The catalog is optional now.
     *
     * It used to be the only source of the npc list, so no catalog meant no
     * page. The per-cache index supplies that directly, in about a tenth of a
     * second against the five minutes ev_catalog takes — so requiring one would
     * make "switch to this cache" a coffee break. What the catalog still adds,
     * when present, is the rig matching.
     */
    if( !rev_name || !cache_dir )
    {
        fprintf(
            stderr,
            "Usage: %s --rev osrs239 <cache_dir> [--catalog <dir>] "
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
    g_cache_open = 1;

    /*
     * The registry. The cache named on the command line is listed and made
     * active so nothing about the existing invocation changes; anything the
     * user added in a previous session comes back from the registry file, and a
     * one-level scan of the repo root turns "add a cache" into "click one".
     */
    snprintf(g_caches_file, sizeof(g_caches_file), "%s/.ev_caches", g_web_dir ? g_web_dir : ".");
    ev_caches_load(&g_caches, g_caches_file);
    int startup_index = ev_caches_add(&g_caches, cache_dir, rev_name);
    /* The scan root is explicit rather than the CWD: the server is normally
     * started from tools/entity_viewer, where a scan finds nothing, and a
     * discovery that silently returns zero looks like "you have one cache". */
    ev_caches_discover(&g_caches, cache_root);
    ev_caches_save(&g_caches, g_caches_file);
    if( startup_index >= 0 )
    {
        g_caches.active = startup_index;
        struct EV_CacheEntry* e = &g_caches.items[startup_index];
        clock_t index_t0 = clock();
        ev_index_build(&g_cache, &profile, e->path, e->rev, &g_index);
        int index_ms = (int)((clock() - index_t0) * 1000 / CLOCKS_PER_SEC);
        ev_textures_load(&g_cache, &g_texture_set);
        ev_build_set_texture_available(server_texture_available, NULL);
        e->npc_count = g_index.npc_count;
        e->seq_count = g_index.seq_count;
        e->model_count = g_index.model_count;
        e->indexed = true;
        fprintf(stderr, "cache: %s (%s) — %d npcs, %d sequences, %d models [index %d ms]\n",
                e->label, e->rev, e->npc_count, e->seq_count, e->model_count, index_ms);
    }
    if( catalog_dir && !load_catalog(catalog_dir) )
        fprintf(stderr, "catalog at %s could not be read — rig matching is off\n", catalog_dir);

    if( names_dir )
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/configs/all.obj.compack", names_dir);
        lc_pack_load(&g_obj_names, path, "obj", 1);
        snprintf(path, sizeof(path), "%s/configs/all.spotanim.compack", names_dir);
        lc_pack_load(&g_spotanim_names, path, "spotanim", 1);
        /* Same directory: a content tree that names records also holds their
         * exported assets, so --names is what turns the override on. */
        g_content_dir = names_dir;
        snprintf(path, sizeof(path), "%s/pack/7_models.pack", names_dir);
        lc_pack_load(&g_model_paths, path, "models", 1);
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

        /*
         * One read is enough for a GET, whose whole request fits comfortably.
         * A POST carries a model file, which does not — so the headers are
         * parsed out of the first read and the body is drained to
         * Content-Length before anything is decoded. A short body would decode
         * as a truncated model, which is exactly the plausible-garbage failure
         * the rest of this tool works to avoid.
         */
        char req[8192];
        ssize_t n = read(fd, req, sizeof(req) - 1);
        if( n > 0 )
        {
            req[n] = '\0';
            char method[16] = { 0 };
            char target[1024] = { 0 };
            if( sscanf(req, "%15s %1023s", method, target) == 2 )
            {
                char* q = strchr(target, '?');
                if( q )
                    *q = '\0';

                if( strcmp(method, "GET") == 0 )
                {
                    /* The query is kept rather than discarded: the player build
                     * is parameterised by what is equipped, and that is a
                     * query. */
                    handle_request(fd, target, q ? q + 1 : NULL);
                }
                else if( strcmp(method, "POST") == 0 &&
                         strcmp(target, "/api/modelfile") == 0 )
                {
                    const char* hdr_end = strstr(req, "\r\n\r\n");
                    size_t want = 0;
                    const char* cl = strcasestr(req, "content-length:");
                    if( cl )
                        want = (size_t)strtoul(cl + 15, NULL, 10);

                    uint8_t* body = NULL;
                    size_t have = 0;
                    if( hdr_end && want > 0 && want < (64u << 20) )
                    {
                        size_t offset = (size_t)(hdr_end + 4 - req);
                        have = (size_t)n > offset ? (size_t)n - offset : 0;
                        if( have > want )
                            have = want;
                        body = (uint8_t*)malloc(want);
                        if( body )
                        {
                            memcpy(body, hdr_end + 4, have);
                            while( have < want )
                            {
                                ssize_t got =
                                    read(fd, body + have, want - have);
                                if( got <= 0 )
                                    break;
                                have += (size_t)got;
                            }
                        }
                    }

                    if( body && have == want )
                        handle_model_file(fd, body, want);
                    else
                        send_404(fd);
                    free(body);
                }
                else
                    send_404(fd);
            }
            else
                send_404(fd);
        }
        close(fd);
    }
}
