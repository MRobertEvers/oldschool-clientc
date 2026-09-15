/*
 * Unit test for notable-moment recognition.
 *
 * Standalone: no cache, no server, no window -- the recogniser is pure text in,
 * struct out, which is the whole reason it is its own module rather than a
 * switch inside the packet exec.
 *
 * The negative cases carry as much weight as the positive ones. This drives
 * screenshots and notifications, so a recogniser that fires on ordinary chat
 * is worse than one that misses: the user turns the plugin off and never sees
 * the moments it does catch.
 */

#include "game/rs_game_events.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                      \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static int
chat(char const* text, struct RS_GameEvent* out)
{
    memset(out, 0, sizeof(*out));
    return RS_GameEvent_FromText(RS_GAME_EVENT_SRC_CHAT, text, out);
}

static int
iface(char const* text, struct RS_GameEvent* out)
{
    memset(out, 0, sizeof(*out));
    return RS_GameEvent_FromText(RS_GAME_EVENT_SRC_INTERFACE, text, out);
}

static void
test_kind_names(void)
{
    printf("TEST: kind + skill names\n");

    for( int i = 0; i < RS_GAME_EVENT_KIND_COUNT; i++ )
        TEST_ASSERT(RS_GameEvent_KindName(i) != NULL, "every kind has a name");
    TEST_ASSERT(RS_GameEvent_KindName(-1) == NULL, "no name below the range");
    TEST_ASSERT(
        RS_GameEvent_KindName(RS_GAME_EVENT_KIND_COUNT) == NULL, "no name past the range");

    TEST_ASSERT(
        strcmp(RS_GameEvent_KindName(RS_GAME_EVENT_LEVEL_UP), "level_up") == 0,
        "level_up is spelled the way plugin config lists it");

    /* stat.pack ids. A shift here would report the wrong skill, silently. */
    TEST_ASSERT(strcmp(RS_GameEvent_SkillName(0), "Attack") == 0, "0 is Attack");
    TEST_ASSERT(strcmp(RS_GameEvent_SkillName(8), "Woodcutting") == 0, "8 is Woodcutting");
    TEST_ASSERT(strcmp(RS_GameEvent_SkillName(20), "Runecraft") == 0, "20 is Runecraft");
    TEST_ASSERT(strcmp(RS_GameEvent_SkillName(24), "Summoning") == 0, "24 is Summoning");
    TEST_ASSERT(RS_GameEvent_SkillName(25) == NULL, "25 is past the stat table");
    TEST_ASSERT(RS_GameEvent_SkillName(-1) == NULL, "and -1 is below it");
}

static void
test_level_up(void)
{
    struct RS_GameEvent ev;

    printf("TEST: level up from UPDATE_STAT\n");

    TEST_ASSERT(RS_GameEvent_FromStat(10, 41, 42, &ev) == 1, "41 -> 42 is a level up");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_LEVEL_UP, "and reports level_up");
    TEST_ASSERT(strcmp(ev.subject, "Fishing") == 0, "naming the skill");
    TEST_ASSERT(ev.value == 42, "and the new level");
    TEST_ASSERT(ev.text[0] == '\0', "a stat update carries no source line");

    /* The login burst: every skill arrives at once, from nothing. Announcing
     * those would fire 25 screenshots on the loading screen. */
    TEST_ASSERT(RS_GameEvent_FromStat(10, 0, 42, &ev) == 0, "first sight is not a level up");

    TEST_ASSERT(RS_GameEvent_FromStat(10, 42, 42, &ev) == 0, "an unchanged level is not one");
    TEST_ASSERT(RS_GameEvent_FromStat(10, 42, 41, &ev) == 0, "and neither is a drain");
    TEST_ASSERT(RS_GameEvent_FromStat(99, 41, 42, &ev) == 0, "an unknown skill is refused");
}

