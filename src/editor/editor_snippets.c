/*
 * RuneScript snippets (editor_snippets.h): the catalogue and its renderer.
 *
 * A template is the snippet's text with three marks:
 *
 *   {s}   the record's name as a script names it      fenwick_crate
 *   {t}   the name in words, for a sentence           fenwick crate
 *   {T}   the same with a capital, for a title        Fenwick crate
 *
 * Every other byte is copied as written, so a template reads as the script
 * it becomes. No template holds a '%' conversion: the text is never a printf
 * format (RuneScript's own `%var` would be one).
 *
 * Where each idiom comes from, so a line here can be checked against a file
 * that builds (all under OSRS-Content/osrs-content/server/scripts):
 *
 *   ~chatnpc_anim / ~chatplayer_anim, ^chat_<mood>   interface_chat/scripts/chat.rs2,
 *                                                    interface_chat/configs/chat.constant
 *   ~p_choice2 / ~p_choice3 / ~p_choice2_header      interface_chat/scripts/chat.rs2
 *   switch_int ... case 1 :                          quests/quest_cook/scripts/quest_cook.rs2
 *   case default :                                   areas/alkharid/scripts/tanner.rs2
 *   "Start the <Quest> quest?", @q_mes@...</col>,    areas/ardougne_east/scripts/brother_omad.rs2
 *   ~objbox
 *   inv_freespace / inv_add / inv_total              quests/quest_animalmagnetism/scripts/anma.rs2
 *   loc_change(<loc>, <ticks>)                       areas/alkharid/scripts/misc_locs.rs2
 *   [opheld1,<obj>], ~mesbox                         areas/area_gnome/scripts/gnome_cocktail_guide.rs2
 *   [proc,<name>]()(boolean), return(true)           areas/area_gnome/scripts/gnome_restaurant.rs2
 *   [debugproc,<name>], p_telejump(<coord>)          quests/quest_cook/scripts/quest_cook.rs2,
 *                                                    areas/area_alkharid/scripts/shantay.rs2
 *   ^<name> = <value>                                quests/quest_cook/configs/quest_cook.constant
 *   the six lines of a varp block                    what a hand-written quest variable needed to
 *                                                    pass RUN's pack build on 2026-11-09 (every key
 *                                                    of the type stated)
 */

#include "editor/editor_snippets.h"

#include <assert.h>
#include <string.h>

struct snippet
{
    const char* title;
    enum Editor_SnippetFile file;
    const char* text;
};

