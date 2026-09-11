/*
 * The server-band codec, and its agreement with the field register.
 *
 * Two different things are checked here, and the second is the one that would
 * otherwise fail silently.
 *
 * **The round trip** is ordinary: encode a record, decode it back, compare every
 * field. It catches a mis-shifted width or a transposed opcode.
 *
 * **The register cross-check** is not. `fields/<type>.ini` declares each server
 * field's opcode and wire width, and `torirs_server_servercodec.c` holds a C table
 * saying the same thing. If those two disagree, the packer writes a field under
 * one opcode and the server reads it under another — and *nothing errors*,
 * because both streams are well-formed. The value simply lands in the wrong
 * field, or nowhere. So the test parses the ini and holds the C table to it,
 * name by name.
 *
 * That is the same reasoning `cp_register.h` records for `content.ini`: a second
 * table transcribing a first is a table that drifts, and the only defence is a
 * check that reads both.
 *
 * ## Why it iterates the registry
 *
 * The cross-check loops over `ToriRSServer_ServerTypes()` rather than naming `npc`.
 * A check that hardcodes its types is a check a new type joins without: someone
 * adds a `ServerType` row, forgets `fields/<name>.ini`, and the packer writes
 * nothing for it while the server happily decodes an empty stream. Iterating
 * makes "every registered type has a register that agrees with it" the actual
 * assertion.
 *
 * The register directory is argv[1], defaulting to the tree's; each type resolves
 * its own `<dir>/<name>.ini`.
 */

#include "../torirs_server_servercodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int ok,
    const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("servercodec: %-58s %s\n", what, ok ? "ok" : "FAILED");
}

/** The engine defaults this codec encodes against, kept minimal on purpose: the
 *  test cares that "equal to default" is omitted, not what the defaults are. */
static struct ToriRSServerNpcDef
defaults_record(void)
{
    struct ToriRSServerNpcDef def;

    memset(&def, 0, sizeof(def));
    def.hitpoints = 10;
    def.attackrate = 4;
    def.respawnrate = 25;
    def.blockwalk = 1; /* LostCity BlockWalk.NPC default */
    /* -1, not 0: "drops nothing" and "drops obj 0" are different records, and a
     * zero-compare would confuse them — see the note in the encoder. */
    def.death_drop = -1;
    return def;
}

static void
check_round_trip(void)
{
    const struct ServerType* npc = ToriRSServer_ServerTypeFor("npc");
    struct ToriRSServerNpcDef defaults = defaults_record();
    struct ToriRSServerNpcDef src = defaults;
    struct ToriRSServerNpcDef dst = defaults;
    uint8_t buf[512];
    uint32_t written;
    int consumed;

    check(npc != NULL, "the registry answers for `npc`");
    if( !npc )
        return;

    src.hitpoints = 4000;
    src.attack = 1;
    src.strength = 250;
    src.defence = 300;
    src.ranged = 7;
    src.magic = 9;
    src.respawnrate = 100;
    src.wanderrange = 5;
    src.huntrange = 12;
    src.huntmode = 3;
    src.nomove = 1;
    src.blockwalk = 2; /* all */
    src.blocksight = 1;
    src.moverestrict = 5; /* nomove */
    src.attackrate = 6;
    src.death_drop = 526;
    src.attack_anim = 4652;
    src.defend_anim = 4653;
    src.death_anim = 4654;

    written = ToriRSServer_ServerEncode(npc, &src, &defaults, buf, sizeof(buf));
    check(written > 0, "a fully-stated record encodes");
    check(
        written <= ToriRSServer_ServerEncodeBound(npc),
        "the encode stays inside its own bound");

    consumed = ToriRSServer_ServerDecode(npc, &dst, buf, (int)written);
    check(consumed == (int)written, "the stream is consumed to its last byte");

    check(dst.hitpoints == 4000, "hitpoints survives the round trip");
    check(dst.attack == 1 && dst.strength == 250 && dst.defence == 300,
          "the melee stats survive");
    check(dst.ranged == 7 && dst.magic == 9, "ranged and magic survive");
    check(dst.respawnrate == 100 && dst.wanderrange == 5, "respawn and wander survive");
    check(dst.huntrange == 12 && dst.huntmode == 3, "hunt range and mode survive");
    check(dst.nomove == 1 && dst.attackrate == 6, "nomove and attackrate survive");
    check(dst.blockwalk == 2 && dst.blocksight == 1 && dst.moverestrict == 5,
          "blockwalk, blocksight and moverestrict survive");
    check(dst.death_drop == 526, "death_drop survives");
    check(
        dst.attack_anim == 4652 && dst.defend_anim == 4653 && dst.death_anim == 4654,
        "the three animations survive");
}

