/*
 * The server band: the server's bindings, the shared codec, and the register.
 *
 * The bytes are the rscache library's (`rscache_band.h`, tested in
 * `3rd/rscache/test/test_band.c`); what is the server's is the binding of field
 * names to struct members, and the register it reads them under. Three things are
 * checked here.
 *
 * **Presence.** A field the band states lands, whatever its value; a field it does
 * not state keeps the seed. A stated 0 and a stated -1 both survive, and a stated
 * value equal to the seed's is still written and still read — there is no "equal
 * to the default, so omitted" path anywhere any more. The old encoder had one, and
 * it is how `attackrate=4` stated on purpose over a cache seed of 6 vanished.
 *
 * **Every binding round-trips.** Each bound field of each type is stated with a
 * distinct value and read back into a zeroed record, so a binding pointing at the
 * wrong member (or a member too narrow for its field) shows up as a wrong value.
 *
 * **The real register agrees with the bindings.** `ToriRSServer_ServerCheck`, the
 * same check the boot runs, over every registered type and the tree's own
 * `fields/<type>.ini` — loaded the way the server loads it (content_fields.c:
 * defaults, file over them). Iterating `ToriRSServer_ServerTypes()` rather than
 * naming `npc` is what makes "every registered type has a register that agrees
 * with it" the actual assertion.
 *
 * argv[1] is the content tree (the directory holding `fields/`).
 */

#include "../torirs_server_servercodec.h"

#include "content/content_fields.h"
#include "rscache_band.h"
#include "rscache_register.h"

#include <stdio.h>
#include <assert.h>
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
    printf("servercodec: %-66s %s\n", what, ok ? "ok" : "FAILED");
}

/* A synthetic register for the presence checks, so they do not depend on which
 * opcodes a tree happens to choose. */
static const char k_npc_register[] = "[npc.hitpoints]\n"
                                     "server = opcode:77:u2\n"
                                     "[npc.attackrate]\n"
                                     "server = opcode:150:u1\n"
                                     "[npc.death_drop]\n"
                                     "server = opcode:151:u4\n"
                                     "[npc.respawnrate]\n"
                                     "server = opcode:204:u2\n";

static const struct RSCache_BandBinding k_presence_bindings[] = {
    { "hitpoints", offsetof(struct ToriRSServerNpcDef, hitpoints), sizeof(int) },
    { "attackrate", offsetof(struct ToriRSServerNpcDef, attackrate), sizeof(int) },
    { "death_drop", offsetof(struct ToriRSServerNpcDef, death_drop), sizeof(int) },
    { "respawnrate", offsetof(struct ToriRSServerNpcDef, respawnrate), sizeof(int) },
};

static const struct ToriRSServerBandType k_presence_type = {
    "npc", k_presence_bindings, 4, sizeof(struct ToriRSServerNpcDef)
};

/** A seeded record: what a cache record plus engine defaults hands the band. */
static struct ToriRSServerNpcDef
seeded_npc(void)
{
    struct ToriRSServerNpcDef def;

    memset(&def, 0, sizeof(def));
    def.hitpoints = 10;
    def.attackrate = 4;
    def.respawnrate = 25;
    def.death_drop = 526;
    return def;
}

static int
stream_has_opcode(
    const uint8_t* stream,
    uint32_t size,
    int opcode)
{
    /* Every field in the synthetic register is at a known width, so walk it. */
    uint32_t at = 0;

    while( at < size && stream[at] != 0 )
    {
        int op = stream[at++];

        if( op == opcode )
            return 1;
        at += op == 150 ? 1u : op == 151 ? 4u : 2u;
    }
    return 0;
}

