/*
 * Workbench 6.7, part 1, the pure half (editor/editor_encounters.h,
 * editor/editor_npcstats.h), content-free:
 *
 *   encounters  parse -> emit byte-identical (comments, blank lines, CRLF, a
 *               missing final newline); the grammar's refusals with their
 *               line; format -> parse; the verbs' new text (an add bringing
 *               the header, a replace, a rename, a taken name, a remove
 *               taking the header back) and their splices applied forward
 *               and back; the centre and the npc names
 *   ledger      parse -> emit byte-identical over a generator-shaped file
 *               (aligned keys, notes, CRLF, no final newline); an edit
 *               rewriting only its value, keeping the note's column; a new
 *               key in the key column after the last stat; `source =
 *               authored` set; a removal; a new ledger's header; refusals
 *   derived     max hit and combat level against hand-computed cases
 *   drops       the threshold walk tools/wiki_droptable.py writes, read into
 *               rows with their chances; a call followed; unread lines counted
 *   combat      the four templates, the caster read from a dark wizard file
 *   builder     the step builder's ai trigger: `[ai,<trigger>]` in the
 *               .interaction round trip, `[ai_opplayer2,<npc>]` emitted with
 *               its steps, its name in the trigger list, refused on a loc
 */

#include "editor/builder/builder_emit.h"
#include "editor/builder/builder_model.h"
#include "editor/builder/builder_text.h"
#include "editor/editor_encounters.h"
#include "editor/editor_npcstats.h"
#include "editor/editor_stamps.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- helpers -------------------------------------------------------------------- */

static char*
encounters_emit(const struct Editor_EncountersFile* file)
{
    size_t const n = Editor_EncountersEmit(file, NULL, 0);
    char* out = malloc(n + 1);
    assert(out);
    Editor_EncountersEmit(file, out, n + 1);
    return out;
}

static int
encounters_round_trip(const char* text)
{
    struct Editor_EncountersFile file;
    struct Editor_EncountersParseResult r;
    char* back;
    int same;

    Editor_EncountersInit(&file);
    r = Editor_EncountersParse(&file, text, strlen(text));
    back = encounters_emit(&file);
    same = r.ok && strcmp(back, text) == 0;
    free(back);
    Editor_EncountersFree(&file);
    return same;
}

/** Apply TEXT_SPLICE commands to a copy of `text` (forward), or undo them
 *  (backward: the commands reversed, before and after swapped). */
static char*
apply_splices(
    const char* text,
    const struct Editor_Cmd* commands,
    int count,
    int backward)
{
    size_t length = strlen(text);
    char* buffer = malloc(length + 1);

    assert(buffer);
    memcpy(buffer, text, length + 1);
    for( int k = 0; k < count; k++ )
    {
        const struct Editor_Cmd* c = &commands[backward ? count - 1 - k : k];
        size_t const offset = c->u.text_splice.offset;
        size_t const remove = backward ? c->u.text_splice.after_len : c->u.text_splice.before_len;
        size_t const add = backward ? c->u.text_splice.before_len : c->u.text_splice.after_len;
        char* next = malloc(length - remove + add + 1);
        assert(next);
        assert(offset + remove <= length);
        memcpy(next, buffer, offset);
        memcpy(next + offset, c->u.text_splice.text, add);
        memcpy(next + offset + add, buffer + offset + remove, length - offset - remove);
        length = length - remove + add;
        next[length] = '\0';
        free(buffer);
        buffer = next;
    }
    return buffer;
}

/* ---- encounters ---------------------------------------------------------------- */

static const char k_encounters[] =
    "# ToriRSMapEd encounter sets (editor/encounters.txt).\n"
    "# a hand comment\n"
    "\n"
    "encounter goblin_camp_east | colour 2 | spawn npc:goblin_unarmed_melee_4 3246 3237 0 | spawn npc:goblin_sword_1 "
    "3250 3239 0 | note by the river\n"
    "  encounter   guards |note  the gate   |  spawn npc:guard 3222 3218 0 | spawn obj:bones 3223 3218 0 5\n"
    "// the end\n";

