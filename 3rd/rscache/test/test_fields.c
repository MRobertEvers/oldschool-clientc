/*
 * The field register as the *writer* reads it, held to the file it came from.
 *
 * `src/torirsserver/test/torirs_server_servercodec_test.c` does this at the reading end: it
 * parses `fields/npc.ini` with a small parser of its own and holds
 * `torirs_server_servercodec.c`'s C table to it, name by name. This is the same check at
 * the writing end, and the pair is the only thing standing between the two.
 *
 * The failure it exists for has no error attached. If the writer thinks
 * `hitpoints` is opcode 77 and the reader thinks it is 78, both streams are
 * well-formed opcode streams — the packer writes a byte and the server reads it
 * into a different field, or into none, and every layer in between reports
 * success. Nothing downstream can detect it, so it has to be caught here.
 *
 * Four things are checked:
 *
 *   the parse       cp_fields' table against an independent parse of the same
 *                   file. `fields/npc.ini` declares a field in *two* blocks — the
 *                   projection at the top, the server opcode at the bottom, under
 *                   the same `[npc.hitpoints]` name — so a reader that appends
 *                   sections instead of merging them silently loses half of them.
 *   the register    `cp_fields_check`: the reserved band, no two fields on one
 *                   opcode, no wire the reader cannot decode.
 *   the band        a written band decoded by an independent implementation of the
 *                   reader's algorithm. Our own encoder agreeing with our own
 *                   decoder proves nothing, which is why this test does not link
 *                   the writer's decoder — it has none.
 *   the header      version, CRC, and what a corrupted byte does.
 */

#include "cp_fields.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(int ok, const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("fields: %-62s %s\n", what, ok ? "ok" : "FAILED");
}

/* ---- an independent parse ------------------------------------------------ */

struct IniField
{
    char name[64];
    int opcode;
    int wire;
};

/**
 * Parse `[<type>.<name>]` blocks carrying `server = opcode:<n>:<wire>`.
 *
 * Deliberately not cp_fields' parser and deliberately not 3rd/ini: a check that
 * shares its reader with the thing it checks tests nothing about the reader.
 * Copied in shape from the reader-side test so the two stay recognisable as a
 * pair.
 */
static int
load_ini(
    const char* path,
    const char* type,
    struct IniField* out,
    int max)
{
    FILE* file = fopen(path, "r");
    char line[512];
    char current[64];
    char prefix[64];
    int count = 0;

    if( !file )
        return -1;
    snprintf(prefix, sizeof(prefix), "%s.", type);
    current[0] = '\0';
    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        if( *cursor == '[' )
        {
            char* end = strchr(cursor, ']');

            if( end )
            {
                size_t len = (size_t)(end - cursor - 1);

                if( len >= sizeof(current) )
                    len = sizeof(current) - 1;
                memcpy(current, cursor + 1, len);
                current[len] = '\0';
            }
            continue;
        }
        /* Only real declarations; the file documents its own grammar in comments
         * whose text would otherwise be parsed as a row. */
        if( *cursor == ';' || *cursor == '#' )
            continue;
        if( strncmp(cursor, "server", 6) != 0 )
            continue;
        {
            char* spec = strstr(cursor, "opcode:");
            int opcode = 0;
            char wire[8] = { 0 };

            if( !spec || count >= max )
                continue;
            if( sscanf(spec, "opcode:%d:%7[^ \t\r\n]", &opcode, wire) != 2 )
                continue;
            if( strncmp(current, prefix, strlen(prefix)) != 0 )
                continue;
            snprintf(out[count].name, sizeof(out[count].name), "%s", current + strlen(prefix));
            out[count].opcode = opcode;
            out[count].wire = wire[1] ? atoi(wire + 1) : 0;
            count++;
        }
    }
    fclose(file);
    return count;
}