static void
check_presence(void)
{
    struct RSCache_Register reg;
    struct RSCache_BandRecord stated;
    struct RSCache_BandRecord back;
    struct ToriRSServerNpcDef npc;
    uint8_t out[64];
    uint32_t written;
    int hp, rate, drop, respawn;

    RSCache_RegisterParse(&reg, "npc", k_npc_register, sizeof(k_npc_register) - 1);
    check(ToriRSServer_ServerCheck(&k_presence_type, &reg) == 0,
          "the synthetic register and its bindings agree");
    hp = RSCache_BandIndex(&reg, "hitpoints");
    rate = RSCache_BandIndex(&reg, "attackrate");
    drop = RSCache_BandIndex(&reg, "death_drop");
    respawn = RSCache_BandIndex(&reg, "respawnrate");

    /* A stated 0 and a stated -1 survive, over a seed that holds neither. */
    RSCache_BandRecordReset(&stated);
    RSCache_BandRecordSet(&stated, rate, 0);
    RSCache_BandRecordSet(&stated, drop, -1);
    written = RSCache_BandEncode(&reg, &stated, out, sizeof(out));
    npc = seeded_npc();
    check(ToriRSServer_ServerDecode(&k_presence_type, &reg, &back, &npc, out, (int)written) ==
              (int)written,
          "the stream is consumed to its last byte");
    check(npc.attackrate == 0, "a stated 0 overrides a non-zero seed");
    check(npc.death_drop == -1, "a stated -1 survives a u4 field");
    check(npc.hitpoints == 10, "an unstated field keeps the seeded value");
    check(npc.respawnrate == 25, "a second unstated field keeps the seeded value");
    check(RSCache_PresenceHas(&back.present, rate) && RSCache_PresenceHas(&back.present, drop) &&
              !RSCache_PresenceHas(&back.present, hp) &&
              !RSCache_PresenceHas(&back.present, respawn),
          "the decoded record says exactly which fields were stated");

    /* A stated value equal to the seed is written, and read. */
    RSCache_BandRecordReset(&stated);
    RSCache_BandRecordSet(&stated, hp, 10);
    written = RSCache_BandEncode(&reg, &stated, out, sizeof(out));
    check(stream_has_opcode(out, written, 77),
          "a stated value equal to the seed is still written");
    npc = seeded_npc();
    npc.hitpoints = 99;
    ToriRSServer_ServerDecode(&k_presence_type, &reg, &back, &npc, out, (int)written);
    check(npc.hitpoints == 10 && RSCache_PresenceHas(&back.present, hp),
          "and still read: the stated 10 replaces a seed of 99");

    /* Nothing stated: a bare terminator, and the seed untouched. */
    RSCache_BandRecordReset(&stated);
    written = RSCache_BandEncode(&reg, &stated, out, sizeof(out));
    check(written == 1 && out[0] == 0, "a record stating nothing is a bare terminator");
    npc = seeded_npc();
    ToriRSServer_ServerDecode(&k_presence_type, &reg, &back, &npc, out, (int)written);
    check(npc.hitpoints == 10 &&
              npc.attackrate == 4 && npc.death_drop == 526 && npc.respawnrate == 25,
          "an empty stream leaves every seeded value alone");

    /* An opcode the register does not declare: refused, and nothing applied. */
    {
        static const uint8_t foreign[] = { 77, 0, 5, 199, 1, 0 };

        npc = seeded_npc();
        check(ToriRSServer_ServerDecode(&k_presence_type, &reg, &back, &npc, foreign,
                                        (int)sizeof(foreign)) == -1,
              "an undeclared opcode refuses the stream");
        check(npc.hitpoints == 10, "and nothing before it is half-applied");
    }
}