static void
test_encounters_codec(void)
{
    struct Editor_EncountersFile file;
    struct Editor_EncountersParseResult r;
    const struct Editor_Encounter* e;
    struct Editor_Encounter parsed;
    char line[1024];
    int x, z, level;
    char names[4][EDITOR_CMD_NAME_MAX];

    CHECK(encounters_round_trip(k_encounters), "the document round-trips byte for byte");
    CHECK(encounters_round_trip("encounter a | spawn npc:x 1 2 3\r\n# c\r\n"), "CRLF round-trips");
    CHECK(encounters_round_trip("encounter a\n\n# no final newline"), "a missing final newline round-trips");
    CHECK(encounters_round_trip(""), "an empty document round-trips");

    Editor_EncountersInit(&file);
    r = Editor_EncountersParse(&file, k_encounters, strlen(k_encounters));
    CHECK(r.ok, "it parses (line %d)", r.line);
    CHECK(Editor_EncountersCount(&file) == 2, "two sets (%d)", Editor_EncountersCount(&file));
    e = Editor_EncountersNth(&file, 0);
    CHECK(e && strcmp(e->name, "goblin_camp_east") == 0 && e->colour == 2 && e->spawn_count == 2,
        "the first set: name, colour, two spawns");
    CHECK(e && strcmp(e->note, "by the river") == 0, "its note");
    CHECK(e && !e->spawns[0].is_obj && strcmp(e->spawns[0].name, "goblin_unarmed_melee_4") == 0
              && e->spawns[0].x == 3246 && e->spawns[0].z == 3237 && e->spawns[0].level == 0
              && e->spawns[0].count == 1,
        "its first spawn");
    e = Editor_EncountersNth(&file, 1);
    CHECK(e && strcmp(e->name, "guards") == 0 && e->colour == -1 && strcmp(e->note, "the gate") == 0,
        "segments in any order, spaces trimmed");
    CHECK(e && e->spawn_count == 2 && e->spawns[1].is_obj && e->spawns[1].count == 5, "an obj spawn with a count");
    CHECK(Editor_EncountersColour(&file, 1) == 1, "a set with no colour draws by its place");
    CHECK(Editor_EncountersFind(&file, "guards") == 4, "Find names the line (%d)", Editor_EncountersFind(&file, "guards"));
    CHECK(Editor_EncountersFind(&file, "nobody") == -1, "and -1 for no such set");
    CHECK(Editor_EncounterCentre(Editor_EncountersNth(&file, 0), &x, &z, &level) && x == 3248 && z == 3238
              && level == 0,
        "the centre is the rounded mean (%d,%d)", x, z);
    CHECK(Editor_EncounterNpcNames(Editor_EncountersNth(&file, 1), names, 4) == 1 && strcmp(names[0], "guard") == 0,
        "npc names skip objs");

    /* Format -> parse. */
    Editor_EncounterFormat(Editor_EncountersNth(&file, 1), line, sizeof(line));
    CHECK(strcmp(line, "encounter guards | spawn npc:guard 3222 3218 0 | spawn obj:bones 3223 3218 0 5 | note the gate")
              == 0,
        "the editor's line: %s", line);
    CHECK(Editor_EncountersParseLine(line, &parsed) == EDITOR_ENCOUNTERS_LINE_RECORD && parsed.spawn_count == 2
              && strcmp(parsed.note, "the gate") == 0,
        "and it parses back");
    Editor_EncountersFree(&file);

    /* Refusals, each with its line. */
    {
        static const struct
        {
            const char* text;
            int line;
        } k_bad[] = {
            { "# ok\nencounter bad name\n", 2 },
            { "encounter a | colour 8\n", 1 },
            { "encounter a | colour 1 | colour 2\n", 1 },
            { "encounter a | note x | note y\n", 1 },
            { "encounter a | spawn npc:x 1 2\n", 1 },
            { "encounter a | spawn npc:x 1 2 4\n", 1 },
            { "encounter a | spawn loc:x 1 2 0\n", 1 },
            { "encounter a | spawn npc:x 1 2 0 | spawn npc:x 1 2 0\n", 1 },
            { "encounter a\nencounter a\n", 2 },
            { "encounter a | rects 1 2 3 4\n", 1 },
            { "goblins\n", 1 },
        };
        for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
        {
            Editor_EncountersInit(&file);
            r = Editor_EncountersParse(&file, k_bad[i].text, strlen(k_bad[i].text));
            CHECK(!r.ok && r.line == k_bad[i].line, "\"%s\" is refused at line %d (%d %d)", k_bad[i].text,
                k_bad[i].line, r.ok, r.line);
            {
                char* back = encounters_emit(&file);
                CHECK(strcmp(back, k_bad[i].text) == 0, "a refused document still emits byte for byte");
                free(back);
            }
            Editor_EncountersFree(&file);
        }
    }
}

