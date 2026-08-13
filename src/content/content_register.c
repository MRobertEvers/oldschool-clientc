/*
 * The namespace register. See content_register.h.
 */

#include "content_register.h"

#include "3rd/ini/ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The defaults.
 *
 * These are the union of what the three old tables covered, which is why some
 * namespaces here have no consumer yet (`varn`, `vars`): the compiler already
 * knew them and the runtime did not, and a register that dropped them would be a
 * regression dressed as a cleanup.
 *
 * `ids`, `names` and `gameval` are stated for every row even where nothing reads
 * them yet. They are the facts that decide whether cachepack may rewrite a file,
 * and writing them down is most of the point.
 *
 * ## The gameval column
 *
 * Archive ids in the cache's own symbol table (OSRS idx 24), correlated against
 * the config groups by record count and content and then verified per cache at
 * load — see cachepack's `cp_names_seed_from_cache`, which refuses an archive
 * whose ids are not the group's. The mapping is recorded nowhere in the cache, so
 * every number here is a checked claim rather than a documented fact:
 *
 *   0 obj   1 npc   2 inv   3 varp   4 varbit   6 loc   7 seq   8 spotanim
 *   9 dbrow   10 dbtable   12 sprite   14 interface + component   15 varc
 *
 * Archive 11 names songs *and* jingles in one id space, so neither table alone
 * can verify it and nothing claims it. 5 and 13 are absent.
 *
 * Archive 14 is the odd one: each record holds an interface's name *and* every
 * one of its components as `u16 child` + name pairs, so one archive names two
 * namespaces. Read flat it looks like one 63-character name per interface, which
 * is why it went unclaimed for so long and why `component` names were being taken
 * from another server's table — that table put `bankmain_items` on child 13, and
 * this cache says child 13 is `scrollbar`.
 *
 * A -1 means every name in that namespace is either `<type>_<id>` filler or
 * something a human wrote. Those namespaces must not claim `names = cache`;
 * `ContentRegister_Validate` is what stops them.
 */
