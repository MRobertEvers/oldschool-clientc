/*
 * A loc / npc / obj record from its text block (editor/editor_recdef.h): what
 * the editor's client draws a lane-made record from.
 *
 *   make -C src test-editor-recdef
 *
 * The loc is held to the CACHE's answer: the same record written here as the
 * client codec's own bytes (a pre-237 OldSchool profile, so the stream is the
 * classic u16 one), decoded by the cache decoder and the loaders' adaptor,
 * must equal the text's decode field for field (Editor_RecDefLocDiff). The
 * npc and the obj are held to the values their lines state. Content-free.
 */
#include "editor/editor_recdef.h"

#include "engine/torirs_location_from_rscache.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(ok, ...)                                                                            \
    do                                                                                            \
    {                                                                                             \
        g_checks++;                                                                               \
        if( !(ok) )                                                                               \
        {                                                                                         \
            g_failures++;                                                                         \
            printf("editor-recdef: FAILED %s:%d: ", __FILE__, __LINE__);                          \
            printf(__VA_ARGS__);                                                                  \
            printf("\n");                                                                         \
        }                                                                                         \
    } while( 0 )

static struct RSCache
profile(void)
{
    struct RSCache p = RSCache_ProfileZero();

    p.game = RSCACHE_GAME_OLDSCHOOL;
    p.epoch = RSCACHE_EPOCH_DAT2;
    p.revision = 230;
    return p;
}

/* The names the blocks below spell, as a catalog would answer them. */
static int g_resolve_calls;

static int
resolve(
    void* user,
    const char* type,
    const char* name)
{
    (void)user;
    g_resolve_calls++;
    if( strcmp(type, "seq") == 0 && strcmp(name, "crate_shake") == 0 )
        return 1234;
    if( strcmp(type, "seq") == 0 && strcmp(name, "human_ready") == 0 )
        return 808;
    if( strcmp(type, "seq") == 0 && strcmp(name, "human_walk_f") == 0 )
        return 819;
    if( strcmp(type, "seq") == 0 && strcmp(name, "human_walk_b") == 0 )
        return 820;
    if( strcmp(type, "loc") == 0 && strcmp(name, "crate_open") == 0 )
        return 100;
    if( strcmp(type, "loc") == 0 && strcmp(name, "crate_empty") == 0 )
        return 101;
    if( strcmp(type, "varbit") == 0 && strcmp(name, "crate_state") == 0 )
        return 4153;
    if( strcmp(type, "obj") == 0 && strcmp(name, "brass_key_noted") == 0 )
        return 984;
    if( strcmp(type, "category") == 0 && strcmp(name, "crates") == 0 )
        return 497;
    return -1;
}

/* ---- loc: the text against the cache's own decode ------------------------------ */

/* opcodes (dat2_config_loc.c): 5 models, 2 name, 3 desc, 14 width, 15 length,
 * 17 blockwalk=0 (and blockrange=0), 19 active, 24 anim, 28 wallwidth,
 * 29 ambient, 30+n op n+1, 39 contrast (x25), 40 recol, 41 retex,
 * 61 category, 62 mirror, 64 shadow=no, 65/66/67 resize, 69 forceapproach,
 * 70/71/72 offset, 75 raiseobject, 77 multi (varbit, varp, count, count+1
 * locs; 0xFFFF is none), 0 end. u16s are big-endian. */