static void
test_drops(void)
{
    struct RS_GameEvent ev;

    printf("TEST: drops\n");

    TEST_ASSERT(chat("Valuable drop: Rune scimitar (25,600 coins)", &ev), "valuable drop");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_VALUABLE_DROP, "reports valuable_drop");
    TEST_ASSERT(strcmp(ev.subject, "Rune scimitar") == 0, "names the item");
    TEST_ASSERT(ev.value == 25600, "and un-groups the coin value");

    /* The name's own brackets must survive: the value is in the LAST pair. */
    TEST_ASSERT(chat("Valuable drop: Clue scroll (hard) (100 coins)", &ev), "bracketed name");
    TEST_ASSERT(strcmp(ev.subject, "Clue scroll (hard)") == 0, "keeps the item's brackets");
    TEST_ASSERT(ev.value == 100, "and still reads the value");

    TEST_ASSERT(chat("Untradeable drop: Clue scroll (elite)", &ev), "untradeable drop");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_UNTRADEABLE_DROP, "reports untradeable_drop");
    TEST_ASSERT(strcmp(ev.subject, "Clue scroll (elite)") == 0, "names the item");
    TEST_ASSERT(ev.value == -1, "an untradeable carries no coin value");

    TEST_ASSERT(!chat("Valuable drop: Rune scimitar", &ev), "no value, no valuable drop");
    TEST_ASSERT(!chat("Valuable drop: ", &ev), "and an empty one is not a drop either");
}

static void
test_kill_counts(void)
{
    struct RS_GameEvent ev;

    printf("TEST: kill counts\n");

    TEST_ASSERT(chat("Your Zulrah kill count is: 122.", &ev), "boss kill count");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_BOSS_KILL, "reports boss_kill");
    TEST_ASSERT(strcmp(ev.subject, "Zulrah") == 0, "names the boss");
    TEST_ASSERT(ev.value == 122, "and the count");

    /* A raid and a chest are the same moment wearing different words. */
    TEST_ASSERT(chat("Your completed Chambers of Xeric count is: 5.", &ev), "raid count");
    TEST_ASSERT(strcmp(ev.subject, "Chambers of Xeric") == 0, "drops the 'completed'");
    TEST_ASSERT(ev.value == 5, "and reads the count");

    TEST_ASSERT(chat("Your Barrows chest count is: 1,024.", &ev), "chest count");
    TEST_ASSERT(strcmp(ev.subject, "Barrows chest") == 0, "keeps the chest in the name");
    TEST_ASSERT(ev.value == 1024, "and un-groups the count");

    TEST_ASSERT(!chat("Your kill count is: not a number", &ev), "no number, no event");
}

/*
 * RuneLite's own corpus, verbatim.
 *
 * Every string below is copied out of ScreenshotPluginTest.java, colour tags
 * and all. They are here rather than paraphrased because three of them broke
 * this recogniser when it was written from the shape of the messages rather
 * than from the messages: Barrows has no colon after "is", Yama says
 * "success" where everything else says "kill", and a stripped colour tag can
 * leave the number pressed straight up against the colon.
 */
