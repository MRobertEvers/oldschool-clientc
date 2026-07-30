#include "lc_pack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* const pack_types[LC_PACK_COUNT] = {
    "npc", "obj", "seq", "spotanim", "loc", "model", "anim", "animset", "base", "map", "flo", "texture",
};

static char*
sdup(const char* s)
{
    size_t n = strlen(s);
    char* d = malloc(n + 1);
    if( d )
        memcpy(d, s, n + 1);
    return d;
}

/** Replace `*slot` with a copy of `text`, or with NULL when `text` is NULL. */
static int
sset(
    char** slot,
    const char* text)
{
    char* copy = NULL;
    if( text )
    {
        copy = sdup(text);
        if( !copy )
            return 0;
    }
    free(*slot);
    *slot = copy;
    return 1;
}

static int
pack_grow(struct LC_Pack* pack, int needed)
{
    assert(pack);
    if( needed <= pack->capacity )
        return 1;
    int cap = pack->capacity == 0 ? 1024 : pack->capacity;
    while( cap < needed )
        cap *= 2;

    /* The three arrays are one table with three columns; growing them apart
     * would let a comment end up on the wrong id after a realloc failure. */
    char** names = realloc(pack->names, (size_t)cap * sizeof(char*));
    if( !names )
        return 0;
    memset(names + pack->capacity, 0, (size_t)(cap - pack->capacity) * sizeof(char*));
    pack->names = names;

    char** trailing = realloc(pack->trailing, (size_t)cap * sizeof(char*));
    if( !trailing )
        return 0;
    memset(trailing + pack->capacity, 0, (size_t)(cap - pack->capacity) * sizeof(char*));
    pack->trailing = trailing;

    char** preceding = realloc(pack->preceding, (size_t)cap * sizeof(char*));
    if( !preceding )
        return 0;
    memset(preceding + pack->capacity, 0, (size_t)(cap - pack->capacity) * sizeof(char*));
    pack->preceding = preceding;

    pack->capacity = cap;
    return 1;
}

int
lc_pack_set(struct LC_Pack* pack, int id, const char* name)
{
    assert(pack && name);
    if( id < 0 )
        return 0;
    if( !pack_grow(pack, id + 1) )
        return 0;
    /* Comments are deliberately untouched: a rename does not retract what
     * someone wrote about the id. */
    if( !sset(&pack->names[id], name) )
        return 0;
    if( id + 1 > pack->max )
        pack->max = id + 1;
    return 1;
}

/* ---- tombstones ---------------------------------------------------------- */

static int
tombstone_add(struct LC_Pack* pack, int id)
{
    if( pack->tombstone_count == pack->tombstone_capacity )
    {
        int cap = pack->tombstone_capacity ? pack->tombstone_capacity * 2 : 16;
        int* grown = realloc(pack->tombstones, (size_t)cap * sizeof(int));
        if( !grown )
            return 0;
        pack->tombstones = grown;
        pack->tombstone_capacity = cap;
    }
    pack->tombstones[pack->tombstone_count++] = id;
    return 1;
}

int
lc_pack_remove(struct LC_Pack* pack, int id)
{
    assert(pack);
    if( id < 0 || id >= pack->capacity || !pack->names[id] )
        return 0;
    free(pack->names[id]);
    pack->names[id] = NULL;
    free(pack->trailing[id]);
    pack->trailing[id] = NULL;
    free(pack->preceding[id]);
    pack->preceding[id] = NULL;
    /* The engine recomputes max from the file it reads, so dropping the top id
     * has to lower it here too — otherwise the next allocation lands past the
     * end of what gets written and leaves an id no line accounts for. */
    if( id + 1 == pack->max )
    {
        int max = 0;
        for( int i = id - 1; i >= 0; i-- )
        {
            if( pack->names[i] )
            {
                max = i + 1;
                break;
            }
        }
        pack->max = max;
    }
    /* Say it out loud. A save merges against the file it is replacing, so an id
     * that is merely absent comes straight back; only a recorded removal is a
     * removal. */
    tombstone_add(pack, id);
    pack->removed++;
    return 1;
}

/* ---- growable text ------------------------------------------------------- */

struct LC_Text
{
    char* data;
    size_t len;
    size_t cap;
};