static void
test_encounters_verbs(void)
{
    struct Editor_EncountersFile file;
    struct Editor_Encounter set;
    struct Editor_EncounterSpawn spawn;
    struct Editor_Cmd commands[16];
    size_t length;
    char out[4096];
    int count;

    /* An add to an empty document brings the header. */
    Editor_EncountersInit(&file);
    Editor_EncounterInit(&set, "camp");
    memset(&spawn, 0, sizeof(spawn));
    snprintf(spawn.name, sizeof(spawn.name), "goblin");
    spawn.x = 3200;
    spawn.z = 3200;
    spawn.count = 1;
    CHECK(Editor_EncounterAddSpawn(&set, &spawn), "a spawn joins the set");
    CHECK(!Editor_EncounterAddSpawn(&set, &spawn), "but not twice");
    CHECK(Editor_EncountersTextWith(&file, NULL, &set, out, sizeof(out), &length), "an add is planned");
    CHECK(strncmp(out, "# ToriRSMapEd encounter sets", 28) == 0 && strstr(out, "\nencounter camp | spawn npc:goblin 3200 3200 0\n"),
        "with the header lines first: %s", out);
    CHECK(length == strlen(out), "its length");

    /* Its splices from "" and back. */
    count = Editor_StampSplices(EDITOR_ENCOUNTERS_DOC_KEY, "", 0, out, length, commands, 16);
    CHECK(count >= 1, "the add is splices (%d)", count);
    {
        char* forward = apply_splices("", commands, count, 0);
        char* back = apply_splices(forward, commands, count, 1);
        CHECK(strcmp(forward, out) == 0, "applied, they write the new text");
        CHECK(back[0] == '\0', "undone, the document is empty again");
        free(forward);
        free(back);
    }

    /* Replace, rename, refusals, remove. */
    {
        char before[8192];
        snprintf(before, sizeof(before), "%s# kept\nencounter other | colour 3\n", out);
        Editor_EncountersParse(&file, before, strlen(before));
        set.colour = 5;
        CHECK(Editor_EncountersTextWith(&file, "camp", &set, out, sizeof(out), &length), "a replace is planned");
        CHECK(strstr(out, "encounter camp | colour 5 | spawn") && strstr(out, "# kept\nencounter other | colour 3\n"),
            "only that line changes: %s", out);
        count = Editor_StampSplices(EDITOR_ENCOUNTERS_DOC_KEY, before, strlen(before), out, length, commands, 16);
        {
            char* forward = apply_splices(before, commands, count, 0);
            char* back = apply_splices(forward, commands, count, 1);
            CHECK(strcmp(forward, out) == 0 && strcmp(back, before) == 0, "its splices apply and undo byte for byte");
            CHECK(count == 1 && commands[0].u.text_splice.before_len == 0
                      && commands[0].u.text_splice.after_len == strlen(" | colour 5"),
                "and touch only the changed bytes: one insert of \" | colour 5\" (%d commands)", count);
            free(forward);
            free(back);
        }
        snprintf(set.name, sizeof(set.name), "other");
        CHECK(!Editor_EncountersTextWith(&file, "camp", &set, out, sizeof(out), &length), "a taken name is refused");
        CHECK(!Editor_EncountersTextWith(&file, NULL, &set, out, sizeof(out), &length), "an add of a taken name too");
        CHECK(!Editor_EncountersTextWith(&file, "nobody", &set, out, sizeof(out), &length), "a replace of no set too");
        snprintf(set.name, sizeof(set.name), "camp_west");
        CHECK(Editor_EncountersTextWith(&file, "camp", &set, out, sizeof(out), &length)
                  && strstr(out, "encounter camp_west | colour 5") && !strstr(out, "encounter camp |"),
            "a rename replaces the line");
        snprintf(set.name, sizeof(set.name), "bad name");
        CHECK(!Editor_EncountersTextWith(&file, "camp", &set, out, sizeof(out), &length), "a bad name is refused");

        CHECK(Editor_EncountersTextWithout(&file, "other", out, sizeof(out), &length)
                  && !strstr(out, "encounter other") && strstr(out, "# kept\n"),
            "a remove drops the line");
        CHECK(!Editor_EncountersTextWithout(&file, "nobody", out, sizeof(out), &length), "a remove of no set is refused");
    }
    {
        Editor_EncounterInit(&set, "solo");
        Editor_EncountersParse(&file, "", 0);
        Editor_EncountersTextWith(&file, NULL, &set, out, sizeof(out), &length);
        Editor_EncountersParse(&file, out, length);
        CHECK(Editor_EncountersTextWithout(&file, "solo", out, sizeof(out), &length) && length == 0,
            "removing the last set of a header-only document leaves it empty (%zu)", length);
    }
    Editor_EncountersFree(&file);
}