/**
 * The same trip over `loc`, which is the check that the codec is really generic.
 *
 * One field and a different record struct: if anything in the encoder still knew
 * an npc's layout, this is where it would show.
 */
static void
check_round_trip_loc(void)
{
    const struct ServerType* loc = ToriRSServer_ServerTypeFor("loc");
    struct ToriRSServerLocDef defaults;
    struct ToriRSServerLocDef src;
    struct ToriRSServerLocDef dst;
    uint8_t buf[64];
    uint32_t written;

    check(loc != NULL, "the registry answers for `loc`");
    if( !loc )
        return;

    memset(&defaults, 0, sizeof(defaults));
    defaults.next_loc_stage = -1;
    src = defaults;
    dst = defaults;
    src.next_loc_stage = 62000; /* past u2, which is why the register says u4 */

    written = ToriRSServer_ServerEncode(loc, &src, &defaults, buf, sizeof(buf));
    check(written == 6, "a loc band is one u4 field plus the terminator");
    check(ToriRSServer_ServerDecode(loc, &dst, buf, (int)written) == (int)written,
          "the loc stream is consumed to its last byte");
    check(dst.next_loc_stage == 62000, "a loc id past 65535 survives");
}

static void
check_sparse(void)
{
    const struct ServerType* npc = ToriRSServer_ServerTypeFor("npc");
    struct ToriRSServerNpcDef defaults = defaults_record();
    struct ToriRSServerNpcDef same = defaults;
    uint8_t buf[512];
    uint32_t written;

    if( !npc )
        return;
    written = ToriRSServer_ServerEncode(npc, &same, &defaults, buf, sizeof(buf));

    /* The whole reason the pack is small: 38 authored npcs, not 16,292 records. */
    check(written == 1 && buf[0] == 0,
          "a record equal to the defaults encodes to a bare terminator");

    /* And "absent" must not read as "zero": decoding an empty stream over a
     * seeded record leaves the seed alone. */
    {
        struct ToriRSServerNpcDef seeded = defaults;
        uint8_t empty[1] = { 0 };

        seeded.hitpoints = 77;
        ToriRSServer_ServerDecode(npc, &seeded, empty, 1);
        check(seeded.hitpoints == 77, "an absent opcode leaves the seeded value alone");
    }

    /*
     * A field whose value is 0 while its default is not must still be written.
     * This is the other half of the same rule and the easier one to break: 0 is a
     * real value far more often than it looks, so "omit when zero" would silently
     * drop `attackrate = 0` and leave the seed's 4 in place.
     */
    {
        struct ToriRSServerNpcDef zeroed = defaults;
        struct ToriRSServerNpcDef seeded = defaults;

        zeroed.attackrate = 0;
        written = ToriRSServer_ServerEncode(npc, &zeroed, &defaults, buf, sizeof(buf));
        ToriRSServer_ServerDecode(npc, &seeded, buf, (int)written);
        check(seeded.attackrate == 0, "a stated zero overrides a non-zero default");
    }
}

/* ---- the register cross-check ------------------------------------------- */

struct IniField
{
    char name[64];
    int opcode;
    int wire;
};

/** Parse `[<type>.<name>]` blocks carrying `server = opcode:<n>:<wire>`. */
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
        /* Only real declarations; the file documents the syntax in a comment
         * whose text would otherwise be parsed as a row. */
        if( *cursor == ';' || *cursor == '#' )
            continue;
        if( strncmp(cursor, "server", 6) == 0 )
        {
            char* spec = strstr(cursor, "opcode:");

            if( spec && count < max )
            {
                int opcode = 0;
                char wire[8] = { 0 };

                if( sscanf(spec, "opcode:%d:%7[^ \t\r\n]", &opcode, wire) == 2 &&
                    strncmp(current, prefix, strlen(prefix)) == 0 )
                {
                    snprintf(out[count].name, sizeof(out[count].name), "%s",
                             current + strlen(prefix));
                    out[count].opcode = opcode;
                    out[count].wire = wire[1] ? atoi(wire + 1) : 0;
                    count++;
                }
            }
        }
    }
    fclose(file);
    return count;
}