static const struct ContentNamespace k_defaults[] = {
    /* name                    names                     shared  gameval    base   idx */
    /* ---- the archive index of every cache table ----------------------- */
    /*
     * One row per cache index, naming its archives. `2_configs` is the config
     * table's, and its "archives" are the config groups — npc is archive 9,
     * obj 10, loc 6. Every other table had one of these and index 2 did not,
     * which made the config groups the only archives in the cache that nothing
     * named.
     */
    { "2_configs",             CONTENT_NAMES_AUTHORED, 0,  -1,      0,   2 },

    /* ---- config types the cache names -------------------------------- */
    { "npc",                   CONTENT_NAMES_CACHE,    0,   1,  20000,  -1 },
    { "obj",                   CONTENT_NAMES_CACHE,    0,   0,  40000,  -1 },
    { "loc",                   CONTENT_NAMES_CACHE,    0,   6,  70000,  -1 },
    { "seq",                   CONTENT_NAMES_CACHE,    0,   7,  20000,  -1 },
    { "spotanim",              CONTENT_NAMES_CACHE,    0,   8,   6000,  -1 },
    { "inv",                   CONTENT_NAMES_CACHE,    0,   2,   2000,  -1 },
    /*
     * Both from archive 14, which carries an interface and its children in one
     * record. `component` ids are `(interface << 16) | child`, and that is how they
     * are *addressed* rather than stored: there is no `pack/component.pack`. The
     * names live in `interfaces/<name>.compack`, the member index over exactly
     * those children, and the id composes from the interface's id and the child's.
     * A file keyed on the composed id was a second index over the same members.
     */
    { "3_interfaces",          CONTENT_NAMES_CACHE,    0,  14,   2000,   3 },
    { "component",             CONTENT_NAMES_CACHE,    0,  14,      0,  -1 },
    /*
     * The client database, and the one place a namespace is `ids = server` while
     * still being `names = cache`: archives 9 and 10 name the cache's own 16,711
     * rows and 246 tables, and every one of those keeps its id — but the server's
     * RuneScript defines tables of its own above the high-water mark, and those
     * are the allocator's.
     *
     * This row once carried an `ids = cache` marking, with the note "neither has an encoder, so
     * authored content cannot create one either way". Both halves are stale. The
     * encoders exist (`RSCache_Dat2ConfigDbTableEncode`, held to byte-identity
     * against every record in the cache; `CP_TYPE_NO_ENCODER` is not set on either
     * type), and a cache encoder was never what a server table needed anyway —
     * `mock230_db.c` parses the server's own `.dbtable`/`.dbrow` text and never goes
     * near the cache, which is how `coord_pair_table` (259) and `combat_style_table`
     * (260) came to exist. `ss_allocate.py` had both in DEFAULT_SERVER_NAMESPACES the
     * whole time, so the allocation worked and the two authorities disagreed in
     * silence — docs/CONTENT_ARCHITECTURE.md §8.2(c), the third occurrence.
     *
     * The bases are fixed and deliberately far above the cache: future caches
     * are patched in manually, and Jagex adds tables every revision (246 at
     * rev 230, 259 by 239), so a base at the current high-water mark is a
     * collision waiting for the next patch. dbtable 2048 absorbs ~1,790 tables
     * of growth; dbrow 65536 likewise, and a nonzero base finally brings dbrow
     * under `validate_id_bases`. Width caps, verified before choosing: the
     * server VM unpacks a column ref as `(packed >> 12) & 0xffff`
     * (mock230_ops_db.c), so a table id caps at 65,535; the client host allows
     * 20 bits; the compiler packs `(table << 12) | (column << 4)` into int32 —
     * 2048 is far inside every one. A dbrow id is only ever a plain int value.
     */
    { "dbrow",                 CONTENT_NAMES_CACHE,    0,   9,  65536,  -1 },
    { "dbtable",               CONTENT_NAMES_CACHE,    0,  10,   2048,  -1 },
    /* ---- config types the cache does not name ------------------------ */
    /* Every name here is filler or authored. Declaring any of them `cache` is
     * what licensed cachepack to rewrite the file and drop its comments. */
    { "param",                 CONTENT_NAMES_AUTHORED, 0,  -1,   2634,  -1 },
    { "hitsplat",              CONTENT_NAMES_AUTHORED, 0,  -1,    200,  -1 },
    { "healthbar",             CONTENT_NAMES_AUTHORED, 0,  -1,    200,  -1 },
    { "mapelement",            CONTENT_NAMES_AUTHORED, 0,  -1,   2000,  -1 },
    { "underlay",              CONTENT_NAMES_AUTHORED, 0,  -1,    400,  -1 },
    { "overlay",               CONTENT_NAMES_AUTHORED, 0,  -1,    800,  -1 },
    { "idk",                   CONTENT_NAMES_AUTHORED, 0,  -1,    400,  -1 },
    /*
     * Three record types state a category and nothing names them: obj at config
     * opcode 94, npc at 18, loc at 61.
     *
     * The base was 0 — "do not allocate" — until 2026-08-02, and this is the one
     * namespace where both authorities coexist: an id below 8192 is the cache's
     * and the crawl READS it, an id at or above is one this tree allocated
     * because no cache record states the concept. Doors forced it: the reference
     * binds `[oploc1,_door_closed]` and **none of this cache's 776 door records
     * carries a category at all**, so there was nothing to read.
     *
     * 8192 is above every category id the three types state here — npc 2504,
     * obj 2506, loc 2474, measured. `content.ini`'s own block carries the
     * argument; this is the number, in the same table as every other base.
     */
    { "category",              CONTENT_NAMES_AUTHORED, 0,  -1,   8192,  -1 },
    /* ---- ours to number --------------------------------------------- */
    { "enum",                  CONTENT_NAMES_AUTHORED, 0,  -1,   5995,  -1 },
    { "struct",                CONTENT_NAMES_AUTHORED, 0,  -1,   8000,  -1 },
    /* One namespace, two destinations: server RS2 compiles to the engine's own
     * bytecode pack and never enters the cache; client CS2 goes into the script
     * asset table and does. See docs/CONTENT_PACK_PLAN.md §6.2. */
    { "12_clientscripts",      CONTENT_NAMES_AUTHORED, 0,  -1,  12000,  12 },
    /* The wire fixes this one — UPDATE_STAT carries the index. */
    { "stat",                  CONTENT_NAMES_AUTHORED, 0,  -1,      0,  -1 },
    /* ---- the four that answer to `%name` ---------------------------- */
    /*
     * 5705, and this read 8000 — the third of the "already allocated" exceptions
     * the header describes beside `param`'s 2634, and the one where the round
     * number was not merely cosmetic but out of bounds.
     *
     * Two independent facts fix it at `MOCK230_VARP_CACHE_MAX`:
     *
     *   - nineteen server varps already sit at 5705..5723 (`%com_*`, the combat
     *     stat block `[proc,player_combat_stat]` computes, plus `%damagestyle`,
     *     `%prayer_drain_*`, `%newplayer_seeded`). They were allocated off the
     *     high-water mark, which is what docs/CONTENT_ARCHITECTURE.md §8.5 records
     *     as the correct result. A floor of 8000 says those nineteen are not ours.
     *   - `MOCK230_VARP_COUNT` is `MOCK230_VARP_CACHE_MAX + 512` = 6217, so a varp
     *     allocated *at* 8000 is past the end of `Mock230Player.varps` and
     *     `mock230_world_set_varp`'s bounds check drops the write and returns —
     *     §8.3's named failure mode, silently, on the first server varp allocated
     *     after the floor started being honoured.
     *
     * It was never honoured before: `tools/ss_allocate.py`'s `declared_base()` read
     * only `content.ini`, which states no `base =` key for anything, so every floor
     * in this table was advisory and the high-water mark did all the work. Making
     * the allocator read this table is what turned an inert number into a live one,
     * and this row is what that surfaced.
     *
     * 5725, not 5705, and the twenty ids between are the reason. A varp namespace
     * is not bounded by the varp group: the cache's *varbits* name varp ids too,
     * and this cache carries varbits based as high as 5724 while its varp group
     * stops at 5704. Records the group has no file for are still spoken for.
     *
     * So `%com_attackanim`..`%mock_mapzone_log` were handed 5705..5724 — every one
     * of them on top of somebody's packed bits, in a region `configs/all.varp`
     * cannot show because it is an export of the varp group alone. Both directions
     * corrupt: `~player_combat_stat` writing a whole 32-bit stat destroys fifteen
     * varbits under `%com_slashattack`, and their owner writes the stat back. It
     * surfaced as the whole-varp complaints out of `mock230_world.c`, which read
     * as content writing the wrong thing when content was writing the wrong *id*.
     * The twenty moved to 6280..6299; `validate_id_bases` now takes the varbit
     * basevars into account so a cache that reaches further says so at boot.
     */
    { "varp",                  CONTENT_NAMES_CACHE,    1,   3,   5725,  -1 },
    { "varbit",                CONTENT_NAMES_CACHE,    1,   4,  25000,  -1 },
    { "varc",                  CONTENT_NAMES_CACHE,    0,  15,   2000,  -1 },
    /* ---- asset tables ------------------------------------------------ */
    /*
     * Idx-addressed rather than config records, but the same three axes apply:
     * an asset only exists once a pack line maps its name to a group id.
     *
     * Only `sprite` is named by the cache. For models, sounds and maps the pack
     * file *is* the name table rather than an overlay on one — which is why the
     * authored side is not optional, and why lines in `pack/model.pack` versus
     * models in the cache is a literal "how much of this have I named" metric.
     */
    { "8_sprites",             CONTENT_NAMES_CACHE,    0,  12,  20000,   8 },
    { "7_models",              CONTENT_NAMES_AUTHORED, 0,  -1, 100000,   7 },
    { "4_soundeffects",        CONTENT_NAMES_AUTHORED, 0,  -1,  20000,   4 },
    /* Archive 11 names songs and jingles together, so neither table can verify
     * it alone; unclaimed rather than half-trusted. */
    { "6_musictracks",         CONTENT_NAMES_AUTHORED, 0,  -1,   2000,   6 },
    { "11_musicjingles",       CONTENT_NAMES_AUTHORED, 0,  -1,   1000,  11 },
    { "14_musicsamples",       CONTENT_NAMES_AUTHORED, 0,  -1,   2000,  14 },
    { "15_musicpatches",       CONTENT_NAMES_AUTHORED, 0,  -1,   1000,  15 },
    { "9_textures",            CONTENT_NAMES_AUTHORED, 0,  -1,   1000,   9 },
    { "0_animations",          CONTENT_NAMES_AUTHORED, 0,  -1,  20000,   0 },
    { "1_skeletons",           CONTENT_NAMES_AUTHORED, 0,  -1,   8000,   1 },
    { "22_animayas",           CONTENT_NAMES_AUTHORED, 0,  -1,   2000,  22 },
    { "5_maps",                CONTENT_NAMES_AUTHORED, 0,  -1,      0,   5 },
    { "13_fonts",              CONTENT_NAMES_AUTHORED, 0,  -1,    100,  13 },
    { "10_binary",             CONTENT_NAMES_AUTHORED, 0,  -1,    100,  10 },
    { "21_dbtableindex",       CONTENT_NAMES_AUTHORED, 0,  -1,    400,  21 },
    { "18_worldmapgeography",  CONTENT_NAMES_AUTHORED, 0,  -1,      0,  18 },
    { "19_worldmap",           CONTENT_NAMES_AUTHORED, 0,  -1,      0,  19 },
    { "20_worldmapground",     CONTENT_NAMES_AUTHORED, 0,  -1,      0,  20 },
};