/** The load check, both directions, on synthetic registers. */
static void
check_check(void)
{
    struct RSCache_Register reg;
    static const char missing_field[] = "[npc.hitpoints]\nserver = opcode:77:u2\n"
                                        "[npc.attackrate]\nserver = opcode:150:u1\n"
                                        "[npc.death_drop]\nserver = opcode:151:u4\n";
    static const char extra_field[] = "[npc.hitpoints]\nserver = opcode:77:u2\n"
                                      "[npc.attackrate]\nserver = opcode:150:u1\n"
                                      "[npc.death_drop]\nserver = opcode:151:u4\n"
                                      "[npc.respawnrate]\nserver = opcode:204:u2\n"
                                      "[npc.magic_missile]\nserver = opcode:220:u1\n";
    static const struct RSCache_BandBinding narrow[] = {
        { "hitpoints", offsetof(struct ToriRSServerNpcDef, hitpoints), 1 },
    };
    static const struct ToriRSServerBandType narrow_type = { "npc", narrow, 1,
                                                             sizeof(struct ToriRSServerNpcDef) };

    RSCache_RegisterParse(&reg, "npc", missing_field, sizeof(missing_field) - 1);
    check(ToriRSServer_ServerCheck(&k_presence_type, &reg) == 1,
          "a binding the register gives no band home is one problem");
    RSCache_RegisterParse(&reg, "npc", extra_field, sizeof(extra_field) - 1);
    check(ToriRSServer_ServerCheck(&k_presence_type, &reg) == 1,
          "a band field no binding receives is one problem");
    RSCache_RegisterParse(&reg, "npc", k_npc_register, sizeof(k_npc_register) - 1);
    check(ToriRSServer_ServerCheck(&narrow_type, &reg) == 4,
          "a member narrower than its wire, plus three unbound fields");
}

/** Every bound field of every type, stated with a distinct value and read back. */
static void
check_type_round_trip(
    const struct ToriRSServerBandType* type,
    const struct RSCache_Register* reg)
{
    struct RSCache_BandRecord stated;
    struct RSCache_BandRecord back;
    unsigned char* object = calloc(1, type->record_size + sizeof(int));
    uint8_t out[1024];
    uint32_t written;
    int wrong = 0;
    char what[160];

    assert(object);
    RSCache_BandRecordReset(&stated);
    /* Plain member bindings only: an `apply` binding (a list, or a statement
     * that implies more than its value) writes through engine tables this
     * round trip does not set up; the boot test covers those on real content. */
    for( int b = 0; b < type->binding_count; b++ )
    {
        int index = RSCache_BandIndex(reg, type->bindings[b].name);
        /* Distinct, and inside u1, so it fits every width. */
        int32_t value = 3 * (b + 1);

        if( index >= 0 && !type->bindings[b].apply && type->bindings[b].size == sizeof(int) &&
            reg->entries[index].wire != RSCACHE_REGISTER_WIRE_LIST &&
            reg->entries[index].wire != RSCACHE_REGISTER_WIRE_STRING )
            RSCache_BandRecordSet(&stated, index, value);
    }
    if( RSCache_BandEncodeBound(reg, &stated) > sizeof(out) )
    {
        snprintf(what, sizeof(what), "%s: the test's buffers hold a record", type->name);
        check(0, what);
        free(object);
        return;
    }
    written = RSCache_BandEncode(reg, &stated, out, sizeof(out));
    memset(object, 0, type->record_size);
    snprintf(what, sizeof(what), "%s: a fully-stated record is consumed whole", type->name);
    check(ToriRSServer_ServerDecode(type, reg, &back, object, out, (int)written) == (int)written,
          what);
    for( int b = 0; b < type->binding_count; b++ )
    {
        int value;
        int index = RSCache_BandIndex(reg, type->bindings[b].name);

        if( index < 0 || !RSCache_PresenceHas(&stated.present, index) )
            continue;
        memcpy(&value, object + type->bindings[b].offset, sizeof(value));
        if( value != 3 * (b + 1) )
        {
            printf("servercodec:   %s.%s read back %d, stated %d\n", type->name,
                   type->bindings[b].name, value, 3 * (b + 1));
            wrong++;
        }
    }
    snprintf(what, sizeof(what), "%s: every bound field lands in its own member", type->name);
    check(wrong == 0, what);
    free(object);
}

