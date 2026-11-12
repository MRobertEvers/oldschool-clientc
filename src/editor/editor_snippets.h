#ifndef SRC_EDITOR_EDITOR_SNIPPETS_H
#define SRC_EDITOR_EDITOR_SNIPPETS_H

/**
 * RuneScript snippets: text the map maker copies out of the Triggers panel
 * and pastes into their own editor.
 *
 * Scripts are written in the user's own editor (owner, 2026-11-09: "it
 * should use an editor and just provide helpful snippets or copy things that
 * can be used in the editor"). The in-app builders took about sixty
 * interactions for a ten-line script; a snippet is one click, and what it
 * holds is what the file would hold anyway. So this is a small catalogue per
 * record kind -- npc, loc, obj, and "any" for what no record owns (a quest
 * variable, a stage list, a proc, a cheat, a boss fight, a private copy of a
 * square, a cutscene's frame, a patrol route, a journal page) -- each
 * rendered for one record's name and, where it reads a quest's variable, for
 * the quest being worked on.
 *
 * The text is RuneScript as THIS content tree writes it, line for line from
 * files that build (server/scripts/quests/quest_cook, areas/ardougne_east/
 * scripts/brother_omad.rs2, interface_chat/scripts/chat.rs2): `~chatnpc_anim`
 * with a `^chat_` mood, `~p_choice2` returning the option's number, `!` for
 * "is not", `inv_freespace(inv)` before an `inv_add`. A snippet that names a
 * quest variable names it `<record>_quest` with stages `^<record>_started`;
 * the "any" snippets declare exactly those, so a pasted set agrees with
 * itself, and the map maker renames them once for the quest.
 *
 * Pure: no chrome, no session, no App. editor_snippets_test holds the rules
 * (every snippet renders, names its record, balances, ends with a line
 * break).
 *
 * Contract violations assert (CLAUDE.md).
 */

#include <stddef.h>

enum Editor_SnippetKind
{
    EDITOR_SNIPPET_NPC = 0,
    EDITOR_SNIPPET_LOC,
    EDITOR_SNIPPET_OBJ,
    /** What belongs to no one record: a quest variable, a stage list, a
     *  proc, a cheat. The Triggers panel lists these after the record's own. */
    EDITOR_SNIPPET_ANY,
    EDITOR_SNIPPET_KIND_COUNT,
};

/** The kind of file a snippet is pasted into. */
enum Editor_SnippetFile
{
    /** A `.rs2` script. */
    EDITOR_SNIPPET_FILE_SCRIPT = 0,
    /** A `.varp` file (player variables). */
    EDITOR_SNIPPET_FILE_VARP,
    /** A `.constant` file. */
    EDITOR_SNIPPET_FILE_CONSTANT,
    /** A `.npc` file: a server overlay of an npc (its patrol route). */
    EDITOR_SNIPPET_FILE_NPC,
};

/** A rendered snippet with its terminator fits this for any record name
 *  (EDITOR_SNIPPET_SUBJECT_MAX): size the buffer with it. */
#define EDITOR_SNIPPET_MAX 4096
/** A record name, terminator included (EDITOR_CMD_NAME_MAX). */
#define EDITOR_SNIPPET_SUBJECT_MAX 64

/** The snippet kind of a record type: "npc", "loc", "obj"; anything else
 *  (a varp, a file with no record) has only the ANY snippets. */
enum Editor_SnippetKind
Editor_SnippetKindOf(const char* type);

/** How many snippets `kind` has. */
int
Editor_SnippetCount(enum Editor_SnippetKind kind);

/** Snippet `index` of `kind`: its title, short enough for a dropdown row
 *  ("Talk-to: two options"). A static string. */
const char*
Editor_SnippetTitle(
    enum Editor_SnippetKind kind,
    int index);

/** The kind of file snippet `index` of `kind` goes into (its first line says
 *  so in words). */
enum Editor_SnippetFile
Editor_SnippetFileKind(
    enum Editor_SnippetKind kind,
    int index);

/** ".rs2", ".varp", ".constant", ".npc". */
const char*
Editor_SnippetFileExtension(enum Editor_SnippetFile file);

