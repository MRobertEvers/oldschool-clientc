#include "rs_game_events.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Skill names, by protocol id -- content/pack/stat.pack, which is authored
 * rather than imported for exactly this reason: the index is fixed by
 * UPDATE_STAT and by the client's own stat table, so both ends can spell the
 * list out and stay in agreement.
 *
 * 23 and 24 exist because the rev-239 stat table has 25 slots; no content
 * grants them, and a level-up in one would still be reported correctly rather
 * than as an unnamed skill.
 */
static char const* const RS_GAME_EVENT_SKILL_NAME[RS_GAME_EVENT_SKILL_COUNT] = {
    "Attack",
    "Defence",
    "Strength",
    "Hitpoints",
    "Ranged",
    "Prayer",
    "Magic",
    "Cooking",
    "Woodcutting",
    "Fletching",
    "Fishing",
    "Firemaking",
    "Crafting",
    "Smithing",
    "Mining",
    "Herblore",
    "Agility",
    "Thieving",
    "Slayer",
    "Farming",
    "Runecraft",
    "Hunter",
    "Construction",
    "Sailing",
    "Summoning",
};

/* Kind names, in enum order. The plugin layer hands these straight to script
 * as the event's `kind`, so they are part of the plugin contract: renaming one
 * breaks every plugin config that lists it. */
static char const* const RS_GAME_EVENT_KIND_NAME[RS_GAME_EVENT_KIND_COUNT] = {
    "level_up",
    "quest_complete",
    "valuable_drop",
    "untradeable_drop",
    "boss_kill",
    "pet",
    "collection_log",
    "combat_achievement",
    "death",
    "treasure_trail",
    "duel_end",
};

char const*
RS_GameEvent_KindName(int kind)
{
    if( kind < 0 || kind >= RS_GAME_EVENT_KIND_COUNT )
        return NULL;
    return RS_GAME_EVENT_KIND_NAME[kind];
}

char const*
RS_GameEvent_SkillName(int skill)
{
    if( skill < 0 || skill >= RS_GAME_EVENT_SKILL_COUNT )
        return NULL;
    return RS_GAME_EVENT_SKILL_NAME[skill];
}

/* ------------------------------------------------------------------ text */

/*
 * Drop the colour/markup the chatbox paints with, so a pattern below matches a
 * line the game coloured the same as a plain one. Two grammars, because the
 * client renders both: the modern <col=ef1020>...</col> tag and the classic
 * five-character @red@ code.
 *
 * An unterminated '<' is left alone rather than eating the rest of the line: a
 * message containing a bare less-than is ordinary text, and a recogniser that
 * silently swallowed it would match the wrong thing.
 */
static void
strip_markup(char const* in, char* out, int out_size)
{
    int at = 0;

    assert(in);
    assert(out);
    assert(out_size > 0);

    while( *in && at < out_size - 1 )
    {
        if( *in == '<' )
        {
            char const* close = strchr(in, '>');
            if( close )
            {
                in = close + 1;
                continue;
            }
        }
        else if( *in == '@' && in[1] && in[2] && in[3] && in[4] == '@' )
        {
            in += 5;
            continue;
        }
        out[at++] = *in++;
    }
    out[at] = '\0';

    /* Trailing whitespace only: a leading space is not something the game
     * writes, and trimming one would hide a message that genuinely starts with
     * indentation. */
    while( at > 0 && (out[at - 1] == ' ' || out[at - 1] == '\t' || out[at - 1] == '\n' ||
                      out[at - 1] == '\r') )
        out[--at] = '\0';
}

/** `s` after `prefix`, or NULL when it does not start with it. */
static char const*
after_prefix(char const* s, char const* prefix)
{
    size_t const n = strlen(prefix);
    assert(s);
    assert(prefix);
    return strncmp(s, prefix, n) == 0 ? s + n : NULL;
}

/*
 * Read a decimal that may be grouped ("25,600").
 *
 * Returns the character after the number, or NULL when there was no digit at
 * all. A comma is only consumed when a digit follows it, so "5, and then" stops
 * at the comma rather than running into the prose.
 */
static char const*
read_grouped_int(char const* s, int* out)
{
    long long value = 0;
    int digits = 0;

    assert(s);
    assert(out);

    while( *s )
    {
        if( *s >= '0' && *s <= '9' )
        {
            value = value * 10 + (*s - '0');
            /* Past int range is not a number this game produces; clamp rather
             * than wrap, so a malformed line cannot report a negative count. */
            if( value > 2147483647LL )
                value = 2147483647LL;
            digits++;
            s++;
            continue;
        }
        if( *s == ',' && s[1] >= '0' && s[1] <= '9' )
        {
            s++;
            continue;
        }
        break;
    }
    if( digits == 0 )
        return NULL;
    *out = (int)value;
    return s;
}