static void
test_runelite_corpus(void)
{
    struct RS_GameEvent ev;

    printf("TEST: RuneLite's ScreenshotPluginTest corpus\n");

    /* No colon at all after "is". */
    TEST_ASSERT(
        chat("Your Barrows chest count is <col=ff0000>310</col>", &ev), "barrows chest");
    TEST_ASSERT(strcmp(ev.subject, "Barrows chest") == 0, "keeps 'chest' in the name");
    TEST_ASSERT(ev.value == 310, "and reads the count without a colon");

    TEST_ASSERT(
        chat("Your completed Chambers of Xeric count is: <col=ff0000>489</col>.", &ev),
        "chambers of xeric");
    TEST_ASSERT(strcmp(ev.subject, "Chambers of Xeric") == 0, "drops the 'completed'");
    TEST_ASSERT(ev.value == 489, "and reads the count");

    TEST_ASSERT(
        chat("Your completed Theatre of Blood: Hard Mode count is: <col=ff0000>73</col>.", &ev),
        "theatre of blood hard mode");
    TEST_ASSERT(
        strcmp(ev.subject, "Theatre of Blood: Hard Mode") == 0, "keeps the mode in the name");

    /* "success", not "kill". */
    TEST_ASSERT(chat("Your Yama success count is: <col=ff0000>227</col>", &ev), "yama");
    TEST_ASSERT(strcmp(ev.subject, "Yama") == 0, "trims 'success' the way it trims 'kill'");
    TEST_ASSERT(ev.value == 227, "and reads the count");

    /* Tag stripped, so the number sits against the colon with no space. */
    TEST_ASSERT(
        chat("Your <col=6800bf>Kalphite Queen (Echo)</col> kill count is:<col=e00a19>1</col>", &ev),
        "kalphite queen echo");
    TEST_ASSERT(strcmp(ev.subject, "Kalphite Queen (Echo)") == 0, "names it through the tags");
    TEST_ASSERT(ev.value == 1, "and reads a number with no space before it");

    TEST_ASSERT(
        chat("Your <col=a53fff>Corrupted Hunllef (Echo)</col> kill count is: <col=ff3045>31</col>",
             &ev),
        "corrupted hunllef echo");
    TEST_ASSERT(ev.value == 31, "reads the count");

    TEST_ASSERT(chat("Your Nightmare kill count is: <col=ff0000>1,130</col>", &ev), "nightmare");
    TEST_ASSERT(ev.value == 1130, "un-groups the thousands");

    /* Drops. */
    TEST_ASSERT(
        chat("<col=ef1020>Valuable drop: 6 x Bronze arrow (42 coins)</col>", &ev),
        "a stacked valuable drop");
    TEST_ASSERT(strcmp(ev.subject, "6 x Bronze arrow") == 0, "keeps the count in the name");
    TEST_ASSERT(ev.value == 42, "and reads the value");

    TEST_ASSERT(
        chat("<col=ef1020>Untradeable drop: Rusty sword</col>", &ev), "untradeable drop");
    TEST_ASSERT(strcmp(ev.subject, "Rusty sword") == 0, "names the item");

    TEST_ASSERT(
        chat("New item added to your collection log: <col=ef1020>Chompy bird hat</col>", &ev),
        "collection log");
    TEST_ASSERT(strcmp(ev.subject, "Chompy bird hat") == 0, "names the item through the tag");

    /* Duels. RuneLite requires the word "now", and so does this: without it
     * the line is the running total being restated, not a duel ending. */
    TEST_ASSERT(chat("You won! You have now won 1,909 duels.", &ev), "duel won");
    TEST_ASSERT(strcmp(ev.subject, "won") == 0 && ev.value == 1909, "which way, and how many");
    TEST_ASSERT(chat("You have now lost 1,909 duels.", &ev), "duel lost");
    TEST_ASSERT(strcmp(ev.subject, "lost") == 0, "reports the loss");
    TEST_ASSERT(!chat("You have lost 145 duels.", &ev), "no 'now', no duel end");
    TEST_ASSERT(
        !chat("You were defeated! You have won 1,909 duels.", &ev),
        "and a restated total is not one either");

    /* Combat achievements, across every tier RuneLite tests. The subject keeps
     * its own punctuation -- "Why Cook?" is the task's name; making it
     * filename-safe is the caller's job and not the recogniser's. */
    TEST_ASSERT(
        chat("Congratulations, you've completed an easy combat task: "
             "<col=06600c>Into the Den of Giants</col>.",
             &ev),
        "easy combat task");
    TEST_ASSERT(strcmp(ev.subject, "Into the Den of Giants") == 0, "names the task");
    TEST_ASSERT(
        chat("Congratulations, you've completed a hard combat task: <col=0cc919>Why Cook?</col>.",
             &ev),
        "hard combat task");
    TEST_ASSERT(strcmp(ev.subject, "Why Cook?") == 0, "keeping the task's own punctuation");
    TEST_ASSERT(
        chat("Congratulations, you've completed a grandmaster combat task: "
             "<col=0cc919>Chambers of Xeric: CM (5-Scale) Speed-Runner</col>.",
             &ev),
        "grandmaster combat task");
    TEST_ASSERT(
        strcmp(ev.subject, "Chambers of Xeric: CM (5-Scale) Speed-Runner") == 0,
        "colons and brackets included");

    /* The pet line RuneLite lists third and this recogniser used to miss: the
     * follower slot was full, so it went into the bag instead. */
    TEST_ASSERT(
        chat("You feel something weird sneaking into your backpack.", &ev), "pet into the bag");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_PET, "is still a pet");
}