static void
check_against_register(const char* dir)
{
    int count = 0;
    const struct ToriRSServerBandType* types = ToriRSServer_ServerTypes(&count);

    check(count > 0, "at least one type has a server band");
    for( int i = 0; i < count; i++ )
    {
        struct RSCache_Register reg;
        char what[160];

        ContentFields_Load(&reg, dir, types[i].name);
        printf("servercodec: %s — register declares %d band field(s), the server binds %d\n",
               types[i].name, reg.band_count, types[i].binding_count);
        snprintf(what, sizeof(what), "%s: fields/%s.ini was read", types[i].name, types[i].name);
        check(reg.from_file, what);
        snprintf(what, sizeof(what), "%s: the register agrees with the bindings, both ways",
                 types[i].name);
        check(ToriRSServer_ServerCheck(&types[i], &reg) == 0, what);
        check_type_round_trip(&types[i], &reg);
    }

    /* The overlay: the tree's file is laid over the defaults, not instead of
     * them. `name` is declared by the defaults for both types and by neither of
     * the tree's files; it must still be there, or an overlay's `name=` stops
     * being a client key and becomes an unknown one. */
    {
        struct RSCache_Register reg;
        const struct RSCache_RegisterField* name;

        ContentFields_Load(&reg, dir, "npc");
        name = RSCache_RegisterFind(&reg, "name");
        check(name && name->scope == RSCACHE_REGISTER_SCOPE_CLIENT,
              "npc: a default-only field (`name`) survives the tree's file");
        check(reg.rejected == 0, "npc: the defaults plus the tree's file parse with no rejected row");
        ContentFields_Load(&reg, dir, "loc");
        name = RSCache_RegisterFind(&reg, "name");
        check(name && name->scope == RSCACHE_REGISTER_SCOPE_CLIENT,
              "loc: a default-only field (`name`) survives the tree's file");
        check(reg.rejected == 0, "loc: the defaults plus the tree's file parse with no rejected row");
    }

    /* A u4 loc id past u2 survives: why the register says u4 for next_loc_stage. */
    {
        const struct ToriRSServerBandType* loc = ToriRSServer_ServerTypeFor("loc");
        struct RSCache_Register reg;
        struct RSCache_BandRecord stated;
        struct RSCache_BandRecord back;
        struct ToriRSServerLocDef def;
        uint8_t out[64];
        uint32_t written;

        check(loc != NULL, "the registry answers for `loc`");
        if( !loc )
            return;
        ContentFields_Load(&reg, dir, "loc");
        RSCache_BandRecordReset(&stated);
        RSCache_BandRecordSet(&stated, RSCache_BandIndex(&reg, "next_loc_stage"), 62000);
        written = RSCache_BandEncode(&reg, &stated, out, sizeof(out));
        memset(&def, 0, sizeof(def));
        def.next_loc_stage = -1;
        check(written == 6, "a loc band is one u4 field plus the terminator");
        ToriRSServer_ServerDecode(loc, &reg, &back, &def, out, (int)written);
        check(def.next_loc_stage == 62000, "a loc id past 65535 survives");
    }
}

/** The defaults alone (a tree with no fields/) declare no band: the band is the
 *  tree's to declare, so cachepack and the server read the same one. */
static void
check_defaults(void)
{
    struct RSCache_Register reg;

    check(ContentFields_Defaults(&reg, "npc") > 0 && reg.band_count == 0 && reg.rejected == 0,
          "the npc defaults parse cleanly and carry no server opcode");
    check(RSCache_RegisterFind(&reg, "name") &&
              RSCache_RegisterFind(&reg, "name")->scope == RSCACHE_REGISTER_SCOPE_CLIENT,
          "npc `name` is a client-scoped default");
    check(ContentFields_Defaults(&reg, "loc") > 0 && reg.band_count == 0 && reg.rejected == 0,
          "the loc defaults parse cleanly and carry no server opcode");
    check(RSCache_RegisterFind(&reg, "next_loc_stage") &&
              strcmp(RSCache_RegisterFind(&reg, "next_loc_stage")->param_name,
                     "next_loc_stage") == 0 &&
              strcmp(RSCache_RegisterFind(&reg, "next_loc_stage")->ref, "loc") == 0,
          "loc `next_loc_stage` binds its param and resolves through loc");
}

int
main(
    int argc,
    char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "OSRS-Content/osrs239-content";

    check_presence();
    check_check();
    check_defaults();
    check_against_register(dir);

    if( g_failures )
    {
        printf("servercodec: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("servercodec: all %d checks passed\n", g_checks);
    return 0;
}