/**
 * Snippet `index` of `kind` for the record named `subject` ("fenwick"), into
 * `out`: its first line a `//` comment saying which kind of file it goes in,
 * the record's name wherever a record name belongs, every line ended with
 * '\n'. Returns the length written, the terminator excluded.
 *
 * `quest` is the quest the map maker is working on ("thistle"): where a
 * snippet reads the quest's stages it writes that quest's names
 * (^thistle_started), the ones New quest's files declare. NULL or "": the
 * record's own name stands in (^fenwick_started), so a set pasted with no
 * quest named still agrees with itself.
 *
 * `quest_var` is the quest's variable as a script names it after the percent
 * sign ("varp9517_thistle_quest": a var's name carries its kind and id once
 * it has one), read off the quest's .varp file. NULL or "": <quest>_quest,
 * the name "New quest variable" declares before a RUN has numbered it.
 *
 * `subject` is a record name: not empty, shorter than
 * EDITOR_SNIPPET_SUBJECT_MAX; so are `quest` and `quest_var` when given.
 * `out_size` must hold the text: asserted, never cut -- half a script is
 * worse than none. EDITOR_SNIPPET_MAX always does.
 */
size_t
Editor_SnippetRender(
    enum Editor_SnippetKind kind,
    int index,
    const char* subject,
    const char* quest,
    const char* quest_var,
    char* out,
    size_t out_size);

/* ---- a new quest's files ------------------------------------------------------
 *
 * "New quest" (the Triggers panel) starts a quest as three files under
 * server/scripts/quests/quest_<name>, modelled on quests/quest_cook:
 *
 *   configs/quest_<name>.varp      the quest's variable, under the name the
 *                                  panel allocated ([varp<N>_<name>_quest],
 *                                  its id in the lane's pack/varp.alloc), so
 *                                  the script builds before any RUN; or
 *                                  [<name>_quest] with no id when it could
 *                                  not, which the first RUN numbers
 *   configs/quest_<name>.constant  ^<name>_not_started / _started / _complete
 *   scripts/quest_<name>.rs2       three cheats that set the stage
 *                                  (::<name>, ::<name>_start, ::<name>_done)
 *                                  and a Talk-to by stage, left as a comment
 *                                  until an npc's name is put in it
 *
 * The names are the ones the snippets write (the variable's own name,
 * ^<name>_started), so a snippet copied for the quest agrees with its files.
 * The panel writes them through documents and saves them; these are the pure
 * half: the name's rule, the paths and the text.
 */

/** A quest's name, terminator included: quests/quest_<name>/configs/
 *  quest_<name>.constant must be a TEXT document key (EDITOR_DOC_KEY_MAX). */
#define EDITOR_QUEST_NAME_MAX 32

/**
 * `typed` as a quest's name, into `out` (EDITOR_QUEST_NAME_MAX at least): a
 * leading "quest_" is the folder's, not the name's, and is dropped. 1 when
 * what is left is [a-z0-9_], starts with a letter and fits; 0 (out "")
 * otherwise.
 */
int
Editor_QuestNameFrom(
    const char* typed,
    char* out,
    size_t out_size);

/** "quests/quest_<name>": the quest's folder under server/scripts. */
void
Editor_QuestFolder(
    const char* name,
    char* out,
    size_t out_size);

/** The quest's file of kind `file` (script, varp, constant), under
 *  server/scripts. Asserted to fit. */
void
Editor_QuestFilePath(
    const char* name,
    enum Editor_SnippetFile file,
    char* out,
    size_t out_size);

/** What that file starts as, the quest's variable named `var`
 *  ("varp9517_thistle_quest"; NULL or "": <name>_quest, no id). Returns the
 *  length; EDITOR_SNIPPET_MAX holds it for any quest name. */
size_t
Editor_QuestFileText(
    const char* name,
    const char* var,
    enum Editor_SnippetFile file,
    char* out,
    size_t out_size);

/**
 * The name of the first `[block]` of a config file's text (a quest's .varp:
 * its variable), into `out`. Comment lines and blank lines before it are
 * passed over. 1 when there is one and it fits; 0 (out "") otherwise.
 */
int
Editor_QuestVarName(
    const char* text,
    size_t length,
    char* out,
    size_t out_size);

/** The 1-based line of the script's first trigger: where the user's editor
 *  is opened. */
int
Editor_QuestScriptLine(void);

#endif