static const struct snippet k_npc[] = {
    { "Talk-to: one line", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. The npc needs op1=Talk-to for the menu entry (Record panel).\n"
        "// Moods: ^chat_neutral ^chat_happy ^chat_sad ^chat_quiz ^chat_angry ^chat_shock ^chat_laugh\n"
        "[opnpc1,{s}]\n"
        "~chatnpc_anim(^chat_neutral, \"Hello there.\");\n" },
    { "Talk-to: two options", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. Talk-to with two options.\n"
        "// ~p_choice2 answers the number written after the option that was picked.\n"
        "[opnpc1,{s}]\n"
        "~chatnpc_anim(^chat_neutral, \"Can I help you?\");\n"
        "if (~p_choice2(\"Who are you?\", 1, \"No thanks.\", 2) = 1) {\n"
        "    ~chatplayer_anim(^chat_quiz, \"Who are you?\");\n"
        "    ~chatnpc_anim(^chat_happy, \"They call me {T}.\");\n"
        "    return;\n"
        "}\n"
        "~chatplayer_anim(^chat_neutral, \"No thanks.\");\n" },
    { "Talk-to: three options", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. Talk-to with three options, one case each.\n"
        "[opnpc1,{s}]\n"
        "~chatnpc_anim(^chat_neutral, \"What would you like to know?\");\n"
        "switch_int(~p_choice3(\"Who are you?\", 1, \"What is this place?\", 2, \"Goodbye.\", 3)) {\n"
        "    case 1 :\n"
        "        ~chatplayer_anim(^chat_quiz, \"Who are you?\");\n"
        "        ~chatnpc_anim(^chat_happy, \"They call me {T}.\");\n"
        "    case 2 :\n"
        "        ~chatplayer_anim(^chat_quiz, \"What is this place?\");\n"
        "        ~chatnpc_anim(^chat_neutral, \"Home, such as it is.\");\n"
        "    case default :\n"
        "        ~chatplayer_anim(^chat_neutral, \"Goodbye.\");\n"
        "}\n" },
    { "Talk-to: by quest stage", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. Talk-to that says something else at each stage of a quest.\n"
        "// %{s}_quest and its stages: the snippets \"New quest variable\" and \"Quest stage list\".\n"
        "[opnpc1,{s}]\n"
        "if (%{s}_quest = ^{s}_not_started) {\n"
        "    ~chatnpc_anim(^chat_sad, \"I could use some help.\");\n"
        "} else if (%{s}_quest = ^{s}_started) {\n"
        "    ~chatnpc_anim(^chat_quiz, \"Any luck?\");\n"
        "} else {\n"
        "    ~chatnpc_anim(^chat_happy, \"Thank you for your help.\");\n"
        "}\n" },
    { "Talk-to: give an item", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. Talk-to that hands over an item unless the backpack is full.\n"
        "// logs is the item's name: put the one you want there (Copy name on the obj).\n"
        "[opnpc1,{s}]\n"
        "if (inv_freespace(inv) < 1) {\n"
        "    ~chatnpc_anim(^chat_neutral, \"Make some room in your pack first.\");\n"
        "    return;\n"
        "}\n"
        "inv_add(inv, logs, 1);\n"
        "~objbox(logs, \"{T} hands you some logs.\");\n" },
    { "Talk-to: offer a quest", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. Talk-to that offers a quest and starts it on Yes.\n"
        "// %{s}_quest and its stages: the snippets \"New quest variable\" and \"Quest stage list\".\n"
        "[opnpc1,{s}]\n"
        "if (%{s}_quest ! ^{s}_not_started) {\n"
        "    ~chatnpc_anim(^chat_quiz, \"How are you getting on?\");\n"
        "    return;\n"
        "}\n"
        "~chatnpc_anim(^chat_sad, \"I could really use some help.\");\n"
        "if (~p_choice2_header(\"Yes.\", 1, \"No.\", 2, \"Start the {T} quest?\") = 1) {\n"
        "    %{s}_quest = ^{s}_started;\n"
        "    mes(\"You've started a new quest: @q_mes@{T}</col>\");\n"
        "    ~chatplayer_anim(^chat_happy, \"I'll help you.\");\n"
        "    return;\n"
        "}\n"
        "~chatplayer_anim(^chat_neutral, \"Not just now.\");\n" },
};

static const struct snippet k_loc[] = {
    { "Op: a message", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. The loc needs op1 for the menu entry, e.g. op1=Search (Record panel).\n"
        "[oploc1,{s}]\n"
        "mes(\"You search the {t} but find nothing.\");\n" },
    { "Op: by quest stage, then move on", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. An op that does its work at one stage of a quest, then moves it on.\n"
        "// %{s}_quest and its stages: the snippets \"New quest variable\" and \"Quest stage list\".\n"
        "[oploc1,{s}]\n"
        "if (%{s}_quest ! ^{s}_started) {\n"
        "    mes(\"You search the {t} but find nothing.\");\n"
        "    return;\n"
        "}\n"
        "mes(\"You find what you were sent for.\");\n"
        "%{s}_quest = ^{s}_complete;\n" },
    { "Op: give an item once", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. An op that gives an item the first time and nothing after.\n"
        "// %{s}_taken is a 0 or 1 variable: declare it with the snippet \"New quest variable\".\n"
        "// logs is the item's name: put the one you want there (Copy name on the obj).\n"
        "[oploc1,{s}]\n"
        "if (%{s}_taken = 1) {\n"
        "    mes(\"You search the {t} but find nothing.\");\n"
        "    return;\n"
        "}\n"
        "if (inv_freespace(inv) < 1) {\n"
        "    mes(\"You have no room to carry anything else.\");\n"
        "    return;\n"
        "}\n"
        "inv_add(inv, logs, 1);\n"
        "%{s}_taken = 1;\n"
        "~objbox(logs, \"You find some logs.\");\n" },
    { "Op: change the loc for a while", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. An op that turns this loc into another for 200 ticks (two minutes).\n"
        "// {s}_open is the other loc's name: put the one you want there (Copy name on the loc).\n"
        "[oploc1,{s}]\n"
        "loc_change({s}_open, 200);\n" },
};