static const uint8_t k_loc_bytes[] = {
    5, 2, 0x08, 0x5D, 0x08, 0x5E,               /* models=2141,2142 */
    2, 'C', 'r', 'a', 't', 'e', 0,              /* name=Crate */
    3, 'A', ' ', 'b', 'o', 'x', '.', 0,         /* desc=A box. */
    14, 2,                                      /* width=2 */
    15, 3,                                      /* length=3 */
    17,                                         /* blockwalk=0, blockrange=0 */
    19, 1,                                      /* active=1 */
    24, 0x04, 0xD2,                             /* anim=1234 */
    28, 8,                                      /* wallwidth=8 */
    29, 10,                                     /* ambient=10 */
    30, 'h', 'i', 'd', 'd', 'e', 'n', 0,        /* op1=hidden */
    32, 'S', 'e', 'a', 'r', 'c', 'h', 0,        /* op3=Search */
    39, 2,                                      /* contrast=50 */
    40, 2, 0x16, 0x14, 0x15, 0x1C, 0x16, 0x18, 0x15, 0x18, /* recol 5652>5404, 5656>5400 */
    41, 1, 0x00, 0x05, 0x00, 0x09,              /* retex 5>9 */
    61, 0x01, 0xF1,                             /* category=497 */
    62,                                         /* mirror=1 */
    64,                                         /* shadow=no */
    65, 0x00, 0x64,                             /* resizex=100 */
    66, 0x00, 0x6E,                             /* resizey=110 */
    67, 0x00, 0x78,                             /* resizez=120 */
    69, 3,                                      /* forceapproach=3 */
    70, 0x00, 0x10,                             /* offsetx=16 */
    71, 0xFF, 0xF0,                             /* offsety=-16 */
    72, 0x00, 0x20,                             /* offsetz=32 */
    75, 1,                                      /* raiseobject=1 */
    77, 0x10, 0x39, 0xFF, 0xFF, 2, 0x00, 0x64, 0xFF, 0xFF, 0x00, 0x65, /* multi */
    0,
};

static const char k_loc_text[] = "[my_crate]\n"
                                 "name=Crate\n"
                                 "desc=A box.\n"
                                 "shape=default\n"
                                 "models=2141,2142\n"
                                 "recol1s=5652\n"
                                 "recol1d=5404\n"
                                 "recol2s=5656\n"
                                 "recol2d=5400\n"
                                 "retex1s=5\n"
                                 "retex1d=9\n"
                                 "width=2\n"
                                 "length=3\n"
                                 "blockwalk=0\n"
                                 "blockwalkalso=default\n"
                                 "blockrange=0\n"
                                 "active=1\n"
                                 "sharelight=default\n"
                                 "// a comment is not a field\n"
                                 "anim=crate_shake\n"
                                 "wallwidth=8\n"
                                 "ambient=10\n"
                                 "op1=hidden\n"
                                 "op2=default\n"
                                 "op3=Search\n"
                                 "contrast=50\n"
                                 "category=crates\n"
                                 "mirror=1\n"
                                 "shadow=no\n"
                                 "resizex=100\n"
                                 "resizey=110\n"
                                 "resizez=120\n"
                                 "forceapproach=3\n"
                                 "offsetx=16\n"
                                 "offsety=-16\n"
                                 "offsetz=32\n"
                                 "raiseobject=1\n"
                                 "multivarbit=crate_state\n"
                                 "multivarp=-1\n"
                                 "multiloc1=crate_open\n"
                                 "multiloc2=-1\n"
                                 "multiloc3=crate_empty\n"
                                 "multiloc4=-1\n"
                                 "param=default\n"
                                 "\n";