/* ---- the ledger ------------------------------------------------------------------- */

static const char k_ledger[] = "// Goblin (npc 3028) -- combat level 2\n"
                               "// Generated by tools/gen_npc_stats.py. Re-running rewrites this file.\n"
                               "// To pin a value by hand: edit it, set `source = authored`, and later runs\n"
                               "\n"
                               "source = generated\n"
                               "\n"
                               "hitpoints     = 5\n"
                               "attack        = 1\n"
                               "attackrate    = 4                    // wiki `attack speed`\n"
                               "crushattack   = -21\n"
                               "strengthbonus = -15\n"
                               "\n"
                               "// Checks -- stated, never written to the config.\n"
                               "cache_combat_level = 2               // wiki says 2, agrees\n";

static char*
ledger_set(
    const char* text,
    const char* key,
    const char* value)
{
    size_t length = 0;
    char* out;
    int ok = Editor_NpcStatsTextSet(text, strlen(text), "goblin", key, value, NULL, 0, &length);
    assert(ok);
    out = malloc(length + 1);
    assert(out);
    Editor_NpcStatsTextSet(text, strlen(text), "goblin", key, value, out, length + 1, &length);
    return out;
}

static void
test_ledger(void)
{
    struct Editor_NpcStatsFile file;
    char value[64];
    char key[EDITOR_DOC_KEY_MAX];
    char path[256];
    char* edited;
    size_t length;

    Editor_NpcStatsInit(&file);
    Editor_NpcStatsParse(&file, k_ledger, strlen(k_ledger));
    {
        size_t const n = Editor_NpcStatsEmit(&file, NULL, 0);
        char* back = malloc(n + 1);
        assert(back);
        Editor_NpcStatsEmit(&file, back, n + 1);
        CHECK(strcmp(back, k_ledger) == 0, "the ledger round-trips byte for byte");
        free(back);
    }
    CHECK(Editor_NpcStatsGet(&file, "attackrate", value, sizeof(value)) && strcmp(value, "4") == 0,
        "a value before its note (%s)", value);
    CHECK(Editor_NpcStatsGet(&file, "crushattack", value, sizeof(value)) && strcmp(value, "-21") == 0, "a negative");
    CHECK(!Editor_NpcStatsGet(&file, "magic", value, sizeof(value)), "an absent key");
    CHECK(Editor_NpcStatsFind(&file, "// To pin a value by hand: edit it, set `source") < 0,
        "a comment holding '=' is not a field");
    Editor_NpcStatsParse(&file, "a = 1\r\nb = -\r\n// c = 2", strlen("a = 1\r\nb = -\r\n// c = 2"));
    CHECK(Editor_NpcStatsGet(&file, "a", value, sizeof(value)) && strcmp(value, "1") == 0, "CRLF values trim the CR");
    CHECK(!Editor_NpcStatsGet(&file, "b", value, sizeof(value)), "a \"-\" value is none");
    {
        size_t const n = Editor_NpcStatsEmit(&file, NULL, 0);
        char back[64];
        Editor_NpcStatsEmit(&file, back, sizeof(back));
        CHECK(n == strlen("a = 1\r\nb = -\r\n// c = 2") && strcmp(back, "a = 1\r\nb = -\r\n// c = 2") == 0,
            "CRLF and no final newline round-trip");
    }
    Editor_NpcStatsFree(&file);

    /* An edit: only its value, the note's column kept, the pin set. */
    edited = ledger_set(k_ledger, "attackrate", "6");
    CHECK(strstr(edited, "\nattackrate    = 6                    // wiki `attack speed`\n") != NULL,
        "the value changes, the note keeps its column:\n%s", edited);
    CHECK(strstr(edited, "\nsource = authored\n") != NULL && !strstr(edited, "generated\n"), "source = authored");
    {
        /* Everything else as it was: the two texts differ in two lines. */
        int differing = 0;
        const char* a = k_ledger;
        const char* b = edited;
        while( *a && *b )
        {
            const char* ae = strchr(a, '\n');
            const char* be = strchr(b, '\n');
            if( !ae || !be )
                break;
            differing += (ae - a != be - b) || memcmp(a, b, (size_t)(ae - a)) != 0;
            a = ae + 1;
            b = be + 1;
        }
        CHECK(differing == 2, "two lines differ (%d)", differing);
    }
    free(edited);
    edited = ledger_set(k_ledger, "attackrate", "12345678901234567890123456");
    CHECK(strstr(edited, "attackrate    = 12345678901234567890123456 // wiki") != NULL, "a long value keeps one space");
    free(edited);

    /* A new key after the last stat, in its key column. */
    edited = ledger_set(k_ledger, "magic", "3");
    CHECK(strstr(edited, "strengthbonus = -15\nmagic         = 3\n\n// Checks") != NULL,
        "a new stat goes after the last one:\n%s", edited);
    free(edited);
    /* A removal. */
    edited = ledger_set(k_ledger, "crushattack", "");
    CHECK(!strstr(edited, "crushattack") && strstr(edited, "attackrate    = 4"), "a removal takes its line out");
    free(edited);
    /* Already authored: only the value. */
    {
        char* first = ledger_set(k_ledger, "attack", "2");
        char* second = ledger_set(first, "attack", "3");
        CHECK(strstr(second, "attack        = 3\n") && strstr(second, "source = authored\n"), "a second edit");
        free(first);
        free(second);
    }
    /* A new ledger. */
    edited = ledger_set("", "hitpoints", "12");
    CHECK(strncmp(edited, "// goblin -- stats authored in ToriRSMapEd", 42) == 0, "a new ledger names its writer");
    CHECK(strstr(edited, "\nsource = authored\n\nhitpoints = 12\n") != NULL, "then the pin and the stat:\n%s", edited);
    free(edited);
    /* Refusals. */
    CHECK(!Editor_NpcStatsTextSet(k_ledger, strlen(k_ledger), "goblin", "a b", "1", NULL, 0, &length), "a key with a space");
    CHECK(!Editor_NpcStatsTextSet(k_ledger, strlen(k_ledger), "goblin", "a", "1 // x", NULL, 0, &length), "a value with //");
    CHECK(!Editor_NpcStatsTextSet(k_ledger, strlen(k_ledger), "goblin", "source", "x", NULL, 0, &length),
        "source is the pin, not a stat");

    /* Names, keys, paths. */
    CHECK(Editor_NpcStatsShard("Goblin") == 'g' && Editor_NpcStatsShard("3rd") == '3'
              && Editor_NpcStatsShard("_x") == '_',
        "the shard is gen_npc_stats.py's shard_of");
    Editor_NpcStatsDocKey(key, "goblin");
    CHECK(strcmp(key, "stats:g/goblin") == 0, "the document key (%s)", key);
    Editor_NpcStatsPath(path, sizeof(path), "goblin");
    CHECK(strcmp(path, "npc_stats/g/goblin.stats") == 0, "the path (%s)", path);
}