static void
event_init(struct RS_GameEvent* out, int kind, char const* text)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->value = -1;
    if( text )
        snprintf(out->text, sizeof(out->text), "%s", text);
}

/** Copy `len` bytes of `src` into the subject, always NUL terminated. */
static void
event_subject(struct RS_GameEvent* out, char const* src, size_t len)
{
    assert(out);
    assert(src);
    if( len >= sizeof(out->subject) )
        len = sizeof(out->subject) - 1;
    memcpy(out->subject, src, len);
    out->subject[len] = '\0';
}

/** Subject from `src` up to (not including) `end`. */
static void
event_subject_span(struct RS_GameEvent* out, char const* src, char const* end)
{
    assert(src);
    assert(end);
    assert(end >= src);
    event_subject(out, src, (size_t)(end - src));
}

/** Subject from `src` to the end, minus one trailing '.' or '!' if present. */
static void
event_subject_sentence(struct RS_GameEvent* out, char const* src)
{
    size_t len = strlen(src);
    if( len > 0 && (src[len - 1] == '.' || src[len - 1] == '!') )
        len--;
    event_subject(out, src, len);
}

/*
 * "Your Zulrah kill count is: 122."
 * "Your Yama success count is: 227"
 * "Your completed Chambers of Xeric count is: 489."
 * "Your Barrows chest count is 310"
 * "Your Kalphite Queen (Echo) kill count is:1"
 *
 * One pattern for all five, and every part of it is load-bearing -- these
 * five spellings are RuneLite's own test corpus (ScreenshotPluginTest), and
 * the differences between them are not decoration:
 *
 *   - The verb before "count" is "kill" for most things and "success" for a
 *     few (Yama, Wintertodt). Both are trimmed, because neither belongs to
 *     the name. "chest" is NOT trimmed: "Barrows chest" is what was opened
 *     and "Barrows" alone is a place.
 *   - The colon after "is" is OPTIONAL. Barrows omits it entirely.
 *   - The space after the colon is optional too, because the number arrives
 *     inside a colour tag and stripping the tag can leave "is:1".
 *   - A leading "completed " is the raids' phrasing and is dropped, so the
 *     subject is the raid and not a sentence fragment.
 *
 * A kill count is the only signal a raid or a chest gives that a run ended,
 * which is why all of them share one kind rather than each getting one nobody
 * would think to enable separately.
 */
static int
match_kill_count(char const* body, struct RS_GameEvent* out, char const* full)
{
    char const* count_at = strstr(body, " count is");
    char const* subject = body;
    char const* number;
    char const* completed;
    size_t span;
    int value = 0;

    if( !count_at )
        return 0;

    number = count_at + strlen(" count is");
    if( *number == ':' )
        number++;
    while( *number == ' ' )
        number++;
    if( !read_grouped_int(number, &value) )
        return 0;

    completed = after_prefix(subject, "completed ");
    if( completed )
        subject = completed;
    if( count_at <= subject )
        return 0;

    span = (size_t)(count_at - subject);
    if( span > 5 && strncmp(count_at - 5, " kill", 5) == 0 )
        span -= 5;
    else if( span > 8 && strncmp(count_at - 8, " success", 8) == 0 )
        span -= 8;
    if( span == 0 )
        return 0;

    event_init(out, RS_GAME_EVENT_BOSS_KILL, full);
    event_subject(out, subject, span);
    out->value = value;
    return 1;
}

/* "Valuable drop: Rune scimitar (25,600 coins)". The value is in the tail, so
 * the item name is everything before the LAST '(' -- an item whose own name
 * carries brackets ("Clue scroll (hard)") would otherwise lose half of it. */
static int
match_valuable_drop(char const* body, struct RS_GameEvent* out, char const* full)
{
    char const* open = strrchr(body, '(');
    int value = 0;
    char const* after;

    if( !open || open == body )
        return 0;
    after = read_grouped_int(open + 1, &value);
    if( !after || !after_prefix(after, " coins)") )
        return 0;

    event_init(out, RS_GAME_EVENT_VALUABLE_DROP, full);
    /* One space before the bracket, and only one: the name itself never ends
     * in whitespace. */
    event_subject_span(out, body, open > body && open[-1] == ' ' ? open - 1 : open);
    out->value = value;
    return 1;
}

/* "You have completed 15 medium Treasure Trails." Checked before the quest
 * scroll's "You have completed <name>!", which shares its opening. */
static int
match_treasure_trail(char const* body, struct RS_GameEvent* out, char const* full)
{
    char const* tier;
    char const* trails;
    int value = 0;

    tier = read_grouped_int(body, &value);
    if( !tier || *tier != ' ' )
        return 0;
    tier++;
    trails = strstr(tier, " Treasure Trail");
    if( !trails || trails <= tier )
        return 0;

    event_init(out, RS_GAME_EVENT_TREASURE_TRAIL, full);
    event_subject_span(out, tier, trails);
    out->value = value;
    return 1;
}