static void
test_loc_against_cache(void)
{
    struct RSCache p = profile();
    struct Editor_RecDefContext ctx = { &p, resolve, NULL };
    struct Editor_RecDefReport report;
    struct RSCache_Dat2ConfigLoc* raw =
        RSCache_Dat2ConfigLocNewDecodeProfile(&p, (char*)k_loc_bytes, (int)sizeof(k_loc_bytes));
    struct ToriRS_Location* cache;
    struct ToriRS_Location* text;
    char fields[256];
    int differs;

    assert(raw);
    CHECK(raw->_consumed == (int)sizeof(k_loc_bytes), "the cache decoder read the whole test record (%d of %d)",
        raw->_consumed, (int)sizeof(k_loc_bytes));
    raw->_id = 70000;
    cache = ToriRS_LocationFromRSCacheDat2(70000, raw);
    RSCache_Dat2ConfigLocFree(raw);

    text = Editor_RecDefLoc(&ctx, 70000, k_loc_text, strlen(k_loc_text), &report);
    CHECK(text != NULL, "a block with fields is a record");
    CHECK(report.bad == 0, "every line was read (%d bad, first %s)", report.bad, report.first_bad);
    CHECK(report.skipped == 0, "nothing skipped: `param=default` states nothing (%d)", report.skipped);
    differs = Editor_RecDefLocDiff(text, cache, fields, sizeof(fields));
    CHECK(differs == 0, "the text decodes to the cache's own record (%d differ: %s)", differs, fields);

    /* The things the comparison would not tell apart from a shared mistake. */
    CHECK(text->id == 70000, "the id is the one asked for (%d)", text->id);
    CHECK(strcmp(text->name, "Crate") == 0, "name (%s)", text->name);
    CHECK(text->shapes == NULL && text->shapes_and_model_count == 1 && text->lengths[0] == 2
              && text->models[0][0] == 2141 && text->models[0][1] == 2142,
        "models= is one list with no shapes");
    CHECK(text->actions[0][0] == '\0' && strcmp(text->actions[2], "Search") == 0,
        "a hidden op is no menu row; op3 is (%s)", text->actions[2]);
    CHECK(text->is_interactive == 1, "active=1");
    CHECK(text->blocks_walk == 0 && text->blocks_projectiles == 0, "blockwalk=0 clears walk and range (%d, %d)",
        text->blocks_walk, text->blocks_projectiles);
    CHECK(text->seq_id == 1234, "anim= by name through the resolver (%d)", text->seq_id);
    CHECK(text->category == 497, "category= by name (%d)", text->category);
    CHECK(text->transform_varbit == 4153 && text->transform_varp == -1 && text->transform_count == 4
              && text->transforms[0] == 100 && text->transforms[1] == -1 && text->transforms[2] == 101
              && text->transforms[3] == -1,
        "the multiloc table, its default last");
    CHECK(text->shadowed == 0 && text->mirrored == 1, "shadow=no, mirror=1");
    ToriRS_LocationFree(text);
    ToriRS_LocationFree(cache);
}

/* A loc stated by shape rows, the walk flags' other spellings, defaults. */
static void
test_loc_shapes_and_defaults(void)
{
    static const uint8_t bytes[] = {
        1, 2, 0x03, 0xE8, 10, 0x03, 0xE9, 22, /* shape1=10,1000 shape2=22,1001 */
        2, 'P', 'o', 's', 't', 0,
        18,                                   /* blockrange=0 */
        27,                                   /* blockwalk=1 */
        0,
    };
    static const char block[] = "[post]\r\n"
                                "name=Post\r\n"
                                "shape1=10,1000\r\n"
                                "shape2=22,1001\r\n"
                                "models=default\r\n"
                                "blockwalk=1\r\n"
                                "blockrange=0\r\n";
    struct RSCache p = profile();
    struct Editor_RecDefContext ctx = { &p, NULL, NULL };
    struct Editor_RecDefReport report;
    struct RSCache_Dat2ConfigLoc* raw = RSCache_Dat2ConfigLocNewDecodeProfile(&p, (char*)bytes, (int)sizeof(bytes));
    struct ToriRS_Location* cache;
    struct ToriRS_Location* text;
    char fields[256];
    int differs;

    assert(raw);
    cache = ToriRS_LocationFromRSCacheDat2(7, raw);
    RSCache_Dat2ConfigLocFree(raw);
    text = Editor_RecDefLoc(&ctx, 7, block, strlen(block), &report);
    CHECK(text != NULL, "CRLF lines read");
    CHECK(report.bad == 0, "no bad line (%d, %s)", report.bad, report.first_bad);
    differs = Editor_RecDefLocDiff(text, cache, fields, sizeof(fields));
    CHECK(differs == 0, "shape rows decode to the cache's record (%d differ: %s)", differs, fields);
    CHECK(text->shapes && text->shapes_and_model_count == 2 && text->shapes[0] == 10 && text->shapes[1] == 22
              && text->models[1][0] == 1001,
        "two shape rows, a model each");
    CHECK(text->blocks_walk == 1 && text->blocks_projectiles == 0, "blockwalk=1 with blockrange=0 (%d, %d)",
        text->blocks_walk, text->blocks_projectiles);
    /* Derived by the decoder's own finish: a centrepiece with a model is
     * active though nothing says so; nothing else moves off its default. */
    CHECK(text->is_interactive == 1, "active derived from the first shape (%d)", text->is_interactive);
    CHECK(text->size_x == 1 && text->size_z == 1 && text->resize_x == 128 && text->seq_id == -1
              && text->wall_width == 16 && text->shadowed == 1,
        "unstated fields keep the client defaults");
    ToriRS_LocationFree(text);
    ToriRS_LocationFree(cache);
}