static int
text_append(
    struct LC_Text* text,
    const char* bytes,
    size_t len)
{
    if( text->len + len + 1 > text->cap )
    {
        size_t cap = text->cap ? text->cap : 4096;
        while( cap < text->len + len + 1 )
            cap *= 2;
        char* grown = realloc(text->data, cap);
        if( !grown )
            return 0;
        text->data = grown;
        text->cap = cap;
    }
    memcpy(text->data + text->len, bytes, len);
    text->len += len;
    text->data[text->len] = '\0';
    return 1;
}

static int
text_str(
    struct LC_Text* text,
    const char* str)
{
    return text_append(text, str, strlen(str));
}

static void
text_free(struct LC_Text* text)
{
    free(text->data);
    text->data = NULL;
    text->len = text->cap = 0;
}

/* ---- load ---------------------------------------------------------------- */

/**
 * Is this line a comment or blank — i.e. content to carry, not a record?
 *
 * The engine's loader requires a record line to start with digits (`/^\d+=/`);
 * everything else it skips. Here the same test decides whether the line is
 * *kept* as prose or *counted* as malformed, which is the whole difference
 * between a file that survives a rewrite and one that gets thinned.
 */
static int
line_is_prose(const char* line)
{
    const char* scan = line;
    while( *scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n' )
        scan++;
    if( !*scan )
        return 1; /* blank */
    return scan[0] == '/' && scan[1] == '/';
}

int
lc_pack_load(
    struct LC_Pack* pack,
    const char* path,
    const char* type,
    int allow_missing)
{
    assert(pack && path && type);
    memset(pack, 0, sizeof(*pack));
    snprintf(pack->type, sizeof(pack->type), "%s", type);

    FILE* f = fopen(path, "rb");
    if( !f )
    {
        if( allow_missing )
            return 1;
        fprintf(stderr, "pack: cannot open %s\n", path);
        return 0;
    }

    /*
     * Prose accumulates here until an id line claims it. Before the first id
     * that is the file header (`preamble`); after the last it is the `trailer`.
     *
     * A line longer than the buffer arrives in two reads, and because both are
     * appended raw it reassembles byte for byte — so an over-long comment costs
     * nothing. An over-long `id=name` line would not survive, which is why the
     * buffer is wide enough that no pack file in any tree comes close.
     */
    struct LC_Text pending = { 0 };
    int have_id = 0;
    int ok = 1;

    char line[2048];
    while( ok && fgets(line, sizeof(line), f) )
    {
        if( line_is_prose(line) )
        {
            ok = text_append(&pending, line, strlen(line));
            continue;
        }

        /* Split off a trailing `//` comment before the name is measured. Layer-1
         * packs used one to carry the cache name an alias stands in for
         * (`3254=guard  // cache: guard1`), and that convention is now the only
         * way to record the provenance the directory split used to encode. */
        char* note = strstr(line, "//");
        char* raw_note = NULL;
        if( note )
        {
            /* Take the whitespace before `//` with it, so a save reproduces the
             * padding rather than re-deciding it. */
            char* pad = note;
            while( pad > line && (pad[-1] == ' ' || pad[-1] == '\t') )
                pad--;
            size_t note_len = strlen(note);
            while( note_len > 0 && (note[note_len - 1] == '\n' || note[note_len - 1] == '\r') )
                note_len--;
            raw_note = malloc((size_t)(note - pad) + note_len + 1);
            if( !raw_note )
            {
                ok = 0;
                break;
            }
            memcpy(raw_note, pad, (size_t)(note - pad));
            memcpy(raw_note + (note - pad), note, note_len);
            raw_note[(size_t)(note - pad) + note_len] = '\0';
            *pad = '\0';
        }

        char* eq = strchr(line, '=');
        if( !eq )
        {
            free(raw_note);
            pack->malformed++;
            continue;
        }
        *eq = '\0';
        char* name = eq + 1;

        while( *name == ' ' || *name == '\t' )
            name++;
        size_t n = strlen(name);
        while( n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r' || name[n - 1] == ' ' ||
                         name[n - 1] == '\t') )
            name[--n] = '\0';

        char* end = NULL;
        char* key = line;
        while( *key == ' ' || *key == '\t' )
            key++;
        long id = strtol(key, &end, 10);
        size_t key_len = end ? (size_t)(end - key) : 0;
        while( end && (*end == ' ' || *end == '\t') )
            end++;
        if( n == 0 || key_len == 0 || !end || *end != '\0' || id < 0 )
        {
            free(raw_note);
            pack->malformed++;
            continue;
        }

        if( !lc_pack_set(pack, (int)id, name) )
        {
            free(raw_note);
            ok = 0;
            break;
        }
        free(pack->trailing[(int)id]);
        pack->trailing[(int)id] = raw_note;

        if( pending.len > 0 )
        {
            if( !have_id )
                pack->preamble = pending.data; /* the file header */
            else
                pack->preceding[(int)id] = pending.data;
            memset(&pending, 0, sizeof(pending));
        }
        have_id = 1;
    }

    /* Whatever is left anchors to no id at all. */
    if( ok && pending.len > 0 )
    {
        if( have_id )
            pack->trailer = pending.data;
        else
            pack->preamble = pending.data;
        memset(&pending, 0, sizeof(pending));
    }
    text_free(&pending);
    fclose(f);

    if( !ok )
        return 0;
    if( pack->malformed )
        fprintf(stderr, "pack: %s has %d %s neither a comment nor `id=name`\n", path,
                pack->malformed, pack->malformed == 1 ? "line that is" : "lines that are");
    return 1;
}