/* Silently truncating the defaults would drop namespaces off the end of the
 * table and leave the tools disagreeing about which exist — the exact failure
 * the register was built to remove. */
_Static_assert(sizeof(k_defaults) / sizeof(k_defaults[0]) <= CONTENT_REGISTER_MAX,
               "more default namespaces than CONTENT_REGISTER_MAX holds");

int
ContentRegister_Defaults(struct ContentRegister* reg)
{
    memset(reg, 0, sizeof(*reg));
    reg->count = (int)(sizeof(k_defaults) / sizeof(k_defaults[0]));
    if( reg->count > CONTENT_REGISTER_MAX )
        reg->count = CONTENT_REGISTER_MAX;
    memcpy(reg->entries, k_defaults, (size_t)reg->count * sizeof(reg->entries[0]));
    return reg->count;
}

int
ContentRegister_Validate(const struct ContentRegister* reg)
{
    int violations = 0;

    for( int i = 0; i < reg->count; i++ )
    {
        const struct ContentNamespace* ns = &reg->entries[i];
        int claims_cache = ns->names == CONTENT_NAMES_CACHE;
        int has_archive = ns->gameval_archive >= 0;


        /*
         * The index is in the name, so the two can disagree. Checking it is what
         * makes the name trustworthy: a reader who sees `7_models.pack` should not
         * have to verify that models really are index 7, and a member-level pack
         * must not look archive-level by carrying a number.
         */
        {
            char prefix[16];
            int len = snprintf(prefix, sizeof(prefix), "%d_", ns->cache_index);
            int numeric = ns->name[0] >= '0' && ns->name[0] <= '9';

            if( ns->cache_index >= 0 && strncmp(ns->name, prefix, (size_t)len) != 0 )
            {
                fprintf(stderr,
                        "content.ini: namespace `%s` lists the archives of cache index %d, so "
                        "its pack file must be `pack/%d_<name>.pack` — the index belongs in "
                        "the name where a reader can see it\n",
                        ns->name, ns->cache_index, ns->cache_index);
                violations++;
            }
            else if( ns->cache_index < 0 && numeric )
            {
                fprintf(stderr,
                        "content.ini: namespace `%s` is spelled with a leading index but "
                        "declares none — a numeric prefix means the pack lists the archives "
                        "of a cache index, and this one lists members\n",
                        ns->name);
                violations++;
            }
        }

        if( claims_cache == has_archive )
            continue;

        /* Name both facts and both consequences: the reader has to know which
         * side is wrong, and that is not derivable from the mismatch alone. */
        if( claims_cache )
            fprintf(stderr,
                    "content.ini: namespace `%s` declares `names = cache` but no gameval "
                    "archive names it — every name in pack/%s.pack is filler or authored, "
                    "and declaring it machine-owned lets cachepack rewrite the file\n",
                    ns->name, ns->name);
        else
            fprintf(stderr,
                    "content.ini: namespace `%s` is named by gameval archive %d but does not "
                    "declare `names = cache` — the cache's own names will never be imported "
                    "and every id stays spelled `%s_<id>`\n",
                    ns->name, ns->gameval_archive, ns->name);
        violations++;
    }
    return violations;
}