static void
test_loc_bad_and_empty(void)
{
    static const char block[] = "[odd]\n"
                                "name=Odd\n"
                                "models=77\n"
                                "width=wide\n"
                                "anim=no_such_anim\n"
                                "length=2\n"
                                "subop=1,2,Peek\n";
    struct RSCache p = profile();
    struct Editor_RecDefContext ctx = { &p, resolve, NULL };
    struct Editor_RecDefReport report;
    struct ToriRS_Location* text = Editor_RecDefLoc(&ctx, 9, block, strlen(block), &report);

    CHECK(text != NULL, "a record with bad lines is still a record");
    CHECK(report.bad == 2, "two lines could not be read (%d)", report.bad);
    CHECK(strcmp(report.first_bad, "width=wide") == 0, "the first is named (%s)", report.first_bad);
    CHECK(report.skipped == 1, "a sub op is counted as not carried (%d)", report.skipped);
    CHECK(text->size_x == 1 && text->seq_id == -1 && text->size_z == 2,
        "a bad line leaves its field at the default; the lines after it are read (%d, %d, %d)", text->size_x,
        text->seq_id, text->size_z);
    ToriRS_LocationFree(text);

    CHECK(Editor_RecDefLoc(&ctx, 9, "", 0, &report) == NULL, "an empty document is no record");
    CHECK(Editor_RecDefLoc(&ctx, 9, "[gone]\n\n", 8, &report) == NULL, "a header with no field is no record");
    CHECK(Editor_RecDefNpc(&ctx, 9, "", 0, NULL) == NULL, "nor an npc");
    CHECK(Editor_RecDefObj(&ctx, 9, "", 0, NULL) == NULL, "nor an obj");
}

/* ---- npc --------------------------------------------------------------------- */

