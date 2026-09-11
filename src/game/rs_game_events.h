#ifndef SRC_GAME_RS_GAME_EVENTS_H
#define SRC_GAME_RS_GAME_EVENTS_H

/*
 * Notable-moment recognition: the client's answer to "something worth
 * reacting to just happened".
 *
 * A plugin that wants to act on a level-up, a quest completion or a boss kill
 * would otherwise have to re-derive each one from the raw feed, and every
 * plugin would derive them slightly differently -- one matching a message the
 * next one spells another way, none of them agreeing about what a "drop" is.
 * The knowledge lives here once, is unit tested here once, and is dispatched
 * to plugins as one event with a named kind.
 *
 * Two sources, because the game genuinely uses two:
 *
 *   - TEXT. Kill counts, drops, pets and collection-log entries arrive as
 *     lines of prose and have no other representation on the wire. Both the
 *     chatbox and interface text are fed through here, because a quest
 *     completion is painted onto the quest scroll rather than said aloud.
 *   - STAT. A level-up is NOT recognised from prose. UPDATE_STAT carries the
 *     skill and the level as numbers, which is authoritative, works on a
 *     server that never sends the congratulation line (ours does not), and
 *     cannot be fooled by someone typing the sentence into public chat.
 *
 * That last one is a deliberate divergence from RuneLite, which is worth
 * writing down because it is the only place this file does not follow it.
 * RuneLite's Screenshot plugin reads a level-up off the LEVELUP_DISPLAY
 * widget, and off the chat line only when the varbit that disables that
 * widget is set -- two sources, kept from colliding by a varbit. Neither
 * exists here: this tree's server sends no level-up interface and no
 * congratulation line, so both of RuneLite's sources would produce nothing at
 * all, while the numbers are always there.
 *
 * The cost is timing. RuneLite photographs the moment the box is on screen,
 * because the box IS its trigger; a stat packet arrives a little before
 * whatever announces it, which is why the plugin on top of this waits a
 * couple of ticks. On a server that does send the box, that wait is the
 * difference, and it is a config key rather than a constant for that reason.
 *
 * Recognition is deliberately conservative. A line nobody recognises is not an
 * event, and a wrong guess is worse than a miss: this drives screenshots and
 * notifications, and a plugin that fires on ordinary chat is noise the user
 * turns off entirely.
 */

/** Skill ids are the protocol's -- UPDATE_STAT's index, content/pack/stat.pack. */
#define RS_GAME_EVENT_SKILL_COUNT 25
#define RS_GAME_EVENT_SUBJECT_MAX 64
#define RS_GAME_EVENT_TEXT_MAX 200

/**
 * What happened. The names double as the strings a plugin matches on, so a
 * value added here must be added to RS_GameEvent_KindName as well -- they are
 * asserted to stay the same length.
 */
enum RS_GameEventKind
{
    RS_GAME_EVENT_NONE = -1,
    /** A skill's BASE level rose. `subject` names it, `value` is the new level. */
    RS_GAME_EVENT_LEVEL_UP = 0,
    /** `subject` is the quest, when the source text named it. */
    RS_GAME_EVENT_QUEST_COMPLETE,
    /** `subject` is the item, `value` its coin value. */
    RS_GAME_EVENT_VALUABLE_DROP,
    /** `subject` is the item; untradeables carry no coin value. */
    RS_GAME_EVENT_UNTRADEABLE_DROP,
    /** `subject` is the boss/raid/chest, `value` the new kill count. */
    RS_GAME_EVENT_BOSS_KILL,
    /** The "funny feeling" line. No subject -- the pet is not named. */
    RS_GAME_EVENT_PET,
    /** `subject` is the item just logged. */
    RS_GAME_EVENT_COLLECTION_LOG,
    /** `subject` is the task, `value` unset. */
    RS_GAME_EVENT_COMBAT_ACHIEVEMENT,
    /** The local player died. */
    RS_GAME_EVENT_DEATH,
    /** `subject` is the tier ("medium"), `value` the completed count. */
    RS_GAME_EVENT_TREASURE_TRAIL,
    /** `subject` is "won" or "lost", `value` the running total. */
    RS_GAME_EVENT_DUEL_END,

    RS_GAME_EVENT_KIND_COUNT
};

/** Which stream a line of text came off. */
enum RS_GameEventSource
{
    /** The chatbox: MESSAGE_GAME and friends. */
    RS_GAME_EVENT_SRC_CHAT = 0,
    /** Interface text: IF_SETTEXT. The quest scroll's title is the only thing
     *  recognised here, and the only reason this source exists. */
    RS_GAME_EVENT_SRC_INTERFACE = 1
};

struct RS_GameEvent
{
    /** enum RS_GameEventKind. */
    int kind;
    /** The thing the event is about, or "" when the source did not name one. */
    char subject[RS_GAME_EVENT_SUBJECT_MAX];
    /** Level / kill count / coin value, or -1 when the kind carries none. */
    int value;
    /** The line it was recognised from, markup stripped. Empty for STAT. */
    char text[RS_GAME_EVENT_TEXT_MAX];
};

/** Stable lowercase name of a kind ("level_up"), or NULL when out of range. */
char const*
RS_GameEvent_KindName(int kind);

/** Skill id to its display name ("Woodcutting"), or NULL when out of range. */
char const*
RS_GameEvent_SkillName(int skill);

/**
 * Recognise one line. Returns 1 and fills `out` when it is a notable moment,
 * 0 otherwise -- which is the common case and not a failure.
 *
 * `text` arrives exactly as the game wrote it; colour tags (<col=...>, @red@)
 * are stripped here rather than by every caller, so the pattern below matches
 * a line the chatbox paints in colour the same as a plain one.
 */
int
RS_GameEvent_FromText(
    int source,
    char const* text,
    struct RS_GameEvent* out);

/**
 * Recognise a level-up from a stat update.
 *
 * `previous_level` is what the client last saw for this skill, and 0 means it
 * has never seen one -- the login burst, where every skill "rises" from
 * nothing. That first sight is recorded and never announced, which is why the
 * caller must keep the previous level rather than passing base_level twice.
 *
 * Returns 1 when the level genuinely rose.
 */
int
RS_GameEvent_FromStat(
    int skill,
    int previous_level,
    int new_level,
    struct RS_GameEvent* out);

#endif /* SRC_GAME_RS_GAME_EVENTS_H */
