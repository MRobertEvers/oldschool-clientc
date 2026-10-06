/*
 * The field register and the server band codec (rscache_register.h,
 * rscache_band.h): one parser and one codec for both the writer (cachepack) and
 * the reader (the game server), with presence recorded rather than inferred.
 */

#include "rscache.h"
#include "rscache_test.h"

#include "rscache_band.h"
#include "rscache_register.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char k_npc_register[] =
    "; a comment\n"
    "[npc]\n"
    "records = client\n"
    "\n"
    "[npc.desc]\n"
    "scope = client\n"
    "client = native\n"
    "\n"
    "[npc.hitpoints]\n"
    "server = opcode:77:u2\n"
    "\n"
    "[npc.death_drop]\n"
    "server = opcode:151:u4\n"
    "ref = obj\n"
    "text = param\n"
    "\n"
    "[npc.attackrate]\n"
    "server = opcode:150:u1\n"
    "client = param:attackrate\n"
    "\n"
    "[loc.next_loc_stage]\n"
    "server = opcode:150:u4\n"
    "\n"
    "; merged by name: a second block for an existing field adds rows to it\n"
    "[npc.hitpoints]\n"
    "scope = server\n";

struct NpcLike
{
    int hitpoints;
    int death_drop;
    int attackrate;
    int untouched;
};

