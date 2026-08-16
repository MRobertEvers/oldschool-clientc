/*
 * The membership file format. See cp_membership.h.
 */

#include "cp_membership.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
sdup(const char* s)
{
    size_t n = strlen(s);
    char* d = malloc(n + 1);

    if( d )
        memcpy(d, s, n + 1);
    return d;
}

const char*
cp_membership_side_name(enum CP_MembershipSide side)
{
    return side == CP_MEMBERSHIP_SERVER ? "server" : "client";
}

void
cp_membership_path(
    char* out,
    size_t out_size,
    const char* srcdir,
    const char* ns,
    enum CP_MembershipSide side)
{
    snprintf(out, out_size, "%s/pack/%s.%s", srcdir, ns, cp_membership_side_name(side));
}

void
cp_membership_init(
    struct CP_Membership* set,
    const char* ns,
    enum CP_MembershipSide side)
{
    memset(set, 0, sizeof(*set));
    snprintf(set->ns, sizeof(set->ns), "%s", ns);
    set->side = side;
}

void
cp_membership_free(struct CP_Membership* set)
{
    for( int i = 0; i < set->count; i++ )
    {
        free(set->names[i]);
        free(set->trailing[i]);
        free(set->preceding[i]);
    }
    free(set->names);
    free(set->trailing);
    free(set->preceding);
    free(set->preamble);
    free(set->trailer);
    memset(set, 0, sizeof(*set));
}

/**
 * Where `name` belongs, and whether it is already there.
 *
 * Binary search over the sorted array. `*out_found` distinguishes "insert here"
 * from "it is here", which the two callers need separately.
 */