static void
test_derived(void)
{
    /* Max hit, by hand: ((level + 9) * (bonus + 64) + 320) / 640.
     *   goblin: strength 1, strengthbonus -15 -> 10 * 49 = 490 -> 810 / 640 = 1 (the wiki's 1)
     *   Sourhog: strength 30, bonus 0 -> 39 * 64 = 2496 -> 2816 / 640 = 4 (the tree's own figure)
     *   Avatar of Ranging: ranged 110, rangebonus 10 -> 119 * 74 = 8806 -> 9126 / 640 = 14 */
    CHECK(Editor_NpcMaxHit(1, -15) == 1, "goblin max hit 1 (%d)", Editor_NpcMaxHit(1, -15));
    CHECK(Editor_NpcMaxHit(30, 0) == 4, "Sourhog max hit 4 (%d)", Editor_NpcMaxHit(30, 0));
    CHECK(Editor_NpcMaxHit(110, 10) == 14, "Avatar of Ranging max hit 14 (%d)", Editor_NpcMaxHit(110, 10));
    CHECK(Editor_NpcMaxHit(1, -80) == 0, "a bonus under -64 hits 0");
    /* Combat level, by hand:
     *   goblin 5 hp, 1/1/1, 1 magic, 1 ranged: 0.25 * (1 + 5) = 1.5; 0.325 * 2 = 0.65 -> 2.15 -> 2
     *   40 att, 40 str, 30 def, 50 hp, 1 magic, 70 ranged: 0.25 * 80 = 20; max(80, 105, 1) = 105;
     *   0.325 * 105 = 34.125 -> 54.125 -> 54 */
    CHECK(Editor_NpcCombatLevel(1, 1, 1, 5, 1, 1) == 2, "goblin level 2 (%d)", Editor_NpcCombatLevel(1, 1, 1, 5, 1, 1));
    CHECK(Editor_NpcCombatLevel(40, 40, 30, 50, 1, 70) == 54, "a ranger's level 54 (%d)",
        Editor_NpcCombatLevel(40, 40, 30, 50, 1, 70));
    /*   1 att, 2 str, 1 def, 11 hp: 0.25 * 12 = 3; 0.325 * 3 = 0.975 -> 3.975 -> 3 (a level that a stray
     *   prayer term, or a rounding, would push to 4) */
    CHECK(Editor_NpcCombatLevel(1, 2, 1, 11, 1, 1) == 3, "a 3.975 is level 3 (%d)",
        Editor_NpcCombatLevel(1, 2, 1, 11, 1, 1));
    CHECK(strcmp(Editor_NpcStatsLevelKey(0), "hitpoints") == 0 && strcmp(Editor_NpcStatsBonusKey(6), "strengthbonus") == 0,
        "the Stats tab's keys");
}