/*
 * The quest scroll's title.
 *
 * A quest completion is painted, not announced -- questscroll.rs2 writes the
 * title into `questscroll:quest_title` and the chatbox says nothing at all --
 * so this is matched off interface text. RuneLite's Screenshot plugin reads
 * the same widget (Questscroll.QUEST_TITLE) for the same reason.
 *
 * The scroll has two shapes, and RuneLite carries a regex for each
 * (QUEST_PATTERN_1 and QUEST_PATTERN_2) because neither covers the other:
 *
 *      "You have completed The Corsair Curse!"      -- name after the verb
 *      "'One Small Favour' completed!"              -- name before it
 *
 * plus two adverbs that change what was completed rather than decorating it:
 * "You have... kind of... completed the Hazeel Cult Quest!" is a partial
 * completion, and "You have completely completed Rag and Bone Man!" is the
 * SECOND quest of that name, not the first one again.
 *
 * What is deliberately NOT ported is RuneLite's cosmetic tail: a list of seven
 * quests whose names contain the word "Quest" and a list of four Recipe for
 * Disaster tags. Both exist to undo an earlier step of their own regex -- it
 * strips a trailing " Quest" and then puts it back for the quests that needed
 * it -- so the honest version is to not strip it in the first place, and
 * "Doric's Quest" arrives as its own name without a lookup table.
 */
static int
match_quest_title(char const* line, struct RS_GameEvent* out, char const* full)
{
    char const* head = line;
    char const* quest;
    char const* at = strstr(line, " completed ");
    size_t len;
    char subject[RS_GAME_EVENT_SUBJECT_MAX];
    int partial = 0;
    int second = 0;

    if( at )
    {
        quest = at + strlen(" completed ");
        /* Lowercase only: "the " here is the article ("completed the Hazeel
         * Cult"), whereas "The Corsair Curse" wears its own. */
        if( after_prefix(quest, "the ") )
            quest += 4;
        partial = strstr(head, "kind of") != NULL;
        second = strstr(head, "completely") != NULL;
    }
    else
    {
        /* The name-first shape. The line must END in the verb, or "completed"
         * anywhere in a sentence would swallow the whole of it. */
        len = strlen(line);
        while( len > 0 && (line[len - 1] == '!' || line[len - 1] == '.') )
            len--;
        if( len < strlen(" completed") ||
            strncmp(line + len - strlen(" completed"), " completed", strlen(" completed")) != 0 )
            return 0;
        len -= strlen(" completed");
        if( len == 0 || len >= sizeof(subject) )
            return 0;
        memcpy(subject, line, len);
        subject[len] = '\0';
        quest = subject;
    }

    len = strlen(quest);
    while( len > 0 && (quest[len - 1] == '!' || quest[len - 1] == '.') )
        len--;
    /* The scroll quotes a name that is not a sentence: 'One Small Favour'. */
    if( len >= 2 && quest[0] == '\'' && quest[len - 1] == '\'' )
    {
        quest++;
        len -= 2;
    }
    if( len == 0 )
        return 0;
    /* A digit here is a count, not a quest -- "You have completed 15 medium
     * Treasure Trails." is prose that reaches this far on the chat channel. */
    if( quest[0] >= '0' && quest[0] <= '9' )
        return 0;

    event_init(out, RS_GAME_EVENT_QUEST_COMPLETE, full);
    event_subject(out, quest, len);
    if( partial )
        strncat(out->subject, " partial completion", sizeof(out->subject) - strlen(out->subject) - 1);
    else if( second )
        strncat(out->subject, " II", sizeof(out->subject) - strlen(out->subject) - 1);
    return 1;
}