/* The quest scroll's two shapes, and the two adverbs that change the answer.
 * Same strings RuneLite asserts on in testParseQuestWidget. */
static void
test_quest_corpus(void)
{
    struct RS_GameEvent ev;

    printf("TEST: quest scroll titles (RuneLite corpus)\n");

    TEST_ASSERT(iface("You have completed The Corsair Curse!", &ev), "name after the verb");
    TEST_ASSERT(strcmp(ev.subject, "The Corsair Curse") == 0, "keeps the quest's own 'The'");

    TEST_ASSERT(iface("'One Small Favour' completed!", &ev), "name before the verb");
    TEST_ASSERT(strcmp(ev.subject, "One Small Favour") == 0, "and unquoted");

    /* Not decoration: this is a partial completion, and the scroll says so. */
    TEST_ASSERT(
        iface("You have... kind of... completed the Hazeel Cult Quest!", &ev), "kind of");
    TEST_ASSERT(
        strcmp(ev.subject, "Hazeel Cult Quest partial completion") == 0,
        "drops the article and says what it was");

    /* Nor is this: it is the SECOND quest of that name. */
    TEST_ASSERT(
        iface("You have completely completed Rag and Bone Man!", &ev), "completely");
    TEST_ASSERT(strcmp(ev.subject, "Rag and Bone Man II") == 0, "which is a different quest");

    /* A quest whose name contains the word Quest keeps it. RuneLite strips a
     * trailing " Quest" and then re-adds it from a list of seven; not
     * stripping it needs no list. */
    TEST_ASSERT(iface("You have completed Doric's Quest!", &ev), "quest named 'Quest'");
    TEST_ASSERT(strcmp(ev.subject, "Doric's Quest") == 0, "keeps its whole name");
    TEST_ASSERT(iface("You have completed Another Cook's Quest!", &ev), "and another");
    TEST_ASSERT(strcmp(ev.subject, "Another Cook's Quest") == 0, "keeps its whole name too");
}

static void
test_quest_complete(void)
{
    struct RS_GameEvent ev;

    printf("TEST: quest completion\n");

    /* questscroll.rs2 paints this; the chatbox says nothing at all. */
    TEST_ASSERT(iface("You have completed Cook's Assistant!", &ev), "quest scroll title");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_QUEST_COMPLETE, "reports quest_complete");
    TEST_ASSERT(strcmp(ev.subject, "Cook's Assistant") == 0, "names the quest");

    TEST_ASSERT(
        chat("Congratulations! You have completed Dragon Slayer!", &ev),
        "and the announced form, for content that says it out loud");
    TEST_ASSERT(strcmp(ev.subject, "Dragon Slayer") == 0, "names the quest there too");

    TEST_ASSERT(chat("Congratulations! Quest complete!", &ev), "the unnamed form");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_QUEST_COMPLETE, "still a completion");
    TEST_ASSERT(ev.subject[0] == '\0', "with nothing to name");

    /* Interface text is a firehose of journal lines and captions. Anything
     * that is not the scroll title has to fall straight through. */
    TEST_ASSERT(!iface("Total Quest Points: 43", &ev), "the points line is not an event");
    TEST_ASSERT(!iface("", &ev), "and a blanked row is not one");
    TEST_ASSERT(!iface("Your Zulrah kill count is: 122.", &ev), "chat patterns stay off it");
    TEST_ASSERT(
        !iface("You have completed 15 medium Treasure Trails.", &ev),
        "a count sentence is not a quest title");
}

