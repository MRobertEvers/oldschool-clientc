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
 * variable, a stage list, a proc, a cheat) -- each rendered for one record's
 * name.
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
};

/** A rendered snippet with its terminator fits this for any record name
 *  (EDITOR_SNIPPET_SUBJECT_MAX): size the buffer with it. */
#define EDITOR_SNIPPET_MAX 2048
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

/** ".rs2", ".varp", ".constant". */
const char*
Editor_SnippetFileExtension(enum Editor_SnippetFile file);

/**
 * Snippet `index` of `kind` for the record named `subject` ("fenwick"), into
 * `out`: its first line a `//` comment saying which kind of file it goes in,
 * the record's name wherever a record name belongs, every line ended with
 * '\n'. Returns the length written, the terminator excluded.
 *
 * `subject` is a record name: not empty, shorter than
 * EDITOR_SNIPPET_SUBJECT_MAX. `out_size` must hold the text: asserted, never
 * cut -- half a script is worse than none. EDITOR_SNIPPET_MAX always does.
 */
size_t
Editor_SnippetRender(
    enum Editor_SnippetKind kind,
    int index,
    const char* subject,
    char* out,
    size_t out_size);

#endif