/* ---- drops and combat ----------------------------------------------------------------- */

static const char k_drops[] = "// Generated by tools/wiki_droptable.py\n"
                              "[ai_queue3,goblin]\n"
                              "@goblin_drop;\n"
                              "\n"
                              "[ai_queue3,guard]\n"
                              "if (npc_findhero = ^false) {\n"
                              "    return;\n"
                              "}\n"
                              "obj_add(npc_coord, bones, 1, ^lootdrop_duration);\n"
                              "\n"
                              "[label,goblin_drop]\n"
                              "if (npc_param(death_drop) ! null) {\n"
                              "    obj_add(npc_coord, npc_param(death_drop), 1, ^lootdrop_duration);\n"
                              "}\n"
                              "obj_add(npc_coord, goblin_mail, 1, ^lootdrop_duration);\n"
                              "def_int $dropint = random(128);\n"
                              "if ($dropint < 5) {\n"
                              "    obj_add(npc_coord, bronze_sq_shield, 1, ^lootdrop_duration);\n"
                              "} else if ($dropint < 8) {\n"
                              "    obj_add(npc_coord, coins, calc(5 + random(10)), ^lootdrop_duration);\n"
                              "}\n";

static void
test_drops(void)
{
    struct Editor_DropTable table;
    char chance[64];

    memset(&table, 0, sizeof(table));
    CHECK(Editor_DropsRead(k_drops, strlen(k_drops), "ai_queue3,goblin", &table), "the death queue is found");
    CHECK(table.row_count == 0 && strcmp(table.call, "goblin_drop") == 0, "it calls its label (%s)", table.call);
    CHECK(Editor_DropsRead(k_drops, strlen(k_drops), "label,goblin_drop", &table), "the label is read");
    CHECK(table.row_count == 4, "four rows (%d)", table.row_count);
    CHECK(table.unread == 0, "every line read (%d unread)", table.unread);
    CHECK(strcmp(table.rows[0].obj, "npc_param(death_drop)") == 0 && table.rows[0].always, "the remains, always");
    CHECK(strcmp(table.rows[1].obj, "goblin_mail") == 0 && table.rows[1].always, "an Always line");
    CHECK(strcmp(table.rows[2].obj, "bronze_sq_shield") == 0 && table.rows[2].numerator == 5
              && table.rows[2].denominator == 128,
        "the walk's first arm is 5/128");
    CHECK(strcmp(table.rows[3].obj, "coins") == 0 && table.rows[3].numerator == 3
              && strcmp(table.rows[3].count, "calc(5 + random(10))") == 0,
        "the second is 3/128, its count verbatim (%s)", table.rows[3].count);
    Editor_DropChance(&table.rows[2], chance, sizeof(chance));
    CHECK(strcmp(chance, "5/128 (3.91%)") == 0, "its chance (%s)", chance);
    Editor_DropChance(&table.rows[1], chance, sizeof(chance));
    CHECK(strcmp(chance, "always") == 0, "an Always chance");

    memset(&table, 0, sizeof(table));
    CHECK(Editor_DropsRead(k_drops, strlen(k_drops), "ai_queue3,guard", &table) && table.row_count == 1
              && table.unread == 2,
        "a hero check is unread lines, the bones a row (%d rows, %d unread)", table.row_count, table.unread);
    memset(&table, 0, sizeof(table));
    CHECK(!Editor_DropsRead(k_drops, strlen(k_drops), "ai_queue3,cow", &table) && !table.found, "no such queue");
}