static void
test_npc(void)
{
    static const char block[] = "[my_man]\n"
                                "name=Man\n"
                                "desc=One of many.\n"
                                "model1=215\n"
                                "model2=246\n"
                                "head1=53\n"
                                "recol1s=6798\n"
                                "recol1d=4888\n"
                                "size=2\n"
                                "readyanim=human_ready\n"
                                "walkanim=human_walk_f\n"
                                "walkanim_b=human_walk_b\n"
                                "walkanim_l=821\n"
                                "walkanim_r=822\n"
                                "op1=Talk-to\n"
                                "op2=Hidden\n"
                                "op3=Pickpocket\n"
                                "vislevel=2\n"
                                "category=266\n"
                                "stat1=1\n"
                                "stat4=7\n"
                                "minimap=no\n"
                                "alwaysontop=yes\n"
                                "resizeh=140\n"
                                "turnspeed=0\n"
                                "headicon1=440,3\n"
                                "param=stabdefence,-21\n";
    struct RSCache p = profile();
    struct Editor_RecDefContext ctx = { &p, resolve, NULL };
    struct Editor_RecDefReport report;
    struct ToriRS_Npctype* npc = Editor_RecDefNpc(&ctx, 27407, block, strlen(block), &report);

    CHECK(npc != NULL, "an npc block is a record");
    CHECK(report.bad == 0, "every line read (%d, %s)", report.bad, report.first_bad);
    CHECK(report.skipped == 1, "the param is not carried, and is counted (%d)", report.skipped);
    CHECK(npc->id == 27407 && strcmp(npc->name, "Man") == 0 && strcmp(npc->desc, "One of many.") == 0, "id, name, desc");
    CHECK(npc->models_count == 2 && npc->models[0] == 215 && npc->models[1] == 246, "models");
    CHECK(npc->heads_count == 1 && npc->heads[0] == 53, "chat head");
    CHECK(npc->recolor_count == 1 && npc->recolors_from[0] == 6798 && npc->recolors_to[0] == 4888, "recolour pair");
    CHECK(npc->size == 2 && npc->combat_level == 2 && npc->category == 266, "size, level, category");
    CHECK(npc->readyanim == 808 && npc->walkanim == 819 && npc->walkanim_b == 820, "anims by name (%d %d %d)",
        npc->readyanim, npc->walkanim, npc->walkanim_b);
    /* The adaptor's names for the two turns are the decode struct's
     * (rotate_left / rotate_right), not crossed. */
    CHECK(npc->walkanim_l == 821 && npc->walkanim_r == 822, "the turns by number (%d %d)", npc->walkanim_l,
        npc->walkanim_r);
    CHECK(strcmp(npc->actions[0], "Talk-to") == 0 && npc->actions[1][0] == '\0'
              && strcmp(npc->actions[2], "Pickpocket") == 0,
        "ops; Hidden is an empty slot");
    CHECK(!npc->minimap_visible && npc->alwaysontop, "minimap=no, alwaysontop=yes");
    CHECK(npc->width_scale == 140 && npc->height_scale == 128, "resizeh stated, resizev default (%d, %d)",
        npc->width_scale, npc->height_scale);
    CHECK(npc->turn_speed == 0, "turnspeed=0 is a value, not a default (%d)", npc->turn_speed);
    CHECK(npc->head_icon_group == 440 && npc->head_icon_index == 3, "head icon (%d, %d)", npc->head_icon_group,
        npc->head_icon_index);
    CHECK(npc->interactable && npc->transform_varbit == -1 && npc->runanim == -1, "unstated fields keep defaults");
    {
        struct ToriRS_Npctype* again = Editor_RecDefNpc(&ctx, 27407, block, strlen(block), NULL);
        char fields[128];
        CHECK(Editor_RecDefNpcDiff(npc, again, fields, sizeof(fields)) == 0, "two decodes of a block agree (%s)", fields);
        again->size = 3;
        again->models[1] = 1;
        CHECK(Editor_RecDefNpcDiff(npc, again, fields, sizeof(fields)) == 2 && strcmp(fields, "size models") == 0,
            "the comparison names what differs (%s)", fields);
        CHECK(Editor_RecDefNpcDiff(npc, again, fields, 6) == 2 && strlen(fields) < 6, "and cuts to its buffer (%s)",
            fields);
        ToriRS_NpctypeFree(again);
    }
    ToriRS_NpctypeFree(npc);
}

/* ---- obj --------------------------------------------------------------------- */