static void
check_against_register(
    const char* dir,
    const char* type)
{
    char path[1024];
    struct IniField ini[CP_FIELDS_MAX];
    struct CP_Fields fields;
    int ini_count;
    int matched = 0;
    int mismatched = 0;
    char what[128];

    snprintf(path, sizeof(path), "%s/fields/%s.ini", dir, type);
    ini_count = load_ini(path, type, ini, CP_FIELDS_MAX);
    if( ini_count < 0 )
    {
        /* Loud, and not a pass — the discipline the cache suites already follow. */
        printf("fields: SKIPPED %s — no %s\n", type, path);
        return;
    }

    cp_fields_load(&fields, dir, type);
    printf("fields: %s — the file declares %d server field(s), cp_fields loaded %d\n", type,
           ini_count, fields.band_count);

    for( int i = 0; i < ini_count; i++ )
    {
        const struct CP_Field* got = cp_fields_find(&fields, ini[i].name);

        if( !got )
        {
            printf("fields:   %s.%s — declared in the file, absent from cp_fields\n", type,
                   ini[i].name);
            mismatched++;
            continue;
        }
        if( got->opcode != ini[i].opcode || (int)got->wire != ini[i].wire )
        {
            printf("fields:   %s.%s — file says opcode %d:u%d, cp_fields says %d:u%d\n", type,
                   ini[i].name, ini[i].opcode, ini[i].wire, got->opcode, (int)got->wire);
            mismatched++;
            continue;
        }
        matched++;
    }

    snprintf(what, sizeof(what), "%s: the register declares server opcodes", type);
    check(ini_count > 0, what);
    snprintf(what, sizeof(what), "%s: every declared field matches cp_fields exactly", type);
    check(matched == ini_count && mismatched == 0, what);
    /* `band_count`, not `count`: the register also declares fields with no server
     * opcode — `[npc.name]`, `[npc.magic]` — and those are load-bearing for the
     * client-side filter rather than rows this check is about. */
    snprintf(what, sizeof(what), "%s: cp_fields states no band field the register does not", type);
    check(fields.band_count == ini_count, what);

    /*
     * The reserved band, restated here rather than only inside cp_fields_check.
     * Client npc opcodes run 1..147, so 64..255 is what keeps a server record from
     * being mistaken for a client one and lets a client decoder fed this stream
     * stop cleanly instead of misreading it.
     */
    {
        int out_of_band = 0;

        for( int i = 0; i < fields.band_count; i++ )
        {
            if( fields.entries[i].opcode < 64 || fields.entries[i].opcode > 255 )
                out_of_band++;
        }
        snprintf(what, sizeof(what), "%s: every opcode is inside the reserved 64..255 band", type);
        check(out_of_band == 0, what);
    }

    snprintf(what, sizeof(what), "%s: cp_fields_check passes on the tree's own register", type);
    check(cp_fields_check(&fields) == 0, what);

    /* Ascending by opcode, which is what makes two packs of the same content
     * byte-identical. Not a decode constraint — the reader dispatches per opcode —
     * but a reproducibility one, and worth asserting because nothing else would
     * notice it drifting. */
    {
        int ascending = 1;

        for( int i = 1; i < fields.band_count; i++ )
        {
            if( fields.entries[i].opcode <= fields.entries[i - 1].opcode )
                ascending = 0;
        }
        snprintf(what, sizeof(what), "%s: the loaded table is ascending by opcode", type);
        check(ascending, what);
    }
}

/* ---- the band, decoded by an independent reader -------------------------- */

struct Decoded
{
    int opcode;
    int value;
};

/**
 * `torirs_server_servercodec.c`'s decode loop, rewritten from its description.
 *
 * Not linked from there: cachepack deliberately links nothing from `src/`, and a
 * test that imported the reader would also import whatever the reader gets wrong.
 * What is reproduced is the *algorithm* — opcode byte, payload of the declared
 * width, zero terminates, an unknown opcode stops the stream because its width is
 * unknowable.
 */
