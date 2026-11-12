/*
 * RuneScript snippets (editor_snippets.h): the catalogue and its renderer.
 *
 * A template is the snippet's text with six marks:
 *
 *   {s}   the record's name as a script names it      fenwick_crate
 *   {t}   the name in words, for a sentence           fenwick crate
 *   {T}   the same with a capital, for a title        Fenwick crate
 *   {q}   the QUEST's name as a script names it       thistle
 *   {Q}   the quest's name with a capital             Thistle
 *   {v}   the quest's VARIABLE as a script names it   varp9517_thistle_quest
 *
 * The quest is the one the map maker is working on (the Triggers panel
 * remembers the quest whose file it last showed or made): a Talk-to written
 * for the npc fenwick then reads %thistle_quest, the variable that quest's
 * files declare. With no quest named the record's own name stands in, as it
 * did before there was one, and the set still agrees with itself.
 *
 * The variable's name is its own mark because it is not the quest's name
 * with a suffix once it has an id: a var carries its kind and id
 * (varp9517_thistle_quest; New quest writes it that way, and the first RUN
 * renames one declared without). The panel reads it off the quest's .varp
 * file. With none known it is <quest>_quest, what "New quest variable"
 * declares.
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
 *   [ai_opplayer2,<npc>] restating the swing,        quests/quest_idesofmilk/scripts/idesofmilk_locs.rs2
 *   npc_bind / npc_queue / [ai_queue6,<npc>],        (Brutus: the stomp is said, bound, queued, and
 *   huntall + damage, [ai_queue3,<npc>] with         lands on whoever still stands in it; the death
 *   npc_findhero and ~npc_default_death              moves the quest on)
 *   ~map_instance_from_square_block,                 general/scripts/misc/map_instance_procs.rs2,
 *   map_instance_coord, map_instance_free,           quests/quest_idesofmilk/scripts/idesofmilk_locs.rs2
 *   %varp6600_map_instance_handle, npc_add           ([proc,iom_arena_enter], [proc,iom_arena_leave])
 *   ~fade_screen, ~cutscene_lock / _hud_lock /       general/scripts/fade_move.rs2,
 *   _unlock / _hud_unlock, cam_moveto, cam_lookat    general/scripts/cutscene_frame.rs2
 *   maxrange, defaultmode=patrol, patrol<N>          areas/lumbridge/configs/lumbridge.npc [hans]
 *   ^journal_todo / _highlight / _done,              quests/quest_cook/scripts/cook_journal.rs2,
 *   ~quest_journal                                   interface_questjournal/configs/questjournal.constant
 */

#include "editor/editor_snippets.h"

