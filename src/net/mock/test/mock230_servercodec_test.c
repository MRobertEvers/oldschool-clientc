/*
 * The npc server-band codec, and its agreement with the field register.
 *
 * Two different things are checked here, and the second is the one that would
 * otherwise fail silently.
 *
 * **The round trip** is ordinary: encode a record, decode it back, compare every
 * field. It catches a mis-shifted width or a transposed opcode.
 *
 * **The register cross-check** is not. `fields/npc.ini` declares each server
 * field's opcode and wire width, and `mock230_servercodec.c` holds a C table
 * saying the same thing. If those two disagree, the packer writes a field under
 * one opcode and the server reads it under another — and *nothing errors*,
 * because both streams are well-formed. The value simply lands in the wrong
 * field, or nowhere. So the test parses the ini and holds the C table to it,
 * name by name.
 *
 * That is the same reasoning `cp_register.h` records for `content.ini`: a second
 * table transcribing a first is a table that drifts, and the only defence is a
 * check that reads both.
 */

#include "../mock230_servercodec.h"

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
    printf("servercodec: %-58s %s\n", what, ok ? "ok" : "FAILED");
}

/** The engine defaults this codec encodes against, kept minimal on purpose: the
 *  test cares that "equal to default" is omitted, not what the defaults are. */
static struct Mock230NpcDef
defaults_record(void)
{
    struct Mock230NpcDef def;

    memset(&def, 0, sizeof(def));
    def.hitpoints = 10;
    def.attackrate = 4;
    def.respawnrate = 25;
    /* -1, not 0: "drops nothing" and "drops obj 0" are different records, and a
     * zero-compare would confuse them — see the note in the encoder. */
    def.death_drop = -1;
    return def;
}

static void
check_round_trip(void)
{
    struct Mock230NpcDef defaults = defaults_record();
    struct Mock230NpcDef src = defaults;
    struct Mock230NpcDef dst = defaults;
    uint8_t buf[512];
    uint32_t written;
    int consumed;

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
    src.attackrate = 6;
    src.death_drop = 526;
    src.attack_anim = 4652;
    src.defend_anim = 4653;
    src.death_anim = 4654;

    written = Mock230_ServerNpcEncode(&src, &defaults, buf, sizeof(buf));
    check(written > 0, "a fully-stated record encodes");
    check(
        written <= Mock230_ServerNpcEncodeBound(&src),
        "the encode stays inside its own bound");

    consumed = Mock230_ServerNpcDecode(&dst, buf, (int)written);
    check(consumed == (int)written, "the stream is consumed to its last byte");

    check(dst.hitpoints == 4000, "hitpoints survives the round trip");
    check(dst.attack == 1 && dst.strength == 250 && dst.defence == 300,
          "the melee stats survive");
    check(dst.ranged == 7 && dst.magic == 9, "ranged and magic survive");
    check(dst.respawnrate == 100 && dst.wanderrange == 5, "respawn and wander survive");
    check(dst.huntrange == 12 && dst.huntmode == 3, "hunt range and mode survive");
    check(dst.nomove == 1 && dst.attackrate == 6, "nomove and attackrate survive");
    check(dst.death_drop == 526, "death_drop survives");
    check(
        dst.attack_anim == 4652 && dst.defend_anim == 4653 && dst.death_anim == 4654,
        "the three animations survive");
}

static void
check_sparse(void)
{
    struct Mock230NpcDef defaults = defaults_record();
    struct Mock230NpcDef same = defaults;
    uint8_t buf[512];
    uint32_t written = Mock230_ServerNpcEncode(&same, &defaults, buf, sizeof(buf));

    /* The whole reason the pack is small: 38 authored npcs, not 16,292 records. */
    check(written == 1 && buf[0] == 0,
          "a record equal to the defaults encodes to a bare terminator");

    /* And "absent" must not read as "zero": decoding an empty stream over a
     * seeded record leaves the seed alone. */
    {
        struct Mock230NpcDef seeded = defaults;
        uint8_t empty[1] = { 0 };

        seeded.hitpoints = 77;
        Mock230_ServerNpcDecode(&seeded, empty, 1);
        check(seeded.hitpoints == 77, "an absent opcode leaves the seeded value alone");
    }
}

/* ---- the register cross-check ------------------------------------------- */

struct IniField
{
    char name[64];
    int opcode;
    int wire;
};

/** Parse `[npc.<name>]` blocks carrying `server = opcode:<n>:<wire>`. */
static int
load_ini(const char* path, struct IniField* out, int max)
{
    FILE* file = fopen(path, "r");
    char line[512];
    char current[64];
    int count = 0;

    if( !file )
        return -1;
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

                if( sscanf(spec, "opcode:%d:%7[^ \t\r\n]", &opcode, wire) == 2 )
                {
                    const char* dot = strchr(current, '.');

                    snprintf(out[count].name, sizeof(out[count].name), "%s",
                             dot ? dot + 1 : current);
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
check_against_register(const char* ini_path)
{
    struct IniField ini[64];
    int ini_count = load_ini(ini_path, ini, 64);
    int table_count = 0;
    const struct ServerField* table = Mock230_ServerNpcFields(&table_count);
    int matched = 0;
    int mismatched = 0;

    if( ini_count < 0 )
    {
        /* Loud, and not a pass — the discipline the cache suites already follow. */
        printf("servercodec: SKIPPED register cross-check — no %s\n", ini_path);
        return;
    }
    printf("servercodec: register declares %d server fields, C table holds %d\n",
           ini_count, table_count);

    for( int i = 0; i < ini_count; i++ )
    {
        int found = 0;

        for( int j = 0; j < table_count; j++ )
        {
            if( strcmp(table[j].name, ini[i].name) != 0 )
                continue;
            found = 1;
            if( table[j].opcode != ini[i].opcode || (int)table[j].wire != ini[i].wire )
            {
                printf("servercodec:   %s — register says opcode %d:u%d, C says %d:u%d\n",
                       ini[i].name, ini[i].opcode, ini[i].wire, table[j].opcode,
                       (int)table[j].wire);
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
            printf("servercodec:   %s — declared in the register, absent from C\n",
                   ini[i].name);
            mismatched++;
        }
    }

    check(ini_count > 0, "the register declares server opcodes");
    check(matched == ini_count, "every declared field matches the C table exactly");
    check(mismatched == 0, "no field disagrees between the register and C");
    check(table_count == ini_count, "the C table states no field the register does not");
}

static void
check_band(void)
{
    int count = 0;
    const struct ServerField* table = Mock230_ServerNpcFields(&count);
    int out_of_band = 0;

    for( int i = 0; i < count; i++ )
    {
        if( table[i].opcode < 64 || table[i].opcode > 255 )
            out_of_band++;
    }
    /*
     * Client npc opcodes run 1..147, so staying at or above 64 is what keeps a
     * server record from being mistaken for a client one — and what lets a client
     * decoder fed this stream stop cleanly instead of misreading it.
     */
    check(out_of_band == 0, "every server opcode is inside the reserved 64..255 band");
}

int
main(int argc, char** argv)
{
    const char* ini = argc > 1
                          ? argv[1]
                          : "OSRS-Content/osrs239-content/fields/npc.ini";

    check_round_trip();
    check_sparse();
    check_band();
    check_against_register(ini);

    if( g_failures )
    {
        printf("servercodec: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("servercodec: all %d checks passed\n", g_checks);
    return 0;
}
