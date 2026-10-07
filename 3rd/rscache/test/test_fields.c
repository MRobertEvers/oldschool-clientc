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
 * The register is `rscache_register.h` and the band codec `rscache_band.h` —
 * one parser and one codec, which the writer (cachepack) and the reader (the game
 * server) both link. `test_band.c` holds the codec to itself; this holds it to
 * the content tree's own file and to an independent decoder. Four things:
 *
 *   the parse       RSCache_RegisterLoad's table against an independent parse of
 *                   the same file. `fields/npc.ini` declares a field in *two*
 *                   blocks — the projection at the top, the server opcode at the
 *                   bottom, under the same `[npc.hitpoints]` name — so a reader
 *                   that appends sections instead of merging them silently loses
 *                   half of them.
 *   the register    RSCache_RegisterCheck plus the parser's refusals: the reserved
 *                   band, no two fields on one opcode, no wire the reader cannot
 *                   decode.
 *   the band        RSCache_BandEncode's bytes decoded by an independent
 *                   implementation of the reader's algorithm. The codec agreeing
 *                   with its own decoder proves nothing, which is why this test
 *                   does not call RSCache_BandDecode.
 *   the header      cachepack's archive framing: version, CRC, and what a
 *                   corrupted byte does.
 */

#include "cp_fields.h"
#include "rscache_band.h"
#include "rscache_register.h"

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
 * Deliberately not rscache_register.c's parser: a check that
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
    struct IniField ini[RSCACHE_REGISTER_MAX];
    static struct RSCache_Register fields;
    int ini_count;
    int matched = 0;
    int mismatched = 0;
    char what[128];

    snprintf(path, sizeof(path), "%s/fields/%s.ini", dir, type);
    ini_count = load_ini(path, type, ini, RSCACHE_REGISTER_MAX);
    if( ini_count < 0 )
    {
        /* Loud, and not a pass — the discipline the cache suites already follow. */
        printf("fields: SKIPPED %s — no %s\n", type, path);
        return;
    }

    RSCache_RegisterLoad(&fields, dir, type);
    printf("fields: %s — the file declares %d server field(s), the register loaded %d\n", type,
           ini_count, fields.band_count);

    for( int i = 0; i < ini_count; i++ )
    {
        const struct RSCache_RegisterField* got = RSCache_RegisterFind(&fields, ini[i].name);

        if( !got )
        {
            printf("fields:   %s.%s — declared in the file, absent from the register\n", type,
                   ini[i].name);
            mismatched++;
            continue;
        }
        if( got->opcode != ini[i].opcode || cp_register_wire_bytes(got->wire) != ini[i].wire )
        {
            printf("fields:   %s.%s — file says opcode %d:u%d, the register says %d:u%d\n", type,
                   ini[i].name, ini[i].opcode, ini[i].wire, got->opcode,
                   cp_register_wire_bytes(got->wire));
            mismatched++;
            continue;
        }
        matched++;
    }

    snprintf(what, sizeof(what), "%s: the register declares server opcodes", type);
    check(ini_count > 0, what);
    snprintf(what, sizeof(what), "%s: every declared field matches the register exactly", type);
    check(matched == ini_count && mismatched == 0, what);
    /* `band_count`, not `count`: the register also declares fields with no server
     * opcode — `[npc.name]`, `[npc.magic]` — and those are load-bearing for the
     * client-side filter rather than rows this check is about. */
    snprintf(what, sizeof(what), "%s: the register states no band field the file does not", type);
    check(fields.band_count == ini_count, what);

    /*
     * The reserved band, restated here rather than only inside the parser.
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

    snprintf(what, sizeof(what), "%s: RSCache_RegisterCheck passes on the tree's own register",
             type);
    check(RSCache_RegisterCheck(&fields) == 0, what);

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
    const struct RSCache_Register* fields,
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
        const struct RSCache_RegisterField* field = NULL;
        int width;
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
        if( !field || count >= max )
            return -1;
        width = cp_register_wire_bytes(field->wire);
        if( at + width > size )
            return -1;
        if( width == 1 )
            value = band[at];
        else if( width == 2 )
            value = (band[at] << 8) | band[at + 1];
        else if( width == 4 )
            value = (int)(((uint32_t)band[at] << 24) | ((uint32_t)band[at + 1] << 16) |
                          ((uint32_t)band[at + 2] << 8) | (uint32_t)band[at + 3]);
        else
            return -1;
        at += width;
        out[count].opcode = opcode;
        out[count].value = value;
        count++;
    }
    return count;
}

/**
 * A register parsed from text, so the band checks run with no tree on disk.
 * Declared out of opcode order on purpose: the parser sorts the band, and the
 * encoder's ascending stream depends on it.
 */