#include <assert.h>
#include <stdio.h>
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
        "// %{v}\n"
        "// is the quest's variable and ^{q}_started one of its stages: its .varp and .constant files.\n"
        "[opnpc1,{s}]\n"
        "if (%{v} = ^{q}_not_started) {\n"
        "    ~chatnpc_anim(^chat_sad, \"I could use some help.\");\n"
        "} else if (%{v} = ^{q}_started) {\n"
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
        "// %{v}\n"
        "// is the quest's variable and ^{q}_started one of its stages: its .varp and .constant files.\n"
        "[opnpc1,{s}]\n"
        "if (%{v} ! ^{q}_not_started) {\n"
        "    ~chatnpc_anim(^chat_quiz, \"How are you getting on?\");\n"
        "    return;\n"
        "}\n"
        "~chatnpc_anim(^chat_sad, \"I could really use some help.\");\n"
        "if (~p_choice2_header(\"Yes.\", 1, \"No.\", 2, \"Start the {Q} quest?\") = 1) {\n"
        "    %{v} = ^{q}_started;\n"
        "    mes(\"You've started a new quest: @q_mes@{Q}</col>\");\n"
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
        "// %{v}\n"
        "// is the quest's variable and ^{q}_started one of its stages: its .varp and .constant files.\n"
        "[oploc1,{s}]\n"
        "if (%{v} ! ^{q}_started) {\n"
        "    mes(\"You search the {t} but find nothing.\");\n"
        "    return;\n"
        "}\n"
        "mes(\"You find what you were sent for.\");\n"
        "%{v} = ^{q}_complete;\n" },
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
        "// e.g. server/scripts/quests/quest_{q}/configs/quest_{q}.varp\n"
        "// {Q}: 0 not started, 1 started, 2 complete.\n"
        "// Write the block as [{q}_quest]. The first RUN gives it an id and renames it\n"
        "// [varp<N>_{q}_quest], here and in every script that reads it. After that RUN,\n"
        "// write the long name in a script: %varp<N>_{q}_quest.\n"
        "[{q}_quest]\n"
        "scope=perm\n"
        "clientcode=default\n"
        "transmit=no\n"
        "protect=no\n"
        "wholewrite=deny\n"
        "wholeread=deny\n" },
    { "Quest stage list (.constant)", EDITOR_SNIPPET_FILE_CONSTANT,
        "// Goes in a .constant file in the quest's configs folder.\n"
        "// e.g. server/scripts/quests/quest_{q}/configs/quest_{q}.constant\n"
        "// The values the quest variable takes, named so a script reads ^{q}_started and not 1.\n"
        "^{q}_not_started = 0\n"
        "^{q}_started = 1\n"
        "^{q}_complete = 2\n" },
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
        "// %{v}\n"
        "// is the quest's variable and ^{q}_started one of its stages: its .varp and .constant files.\n"
        "[debugproc,{s}]\n"
        "%{v} = ^{q}_not_started;\n"
        "mes(\"{Q}: reset.\");\n" },
    { "Cheat: teleport", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A cheat for testing, typed in the chat box as ::{s}_go\n"
        "// 0_50_50_24_18 is level_mapx_mapz_tilex_tilez: level 0, square 50 50, tile 24 18 (Lumbridge).\n"
        "[debugproc,{s}_go]\n"
        "p_telejump(0_50_50_24_18);\n"
        "mes(\"You arrive.\");\n" },
    { "Boss: swing, telegraphed special, death", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A boss fight for the npc {s}: its swing, a special that is\n"
        "// announced and can be walked out of, and what its death does to the quest.\n"
        "// {s} in the triggers below is the boss: the quest's own npc (Catalog > Copy to new), its name\n"
        "// there. A monster with a drop table has its death, [ai_queue3,...], in drop_tables/ already.\n"
        "// Naming [ai_opplayer2,{s}] takes the npc off the default swing, so the swing is restated here.\n"
        "// The special comes about one swing in four: the boss stops, the tiles round it are marked,\n"
        "// and three ticks later whoever still stands next to it is hit for 8 to 14.\n"
        "// The npc needs hitpoints and an attack in its config (Record panel); smokepuff is the mark.\n"
        "[ai_opplayer2,{s}]\n"
        "if (npc_stat(hitpoints) = 0) {\n"
        "    return;\n"
        "}\n"
        "if (random(4) = 0) {\n"
        "    npc_say(\"Stand back!\");\n"
        "    npc_bind(3);\n"
        "    npc_attackdelay(6);\n"
        "    ~{s}_special_mark;\n"
        "    npc_queue(6, 0, 3);\n"
        "    return;\n"
        "}\n"
        "npc_anim(npc_param(attack_anim), 0);\n"
        "~npc_meleeattack;\n"
        "\n"
        "// The eight tiles round a one-tile boss, marked while it winds up.\n"
        "[proc,{s}_special_mark]\n"
        "def_int $dx = -1;\n"
        "def_int $dz;\n"
        "while ($dx <= 1) {\n"
        "    $dz = -1;\n"
        "    while ($dz <= 1) {\n"
        "        if ($dx ! 0 | $dz ! 0) {\n"
        "            spotanim_map(smokepuff, movecoord(npc_coord, $dx, 0, $dz), 0, 0);\n"
        "        }\n"
        "        $dz = calc($dz + 1);\n"
        "    }\n"
        "    $dx = calc($dx + 1);\n"
        "}\n"
        "\n"
        "// The special lands: every player still within a tile of the boss.\n"
        "[ai_queue6,{s}]\n"
        "if (npc_stat(hitpoints) = 0) {\n"
        "    return;\n"
        "}\n"
        "huntall(npc_coord, 2, 0);\n"
        "while (huntnext = true) {\n"
        "    if (abs(calc(coordx(coord) - coordx(npc_coord))) <= 1 & abs(calc(coordz(coord) - coordz(npc_coord))) <= 1) {\n"
        "        damage(uid, hitsplat_damage, ~random_range(8, 14));\n"
        "        if (stat(hitpoints) = 0) {\n"
        "            queue(player_death, 0, 0);\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "// The boss dies: the quest moves on for the player who did the most damage.\n"
        "[ai_queue3,{s}]\n"
        "if (npc_findhero = true) {\n"
        "    if (%{v} = ^{q}_started) {\n"
        "        %{v} = ^{q}_complete;\n"
        "        mes(\"The {t} falls.\");\n"
        "    }\n"
        "}\n"
        "~npc_default_death;\n" },
    { "Instance: enter a private copy, leave", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A private copy of one map square: go in, put an npc in it, come out.\n"
        "// 0_50_50_0_0 is the square copied, by its south-west tile: level_mapx_mapz_0_0 (50 50 is Lumbridge).\n"
        "// map_instance_coord gives a tile of the copy: so many tiles east and north of that corner, then the level.\n"
        "// To try it type ::{s}_enter\n"
        "// and then ::{s}_leave\n"
        "// In a quest, call the enter proc from a trigger and the leave proc from the way out.\n"
        "[proc,{s}_enter]\n"
        "def_int $handle = ~map_instance_from_square_block(0_50_50_0_0, 1, 1);\n"
        "if ($handle = ^map_instance_none) {\n"
        "    mes(\"There is no room for another copy just now.\");\n"
        "    return;\n"
        "}\n"
        "%varp6600_map_instance_handle = $handle;\n"
        "p_telejump(map_instance_coord($handle, 24, 18, 0));\n"
        "// man is the npc put in the copy, two tiles east: put the one you want there (Copy name on the npc).\n"
        "npc_add(map_instance_coord($handle, 26, 18, 0), man, 0);\n"
        "\n"
        "// Out again, to the tile given, and the copy handed back.\n"
        "[proc,{s}_leave](coord $out)\n"
        "def_int $handle = %varp6600_map_instance_handle;\n"
        "p_telejump($out);\n"
        "if ($handle ! ^map_instance_none) {\n"
        "    map_instance_free($handle);\n"
        "    %varp6600_map_instance_handle = ^map_instance_none;\n"
        "}\n"
        "\n"
        "[debugproc,{s}_enter]\n"
        "~{s}_enter;\n"
        "\n"
        "[debugproc,{s}_leave]\n"
        "~{s}_leave(0_50_50_24_18);\n" },
    { "Cutscene frame: fade, lock, camera, unlock", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. A cutscene: fade out, lock the player, hold the camera, put it all back.\n"
        "// A tile is level_mapx_mapz_tilex_tilez. cam_moveto is where the camera is, cam_lookat what it looks at:\n"
        "// a tile, a height, then two speeds. ~cutscene_lock takes the ops and the minimap away for the scene.\n"
        "// To try it type ::{s}_scene\n"
        "// In a quest, call the proc from a trigger.\n"
        "[proc,{s}_scene]\n"
        "~fade_screen(0, 255, 0, 0);\n"
        "p_delay(1);\n"
        "~cutscene_lock(3, 2);\n"
        "~cutscene_hud_lock;\n"
        "cam_moveto(0_50_50_24_12, 1100, 232, 100);\n"
        "cam_lookat(0_50_50_24_18, 200, 232, 100);\n"
        "~fade_screen(0, 0, 0, 255);\n"
        "p_delay(1);\n"
        "~mesbox(\"The scene plays here.\");\n"
        "~fade_screen(0, 255, 0, 0);\n"
        "p_delay(1);\n"
        "~cutscene_unlock;\n"
        "~cutscene_hud_unlock;\n"
        "p_delay(1);\n"
        "~fade_screen(0, 0, 0, 255);\n"
        "p_delay(1);\n"
        "if_closesub(toplevel_osrs_stretch:overlay_atmosphere);\n"
        "\n"
        "[debugproc,{s}_scene]\n"
        "~{s}_scene;\n" },
    { "Npc: patrol route (.npc)", EDITOR_SNIPPET_FILE_NPC,
        "// Goes in a .npc file in the quest's configs folder: a server overlay, these keys and no others.\n"
        "// e.g. server/scripts/quests/quest_{q}/configs/quest_{q}.npc\n"
        "// A key the npc's own record states already (hans has maxrange and a patrol) is stated twice\n"
        "// and refuses the RUN: change those in its record (Record panel), or patrol the quest's own npc.\n"
        "// {T} walks the route and starts it again. Each patrol line is a tile and the ticks to wait on it;\n"
        "// a tile is level_mapx_mapz_tilex_tilez. maxrange must reach the farthest tile from where it spawns.\n"
        "// [{s}] is the npc: put its name there (Copy name on the npc) if it is not already.\n"
        "[{s}]\n"
        "maxrange=30\n"
        "defaultmode=patrol\n"
        "patrol1=0_50_50_20_20,0\n"
        "patrol2=0_50_50_26_20,0\n"
        "patrol3=0_50_50_26_26,5\n"
        "patrol4=0_50_50_20_26,0\n" },
    { "Quest journal proc", EDITOR_SNIPPET_FILE_SCRIPT,
        "// Goes in a .rs2 script file. The quest's journal page: one paragraph for each stage.\n"
        "// A | starts a new line. ^journal_todo is the blue of what is still to do, ^journal_highlight the\n"
        "// red of a name, ^journal_done strikes a line through. To read it type ::{s}_journal\n"
        "[proc,{s}_journal]\n"
        "def_string $text;\n"
        "if (%{v} = ^{q}_not_started) {\n"
        "    $text = append(^journal_todo, \"I can start this quest by speaking to \");\n"
        "    $text = append($text, ^journal_highlight);\n"
        "    $text = append($text, \"someone\");\n"
        "    $text = append($text, ^journal_todo);\n"
        "    $text = append($text, \".\");\n"
        "} else if (%{v} = ^{q}_started) {\n"
        "    $text = append(^journal_done, \"I have agreed to help.|\");\n"
        "    $text = append($text, ^journal_todo);\n"
        "    $text = append($text, \"I should do what was asked of me.\");\n"
        "} else {\n"
        "    $text = append(^journal_done, \"I did what was asked of me.||\");\n"
        "    $text = append($text, ^journal_complete);\n"
        "    $text = append($text, \"QUEST COMPLETE!\");\n"
        "}\n"
        "~quest_journal(\"{Q}\", $text);\n"
        "\n"
        "[debugproc,{s}_journal]\n"
        "~{s}_journal;\n" },
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
    assert(file <= EDITOR_SNIPPET_FILE_NPC);
    return file == EDITOR_SNIPPET_FILE_VARP       ? ".varp"
           : file == EDITOR_SNIPPET_FILE_CONSTANT ? ".constant"
           : file == EDITOR_SNIPPET_FILE_NPC      ? ".npc"
                                                  : ".rs2";
}