static int
decode_band(
    const struct CP_Fields* fields,
    const uint8_t* band,
    int size,
    struct Decoded* out,
    int max)
{
    int at = 0;
    int count = 0;

    while( at < size )
    {
        int opcode = band[at++];
        const struct CP_Field* field = NULL;
        int value = 0;

        if( opcode == 0 )
            break;
        for( int i = 0; i < fields->count; i++ )
        {
            if( fields->entries[i].opcode == opcode )
            {
                field = &fields->entries[i];
                break;
            }
        }
        if( !field || at + (int)field->wire > size || count >= max )
            return -1;
        switch( field->wire )
        {
        case CP_FIELD_WIRE_U1:
            value = band[at];
            break;
        case CP_FIELD_WIRE_U2:
            value = (band[at] << 8) | band[at + 1];
            break;
        case CP_FIELD_WIRE_U4:
            value = (int)(((uint32_t)band[at] << 24) | ((uint32_t)band[at + 1] << 16) |
                          ((uint32_t)band[at + 2] << 8) | (uint32_t)band[at + 3]);
            break;
        default:
            return -1;
        }
        at += (int)field->wire;
        out[count].opcode = opcode;
        out[count].value = value;
        count++;
    }
    return count;
}

/** A register built in memory, so the band checks run with no tree on disk. */
static void
synthetic_register(struct CP_Fields* fields)
{
    memset(fields, 0, sizeof(*fields));
    snprintf(fields->type, sizeof(fields->type), "%s", "synthetic");
    fields->count = 3;
    fields->band_count = 3;
    snprintf(fields->entries[0].name, sizeof(fields->entries[0].name), "%s", "huntrange");
    fields->entries[0].opcode = 202;
    fields->entries[0].wire = CP_FIELD_WIRE_U1;
    snprintf(fields->entries[1].name, sizeof(fields->entries[1].name), "%s", "hitpoints");
    fields->entries[1].opcode = 77;
    fields->entries[1].wire = CP_FIELD_WIRE_U2;
    snprintf(fields->entries[2].name, sizeof(fields->entries[2].name), "%s", "death_drop");
    fields->entries[2].opcode = 151;
    fields->entries[2].wire = CP_FIELD_WIRE_U4;
}

static void
check_band(void)
{
    struct CP_Fields fields;
    struct CP_ServerBand band;
    struct Decoded got[8];
    int size;
    int count;

    synthetic_register(&fields);
    cp_server_band_init(&band);

    check(cp_server_band_put(&band, &fields.entries[1], 4000), "a u2 field is written");
    check(cp_server_band_put(&band, &fields.entries[2], -1),
          "a u4 field carries -1 — `drops nothing`, not obj 0");
    check(cp_server_band_put(&band, &fields.entries[0], 12), "a u1 field is written");

    size = cp_server_band_finish(&band);
    /* u2 -> 1+2, u4 -> 1+4, u1 -> 1+1, then the terminator. */
    check(size == 3 + 5 + 2 + 1,
          "the band is exactly opcode+payload per field, plus the terminator");

    count = decode_band(&fields, band.bytes, size, got, 8);
    check(count == 3, "an independent decode reads back every field");
    if( count == 3 )
    {
        check(got[0].opcode == 77 && got[0].value == 4000, "hitpoints survives at its own opcode");
        check(got[1].opcode == 151 && got[1].value == -1, "death_drop's -1 survives as -1");
        check(got[2].opcode == 202 && got[2].value == 12, "huntrange survives");
    }

    /*
     * A record stating nothing encodes to a bare terminator, and that is the whole
     * reason the pack is proportional to what someone wrote rather than to the
     * cache's 16,292 npcs. `stated` is what the packer keys off to skip the archive
     * entirely.
     */
    {
        struct CP_ServerBand empty;

        cp_server_band_init(&empty);
        check(cp_server_band_finish(&empty) == 1 && empty.stated == 0,
              "a record stating nothing encodes to a bare terminator");
    }

    /*
     * Out-of-range is refused, never masked. A masked id is a valid id for some
     * other record, which is the failure this whole file exists to prevent — it
     * just arrives one layer earlier.
     */
    {
        struct CP_ServerBand narrow;

        cp_server_band_init(&narrow);
        check(!cp_server_band_put(&narrow, &fields.entries[0], 256),
              "a value too wide for u1 is refused rather than truncated");
        check(!cp_server_band_put(&narrow, &fields.entries[1], -1),
              "a negative is refused on u2, which the reader zero-extends");
        check(narrow.stated == 0, "a refused field writes no bytes");
    }
}