static const char k_synthetic[] = "[synthetic.huntrange]\n"
                                  "server = opcode:202:u1\n"
                                  "[synthetic.hitpoints]\n"
                                  "server = opcode:77:u2\n"
                                  "[synthetic.death_drop]\n"
                                  "server = opcode:151:u4\n"
                                  "[synthetic.name]\n"
                                  "client = native\n";

static void
synthetic_register(struct RSCache_Register* fields)
{
    RSCache_RegisterParse(fields, "synthetic", k_synthetic, sizeof(k_synthetic) - 1);
}

static void
check_band(void)
{
    static struct RSCache_Register fields;
    struct RSCache_BandRecord record;
    uint8_t band[CP_SERVER_BAND_MAX];
    struct Decoded got[8];
    int hitpoints;
    int death_drop;
    int huntrange;
    uint32_t size;
    int count;

    synthetic_register(&fields);
    check(fields.band_count == 3 && fields.count == 4,
          "three band fields and one client-only declaration");
    check(fields.entries[0].opcode == 77 && fields.entries[1].opcode == 151 &&
              fields.entries[2].opcode == 202,
          "the band fields are sorted ascending by opcode");
    hitpoints = RSCache_BandIndex(&fields, "hitpoints");
    death_drop = RSCache_BandIndex(&fields, "death_drop");
    huntrange = RSCache_BandIndex(&fields, "huntrange");
    check(RSCache_BandIndex(&fields, "name") < 0, "a field with no opcode has no band index");

    RSCache_BandRecordReset(&record);
    check(RSCache_BandFits(&fields, hitpoints, 4000), "a u2 field takes 4000");
    RSCache_BandRecordSet(&record, hitpoints, 4000);
    check(RSCache_BandFits(&fields, death_drop, -1),
          "a u4 field carries -1 — `drops nothing`, not obj 0");
    RSCache_BandRecordSet(&record, death_drop, -1);
    check(RSCache_BandFits(&fields, huntrange, 12), "a u1 field takes 12");
    RSCache_BandRecordSet(&record, huntrange, 12);

    size = RSCache_BandEncode(&fields, &record, band, sizeof(band));
    /* u2 -> 1+2, u4 -> 1+4, u1 -> 1+1, then the terminator. */
    check(size == 3 + 5 + 2 + 1,
          "the band is exactly opcode+payload per field, plus the terminator");

    count = decode_band(&fields, band, (int)size, got, 8);
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
     * cache's 16,292 npcs. The packer skips the archive when nothing is stated.
     */
    RSCache_BandRecordReset(&record);
    check(RSCache_BandEncode(&fields, &record, band, sizeof(band)) == 1 && band[0] == 0,
          "a record stating nothing encodes to a bare terminator");

    /*
     * A stated zero is not an absent field: `death_drop` 0 is a real obj. Presence
     * decides what is written, never the value.
     */
    RSCache_BandRecordSet(&record, death_drop, 0);
    size = RSCache_BandEncode(&fields, &record, band, sizeof(band));
    count = decode_band(&fields, band, (int)size, got, 8);
    check(count == 1 && got[0].opcode == 151 && got[0].value == 0,
          "a stated 0 is written, and only the stated field is");

    /*
     * Out-of-range is refused, never masked. A masked id is a valid id for some
     * other record, which is the failure this whole file exists to prevent — it
     * just arrives one layer earlier. The writer asks before stating the field;
     * the encoder asserts on one it was handed anyway.
     */
    check(!RSCache_BandFits(&fields, huntrange, 256),
          "a value too wide for u1 is refused rather than truncated");
    check(!RSCache_BandFits(&fields, hitpoints, -1),
          "a negative is refused on u2, which the reader zero-extends");
}