static void
test_misc(void)
{
    struct RS_GameEvent ev;

    printf("TEST: pets, logs, tasks, deaths, trails, duels\n");

    TEST_ASSERT(
        chat("You have a funny feeling like you're being followed.", &ev), "pet drop");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_PET, "reports pet");
    TEST_ASSERT(ev.subject[0] == '\0', "the pet is never named");

    TEST_ASSERT(
        chat("You have a funny feeling like you would have been followed.", &ev),
        "and the full-inventory spelling of the same moment");

    TEST_ASSERT(
        chat("New item added to your collection log: Abyssal whip", &ev), "collection log");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_COLLECTION_LOG, "reports collection_log");
    TEST_ASSERT(strcmp(ev.subject, "Abyssal whip") == 0, "names the item");

    TEST_ASSERT(
        chat("Congratulations, you've completed a hard combat task: Chaos Fanatic Adept.", &ev),
        "combat achievement");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_COMBAT_ACHIEVEMENT, "reports combat_achievement");
    TEST_ASSERT(strcmp(ev.subject, "Chaos Fanatic Adept") == 0, "names the task");

    TEST_ASSERT(chat("Oh dear, you are dead!", &ev), "death");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_DEATH, "reports death");

    TEST_ASSERT(chat("You have completed 15 medium Treasure Trails.", &ev), "treasure trail");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_TREASURE_TRAIL, "reports treasure_trail");
    TEST_ASSERT(strcmp(ev.subject, "medium") == 0, "names the tier");
    TEST_ASSERT(ev.value == 15, "and the completed count");

    TEST_ASSERT(chat("You won! You have now won 3 duels.", &ev), "duel won");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_DUEL_END, "reports duel_end");
    TEST_ASSERT(strcmp(ev.subject, "won") == 0, "which way it went");
    TEST_ASSERT(ev.value == 3, "and the running total");

    TEST_ASSERT(chat("You were defeated! You have now lost 2 duels.", &ev), "duel lost");
    TEST_ASSERT(strcmp(ev.subject, "lost") == 0, "reports the loss");
}

static void
test_markup_and_noise(void)
{
    struct RS_GameEvent ev;

    printf("TEST: markup and ordinary chat\n");

    /* The chatbox paints these lines in colour. A recogniser that only matched
     * the plain form would work in a test and never fire in the game. */
    TEST_ASSERT(
        chat("<col=ef1020>Valuable drop: Rune scimitar (25,600 coins)</col>", &ev),
        "modern colour tags are stripped");
    TEST_ASSERT(strcmp(ev.subject, "Rune scimitar") == 0, "leaving the item name clean");

    TEST_ASSERT(chat("@red@Oh dear, you are dead!", &ev), "classic colour codes too");
    TEST_ASSERT(ev.kind == RS_GAME_EVENT_DEATH, "still a death");

    /* An unterminated '<' is ordinary text, not the start of a tag. */
    TEST_ASSERT(!chat("i < 3 and that is all", &ev), "a bare less-than is left alone");

    TEST_ASSERT(!chat("Hello world", &ev), "ordinary chat is not an event");
    TEST_ASSERT(!chat("You feel a funny feeling", &ev), "and neither is a near miss");
    TEST_ASSERT(!chat("Your attack is now stronger.", &ev), "'Your ...' alone is not a kill");
    /* Markup that leaves nothing behind is not a line. */
    TEST_ASSERT(!chat("<col=ffffff></col>", &ev), "a line that is only markup is not an event");
    TEST_ASSERT(!chat(NULL, &ev), "a NULL line is not an event");
    TEST_ASSERT(!chat("", &ev), "and neither is an empty one");
}

int
main(void)
{
    printf("=== rs_game_events ===\n");

    test_kind_names();
    test_level_up();
    test_drops();
    test_kill_counts();
    test_runelite_corpus();
    test_quest_complete();
    test_quest_corpus();
    test_misc();
    test_markup_and_noise();

    if( g_failures )
    {
        fprintf(stderr, "\n%d failure(s)\n", g_failures);
        return 1;
    }
    printf("\nall passed\n");
    return 0;
}