static const struct snippet k_obj[] = {
    { "Inventory op: a message", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. The obj needs ifop1 for the inventory entry, e.g. ifop1=Read (Record panel).\n"
        "[opheld1,{s}]\n"
        "mes(\"You look at the {t}. Nothing interesting happens.\");\n" },
    { "Inventory op: a page to read", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. An inventory op that shows a page and waits for the click.\n"
        "// The obj needs ifop1 for the inventory entry, e.g. ifop1=Read (Record panel).\n"
        "[opheld1,{s}]\n"
        "~mesbox(\"You read the {t}. It says nothing yet.\");\n" },
};

static const struct snippet k_any[] = {
    { "New quest variable (.varp)", EDITOR_SNIPPET_FILE_VARP,
        "// Goes in a .varp file in the quest's configs folder.\n"
        "// e.g. server/scripts/quests/quest_{s}/configs/quest_{s}.varp\n"
        "// {T}: 0 not started, 1 started, 2 complete.\n"
        "// Write the block as [{s}_quest]. The first RUN gives it an id and renames it\n"
        "// [varp<N>_{s}_quest], here and in every script that reads it. After that RUN,\n"
        "// write the long name in a script: %varp<N>_{s}_quest.\n"
        "[{s}_quest]\n"
        "scope=perm\n"
        "clientcode=default\n"
        "transmit=no\n"
        "protect=no\n"
        "wholewrite=deny\n"
        "wholeread=deny\n" },
    { "Quest stage list (.constant)", EDITOR_SNIPPET_FILE_CONSTANT,
        "// Goes in a .constant file in the quest's configs folder.\n"
        "// e.g. server/scripts/quests/quest_{s}/configs/quest_{s}.constant\n"
        "// The values the quest variable takes, named so a script reads ^{s}_started and not 1.\n"
        "^{s}_not_started = 0\n"
        "^{s}_started = 1\n"
        "^{s}_complete = 2\n" },
    { "Proc: true or false", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A check other scripts ask: if (~{s}_ready = true) { ... }\n"
        "// logs is an item's name: put the one you want there (Copy name on the obj).\n"
        "[proc,{s}_ready]()(boolean)\n"
        "if (inv_total(inv, logs) < 1) {\n"
        "    return(false);\n"
        "}\n"
        "return(true);\n" },
    { "Cheat: reset the quest", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A cheat for testing, typed in the chat box as ::{s}\n"
        "// %{s}_quest and its stages: the snippets \"New quest variable\" and \"Quest stage list\".\n"
        "[debugproc,{s}]\n"
        "%{s}_quest = ^{s}_not_started;\n"
        "mes(\"{T}: reset.\");\n" },
    { "Cheat: teleport", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A cheat for testing, typed in the chat box as ::{s}_go\n"
        "// 0_50_50_24_18 is level_mapx_mapz_tilex_tilez: level 0, square 50 50, tile 24 18 (Lumbridge).\n"
        "[debugproc,{s}_go]\n"
        "p_telejump(0_50_50_24_18);\n"
        "mes(\"You arrive.\");\n" },
};