/* ---- notes -------------------------------------------------------------- */

const char*
lc_pack_note(
    const struct LC_Pack* pack,
    int id)
{
    assert(pack);
    if( id < 0 || id >= pack->capacity || !pack->trailing || !pack->trailing[id] )
        return NULL;
    const char* scan = strstr(pack->trailing[id], "//");
    if( !scan )
        return NULL;
    scan += 2;
    while( *scan == ' ' || *scan == '\t' )
        scan++;
    return scan;
}

int
lc_pack_set_note(
    struct LC_Pack* pack,
    int id,
    const char* text)
{
    assert(pack);
    if( id < 0 )
        return 0;
    if( !pack_grow(pack, id + 1) )
        return 0;
    if( !text )
        return sset(&pack->trailing[id], NULL);
    char* raw = malloc(strlen(text) + 6);
    if( !raw )
        return 0;
    /* Two spaces then `//`, matching what the existing files use. */
    sprintf(raw, "  // %s", text);
    free(pack->trailing[id]);
    pack->trailing[id] = raw;
    return 1;
}

/* ---- save --------------------------------------------------------------- */

int
lc_pack_synthetic_id(
    const char* type,
    const char* name)
{
    assert(type && name);
    size_t type_len = strlen(type);

    if( strncmp(name, type, type_len) != 0 || name[type_len] != '_' )
        return -1;

    const char* digits = name + type_len + 1;
    if( !*digits )
        return -1;
    /* Leading zeros would make two spellings of one id, and nothing writes
     * them, so `param_007` is not param 7 — it is a name that happens to look
     * like one, and treating it as an id would bind it silently. */
    if( digits[0] == '0' && digits[1] )
        return -1;
    for( const char* c = digits; *c; c++ )
    {
        if( *c < '0' || *c > '9' )
            return -1;
    }

    long id = strtol(digits, NULL, 10);
    if( id < 0 || id > 0x7fffffff )
        return -1;
    return (int)id;
}

/** Does the id carry prose someone wrote? Filler lines are kept for this. */
static int
has_comment(const struct LC_Pack* pack, int id)
{
    if( pack->trailing && pack->trailing[id] )
        return 1;
    if( pack->preceding && pack->preceding[id] )
        return 1;
    return 0;
}

/** Would `sparse` omit this line? */
static int
is_omitted_filler(const struct LC_Pack* pack, int id)
{
    if( lc_pack_synthetic_id(pack->type, pack->names[id]) != id )
        return 0;
    return !has_comment(pack, id);
}

int
lc_pack_named_count(const struct LC_Pack* pack)
{
    int real = 0;
    assert(pack);
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( pack->names[id] && lc_pack_synthetic_id(pack->type, pack->names[id]) != id )
            real++;
    }
    return real;
}