static void
test_combat(void)
{
    static const char k_wizard[] = "[opnpc1,dark_wizard]\n"
                                   "~chatnpc(\"Die!\");\n"
                                   "\n"
                                   "[ai_opplayer2,dark_wizard]\n"
                                   "// cast the dbrow's spell\n"
                                   "~npc_spellattack(dark_wizard_spells);\n"
                                   "\n"
                                   "[proc,other]\n";
    char trigger[32];
    char body[512];

    CHECK(Editor_CombatTemplateBody(EDITOR_COMBAT_MELEE, NULL, 0, trigger, body, sizeof(body)) > 0
              && strcmp(trigger, "ai_opplayer2") == 0 && strcmp(body, "~npc_meleeattack;\n") == 0,
        "melee is the default swing");
    CHECK(Editor_CombatTemplateBody(EDITOR_COMBAT_RANGER, NULL, 0, trigger, body, sizeof(body)) > 0
              && strcmp(body, "~npc_rangeattack;\n") == 0,
        "ranger the generic ranged swing");
    CHECK(Editor_CombatTemplateBody(EDITOR_COMBAT_HEALER, NULL, 0, trigger, body, sizeof(body)) > 0
              && strstr(body, "npc_statheal(hitpoints"),
        "healer mends itself");
    CHECK(Editor_CombatTemplateBody(EDITOR_COMBAT_CASTER, k_wizard, strlen(k_wizard), trigger, body, sizeof(body)) > 0
              && strcmp(trigger, "ai_opplayer2") == 0 && strcmp(body, "~npc_spellattack(dark_wizard_spells);\n") == 0,
        "caster is the dark wizard's own swing, comments dropped (%s)", body);
    CHECK(Editor_CombatTemplateBody(EDITOR_COMBAT_CASTER, NULL, 0, trigger, body, sizeof(body)) < 0,
        "without dark_wizard.rs2 the caster is refused, not invented");
    CHECK(strcmp(Editor_CombatTemplateName(EDITOR_COMBAT_CASTER), "caster") == 0, "the names");
}