static int
find_slot(
    const struct CP_Membership* set,
    const char* name,
    int* out_found)
{
    int lo = 0;
    int hi = set->count;

    *out_found = 0;
    while( lo < hi )
    {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(set->names[mid], name);

        if( cmp == 0 )
        {
            *out_found = 1;
            return mid;
        }
        if( cmp < 0 )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int
cp_membership_has(
    const struct CP_Membership* set,
    const char* name)
{
    int found = 0;

    if( !set->count )
        return 0;
    find_slot(set, name, &found);
    return found;
}

/* The three arrays are one table with three columns; growing them apart would
 * let a comment end up on the wrong name after a partial failure. */
static int
grow(struct CP_Membership* set, int needed)
{
    int cap;
    char** grown;

    if( needed <= set->capacity )
        return 1;
    cap = set->capacity ? set->capacity : 64;
    while( cap < needed )
        cap *= 2;

    grown = realloc(set->names, (size_t)cap * sizeof(char*));
    if( !grown )
        return 0;
    memset(grown + set->capacity, 0, (size_t)(cap - set->capacity) * sizeof(char*));
    set->names = grown;

    grown = realloc(set->trailing, (size_t)cap * sizeof(char*));
    if( !grown )
        return 0;
    memset(grown + set->capacity, 0, (size_t)(cap - set->capacity) * sizeof(char*));
    set->trailing = grown;

    grown = realloc(set->preceding, (size_t)cap * sizeof(char*));
    if( !grown )
        return 0;
    memset(grown + set->capacity, 0, (size_t)(cap - set->capacity) * sizeof(char*));
    set->preceding = grown;

    set->capacity = cap;
    return 1;
}

/** Insert at `slot`, shifting the three columns together. */
static int
insert_at(
    struct CP_Membership* set,
    int slot,
    const char* name,
    char* trailing,
    char* preceding)
{
    char* copy;

    if( !grow(set, set->count + 1) )
        return -1;
    copy = sdup(name);
    if( !copy )
        return -1;
    if( slot < set->count )
    {
        memmove(set->names + slot + 1, set->names + slot,
                (size_t)(set->count - slot) * sizeof(char*));
        memmove(set->trailing + slot + 1, set->trailing + slot,
                (size_t)(set->count - slot) * sizeof(char*));
        memmove(set->preceding + slot + 1, set->preceding + slot,
                (size_t)(set->count - slot) * sizeof(char*));
    }
    set->names[slot] = copy;
    set->trailing[slot] = trailing;
    set->preceding[slot] = preceding;
    set->count++;
    return 1;
}

int
cp_membership_add(
    struct CP_Membership* set,
    const char* name)
{
    int found = 0;
    int slot;

    if( !name || !*name )
        return -1;
    slot = find_slot(set, name, &found);
    if( found )
        return 0;
    return insert_at(set, slot, name, NULL, NULL);
}

/* ---- growable text, for the comment blocks -------------------------------- */

struct CP_Text
{
    char* data;
    size_t len;
    size_t cap;
};

static int
text_append(
    struct CP_Text* text,
    const char* bytes,
    size_t len)
{
    if( text->len + len + 1 > text->cap )
    {
        size_t cap = text->cap ? text->cap : 4096;
        char* grown;

        while( cap < text->len + len + 1 )
            cap *= 2;
        grown = realloc(text->data, cap);
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

/* ---- load ---------------------------------------------------------------- */

/** A comment or a blank line — content to carry, not a name. */
static int
line_is_prose(const char* line)
{
    const char* scan = line;

    while( *scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n' )
        scan++;
    if( !*scan )
        return 1;
    return scan[0] == '/' && scan[1] == '/';
}

/**
 * A name, or 0.
 *
 * Deliberately narrow: the characters a config `[block]` name can hold, which is
 * what these lines are. Anything else is `malformed` rather than a name, so a
 * line that drifted in from another format is reported instead of being bound to
 * an entity that does not exist.
 */
static int
name_is_plausible(const char* name)
{
    if( !*name )
        return 0;
    for( const char* c = name; *c; c++ )
    {
        if( *c >= 'a' && *c <= 'z' )
            continue;
        if( *c >= 'A' && *c <= 'Z' )
            continue;
        if( *c >= '0' && *c <= '9' )
            continue;
        if( *c == '_' || *c == '.' || *c == '+' || *c == '-' || *c == ':' )
            continue;
        return 0;
    }
    return 1;
}

int
cp_membership_load(
    struct CP_Membership* set,
    const char* path,
    const char* ns,
    enum CP_MembershipSide side,
    int allow_missing)
{
    FILE* file;
    struct CP_Text pending = { 0 };
    int have_name = 0;
    int ok = 1;
    char line[2048];

    cp_membership_init(set, ns, side);

    file = fopen(path, "rb");
    if( !file )
    {
        if( allow_missing )
            return 1;
        fprintf(stderr, "cachepack: cannot open %s\n", path);
        return 0;
    }
    set->present = 1;

    while( ok && fgets(line, sizeof(line), file) )
    {
        char* note = NULL;
        char* raw_note = NULL;
        char* name = line;
        size_t n;
        int found = 0;
        int slot;

        if( line_is_prose(line) )
        {
            ok = text_append(&pending, line, strlen(line));
            continue;
        }

        /* Split the trailing `//` off before the name is measured, carrying the
         * whitespace before it so a save reproduces the padding rather than
         * re-deciding how much a note gets. */
        note = strstr(line, "//");
        if( note )
        {
            char* pad = note;
            size_t note_len = strlen(note);

            while( pad > line && (pad[-1] == ' ' || pad[-1] == '\t') )
                pad--;
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

        while( *name == ' ' || *name == '\t' )
            name++;
        n = strlen(name);
        while( n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r' || name[n - 1] == ' ' ||
                         name[n - 1] == '\t') )
            name[--n] = '\0';

        if( !name_is_plausible(name) )
        {
            free(raw_note);
            set->malformed++;
            continue;
        }

        slot = find_slot(set, name, &found);
        if( found )
        {
            /* Stating a name twice says nothing a reader can act on, and
             * silently collapsing it would hide an editing mistake in a file
             * whose entire content is a set. */
            free(raw_note);
            set->duplicates++;
            continue;
        }
        if( insert_at(set, slot, name, raw_note, NULL) < 0 )
        {
            free(raw_note);
            ok = 0;
            break;
        }

        if( pending.len > 0 )
        {
            if( !have_name )
                set->preamble = pending.data; /* the file header */
            else
                set->preceding[slot] = pending.data;
            memset(&pending, 0, sizeof(pending));
        }
        have_name = 1;
    }

    /* Whatever is left anchors to no name at all. */
    if( ok && pending.len > 0 )
    {
        if( have_name )
            set->trailer = pending.data;
        else
            set->preamble = pending.data;
        memset(&pending, 0, sizeof(pending));
    }
    free(pending.data);
    fclose(file);

    if( !ok )
    {
        cp_membership_free(set);
        return 0;
    }
    if( set->malformed )
        fprintf(stderr, "cachepack: %s has %d line(s) that are neither a comment nor a name\n",
                path, set->malformed);
    if( set->duplicates )
        fprintf(stderr, "cachepack: %s states %d name(s) twice\n", path, set->duplicates);
    return 1;
}

/* ---- save ---------------------------------------------------------------- */

int
cp_membership_save(
    const struct CP_Membership* set,
    const char* path)
{
    FILE* file = fopen(path, "wb");

    if( !file )
    {
        fprintf(stderr, "cachepack: cannot write %s\n", path);
        return 0;
    }
    if( set->preamble )
        fputs(set->preamble, file);
    for( int i = 0; i < set->count; i++ )
    {
        if( set->preceding[i] )
            fputs(set->preceding[i], file);
        fputs(set->names[i], file);
        if( set->trailing[i] )
            fputs(set->trailing[i], file);
        fputc('\n', file);
    }
    if( set->trailer )
        fputs(set->trailer, file);
    if( fclose(file) != 0 )
    {
        fprintf(stderr, "cachepack: failed to close %s\n", path);
        return 0;
    }
    return 1;
}