/** `text` with its marks filled for the record `subject`, the quest `quest`
 *  (NULL or "": the record's name stands in) and the quest's variable `var`
 *  (NULL or "": <quest>_quest), into `out`: asserted to fit, never cut.
 *  Returns the length, the terminator excluded. */
static size_t
render(
    const char* text,
    const char* subject,
    const char* quest,
    const char* var,
    char* out,
    size_t out_size)
{
    size_t n = 0;

    assert(subject);
    assert(subject[0] != '\0');
    assert(out);
    (void)out_size; /* asserted below, never cut: read only by assert() */
    assert(strlen(subject) < EDITOR_SNIPPET_SUBJECT_MAX);
    if( !quest || !quest[0] )
        quest = subject;
    assert(strlen(quest) < EDITOR_SNIPPET_SUBJECT_MAX);

    if( var && !var[0] )
        var = NULL;
    assert(!var || strlen(var) < EDITOR_SNIPPET_SUBJECT_MAX);

    for( const char* t = text; *t; )
    {
        /* {v} the quest's variable: its own name, else <quest>_quest. */
        if( t[0] == '{' && t[1] == 'v' && t[2] == '}' )
        {
            static const char k_suffix[] = "_quest";
            size_t const length = var ? strlen(var) : strlen(quest) + sizeof(k_suffix) - 1;
            assert(n + length < out_size);
            if( var )
                memcpy(out + n, var, length);
            else
            {
                memcpy(out + n, quest, strlen(quest));
                memcpy(out + n + strlen(quest), k_suffix, sizeof(k_suffix) - 1);
            }
            n += length;
            t += 3;
            continue;
        }
        /* {s} / {q} a name as written; {t}, {T} and {Q} a name in words. */
        if( t[0] == '{' && (t[1] == 's' || t[1] == 't' || t[1] == 'T' || t[1] == 'q' || t[1] == 'Q') && t[2] == '}' )
        {
            const char* name = t[1] == 'q' || t[1] == 'Q' ? quest : subject;
            size_t const length = strlen(name);
            int const words = t[1] == 't' || t[1] == 'T' || t[1] == 'Q';
            int const capital = t[1] == 'T' || t[1] == 'Q';
            assert(n + length < out_size);
            for( size_t i = 0; i < length; i++ )
            {
                char c = name[i];
                if( words && c == '_' )
                    c = ' ';
                if( capital && i == 0 && c >= 'a' && c <= 'z' )
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

size_t
Editor_SnippetRender(
    enum Editor_SnippetKind kind,
    int index,
    const char* subject,
    const char* quest,
    const char* quest_var,
    char* out,
    size_t out_size)
{
    return render(snippet_at(kind, index)->text, subject, quest, quest_var, out, out_size);
}

/* ---- a new quest's files ------------------------------------------------------
 *
 * What "New quest" writes (edpanel_triggers.c), after quests/quest_cook: the
 * variable, its stages, and a script that already builds -- three cheats that
 * set the stage, and a Talk-to by stage left as a comment because it needs an
 * npc's name. The variable is named as the panel allocated it
 * (varp<N>_<name>_quest: it compiles at once, and no RUN has to rename it), or
 * <name>_quest with no id when it could not; the stages are the snippets'
 * own (^<name>_started), so a snippet copied for the quest agrees with its
 * files.
 */

static const char k_quest_varp[] =
    "// {T}: the quest's progress, one number for each player.\n"
    "//   0 not started, 1 started, 2 complete (named in quest_{s}.constant).\n"
    "// scope=perm keeps it across logins. transmit=no: only the server reads it;\n"
    "// write transmit=yes when a client script must see it.\n"
    "//\n"
    "// A script reads and writes it as %{v}\n"
    "// A variable's name carries its kind and its id. One written with neither, [my_name],\n"
    "// is given both by the first RUN, here and in every script of the lane that reads it.\n"
    "[{v}]\n"
    "scope=perm\n"
    "clientcode=default\n"
    "transmit=no\n"
    "protect=no\n"
    "wholewrite=deny\n"
    "wholeread=deny\n";

static const char k_quest_constant[] =
    "// {T}: the values the quest's variable takes, named so a script reads\n"
    "// ^{s}_started and not 1. Add a line for each step between started and complete,\n"
    "// and keep ^{s}_complete the highest.\n"
    "^{s}_not_started = 0\n"
    "^{s}_started = 1\n"
    "^{s}_complete = 2\n";

/* The first trigger header's line, 1-based: where the editor is opened. */
#define QUEST_SCRIPT_LINE 7

static const char k_quest_script[] =
    "// {T}: the quest's scripts (ToriRSEd, Triggers > New quest).\n"
    "// Its variable is ../configs/quest_{s}.varp, its stages ../configs/quest_{s}.constant.\n"
    "// Paste snippets from the Triggers panel below; RUN (Ctrl+Enter) builds them.\n"
    "\n"
    "// Cheats for testing, typed in the play client's chat box: ::{s} puts the quest\n"
    "// back to not started, ::{s}_start starts it, ::{s}_done completes it.\n"
    "[debugproc,{s}]\n"
    "%{v} = ^{s}_not_started;\n"
    "mes(\"{T}: not started.\");\n"
    "\n"
    "[debugproc,{s}_start]\n"
    "%{v} = ^{s}_started;\n"
    "mes(\"{T}: started.\");\n"
    "\n"
    "[debugproc,{s}_done]\n"
    "%{v} = ^{s}_complete;\n"
    "mes(\"{T}: complete.\");\n"
    "\n"
    "// A Talk-to that follows the stages. Take the // off each line and put the npc's\n"
    "// name in place of your_npc (Triggers > Copy name); the npc needs op1=Talk-to\n"
    "// (Record panel).\n"
    "//[opnpc1,your_npc]\n"
    "//if (%{v} = ^{s}_not_started) {\n"
    "//    ~chatnpc_anim(^chat_sad, \"I could use some help.\");\n"
    "//    if (~p_choice2_header(\"Yes.\", 1, \"No.\", 2, \"Start the {T} quest?\") = 1) {\n"
    "//        %{v} = ^{s}_started;\n"
    "//        mes(\"You've started a new quest: @q_mes@{T}</col>\");\n"
    "//    }\n"
    "//} else if (%{v} = ^{s}_started) {\n"
    "//    ~chatnpc_anim(^chat_quiz, \"Any luck?\");\n"
    "//} else {\n"
    "//    ~chatnpc_anim(^chat_happy, \"Thank you for your help.\");\n"
    "//}\n";

int
Editor_QuestNameFrom(
    const char* typed,
    char* out,
    size_t out_size)
{
    static const char k_prefix[] = "quest_";
    size_t length;

    assert(typed);
    assert(out);
    assert(out_size >= EDITOR_QUEST_NAME_MAX);
    (void)out_size; /* read only by the assert: the name's own limit is the bound */
    out[0] = '\0';
    /* "quest_thistle" names the folder, not the quest: the same quest. */
    if( strncmp(typed, k_prefix, sizeof(k_prefix) - 1) == 0 )
        typed += sizeof(k_prefix) - 1;
    length = strlen(typed);
    if( length == 0 || length >= EDITOR_QUEST_NAME_MAX || typed[0] < 'a' || typed[0] > 'z' )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = typed[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    memcpy(out, typed, length + 1);
    return 1;
}

void
Editor_QuestFolder(
    const char* name,
    char* out,
    size_t out_size)
{
    int written;

    assert(name);
    assert(out);
    written = snprintf(out, out_size, "quests/quest_%s", name);
    assert(written > 0);
    assert((size_t)written < out_size);
    (void)written;
}

void
Editor_QuestFilePath(
    const char* name,
    enum Editor_SnippetFile file,
    char* out,
    size_t out_size)
{
    int written;

    assert(name);
    assert(out);
    assert(file == EDITOR_SNIPPET_FILE_SCRIPT || file == EDITOR_SNIPPET_FILE_VARP
           || file == EDITOR_SNIPPET_FILE_CONSTANT);
    written = snprintf(out, out_size, "quests/quest_%s/%s/quest_%s%s", name,
        file == EDITOR_SNIPPET_FILE_SCRIPT ? "scripts" : "configs", name, Editor_SnippetFileExtension(file));
    assert(written > 0);
    assert((size_t)written < out_size);
    (void)written;
}

size_t
Editor_QuestFileText(
    const char* name,
    const char* var,
    enum Editor_SnippetFile file,
    char* out,
    size_t out_size)
{
    assert(file == EDITOR_SNIPPET_FILE_SCRIPT || file == EDITOR_SNIPPET_FILE_VARP
           || file == EDITOR_SNIPPET_FILE_CONSTANT);
    return render(file == EDITOR_SNIPPET_FILE_VARP       ? k_quest_varp
                  : file == EDITOR_SNIPPET_FILE_CONSTANT ? k_quest_constant
                                                         : k_quest_script,
        name, NULL, var, out, out_size);
}

int
Editor_QuestScriptLine(void)
{
    return QUEST_SCRIPT_LINE;
}

int
Editor_QuestVarName(
    const char* text,
    size_t length,
    char* out,
    size_t out_size)
{
    assert(text || length == 0);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( size_t at = 0; at < length; )
    {
        const char* line = text + at;
        const char* end = memchr(line, '\n', length - at);
        size_t const line_length = end ? (size_t)(end - line) : length - at;

        if( line_length > 2 && line[0] == '[' )
        {
            const char* close = memchr(line, ']', line_length);
            size_t const name_length = close ? (size_t)(close - line) - 1 : 0;
            if( name_length == 0 || name_length >= out_size )
                return 0;
            memcpy(out, line + 1, name_length);
            out[name_length] = '\0';
            return 1;
        }
        at += line_length + 1;
    }
    return 0;
}