static void
check_header(void)
{
    struct CP_Fields fields;
    struct CP_ServerBand band;
    uint8_t archive[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER];
    const uint8_t* got = NULL;
    int got_size = 0;
    int version = 0;
    int kind = 0;
    uint32_t written;

    synthetic_register(&fields);
    cp_server_band_init(&band);
    cp_server_band_put(&band, &fields.entries[1], 5);
    cp_server_band_finish(&band);

    written = cp_server_archive_build(&band, archive, sizeof(archive));
    check(written == (uint32_t)band.at + CP_SERVER_PACK_HEADER,
          "the archive is the band plus a fixed header");
    check(cp_server_archive_open(archive, (int)written, &version, &kind, &got, &got_size),
          "the header it wrote is the header it accepts");
    check(version == CP_SERVER_PACK_VERSION, "the version reads back");
    check(kind == CP_SERVER_PAYLOAD_BAND, "the kind byte says this archive holds a band");
    check(got_size == band.at && got && memcmp(got, band.bytes, (size_t)band.at) == 0,
          "the band inside is byte-identical");

    /*
     * The reason the header exists at all: `RSCache_Dat2DiskWriteArchive` creates
     * the container from nothing and writes no idx255, so a server pack has no
     * reference table and therefore no per-archive CRC. Without this, an archive
     * left behind by a tree two edits ago reads as current.
     */
    archive[CP_SERVER_PACK_HEADER] ^= 0xFF;
    check(!cp_server_archive_open(archive, (int)written, NULL, NULL, NULL, NULL),
          "a flipped payload byte fails the CRC");
    archive[CP_SERVER_PACK_HEADER] ^= 0xFF;
    archive[0] = 'X';
    check(!cp_server_archive_open(archive, (int)written, NULL, NULL, NULL, NULL),
          "an archive that is not a server band is refused on its first byte");
    archive[0] = 'S';
    check(!cp_server_archive_open(archive, CP_SERVER_PACK_HEADER - 1, NULL, NULL, NULL, NULL),
          "a truncated archive is refused");
}

/* ---- server-only namespaces ---------------------------------------------- */

static void
check_server_groups(void)
{
    int count = 0;
    const struct CP_ServerGroup* groups = cp_server_groups(&count);
    int out_of_space = 0;
    int collides = 0;

    check(count > 0, "at least one server-only namespace has a group");
    for( int i = 0; i < count; i++ )
    {
        /*
         * The upper bound is a *format* limit, not taste: every dat2 sector
         * carries its table id in one byte, so 256 is written as 0 and silently
         * aliases another table. The lower bound is the margin over
         * `dat2_configs.h`, whose largest kind is 39.
         */
        if( groups[i].group < CP_SERVER_GROUP_BASE || groups[i].group > 255 )
            out_of_space++;
        for( int j = i + 1; j < count; j++ )
        {
            if( groups[i].group == groups[j].group )
                collides++;
        }
        if( cp_server_group_for(groups[i].name) != groups[i].group )
            collides++;
    }
    check(out_of_space == 0, "every server-only group is inside 128..255");
    check(collides == 0, "no two server-only namespaces share a group, and lookup agrees");
    check(cp_server_group_for("npc") < 0, "a cache config type has no server-only group");
    /* 39 is `dbtable`, the largest kind this revision defines. The assertion is
     * the margin, not the equality. */
    check(CP_SERVER_GROUP_BASE > 39, "the base clears every config kind the cache defines");
}