const struct ContentNamespace*
ContentRegister_Find(
    const struct ContentRegister* reg,
    const char* name)
{
    if( !name )
        return NULL;
    for( int i = 0; i < reg->count; i++ )
    {
        if( strcmp(reg->entries[i].name, name) == 0 )
            return &reg->entries[i];
    }
    return NULL;
}

const struct ContentNamespace*
ContentRegister_ForPackFile(
    const struct ContentRegister* reg,
    const char* filename)
{
    char stem[64];
    const char* dot;
    size_t length;

    if( !filename )
        return NULL;
    dot = strrchr(filename, '.');
    if( !dot )
        return NULL;
    /*
     * Two extensions, because there are two levels of index. `pack/7_models.pack`
     * lists the archives of a cache index; `configs/all.seq.compack` lists the
     * records inside one archive, and is a member index like every other `.compack`.
     */
    if( strcmp(dot, ".compack") == 0 )
    {
        /* `all.seq.compack` -> `seq`: the type is between the two dots. */
        const char* prefix = strstr(filename, "all.");

        if( prefix != filename )
            return NULL;
        filename += 4;
        dot = strrchr(filename, '.');
        if( !dot )
            return NULL;
    }
    /* `dbrow.alloc` -> `dbrow`: the server's allocation ledger — `id=name` like
     * the member index it layers beside, holding only the ids ss_allocate.py
     * handed out past the cache's high-water mark. */
    else if( strcmp(dot, ".pack") != 0 && strcmp(dot, ".alloc") != 0 )
        return NULL;
    length = (size_t)(dot - filename);
    if( length == 0 || length >= sizeof(stem) )
        return NULL;
    memcpy(stem, filename, length);
    stem[length] = '\0';
    return ContentRegister_Find(reg, stem);
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Trim, in place, and drop an inline comment.
 *
 * 3rd/ini does not do this: it skips whitespace *before* an element, then takes
 * the key as everything up to `=` and the value as everything to end of line.
 * So `ids       = cache` arrives as key `"ids       "` and value `" cache"`, and
 * a naive strcmp silently matches nothing — which is how the first version of
 * this file parsed a valid content.ini into no overrides at all while reporting
 * success. Every consumer of that parser has to do this or write its ini files
 * without spaces.
 *
 * Inline comments are dropped too, so `names = cache ; the gameval table` works.
 * Safe because every value in this grammar is a single bare word.
 */
void
ContentIni_Trim(char* text)
{
    char* start = text;
    size_t length;

    for( char* scan = text; *scan; scan++ )
    {
        if( *scan == ';' || *scan == '#' )
        {
            *scan = '\0';
            break;
        }
    }

    while( *start == ' ' || *start == '\t' )
        start++;
    if( start != text )
        memmove(text, start, strlen(start) + 1);

    length = strlen(text);
    while( length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\r' || text[length - 1] == '\n') )
        text[--length] = '\0';
}


static enum ContentNameAuthority
parse_names(
    const char* value,
    enum ContentNameAuthority fallback)
{
    if( strcmp(value, "cache") == 0 )
        return CONTENT_NAMES_CACHE;
    if( strcmp(value, "authored") == 0 )
        return CONTENT_NAMES_AUTHORED;
    if( strcmp(value, "derived") == 0 )
        return CONTENT_NAMES_DERIVED;
    if( strncmp(value, "imported", 8) == 0 )
        return CONTENT_NAMES_IMPORTED;
    return fallback;
}

int
ContentRegister_Load(
    struct ContentRegister* reg,
    const char* dir)
{
    char path[1024];
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    struct ContentNamespace* current = NULL;

    /* Defaults first, then the file *overlays* them. A tree that declares only
     * the one namespace it invented still gets the other twenty — which is what
     * makes adopting the register a one-line change rather than a transcription
     * of everything that already worked. */
    ContentRegister_Defaults(reg);

    snprintf(path, sizeof(path), "%s/content.ini", dir);
    file = fopen(path, "rb");
    if( !file )
        return reg->count;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return reg->count;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data )
    {
        fclose(file);
        return reg->count;
    }
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return reg->count;
    }
    fclose(file);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            /* `[namespace:varp]`. Anything else in the file is not ours. */
            const char* name = element._section.name;

            ContentIni_Trim(element._section.name);
            current = NULL;
            if( strncmp(name, "namespace:", 10) != 0 )
                continue;
            name += 10;

            for( int i = 0; i < reg->count; i++ )
            {
                if( strcmp(reg->entries[i].name, name) == 0 )
                {
                    current = &reg->entries[i];
                    break;
                }
            }
            if( !current && reg->count < CONTENT_REGISTER_MAX )
            {
                current = &reg->entries[reg->count++];
                memset(current, 0, sizeof(*current));
                current->cache_index = -1; /* member-level unless the file says otherwise */
                if( strlen(name) >= sizeof(current->name) )
                    fprintf(stderr,
                            "content.ini: namespace \"%s\" is %zu bytes, truncating to "
                            "%zu\n",
                            name, strlen(name), sizeof(current->name) - 1);
                /* Precision caps the copy at sizeof(name)-1 so this is provably
                 * in-bounds regardless of the section header's declared size. */
                snprintf(current->name, sizeof(current->name), "%.*s",
                         (int)sizeof(current->name) - 1, name);
                /* Zero would mean "gameval archive 0", which is obj. A namespace
                 * the defaults never had is unnamed by the cache until it says
                 * otherwise. */
                current->gameval_archive = -1;
            }
            continue;
        }

        if( element.kind != INI_ELEMENT_KEYVAL || !current )
            continue;

        ContentIni_Trim(element._keyval.name);
        ContentIni_Trim(element._keyval.value);

        /* `ids` was here. It is accepted and ignored: the key still appears in
         * content.ini and an unknown key is silently skipped by the loop below,
         * so those lines are inert rather than an error until they are deleted.
         * See docs/PACK_ENTITY_SPLIT_PLAN.md §5 for why the axis went. */
        if( strcmp(element._keyval.name, "names") == 0 )
            current->names = parse_names(element._keyval.value, current->names);
        else if( strcmp(element._keyval.name, "vardomain") == 0 )
            current->shared_var_domain = atoi(element._keyval.value) != 0 ||
                                         strcmp(element._keyval.value, "yes") == 0;
        /* A tree may state the archive for a namespace the defaults do not carry.
         * `none` is how it says there is no archive without relying on -1 being
         * spellable. */
        else if( strcmp(element._keyval.name, "gameval") == 0 )
            current->gameval_archive =
                strcmp(element._keyval.value, "none") == 0 ? -1 : atoi(element._keyval.value);
        /* `base = none` is the same as 0 and reads better where the point is that
         * the namespace cannot be allocated into. */
        else if( strcmp(element._keyval.name, "base") == 0 )
            current->server_base =
                strcmp(element._keyval.value, "none") == 0 ? 0 : atoi(element._keyval.value);
        /* `index = none` for a member-level pack, which is the default. */
        else if( strcmp(element._keyval.name, "index") == 0 )
            current->cache_index =
                strcmp(element._keyval.value, "none") == 0 ? -1 : atoi(element._keyval.value);
    }

    free(data);
    reg->from_file = 1;
    return reg->count;
}