static void
test_obj(void)
{
    static const char block[] = "[my_key]\n"
                                "name=Brass key\n"
                                "desc=It opens something.\n"
                                "model=2372\n"
                                "2dzoom=860\n"
                                "2dxan=460\n"
                                "2dyan=12\n"
                                "2dxof=-1\n"
                                "cost=1\n"
                                "stackable=1\n"
                                "members=yes\n"
                                "op3=Take\n"
                                "ifop1=Inspect\n"
                                "certlink=brass_key_noted\n"
                                "recol1s=10\n"
                                "recol1d=20\n"
                                "weight=10\n"
                                "tradeable=no\n"
                                "countobj1=brass_key_noted,5\n";
    struct RSCache p = profile();
    struct Editor_RecDefContext ctx = { &p, resolve, NULL };
    struct Editor_RecDefReport report;
    struct ToriRS_Objtype* obj = Editor_RecDefObj(&ctx, 49001, block, strlen(block), &report);

    CHECK(obj != NULL, "an obj block is a record");
    CHECK(report.bad == 0, "every line read (%d, %s)", report.bad, report.first_bad);
    CHECK(obj->id == 49001 && strcmp(obj->name, "Brass key") == 0 && strcmp(obj->desc, "It opens something.") == 0,
        "id, name, examine");
    CHECK(obj->inventory_model_id == 2372 && obj->zoom2d == 860 && obj->xan2d == 460 && obj->yan2d == 12
              && obj->offset_x2d == -1,
        "the icon's model and pose");
    CHECK(obj->stackable == 1 && obj->members == 1 && obj->cost == 1, "stackable, members, cost");
    CHECK(strcmp(obj->ground_actions[2], "Take") == 0 && strcmp(obj->inv_actions[0], "Inspect") == 0,
        "ground op and inventory op");
    CHECK(obj->cert_link == 984, "a link by name (%d)", obj->cert_link);
    CHECK(obj->count_obj[0] == 984 && obj->count_co[0] == 5, "countobj (%d, %d)", obj->count_obj[0], obj->count_co[0]);
    CHECK(obj->recolor_count == 1 && obj->recolors_from[0] == 10 && obj->recolors_to[0] == 20, "recolour pair");
    CHECK(obj->resize_x == 128 && obj->manwear == -1 && obj->cert_template == -1, "unstated fields keep defaults");
    ToriRS_ObjtypeFree(obj);

    /* No name stated: the decoder's own default, as the cache gives it. */
    obj = Editor_RecDefObj(&ctx, 49002, "[bare]\nmodel=5\n", 15, NULL);
    CHECK(obj && strcmp(obj->name, "null") == 0, "an unnamed obj is the decoder's \"null\"");
    ToriRS_ObjtypeFree(obj);
}

/* ---- the ledger ----------------------------------------------------------------- */

static void
test_ledger(void)
{
    static const char ledger[] = "// The `loc` namespace: ids this tree allocated.\n"
                                 "//\n"
                                 "70000=my_crate\n"
                                 "70001=my_other_crate   // a note\r\n"
                                 "7000=short_id\n"
                                 "not=a_line\n"
                                 "70002=last";
    char name[64];

    CHECK(Editor_RecDefLedgerName(ledger, strlen(ledger), 70000, name, sizeof(name)) && strcmp(name, "my_crate") == 0,
        "an id's name (%s)", name);
    CHECK(Editor_RecDefLedgerName(ledger, strlen(ledger), 70001, name, sizeof(name))
              && strcmp(name, "my_other_crate") == 0,
        "the name ends at a space (%s)", name);
    CHECK(Editor_RecDefLedgerName(ledger, strlen(ledger), 70002, name, sizeof(name)) && strcmp(name, "last") == 0,
        "the last line without a newline (%s)", name);
    CHECK(Editor_RecDefLedgerName(ledger, strlen(ledger), 7000, name, sizeof(name)) && strcmp(name, "short_id") == 0,
        "7000 is not a prefix of 70000 (%s)", name);
    CHECK(!Editor_RecDefLedgerName(ledger, strlen(ledger), 70003, name, sizeof(name)) && name[0] == '\0',
        "an id the ledger lacks");
    CHECK(!Editor_RecDefLedgerName("", 0, 1, name, sizeof(name)), "an empty ledger");
    CHECK(!Editor_RecDefLedgerName(ledger, strlen(ledger), 70001, name, 4), "a name that does not fit is not cut");
}

int
main(void)
{
    test_loc_against_cache();
    test_loc_shapes_and_defaults();
    test_loc_bad_and_empty();
    test_npc();
    test_obj();
    test_ledger();
    CHECK(g_resolve_calls > 0, "names went through the resolver");
    printf("editor-recdef: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