struct catalogue
{
    const struct snippet* snippets;
    int count;
};

static const struct catalogue k_catalogues[EDITOR_SNIPPET_KIND_COUNT] = {
    [EDITOR_SNIPPET_NPC] = { k_npc, (int)(sizeof(k_npc) / sizeof(k_npc[0])) },
    [EDITOR_SNIPPET_LOC] = { k_loc, (int)(sizeof(k_loc) / sizeof(k_loc[0])) },
    [EDITOR_SNIPPET_OBJ] = { k_obj, (int)(sizeof(k_obj) / sizeof(k_obj[0])) },
    [EDITOR_SNIPPET_ANY] = { k_any, (int)(sizeof(k_any) / sizeof(k_any[0])) },
};

static const struct snippet*
snippet_at(
    enum Editor_SnippetKind kind,
    int index)
{
    assert(kind >= EDITOR_SNIPPET_NPC);
    assert(kind < EDITOR_SNIPPET_KIND_COUNT);
    assert(index >= 0);
    assert(index < k_catalogues[kind].count);
    return &k_catalogues[kind].snippets[index];
}

enum Editor_SnippetKind
Editor_SnippetKindOf(const char* type)
{
    assert(type);
    if( strcmp(type, "npc") == 0 )
        return EDITOR_SNIPPET_NPC;
    if( strcmp(type, "loc") == 0 )
        return EDITOR_SNIPPET_LOC;
    if( strcmp(type, "obj") == 0 )
        return EDITOR_SNIPPET_OBJ;
    return EDITOR_SNIPPET_ANY;
}

int
Editor_SnippetCount(enum Editor_SnippetKind kind)
{
    assert(kind >= EDITOR_SNIPPET_NPC);
    assert(kind < EDITOR_SNIPPET_KIND_COUNT);
    return k_catalogues[kind].count;
}

const char*
Editor_SnippetTitle(
    enum Editor_SnippetKind kind,
    int index)
{
    return snippet_at(kind, index)->title;
}

enum Editor_SnippetFile
Editor_SnippetFileKind(
    enum Editor_SnippetKind kind,
    int index)
{
    return snippet_at(kind, index)->file;
}

const char*
Editor_SnippetFileExtension(enum Editor_SnippetFile file)
{
    assert(file >= EDITOR_SNIPPET_FILE_SCRIPT);
    assert(file <= EDITOR_SNIPPET_FILE_CONSTANT);
    return file == EDITOR_SNIPPET_FILE_VARP ? ".varp" : file == EDITOR_SNIPPET_FILE_CONSTANT ? ".constant" : ".rs2";
}

size_t
Editor_SnippetRender(
    enum Editor_SnippetKind kind,
    int index,
    const char* subject,
    char* out,
    size_t out_size)
{
    const struct snippet* snippet = snippet_at(kind, index);
    size_t subject_length;
    size_t n = 0;

    assert(subject);
    assert(subject[0] != '\0');
    assert(out);
    subject_length = strlen(subject);
    assert(subject_length < EDITOR_SNIPPET_SUBJECT_MAX);

    for( const char* t = snippet->text; *t; )
    {
        /* {s} the name as written; {t} and {T} the name in words. */
        if( t[0] == '{' && (t[1] == 's' || t[1] == 't' || t[1] == 'T') && t[2] == '}' )
        {
            assert(n + subject_length < out_size);
            for( size_t i = 0; i < subject_length; i++ )
            {
                char c = subject[i];
                if( t[1] != 's' && c == '_' )
                    c = ' ';
                if( t[1] == 'T' && i == 0 && c >= 'a' && c <= 'z' )
                    c = (char)(c - 'a' + 'A');
                out[n++] = c;
            }
            t += 3;
            continue;
        }
        assert(n + 1 < out_size);
        out[n++] = *t++;
    }
    assert(n < out_size);
    out[n] = '\0';
    return n;
}