static void
check_header(void)
{
    static struct RSCache_Register fields;
    struct RSCache_BandRecord record;
    uint8_t band[CP_SERVER_BAND_MAX];
    uint8_t archive[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER];
    const uint8_t* got = NULL;
    int got_size = 0;
    int version = 0;
    int kind = 0;
    uint32_t band_size;
    uint32_t written;

    synthetic_register(&fields);
    RSCache_BandRecordReset(&record);
    RSCache_BandRecordSet(&record, RSCache_BandIndex(&fields, "hitpoints"), 5);
    band_size = RSCache_BandEncode(&fields, &record, band, sizeof(band));

    written = cp_server_archive_build(band, band_size, archive, sizeof(archive));
    check(written == band_size + CP_SERVER_PACK_HEADER,
          "the archive is the band plus a fixed header");
    check(cp_server_archive_open(archive, (int)written, &version, &kind, &got, &got_size),
          "the header it wrote is the header it accepts");
    check(version == CP_SERVER_PACK_VERSION, "the version reads back");
    check(kind == CP_SERVER_PAYLOAD_BAND, "the kind byte says this archive holds a band");
    check(got_size == (int)band_size && got && memcmp(got, band, band_size) == 0,
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

/* ---- the register's refusals -------------------------------------------- */

/** RSCache_RegisterCheck over a register parsed from `text`. */
static int
register_problems(const char* text)
{
    static struct RSCache_Register fields;

    RSCache_RegisterParse(&fields, "synthetic", text, strlen(text));
    return RSCache_RegisterCheck(&fields);
}

static void
check_register_rules(void)
{
    check(register_problems(k_synthetic) == 0, "a well-formed register passes");

    /* The `fields/synthetic.ini: ...` lines on stderr are these checks working,
     * not a broken tree. */
    check(register_problems("[synthetic.a]\nserver = opcode:77:u2\n"
                            "[synthetic.b]\nserver = opcode:77:u1\n") > 0,
          "two fields on one opcode is refused — one would be written and never read");
    check(register_problems("[synthetic.a]\nserver = opcode:13:u1\n") > 0,
          "an opcode below 64 is refused");
    check(register_problems("[synthetic.a]\nserver = opcode:256:u1\n") > 0,
          "an opcode above 255 is refused");
    check(register_problems("[synthetic.a]\nserver = opcode:90:string\n") == 0,
          "`wire = string` is a band field");
    check(register_problems("[synthetic.a]\nserver = opcode:90:list\ntext = indexed\n") > 0,
          "a list that states no `type` is refused");
    check(register_problems("[synthetic.a]\nserver = opcode:90:list\ntype = obj,int\n") > 0,
          "a list spelled as a single key is refused — a list is `text = indexed` or `list`");
    check(register_problems("[synthetic.a]\nserver = opcode:90:list\ntype = obj,int\n"
                            "text = indexed\n") == 0,
          "a typed, indexed list is a band field");
    check(register_problems("[synthetic.a]\nserver = opcode:90:u3\n") > 0,
          "an unknown wire width is refused");
    check(register_problems("[synthetic.a]\ntext = both\n") > 0,
          "a row the parser refused fails the register");

    check(register_problems("[synthetic.a]\nserver = opcode:90:u1\n"
                            "values = none:0, aggressive:1\n") == 0,
          "an int field may spell its numbers as words");
    check(register_problems("[synthetic.a]\nserver = opcode:90:u1\nvalues = none\n") > 0,
          "a word with no number is refused");
    check(register_problems("[synthetic.a]\nserver = opcode:90:u1\nvalues = big:256\n") > 0,
          "a word whose number the width cannot carry is refused");
    check(register_problems("[synthetic.a]\nserver = opcode:90:string\nvalues = a:1\n") > 0,
          "`values` on a string field is refused");
    {
        static struct RSCache_Register words;
        static const char text[] = "[synthetic.mode]\nserver = opcode:90:u1\n"
                                   "values = none:0,wander:0,patrol:1\n";
        const struct RSCache_RegisterField* field;
        int value = -1;

        RSCache_RegisterParse(&words, "synthetic", text, strlen(text));
        field = RSCache_RegisterFind(&words, "mode");
        check(field && field->word_count == 3, "three words parse");
        check(field && RSCache_RegisterWordValue(field, "patrol", &value) && value == 1,
              "a word resolves to its number");
        check(field && !RSCache_RegisterWordValue(field, "aggressive", &value),
              "an undeclared word does not resolve");
        check(field && strcmp(RSCache_RegisterValueWord(field, 0), "none") == 0,
              "a shared number spells as its first word");
    }
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