static void
check_type_against_register(
    const struct ServerType* type,
    const char* dir)
{
    char path[1024];
    char what[160];
    struct IniField ini[64];
    int ini_count;
    int matched = 0;
    int mismatched = 0;

    snprintf(path, sizeof(path), "%s/%s.ini", dir, type->name);
    ini_count = load_ini(path, type->name, ini, 64);
    if( ini_count < 0 )
    {
        /*
         * A registered type with no register file is a failure, not a skip. That
         * is the whole reason this loops: the type would encode under opcodes
         * nothing agreed to, and the packer — which reads only the register —
         * would write nothing at all for it.
         */
        printf("servercodec:   %s — registered in C, no %s\n", type->name, path);
        snprintf(what, sizeof(what), "%s: the register file exists", type->name);
        check(0, what);
        return;
    }
    printf("servercodec: %s — register declares %d server field(s), C table holds %d\n",
           type->name, ini_count, type->count);

    for( int i = 0; i < ini_count; i++ )
    {
        int found = 0;

        for( int j = 0; j < type->count; j++ )
        {
            if( strcmp(type->fields[j].name, ini[i].name) != 0 )
                continue;
            found = 1;
            if( type->fields[j].opcode != ini[i].opcode ||
                (int)type->fields[j].wire != ini[i].wire )
            {
                printf("servercodec:   %s.%s — register says opcode %d:u%d, C says %d:u%d\n",
                       type->name, ini[i].name, ini[i].opcode, ini[i].wire,
                       type->fields[j].opcode, (int)type->fields[j].wire);
                mismatched++;
            }
            else
            {
                matched++;
            }
            break;
        }
        if( !found )
        {
            printf("servercodec:   %s.%s — declared in the register, absent from C\n", type->name,
                   ini[i].name);
            mismatched++;
        }
    }

    snprintf(what, sizeof(what), "%s: the register declares server opcodes", type->name);
    check(ini_count > 0, what);
    snprintf(what, sizeof(what), "%s: every declared field matches the C table exactly",
             type->name);
    check(matched == ini_count, what);
    snprintf(what, sizeof(what), "%s: no field disagrees between the register and C", type->name);
    check(mismatched == 0, what);
    snprintf(what, sizeof(what), "%s: the C table states no field the register does not",
             type->name);
    check(type->count == ini_count, what);
}

static void
check_against_register(const char* dir)
{
    int count = 0;
    const struct ServerType* types = ToriRSServer_ServerTypes(&count);

    check(count > 0, "at least one type has a server band");
    for( int i = 0; i < count; i++ )
        check_type_against_register(&types[i], dir);
}

static void
check_band(void)
{
    int count = 0;
    const struct ServerType* types = ToriRSServer_ServerTypes(&count);
    int out_of_band = 0;
    int duplicated = 0;

    for( int t = 0; t < count; t++ )
    {
        const struct ServerType* type = &types[t];

        for( int i = 0; i < type->count; i++ )
        {
            if( type->fields[i].opcode < 64 || type->fields[i].opcode > 255 )
                out_of_band++;
            for( int j = i + 1; j < type->count; j++ )
            {
                if( type->fields[i].opcode == type->fields[j].opcode )
                    duplicated++;
            }
        }
    }
    /*
     * Client npc opcodes run 1..147, so staying at or above 64 is what keeps a
     * server record from being mistaken for a client one — and what lets a client
     * decoder fed this stream stop cleanly instead of misreading it.
     *
     * Per type, not across types: `npc` and `loc` both use 150 and that is fine,
     * because a band is only ever decoded against the type it was written for.
     */
    check(out_of_band == 0, "every server opcode is inside the reserved 64..255 band");
    check(duplicated == 0, "no two fields of one type claim the same opcode");
}

int
main(int argc, char** argv)
{
    /* A directory, not a file: the check resolves one register per registered
     * type, so it cannot be handed a single `npc.ini`. */
    const char* dir = argc > 1 ? argv[1] : "OSRS-Content/osrs239-content/fields";

    check_round_trip();
    check_round_trip_loc();
    check_sparse();
    check_band();
    check_against_register(dir);

    if( g_failures )
    {
        printf("servercodec: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("servercodec: all %d checks passed\n", g_checks);
    return 0;
}