static int
pack_render(
    const struct LC_Pack* pack,
    int sparse,
    struct LC_Text* out,
    int* out_lines)
{
    int lines = 0;

    if( pack->preamble && !text_str(out, pack->preamble) )
        return 0;
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( !pack->names[id] )
            continue;
        if( sparse && is_omitted_filler(pack, id) )
            continue;
        if( pack->preceding[id] && !text_str(out, pack->preceding[id]) )
            return 0;

        char head[64];
        snprintf(head, sizeof(head), "%d=", id);
        if( !text_str(out, head) || !text_str(out, pack->names[id]) )
            return 0;
        if( pack->trailing[id] && !text_str(out, pack->trailing[id]) )
            return 0;
        if( !text_str(out, "\n") )
            return 0;
        lines++;
    }
    if( pack->trailer && !text_str(out, pack->trailer) )
        return 0;

    *out_lines = lines;
    return 1;
}

/**
 * The pack as it will be written: the file at `path` with `pack` laid over it.
 *
 * Starting from disk rather than from memory is what makes a save a merge. A tool
 * that regenerates one namespace holds only the ids it generated; anything else
 * in the file is someone else's, and the only way not to delete it is to read it
 * back before writing.
 */
static int
pack_merged_snapshot(
    const struct LC_Pack* pack,
    const char* path,
    struct LC_Pack* out)
{
    if( !lc_pack_load(out, path, pack->type, 1) )
        return 0;
    out->added = pack->added;
    out->removed = pack->removed;
    /* A malformed line in the file being replaced is not this write's news. */
    out->malformed = 0;

    for( int i = 0; i < pack->tombstone_count; i++ )
    {
        int id = pack->tombstones[i];
        if( id >= 0 && id < out->capacity && out->names[id] )
        {
            free(out->names[id]);
            out->names[id] = NULL;
            free(out->trailing[id]);
            out->trailing[id] = NULL;
            free(out->preceding[id]);
            out->preceding[id] = NULL;
        }
    }

    for( int id = 0; id < pack->capacity; id++ )
    {
        if( !pack->names[id] )
            continue;
        if( !pack_grow(out, id + 1) )
            return 0;
        if( !sset(&out->names[id], pack->names[id]) )
            return 0;
        /* Comments the caller carries win; comments only the file has survive.
         * The usual case is that the caller loaded this same file, so the two
         * agree and neither rule is doing anything. */
        if( pack->trailing[id] && !sset(&out->trailing[id], pack->trailing[id]) )
            return 0;
        if( pack->preceding[id] && !sset(&out->preceding[id], pack->preceding[id]) )
            return 0;
    }
    if( pack->preamble && !sset(&out->preamble, pack->preamble) )
        return 0;
    if( pack->trailer && !sset(&out->trailer, pack->trailer) )
        return 0;

    out->max = 0;
    for( int id = out->capacity - 1; id >= 0; id-- )
    {
        if( out->names[id] )
        {
            out->max = id + 1;
            break;
        }
    }
    return 1;
}

/** The file's bytes, or NULL. `*out_len` is set on success. */
static char*
slurp(
    const char* path,
    size_t* out_len)
{
    FILE* f = fopen(path, "rb");
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(f);
        return NULL;
    }
    char* data = malloc((size_t)size + 1);
    if( !data )
    {
        fclose(f);
        return NULL;
    }
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    *out_len = (size_t)size;
    fclose(f);
    return data;
}

/** The path a report should name: the last two components, or the whole thing. */
static const char*
path_tail(const char* path)
{
    const char* last = NULL;
    for( const char* scan = path; *scan; scan++ )
    {
        if( *scan == '/' || *scan == '\\' )
            last = scan;
    }
    if( !last )
        return path;
    for( const char* scan = last - 1; scan >= path; scan-- )
    {
        if( *scan == '/' || *scan == '\\' )
            return scan + 1;
    }
    return path;
}