static int
match_chat(char const* line, struct RS_GameEvent* out)
{
    char const* body;

    if( (body = after_prefix(line, "Valuable drop: ")) != NULL )
        return match_valuable_drop(body, out, line);

    if( (body = after_prefix(line, "Untradeable drop: ")) != NULL )
    {
        if( !*body )
            return 0;
        event_init(out, RS_GAME_EVENT_UNTRADEABLE_DROP, line);
        event_subject_sentence(out, body);
        return 1;
    }

    if( (body = after_prefix(line, "Your ")) != NULL && match_kill_count(body, out, line) )
        return 1;

    if( (body = after_prefix(line, "New item added to your collection log: ")) != NULL )
    {
        if( !*body )
            return 0;
        event_init(out, RS_GAME_EVENT_COLLECTION_LOG, line);
        event_subject_sentence(out, body);
        return 1;
    }

    /* Both spellings the game has used for the same line; the task name is the
     * subject either way, because the tier is already in the text. */
    if( (body = strstr(line, "combat task: ")) != NULL ||
        (body = strstr(line, "combat achievement task: ")) != NULL )
    {
        char const* name = strstr(body, ": ") + 2;
        if( !*name )
            return 0;
        event_init(out, RS_GAME_EVENT_COMBAT_ACHIEVEMENT, line);
        event_subject_sentence(out, name);
        return 1;
    }

    /*
     * Three sentences for one moment, and RuneLite lists all three
     * (PET_MESSAGES) because they are not variations of a phrase -- the first
     * two are the pet following you, and the third is the pet going into your
     * bag because your follower slot was taken. Matching only the "funny
     * feeling" ones silently misses every pet dropped while you already had
     * one out. The pet is never named, so there is no subject to take.
     */
    if( after_prefix(line, "You have a funny feeling like") ||
        strstr(line, "You feel something weird sneaking into your backpack") )
    {
        event_init(out, RS_GAME_EVENT_PET, line);
        return 1;
    }

    if( after_prefix(line, "Oh dear, you are dead") )
    {
        event_init(out, RS_GAME_EVENT_DEATH, line);
        return 1;
    }

    if( (body = strstr(line, "You have now won ")) != NULL )
    {
        int value = 0;
        event_init(out, RS_GAME_EVENT_DUEL_END, line);
        event_subject(out, "won", 3);
        if( read_grouped_int(body + strlen("You have now won "), &value) )
            out->value = value;
        return 1;
    }
    if( (body = strstr(line, "You have now lost ")) != NULL )
    {
        int value = 0;
        event_init(out, RS_GAME_EVENT_DUEL_END, line);
        event_subject(out, "lost", 4);
        if( read_grouped_int(body + strlen("You have now lost "), &value) )
            out->value = value;
        return 1;
    }

    if( (body = after_prefix(line, "You have completed ")) != NULL &&
        match_treasure_trail(body, out, line) )
        return 1;

    /* A server that announces the completion in the chatbox instead of on the
     * scroll. Ours paints the scroll, but LostCity-era content says it. The
     * name-less form is checked first because it has nothing for the title
     * matcher to find. */
    if( (body = after_prefix(line, "Congratulations! ")) != NULL &&
        after_prefix(body, "Quest complete") )
    {
        event_init(out, RS_GAME_EVENT_QUEST_COMPLETE, line);
        return 1;
    }
    if( strstr(line, " completed ") && match_quest_title(line, out, line) )
        return 1;

    return 0;
}

/*
 * Interface text is a wide firehose -- every journal line, every reward row,
 * every button caption, rewritten whenever a panel is repainted -- so exactly
 * one pattern is recognised off it, and the scan below rejects everything else
 * before the line is even copied.
 */
static int
match_interface(char const* line, struct RS_GameEvent* out)
{
    return match_quest_title(line, out, line);
}

int
RS_GameEvent_FromText(
    int source,
    char const* text,
    struct RS_GameEvent* out)
{
    char line[RS_GAME_EVENT_TEXT_MAX];

    assert(out);
    /* An empty line is a legitimate runtime state -- IF_SETTEXT blanks unused
     * rows by writing one -- so it is a guard rather than an assert, and it
     * goes ahead of the text assert because it does not read the pointer. */
    if( !text )
        return 0;
    if( !text[0] )
        return 0;

    /*
     * Reject before copying.
     *
     * strip_markup writes up to 200 bytes, and this runs on every IF_SETTEXT
     * the server sends -- a panel refresh is hundreds of them in one tick. A
     * scan for the one word every interface pattern contains costs nothing and
     * turns that into a pass over a string already in cache. The chat side has
     * no such gate because chat is a handful of lines a tick, and gating it
     * would be a second copy of the pattern list to keep in step.
     */
    if( source == RS_GAME_EVENT_SRC_INTERFACE && !strstr(text, "completed") )
        return 0;

    strip_markup(text, line, (int)sizeof(line));
    if( !line[0] )
        return 0;

    return source == RS_GAME_EVENT_SRC_INTERFACE ? match_interface(line, out)
                                                 : match_chat(line, out);
}

int
RS_GameEvent_FromStat(
    int skill,
    int previous_level,
    int new_level,
    struct RS_GameEvent* out)
{
    char const* name;

    assert(out);

    if( previous_level <= 0 )
        return 0;
    if( new_level <= previous_level )
        return 0;

    name = RS_GameEvent_SkillName(skill);
    if( !name )
        return 0;

    event_init(out, RS_GAME_EVENT_LEVEL_UP, NULL);
    event_subject(out, name, strlen(name));
    out->value = new_level;
    return 1;
}