static void
test_builder_ai(void)
{
    static struct Builder_Model model;
    static struct Builder_Model back;
    static char ini[1 << 16];
    struct Builder_Issues issues;
    struct Builder_Buf script;
    struct Builder_Trigger* trigger;
    struct Builder_Step step;
    char names[8][BUILDER_TRIGGER_NAME_MAX];
    char error[256];
    int n;
    int found = 0;

    CHECK(Builder_AiTriggerValid("ai_opplayer2") && !Builder_AiTriggerValid("opnpc1")
              && !Builder_AiTriggerValid("ai_") && !Builder_AiTriggerValid("ai_Op"),
        "ai trigger names");
    Builder_ModelInit(&model, BUILDER_SUBJECT_NPC, "goblin");
    trigger = Builder_ModelAiTriggerAdd(&model, "ai_opplayer2");
    CHECK(trigger && trigger->op == BUILDER_OP_AI, "an ai trigger is added");
    CHECK(Builder_ModelAiTriggerAdd(&model, "ai_opplayer2") == trigger, "once");
    Builder_StepInit(&step, BUILDER_STEP_RAW);
    snprintf(step.text, sizeof(step.text), "~npc_meleeattack;\nnpc_statheal(hitpoints, 2, 0);");
    CHECK(Builder_TriggerStepAdd(trigger, &step) == 0, "its body is a RAW step");

    Builder_ModelEmitIni(&model, ini, sizeof(ini));
    CHECK(strstr(ini, "\n[ai,ai_opplayer2]\n") != NULL, "the model writes [ai,ai_opplayer2]:\n%s", ini);
    CHECK(Builder_ModelParseIni(&back, ini, strlen(ini), error, sizeof(error)), "and reads it back (%s)", error);
    CHECK(back.trigger_count == 1 && back.triggers[0].op == BUILDER_OP_AI
              && strcmp(back.triggers[0].ai, "ai_opplayer2") == 0 && back.triggers[0].step_count == 1,
        "the same trigger and step");

    Builder_IssuesClear(&issues);
    Builder_BufInit(&script);
    Builder_EmitScript(&model, &script, &issues);
    CHECK(strstr(Builder_BufText(&script), "\n[ai_opplayer2,goblin]\n~npc_meleeattack;\nnpc_statheal(hitpoints, 2, 0);\n")
              != NULL,
        "the script states the swing:\n%s", Builder_BufText(&script));
    CHECK(issues.errors == 0, "with no error (%d)", issues.errors);
    Builder_BufFree(&script);
    n = Builder_ScriptTriggerNames(&model, names, 8);
    for( int i = 0; i < n && i < 8; i++ )
        found += strcmp(names[i], "ai_opplayer2,goblin") == 0;
    CHECK(found == 1, "its name is in the conflict list");

    model.subject = BUILDER_SUBJECT_LOC;
    Builder_IssuesClear(&issues);
    Builder_BufInit(&script);
    Builder_EmitScript(&model, &script, &issues);
    CHECK(issues.errors > 0, "a loc's ai trigger is an error");
    Builder_BufFree(&script);
}

int
main(void)
{
    ToriMem_Init();
    test_encounters_codec();
    test_encounters_verbs();
    test_ledger();
    test_derived();
    test_drops();
    test_combat();
    test_builder_ai();
    printf("editor_encounters_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