static int
pack_write(
    const struct LC_Pack* pack,
    const char* path,
    int sparse)
{
    assert(pack && path);

    struct LC_Pack merged;
    if( !pack_merged_snapshot(pack, path, &merged) )
    {
        lc_pack_free(&merged);
        return 0;
    }

    struct LC_Text text = { 0 };
    int lines = 0;
    if( !pack_render(&merged, sparse, &text, &lines) )
    {
        text_free(&text);
        lc_pack_free(&merged);
        return 0;
    }
    int named = lc_pack_named_count(&merged);
    lc_pack_free(&merged);

    if( text.len == 0 )
    {
        /* Not an empty namespace — a namespace the cache does not name. Leaving
         * a file of pure filler behind would say the opposite. A file holding
         * only prose does not reach here: the prose is bytes. */
        text_free(&text);
        remove(path);
        return 1;
    }

    /* Unchanged means unchanged: no write, no mtime bump, and nothing printed.
     * That is what makes the report below worth reading — a line only appears
     * when a pack actually moved. */
    size_t existing_len = 0;
    char* existing = slurp(path, &existing_len);
    if( existing && existing_len == text.len && memcmp(existing, text.data, text.len) == 0 )
    {
        free(existing);
        text_free(&text);
        return 1;
    }
    free(existing);

    FILE* f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "pack: cannot write %s\n", path);
        text_free(&text);
        return 0;
    }
    size_t wrote = fwrite(text.data, 1, text.len, f);
    int ok = wrote == text.len;
    if( fclose(f) != 0 )
        ok = 0;
    text_free(&text);
    if( !ok )
    {
        fprintf(stderr, "pack: short write to %s\n", path);
        return 0;
    }

    /*
     * A removal reported here with no `lc_pack_remove` behind it is impossible by
     * construction, which is the point of printing it: the counter is only
     * touched by the explicit path, so a non-zero `-N` is always something
     * someone asked for.
     */
    char delta[64] = "";
    if( pack->added && pack->removed )
        snprintf(delta, sizeof(delta), " (+%d, -%d)", pack->added, pack->removed);
    else if( pack->added )
        snprintf(delta, sizeof(delta), " (+%d)", pack->added);
    else if( pack->removed )
        snprintf(delta, sizeof(delta), " (-%d)", pack->removed);
    printf("  %-28s %6d %s, %d named%s\n", path_tail(path), lines,
           lines == 1 ? "line " : "lines", named, delta);
    return 1;
}

int
lc_pack_save(
    const struct LC_Pack* pack,
    const char* path)
{
    return pack_write(pack, path, 0);
}

int
lc_pack_save_sparse(
    const struct LC_Pack* pack,
    const char* path)
{
    return pack_write(pack, path, 1);
}

void
lc_pack_free(struct LC_Pack* pack)
{
    if( !pack )
        return;
    for( int id = 0; id < pack->capacity; id++ )
    {
        free(pack->names[id]);
        if( pack->trailing )
            free(pack->trailing[id]);
        if( pack->preceding )
            free(pack->preceding[id]);
    }
    free(pack->names);
    free(pack->trailing);
    free(pack->preceding);
    free(pack->preamble);
    free(pack->trailer);
    free(pack->tombstones);
    memset(pack, 0, sizeof(*pack));
}

int
lc_pack_find(
    const struct LC_Pack* pack,
    const char* name)
{
    assert(pack && name);
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( pack->names[id] && strcmp(pack->names[id], name) == 0 )
            return id;
    }
    return -1;
}

int
lc_pack_alloc(
    struct LC_Pack* pack,
    const char* name)
{
    return lc_pack_alloc_from(pack, name, 0);
}

int
lc_pack_alloc_from(
    struct LC_Pack* pack,
    const char* name,
    int base)
{
    assert(pack && name);
    int existing = lc_pack_find(pack, name);
    if( existing >= 0 )
        return existing;
    int id = pack->max > base ? pack->max : base;
    if( !lc_pack_set(pack, id, name) )
        return -1;
    pack->added++;
    return id;
}

int
lc_packs_load(
    struct LC_Packs* packs,
    const char* content_dir)
{
    assert(packs && content_dir);
    memset(packs, 0, sizeof(*packs));
    for( int i = 0; i < LC_PACK_COUNT; i++ )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", content_dir, pack_types[i]);
        if( !lc_pack_load(&packs->packs[i], path, pack_types[i], 0) )
            return 0;
    }
    return 1;
}

void
lc_packs_free(struct LC_Packs* packs)
{
    if( !packs )
        return;
    for( int i = 0; i < LC_PACK_COUNT; i++ )
        lc_pack_free(&packs->packs[i]);
    memset(packs, 0, sizeof(*packs));
}

int
lc_packs_write(
    const struct LC_Packs* packs,
    const char* content_dir)
{
    assert(packs && content_dir);
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/pack", content_dir);
    mkdir(dir, 0755);

    for( int i = 0; i < LC_PACK_COUNT; i++ )
    {
        const struct LC_Pack* pack = &packs->packs[i];
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.pack", dir, pack->type);
        if( !lc_pack_save(pack, path) )
            return 0;
    }
    return 1;
}