static void
check_name_table(void)
{
    /* Sparse on purpose: `category`'s ids are the cache's own and run to 131
     * across two dozen names, so position cannot imply the id. */
    static const int ids[] = { 0, 5, 131 };
    static const char* const names[] = { "attack", "prayer", "bones" };
    uint8_t table[256];
    uint8_t archive[512];
    uint32_t size;
    uint32_t framed;
    int got_ids[8];
    const char* got_names[8];
    const uint8_t* payload = NULL;
    int payload_size = 0;
    int kind = 0;
    int count;

    size = cp_server_names_encode(ids, names, 3, table, sizeof(table));
    check(size > 0, "a name table encodes");

    framed = cp_server_archive_build_payload(CP_SERVER_PAYLOAD_NAMES, table, size, archive,
                                             sizeof(archive));
    check(framed == size + CP_SERVER_PACK_HEADER, "it wraps in the same header a band does");
    check(cp_server_archive_open(archive, (int)framed, NULL, &kind, &payload, &payload_size),
          "the wrapped table passes its own CRC");
    check(kind == CP_SERVER_PAYLOAD_NAMES,
          "the kind byte distinguishes a name table from a band");

    count = cp_server_names_decode(payload, payload_size, got_ids, got_names, 8);
    check(count == 3, "every entry reads back");
    if( count == 3 )
    {
        check(got_ids[0] == 0 && strcmp(got_names[0], "attack") == 0, "id 0 survives");
        check(got_ids[2] == 131 && strcmp(got_names[2], "bones") == 0,
              "a sparse id far past the count survives");
    }

    /*
     * A table whose last name runs off the end must be refused, not returned. The
     * decoder hands out pointers into the payload, so an unterminated string
     * would be a pointer into whatever the container put after it.
     */
    check(cp_server_names_decode(payload, payload_size - 1, got_ids, got_names, 8) == -1,
          "a truncated name table is refused rather than returning a runaway string");

    /* Capacity is checked before a byte is written: the cursor is borrowed, and
     * `p` asserts rather than growing. */
    check(cp_server_names_encode(ids, names, 3, table, 4) == 0,
          "an undersized buffer is refused rather than overrun");
}

/* ---- the checks cp_fields_check owns ------------------------------------- */

static void
check_register_rules(void)
{
    struct CP_Fields fields;

    synthetic_register(&fields);
    check(cp_fields_check(&fields) == 0, "a well-formed register passes");

    /* The three `cachepack: fields/synthetic.ini: ...` lines on stderr are these
     * checks working, not a broken tree. */
    fields.entries[2].opcode = 77; /* the same opcode as hitpoints */
    check(cp_fields_check(&fields) > 0,
          "two fields on one opcode is refused — one would be written and never read");

    synthetic_register(&fields);
    fields.entries[0].opcode = 13; /* inside the client band */
    check(cp_fields_check(&fields) > 0, "an opcode below 64 is refused");

    synthetic_register(&fields);
    fields.entries[0].wire = CP_FIELD_WIRE_STRING;
    check(cp_fields_check(&fields) > 0,
          "`wire = string` is refused — the reader's decoder has no string case");

    synthetic_register(&fields);
    fields.rejected = 1;
    check(cp_fields_check(&fields) > 0, "a row the parser refused fails the register");
}

int
main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "../../OSRS-Content/osrs239-content";

    check_band();
    check_header();
    check_server_groups();
    check_name_table();
    check_register_rules();
    check_against_register(dir, "npc");
    check_against_register(dir, "loc");

    if( g_failures )
    {
        printf("fields: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("fields: all %d checks passed\n", g_checks);
    return 0;
}