int
main(void)
{
    struct RSCache_Register reg;
    struct RSCache_BandRecord record;
    struct RSCache_BandRecord back;
    uint8_t out[128];
    uint32_t written;
    int hp, drop, rate;

    RSCACHE_TEST_GROUP("register: parse, merge, band order");
    RSCACHE_CHECK_EQ(RSCache_RegisterParse(&reg, "npc", k_npc_register, sizeof(k_npc_register) - 1),
                     4);
    RSCACHE_CHECK_EQ(reg.rejected, 0);
    RSCACHE_CHECK_EQ(reg.records_client, 1);
    RSCACHE_CHECK_EQ(reg.band_count, 3);
    /* Band first, ascending: 77, 150, 151; then desc. */
    RSCACHE_CHECK_STR_EQ(reg.entries[0].name, "hitpoints");
    RSCACHE_CHECK_STR_EQ(reg.entries[1].name, "attackrate");
    RSCACHE_CHECK_STR_EQ(reg.entries[2].name, "death_drop");
    RSCACHE_CHECK_STR_EQ(reg.entries[3].name, "desc");
    RSCACHE_CHECK_EQ(RSCache_RegisterFind(&reg, "death_drop")->text, RSCACHE_REGISTER_TEXT_PARAM);
    RSCACHE_CHECK_STR_EQ(RSCache_RegisterFind(&reg, "death_drop")->ref, "obj");
    RSCACHE_CHECK_EQ(RSCache_RegisterFind(&reg, "attackrate")->client, RSCACHE_REGISTER_CLIENT_PARAM);
    RSCACHE_CHECK_STR_EQ(RSCache_RegisterFind(&reg, "attackrate")->param_name, "attackrate");
    RSCACHE_CHECK_EQ(RSCache_RegisterFind(&reg, "desc")->client, RSCACHE_REGISTER_CLIENT_NATIVE);
    RSCACHE_CHECK(RSCache_RegisterFind(&reg, "next_loc_stage") == NULL); /* another type's */
    RSCACHE_CHECK_EQ(RSCache_RegisterCheck(&reg), 0);

    RSCACHE_TEST_GROUP("band: a stated 0 and a stated -1 survive; an unstated field is absent");
    hp = RSCache_BandIndex(&reg, "hitpoints");
    drop = RSCache_BandIndex(&reg, "death_drop");
    rate = RSCache_BandIndex(&reg, "attackrate");
    RSCache_BandRecordReset(&record);
    RSCache_BandRecordSet(&record, hp, 0);
    RSCache_BandRecordSet(&record, drop, -1);
    written = RSCache_BandEncode(&reg, &record, out, sizeof(out));
    {
        static const uint8_t want[] = { 77, 0, 0, 151, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        RSCACHE_CHECK_EQ(written, sizeof(want));
        RSCACHE_CHECK(memcmp(out, want, sizeof(want)) == 0);
    }
    RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, out, (int)written), (int)written);
    RSCACHE_CHECK(RSCache_PresenceHas(&back.present, hp));
    RSCACHE_CHECK_EQ(back.values[hp], 0);
    RSCACHE_CHECK(RSCache_PresenceHas(&back.present, drop));
    RSCACHE_CHECK_EQ(back.values[drop], -1);
    RSCACHE_CHECK(!RSCache_PresenceHas(&back.present, rate));

    RSCACHE_TEST_GROUP("band: nothing stated is a bare terminator");
    RSCache_BandRecordReset(&record);
    RSCACHE_CHECK_EQ(RSCache_BandEncode(&reg, &record, out, sizeof(out)), 1);
    RSCACHE_CHECK_EQ(out[0], 0);

    RSCACHE_TEST_GROUP("band: an undeclared opcode stops the decode");
    {
        static const uint8_t foreign[] = { 77, 0, 5, 199, 1, 0 };
        RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, foreign, (int)sizeof(foreign)), -1);
    }

    RSCACHE_TEST_GROUP("band: a payload cut short is unreadable, not a stated 0");
    {
        static const uint8_t truncated[] = { 77, 150 }; /* 77 is u2: one byte short */
        RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, truncated, (int)sizeof(truncated)), -1);
    }

    RSCACHE_TEST_GROUP("band: widths");
    RSCACHE_CHECK(RSCache_BandFits(&reg, rate, 255));
    RSCACHE_CHECK(!RSCache_BandFits(&reg, rate, 256));
    RSCACHE_CHECK(!RSCache_BandFits(&reg, hp, -1));
    RSCACHE_CHECK(RSCache_BandFits(&reg, drop, -1));

    RSCACHE_TEST_GROUP("binding: only stated fields land; a reader's member keeps its seed");
    {
        static const struct RSCache_BandBinding bindings[] = {
            { "hitpoints", offsetof(struct NpcLike, hitpoints), sizeof(int), NULL },
            { "death_drop", offsetof(struct NpcLike, death_drop), sizeof(int), NULL },
            { "attackrate", offsetof(struct NpcLike, attackrate), sizeof(int), NULL },
        };
        struct NpcLike npc = { 10, 526, 4, 7 };

        RSCACHE_CHECK_EQ(RSCache_BandBindingCheck(&reg, bindings, 3), 0);
        RSCache_BandRecordReset(&record);
        RSCache_BandRecordSet(&record, hp, 0);
        RSCache_BandRecordSet(&record, drop, -1);
        RSCACHE_CHECK_EQ(RSCache_BandBindingApply(&reg, bindings, 3, &record, &npc), 2);
        RSCACHE_CHECK_EQ(npc.hitpoints, 0);
        RSCACHE_CHECK_EQ(npc.death_drop, -1);
        RSCACHE_CHECK_EQ(npc.attackrate, 4); /* not stated: the seed stands */
        RSCACHE_CHECK_EQ(npc.untouched, 7);
    }

    RSCACHE_TEST_GROUP("binding: a field the band does not carry is a reader bug");
    {
        static const struct RSCache_BandBinding stray[] = {
            { "magic", 0, sizeof(int), NULL },
        };
        RSCACHE_CHECK_EQ(RSCache_BandBindingCheck(&reg, stray, 1), 1);
    }

    RSCACHE_TEST_GROUP("register: malformed rows are refused, never guessed");
    {
        static const char bad[] = "[npc.a]\n"
                                  "server = opcode:12:u1\n" /* the client band */
                                  "[npc.b]\n"
                                  "server = opcode:90:u3\n" /* no such width */
                                  "[npc.c]\n"
                                  "text = sometimes\n"
                                  "[npc.d]\n"
                                  "server = opcode:90:u1\n"
                                  "[npc.e]\n"
                                  "server = opcode:90:u2\n"; /* shares 90 with d */
        RSCache_RegisterParse(&reg, "npc", bad, sizeof(bad) - 1);
        RSCACHE_CHECK_EQ(reg.rejected, 3);
        RSCACHE_CHECK_EQ(RSCache_RegisterCheck(&reg), 4); /* the 3, plus the shared opcode */
    }

    RSCACHE_TEST_GROUP("band: strings and lists of typed tuples");
    {
        static const char shop[] = "[inv.owner]\n"
                                   "server = opcode:90:string\n"
                                   "[inv.stock]\n"
                                   "server = opcode:91:list\n"
                                   "type = obj,int,string\n"
                                   "text = indexed\n";
        struct RSCache_BandValue tuple[3];
        uint8_t big[256];
        int owner, stock;

        RSCACHE_CHECK_EQ(RSCache_RegisterParse(&reg, "inv", shop, sizeof(shop) - 1), 2);
        RSCACHE_CHECK_EQ(RSCache_RegisterCheck(&reg), 0);
        owner = RSCache_BandIndex(&reg, "owner");
        stock = RSCache_BandIndex(&reg, "stock");
        RSCACHE_CHECK(RSCache_BandElementIsString(&reg, stock, 2));
        RSCACHE_CHECK(!RSCache_BandElementIsString(&reg, stock, 0));

        RSCache_BandRecordReset(&record);
        RSCache_BandRecordSetString(&record, owner, "Bob, the axeman");
        tuple[0].i = 1351;
        tuple[0].s = NULL;
        tuple[1].i = 10;
        tuple[1].s = NULL;
        tuple[2].i = 0;
        tuple[2].s = "first";
        RSCache_BandRecordAppendTuple(&record, stock, tuple, 3);
        tuple[0].i = -1;
        tuple[2].s = "";
        RSCache_BandRecordAppendTuple(&record, stock, tuple, 3);
        written = RSCache_BandEncode(&reg, &record, big, sizeof(big));
        RSCACHE_CHECK(written <= RSCache_BandEncodeBound(&reg, &record));
        RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, big, (int)written), (int)written);
        RSCACHE_CHECK_STR_EQ(back.strings[owner], "Bob, the axeman");
        RSCACHE_CHECK(back.lists[stock] != NULL);
        RSCACHE_CHECK_EQ(back.lists[stock]->count, 2);
        RSCACHE_CHECK_EQ(back.lists[stock]->items[0].i, 1351);
        RSCACHE_CHECK_EQ(back.lists[stock]->items[1].i, 10);
        RSCACHE_CHECK_STR_EQ(back.lists[stock]->items[2].s, "first");
        RSCACHE_CHECK_EQ(back.lists[stock]->items[3].i, -1);
        RSCACHE_CHECK_STR_EQ(back.lists[stock]->items[5].s, "");
        RSCache_BandRecordFree(&back);

        /* A list stated with no tuples is present, with a count of 0. */
        RSCache_BandRecordFree(&record);
        RSCache_BandRecordAppendTuple(&record, stock, NULL, 0);
        written = RSCache_BandEncode(&reg, &record, big, sizeof(big));
        RSCACHE_CHECK_EQ(written, 4); /* 91, u2 0, terminator */
        RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, big, (int)written), 4);
        RSCACHE_CHECK(RSCache_PresenceHas(&back.present, stock));
        RSCACHE_CHECK_EQ(back.lists[stock]->count, 0);
        RSCache_BandRecordFree(&back);

        /* A tuple cut short, and a string with no terminator, are unreadable. */
        {
            static const uint8_t short_tuple[] = { 91, 0, 1, 0, 0, 5, 71 };
            static const uint8_t open_string[] = { 90, 'B', 'o', 'b' };
            RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, short_tuple, (int)sizeof(short_tuple)),
                             -1);
            RSCache_BandRecordFree(&back);
            RSCACHE_CHECK_EQ(RSCache_BandDecode(&reg, &back, open_string, (int)sizeof(open_string)),
                             -1);
            RSCache_BandRecordFree(&back);
        }

        /* A list binds through `apply`, never to a member. */
        {
            static const struct RSCache_BandBinding as_member[] = {
                { "stock", 0, sizeof(int), NULL },
            };
            RSCACHE_CHECK_EQ(RSCache_BandBindingCheck(&reg, as_member, 1), 1);
        }
        RSCache_BandRecordFree(&record);
    }

    return rscache_test_report("band");
}
