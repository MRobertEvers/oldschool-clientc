/*
 * The interaction builders (MAPED_IMPLEMENTATION.md phase 3 `builders`).
 *
 *   builder_test <content-dir> <tmp-dir> <sscompile> <golden-dir>
 *
 * content-dir is READ ONLY (OSRS-Content/osrs-content). Everything written
 * goes under tmp-dir: the embedded authority's content tree, the compile
 * stage and its --out. BUILDER_GOLDEN_UPDATE=1 rewrites the golden files
 * instead of comparing against them.
 *
 * Sections:
 *   ini        the .interaction model round-trips; damaged text is refused
 *   golden     emitter output for a loc and an npc fixture is byte-stable
 *   dialogue   shared tails become labels, loops jump, nesting is if/else
 *   lint       the NBSP rule, quotes, {player}
 *   conflicts  a hand-authored trigger makes the subject read-only
 *   splices    text diffs cut into TEXT_SPLICEs apply and undo exactly
 *   shell      multiloc/multinpc: 1-based, -1 hidden, last default; every
 *              real shell in all.loc / all.npc parses to no-op commands;
 *              edits apply and undo byte-exact through Editor_CmdApply
 *   preview    STATE_VAR_PREVIEW encoding
 *   authority  regenerate / save / undo / detach through an embedded
 *              ToriRSMapEd, and the panels driven over the real chrome
 *   compile    a generated Talk-to compiles under sscompile against a stage
 *              copy of the scripts tree; a broken one names its line
 *   graph      (workbench 6.6) ports, links, reach and orphans; auto-layout
 *              by depth, stable and a pure function of the links; pos=
 *              round-trips
 *   chat       the chat box's line rule: page breaks at synthetic metrics
 *              by hand, and at the baked p12_full against a greedy wrap
 *   lintrows   the dialogue lint: literal player name, orphan, one-arm
 *              choice, unknown var, unknown proc, each at its node or step
 *   graphpanel the NPC builder's graph driven by GRAPH events: a port
 *              dragged to a node links, dropped on empty space opens the
 *              chip row, Shift+drag moves a subtree, Ctrl+L, the outline
 *              and the graph select together, the pages and the preview,
 *              lint rows to Problems and back, Delete names the orphans
 *
 * Without the content tree (OSRS-Content not checked out) the sweep over
 * every real shell and the compile section are skipped and say so; the
 * rest is content-free.
 */
#include "editor/builder/builder_chat.h"
#include "editor/builder/builder_emit.h"
#include "editor/builder/builder_lint.h"
#include "editor/builder/builder_model.h"
#include "editor/builder/builder_panels.h"
#include "editor/builder/builder_shell.h"
#include "editor/builder/builder_ssc.h"
#include "editor/builder/builder_text.h"
#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_problems_api.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int g_checks;
static int g_failed;
/** The content dir holds configs/all.loc (OSRS-Content is checked out). */
static int g_have_content;

static void
check(
    int ok,
    const char* what)
{
    g_checks++;
    if( !ok )
    {
        g_failed++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* data;

    *out_size = 0;
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    assert(data);
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        fclose(file);
        free(data);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    *out_size = (size_t)size;
    return data;
}

static int
spit(
    const char* path,
    const char* data,
    size_t size)
{
    FILE* file = fopen(path, "wb");
    int ok;
    if( !file )
        return 0;
    ok = size == 0 || fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

static void
mkdir_p(const char* path)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    for( char* at = dir + 1; *at; at++ )
    {
        if( *at == '/' )
        {
            *at = '\0';
            mkdir(dir, 0755);
            *at = '/';
        }
    }
    mkdir(dir, 0755);
}

/* ---- fixtures ------------------------------------------------------------ */

static void
step(
    struct Builder_Trigger* trigger,
    enum Builder_StepKind kind,
    const char* a,
    const char* b,
    const char* c,
    const char* text)
{
    struct Builder_Step s;
    Builder_StepInit(&s, kind);
    snprintf(s.a, sizeof(s.a), "%s", a);
    snprintf(s.b, sizeof(s.b), "%s", b);
    snprintf(s.c, sizeof(s.c), "%s", c);
    snprintf(s.text, sizeof(s.text), "%s", text);
    int added = Builder_TriggerStepAdd(trigger, &s);
    assert(added >= 0);
    (void)added;
}

/** A crate: search with a var gate and a reward, a use-on chain, a raw
 *  escape, and server fields. */
static void
fixture_loc(struct Builder_Model* model)
{
    struct Builder_Trigger* t;

    Builder_ModelInit(model, BUILDER_SUBJECT_LOC, "viking_crate");
    snprintf(model->server.category, sizeof(model->server.category), "%s", "crate");
    snprintf(model->server.params[0].name, BUILDER_NAME_MAX, "%s", "searched_ticks");
    snprintf(model->server.params[0].value, BUILDER_NAME_MAX, "%s", "3");
    model->server.param_count = 1;

    t = Builder_ModelTriggerAdd(model, 1, 0, "");
    snprintf(t->label, sizeof(t->label), "%s", "Search");
    step(t, BUILDER_STEP_ANIM, "human_pickuptable", "", "", "");
    step(t, BUILDER_STEP_SOUND, "pick2", "", "", "");
    step(t, BUILDER_STEP_DELAY, "1", "", "", "");
    step(t, BUILDER_STEP_IF_VAR, "varp7_mock_quest_progress", "=", "2", "");
    step(t, BUILDER_STEP_MES, "", "", "", "You find a bucket in the crate.");
    step(t, BUILDER_STEP_GIVE, "bucket_empty", "1", "", "");
    step(t, BUILDER_STEP_SET_VAR, "varp7_mock_quest_progress", "3", "", "");
    step(t, BUILDER_STEP_ELSE, "", "", "", "");
    step(t, BUILDER_STEP_MES, "", "", "", "There is nothing interesting in the crate.");
    step(t, BUILDER_STEP_END, "", "", "", "");

    t = Builder_ModelTriggerAdd(model, 2, 1, "");
    snprintf(t->label, sizeof(t->label), "%s", "Kick");
    step(t, BUILDER_STEP_REQUIRE_LEVEL, "strength", "10", "", "You are not strong enough.");
    step(t, BUILDER_STEP_LOC_SWAP, "viking_crate_2", "50", "", "");
    step(t, BUILDER_STEP_CALL, "chatplayer", "\"Ow, my foot.\"", "", "");

    t = Builder_ModelTriggerAdd(model, 0, 0, "hammer");
    step(t, BUILDER_STEP_REQUIRE_OBJ, "hammer", "1", "", "You need a hammer.");
    step(t, BUILDER_STEP_TELEPORT, "0_50_50_10_10", "", "", "");
    t = Builder_ModelTriggerAdd(model, 0, 0, "coins");
    step(t, BUILDER_STEP_TAKE, "coins", "10", "", "");
    step(t, BUILDER_STEP_RAW, "", "", "", "if (inv_total(inv, coins) = 0) {\n    mes(\"Broke.\");\n}");
}

static struct Builder_Node*
node(
    struct Builder_Model* model,
    const char* id,
    enum Builder_NodeKind kind,
    const char* text,
    const char* next)
{
    struct Builder_Node* n = Builder_ModelNodeAdd(model, kind);
    assert(n);
    snprintf(n->id, sizeof(n->id), "%s", id);
    snprintf(n->text, sizeof(n->text), "%s", text);
    snprintf(n->next, sizeof(n->next), "%s", next);
    return n;
}

/** A Talk-to: greeting, a choice whose two arms share a tail (a label), a
 *  branch on a var, an item box, a loop back to the choice, a quest start
 *  and an explicit end. */
static void
fixture_npc(
    struct Builder_Model* model,
    const char* name)
{
    struct Builder_Trigger* t;
    struct Builder_Node* n;

    Builder_ModelInit(model, BUILDER_SUBJECT_NPC, name);
    t = Builder_ModelTriggerAdd(model, 1, 0, "");
    snprintf(t->label, sizeof(t->label), "%s", "Talk-to");
    t->dialogue = 1;
    t = Builder_ModelTriggerAdd(model, 0, 0, "*");
    t->dialogue = 1;

    node(model, "hello", BUILDER_NODE_NPC, "Welcome to the bar, {player}. What can I get you?", "ask");
    snprintf(model->start, sizeof(model->start), "%s", "hello");
    n = node(model, "ask", BUILDER_NODE_CHOICE, "", "");
    n->arm_count = 3;
    snprintf(n->arms[0].text, sizeof(n->arms[0].text), "%s", "A beer, please.");
    snprintf(n->arms[0].next, sizeof(n->arms[0].next), "%s", "beer");
    snprintf(n->arms[1].text, sizeof(n->arms[1].text), "%s", "Any work going?");
    snprintf(n->arms[1].next, sizeof(n->arms[1].next), "%s", "work");
    snprintf(n->arms[2].text, sizeof(n->arms[2].text), "%s", "Nothing, thanks.");
    snprintf(n->arms[2].next, sizeof(n->arms[2].next), "%s", "bye");
    n = node(model, "beer", BUILDER_NODE_PLAYER, "A beer, please.", "pay");
    snprintf(n->expr, sizeof(n->expr), "%s", "happy");
    n = node(model, "pay", BUILDER_NODE_STEP, "", "served");
    Builder_StepInit(&n->step, BUILDER_STEP_REQUIRE_OBJ);
    snprintf(n->step.a, sizeof(n->step.a), "%s", "coins");
    snprintf(n->step.b, sizeof(n->step.b), "%s", "2");
    snprintf(n->step.text, sizeof(n->step.text), "%s", "You can't afford a beer.");
    n = node(model, "served", BUILDER_NODE_OBJBOX, "The patron hands you a beer.", "thanks");
    snprintf(n->obj, sizeof(n->obj), "%s", "beer");
    n = node(model, "work", BUILDER_NODE_BRANCH, "", "nowork");
    snprintf(n->obj, sizeof(n->obj), "%s", "varp7_mock_quest_progress");
    n->arm_count = 1;
    snprintf(n->arms[0].cmp, sizeof(n->arms[0].cmp), "%s", "<");
    snprintf(n->arms[0].value, sizeof(n->arms[0].value), "%s", "2");
    snprintf(n->arms[0].next, sizeof(n->arms[0].next), "%s", "job");
    n = node(model, "job", BUILDER_NODE_NPC, "Clean the tables and I'll see you right.", "start");
    snprintf(n->expr, sizeof(n->expr), "%s", "neutral");
    n = node(model, "start", BUILDER_NODE_QUEST, "", "thanks");
    n->quest = BUILDER_QUEST_START;
    snprintf(n->obj, sizeof(n->obj), "%s", "varp7_mock_quest_progress");
    snprintf(n->value, sizeof(n->value), "%s", "2");
    n = node(model, "nowork", BUILDER_NODE_NPC, "Not today. Anything else?", "ask");
    snprintf(n->expr, sizeof(n->expr), "%s", "sad");
    n = node(model, "thanks", BUILDER_NODE_PLAYER, "Thanks.", "bye");
    n->expr[0] = '\0';
    n = node(model, "bye", BUILDER_NODE_MESBOX, "The patron goes back to polishing a glass.", "end");
    node(model, "end", BUILDER_NODE_END, "", "");
    for( int i = 0; i < model->node_count; i++ )
    {
        model->nodes[i].x = 10 + (i % 4) * 136;
        model->nodes[i].y = 10 + (i / 4) * 56;
    }
}

/* ---- ini ----------------------------------------------------------------- */

static struct Builder_Model*
model_new(void)
{
    struct Builder_Model* model = malloc(sizeof(*model));
    assert(model);
    memset(model, 0, sizeof(*model));
    return model;
}

static char*
emit_ini(
    const struct Builder_Model* model,
    size_t* out_length)
{
    size_t length = Builder_ModelEmitIni(model, NULL, 0);
    char* text = malloc(length + 1);
    assert(text);
    Builder_ModelEmitIni(model, text, length + 1);
    *out_length = length;
    return text;
}

static void
test_ini(void)
{
    struct Builder_Model* a = model_new();
    struct Builder_Model* b = model_new();
    char error[160];
    size_t length;
    size_t length2;
    char* text;
    char* text2;
    const char* bad[] = {
        "[interaction]\nversion=1\nsubject=loc\nname=x\n[op1]\nstep=nope|||\n",
        "[interaction]\nversion=1\nsubject=loc\nname=x\n[op9]\n",
        "[interaction]\nversion=2\nsubject=loc\nname=x\n",
        "[interaction]\nversion=1\nsubject=obj\nname=x\n",
        "[interaction]\nversion=1\nsubject=npc\nname=x\n[dialogue]\nstart=n9\n[node,n1]\nkind=end\n",
        "[interaction]\nversion=1\nsubject=npc\nname=x\n[node,n1]\nkind=npc\nnext=Bad Id\n",
        "name=x\n",
        "[interaction]\nversion=1\nsubject=loc\nname=x\njunk line\n",
        "[interaction]\nversion=1\nsubject=loc\nname=x\n[op1]\nstep=mes|a|b|c|t\\q\n",
    };

    fixture_loc(a);
    text = emit_ini(a, &length);
    check(Builder_ModelParseIni(b, text, length, error, sizeof(error)), "the loc model reads back");
    text2 = emit_ini(b, &length2);
    check(length == length2 && memcmp(text, text2, length) == 0, "the loc model round-trips byte-exact");
    check(b->trigger_count == a->trigger_count && b->triggers[0].step_count == a->triggers[0].step_count,
          "the round trip keeps every trigger and step");
    check(strcmp(b->triggers[3].steps[1].text, a->triggers[3].steps[1].text) == 0,
          "a raw step's line breaks survive the escapes");
    free(text);
    free(text2);

    fixture_npc(a, "port_roberts_bar_patron");
    text = emit_ini(a, &length);
    check(Builder_ModelParseIni(b, text, length, error, sizeof(error)), "the npc model reads back");
    text2 = emit_ini(b, &length2);
    check(length == length2 && memcmp(text, text2, length) == 0, "the npc model round-trips byte-exact");
    check(b->node_count == a->node_count && strcmp(b->start, "hello") == 0,
          "the graph's nodes and start survive");
    free(text);
    free(text2);

    check(!Builder_ModelParseIni(b, "", 0, error, sizeof(error)), "an empty model is no model");
    for( size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++ )
    {
        char what[96];
        snprintf(what, sizeof(what), "damaged model %zu is refused", i);
        check(!Builder_ModelParseIni(b, bad[i], strlen(bad[i]), error, sizeof(error)) && error[0], what);
    }
    free(a);
    free(b);
}

/* ---- golden -------------------------------------------------------------- */

static void
golden(
    const char* dir,
    const char* name,
    const char* text,
    size_t length)
{
    char path[1024];
    char what[160];
    size_t size;
    char* expected;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if( getenv("BUILDER_GOLDEN_UPDATE") && strcmp(getenv("BUILDER_GOLDEN_UPDATE"), "1") == 0 )
    {
        check(spit(path, text, length), "golden file written");
        return;
    }
    expected = slurp(path, &size);
    snprintf(what, sizeof(what), "%s matches its golden file", name);
    check(expected && size == length && memcmp(expected, text, length) == 0, what);
    if( expected && (size != length || memcmp(expected, text, length) != 0) )
    {
        char got[1100];
        snprintf(got, sizeof(got), "%s.got", path);
        fprintf(stderr, "      (wrote the output beside it as %s.got)\n", name);
        spit(got, text, length);
    }
    free(expected);
}

static void
golden_model(
    const char* dir,
    const struct Builder_Model* model,
    const char* stem)
{
    struct Builder_Buf buf;
    struct Builder_Issues issues;
    char name[128];
    size_t length;
    char* ini = emit_ini(model, &length);

    snprintf(name, sizeof(name), "%s.interaction", stem);
    golden(dir, name, ini, length);
    free(ini);

    Builder_IssuesClear(&issues);
    Builder_BufInit(&buf);
    Builder_EmitScript(model, &buf, &issues);
    snprintf(name, sizeof(name), "%s.rs2", stem);
    golden(dir, name, Builder_BufText(&buf), buf.length);
    check(issues.errors == 0, "a fixture validates with no errors");
    if( issues.errors )
    {
        for( int i = 0; i < issues.count; i++ )
            fprintf(stderr, "      %s: %s\n", issues.items[i].where, issues.items[i].message);
    }
    Builder_BufFree(&buf);

    Builder_BufInit(&buf);
    Builder_EmitConfig(model, &buf, &issues);
    snprintf(name, sizeof(name), "%s.%s", stem, model->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc");
    golden(dir, name, Builder_BufText(&buf), buf.length);
    Builder_BufFree(&buf);

    Builder_BufInit(&buf);
    Builder_EmitDbrow(model, &buf, &issues);
    snprintf(name, sizeof(name), "%s.dbrow", stem);
    golden(dir, name, Builder_BufText(&buf), buf.length);
    Builder_BufFree(&buf);
}

static void
test_golden(const char* dir)
{
    struct Builder_Model* model = model_new();
    struct Builder_Buf a;
    struct Builder_Buf b;
    struct Builder_Issues issues;

    fixture_loc(model);
    golden_model(dir, model, "builder_loc_crate");
    fixture_npc(model, "port_roberts_bar_patron");
    golden_model(dir, model, "builder_npc_patron");

    /* A door and a ladder are presets: field edits, no script. */
    Builder_ModelInit(model, BUILDER_SUBJECT_LOC, "ed_test_door");
    model->server.door = BUILDER_DOOR_CLOSED;
    snprintf(model->server.door_other, sizeof(model->server.door_other), "%s", "ed_test_door_open");
    golden_model(dir, model, "builder_loc_door");
    Builder_ModelInit(model, BUILDER_SUBJECT_LOC, "ed_test_ladder");
    model->server.ladder = BUILDER_LADDER_DOWN;
    model->server.ladder_animated = 1;
    snprintf(model->server.maplinks[0].src, BUILDER_NAME_MAX, "%s", "0_50_50_10_10");
    snprintf(model->server.maplinks[0].dest, BUILDER_NAME_MAX, "%s", "0_50_150_10_10");
    model->server.maplink_count = 1;
    golden_model(dir, model, "builder_loc_ladder");

    /* Stable: emitting twice gives the same bytes. */
    fixture_npc(model, "port_roberts_bar_patron");
    Builder_IssuesClear(&issues);
    Builder_BufInit(&a);
    Builder_BufInit(&b);
    Builder_EmitScript(model, &a, &issues);
    Builder_EmitScript(model, &b, &issues);
    check(a.length == b.length && memcmp(a.data, b.data, a.length) == 0, "the emitter is deterministic");
    Builder_BufFree(&a);
    Builder_BufFree(&b);
    free(model);
}

/* ---- dialogue structure ------------------------------------------------- */

static int
contains(
    const char* text,
    const char* needle)
{
    return strstr(text, needle) != NULL;
}

static int
count_of(
    const char* text,
    const char* needle)
{
    int count = 0;
    for( const char* at = strstr(text, needle); at; at = strstr(at + 1, needle) )
        count++;
    return count;
}

static void
test_dialogue(void)
{
    struct Builder_Model* model = model_new();
    struct Builder_Buf out;
    struct Builder_Issues issues;
    const char* text;
    char names[64][BUILDER_TRIGGER_NAME_MAX];
    int name_count;

    fixture_npc(model, "port_roberts_bar_patron");
    Builder_IssuesClear(&issues);
    Builder_BufInit(&out);
    Builder_EmitScript(model, &out, &issues);
    text = Builder_BufText(&out);

    check(contains(text, "[opnpc1,port_roberts_bar_patron]\n@ed_port_roberts_bar_patron_talk;"),
          "Talk-to jumps into the dialogue label");
    check(contains(text, "[opnpcu,port_roberts_bar_patron]\n@ed_port_roberts_bar_patron_talk;"),
          "a use-on '*' entry with the dialogue is the trigger's whole body");
    check(contains(text, "[label,ed_port_roberts_bar_patron_talk]\n"), "the start is the talk label");
    check(contains(text, "def_int $choice_ask = ~p_choice3(\"A beer, please.\", 1, \"Any work going?\", 2, "
                         "\"Nothing, thanks.\", 3);"),
          "a choice is one p_choice call");
    check(contains(text, "if ($choice_ask = 1) {") && contains(text, "} else if ($choice_ask = 2) {"),
          "the options are an if / else if chain");
    check(contains(text, "[label,ed_port_roberts_bar_patron_ask]"),
          "the choice, reached again from a later node, is a label (a loop)");
    check(contains(text, "@ed_port_roberts_bar_patron_ask;"), "the loop back is a jump");
    check(contains(text, "[label,ed_port_roberts_bar_patron_thanks]")
              && count_of(text, "@ed_port_roberts_bar_patron_thanks;") == 2,
          "a tail two arms share is written once and jumped to twice");
    check(contains(text, "if (%varp7_mock_quest_progress < 2) {") && contains(text, "} else {"),
          "a branch is if / else with the default arm");
    check(contains(text, "Welcome to the bar, <displayname>."), "{player} becomes the display-name helper");
    check(contains(text, "~chatplayer_anim(^chat_happy, \"A beer, please.\");"),
          "a player page with an expression");
    check(contains(text, "~chatplayer(\"Thanks.\");"), "a player page without one");
    check(contains(text, "if (inv_total(inv, coins) < 2) {"), "a dialogue step uses the loc vocabulary");
    check(contains(text, "~objbox(beer, \"The patron hands you a beer.\");"), "an item box");
    check(contains(text, "%varp7_mock_quest_progress = 2;"), "the quest start writes the progress var");
    check(contains(text, "    return;\n") || contains(text, "return;\n"), "an End returns");
    check(issues.errors == 0, "the fixture has no errors");

    name_count = Builder_ScriptTriggerNames(model, names, 64);
    check(name_count == 6, "the trigger list names every header the script writes");
    Builder_BufFree(&out);

    /* An unreachable node is a warning and is not emitted. */
    node(model, "orphan", BUILDER_NODE_MESBOX, "Nobody reads this.", "");
    Builder_IssuesClear(&issues);
    Builder_BufInit(&out);
    Builder_EmitScript(model, &out, &issues);
    check(!contains(Builder_BufText(&out), "Nobody reads this."), "an unreachable node is not emitted");
    {
        int warned = 0;
        for( int i = 0; i < issues.count; i++ )
            warned |= strcmp(issues.items[i].where, "node orphan") == 0;
        check(warned, "an unreachable node is reported");
    }
    Builder_BufFree(&out);

    /* A port to nowhere and an if step in a dialogue are errors. */
    snprintf(Builder_ModelNode(model, "orphan")->next, BUILDER_NODE_ID_MAX, "%s", "missing");
    Builder_ModelNode(model, "pay")->step.kind = BUILDER_STEP_IF_VAR;
    Builder_IssuesClear(&issues);
    check(Builder_Validate(model, &issues) >= 2, "dangling ports and dialogue if-steps are errors");

    /* Removing a node clears the ports that led to it. */
    Builder_ModelNodeRemove(model, "thanks");
    check(Builder_ModelNode(model, "job") && Builder_ModelNode(model, "served")->next[0] == '\0',
          "removing a node ends the dialogue where its ports were");

    /* Loc: an unbalanced else is an error; an open if is closed. */
    {
        struct Builder_Trigger* t;
        Builder_ModelInit(model, BUILDER_SUBJECT_LOC, "x_crate");
        t = Builder_ModelTriggerAdd(model, 1, 0, "");
        step(t, BUILDER_STEP_ELSE, "", "", "", "");
        step(t, BUILDER_STEP_IF_VAR, "varp1_x", "=", "1", "");
        step(t, BUILDER_STEP_MES, "", "", "", "open");
        Builder_IssuesClear(&issues);
        Builder_BufInit(&out);
        Builder_EmitScript(model, &out, &issues);
        check(issues.errors == 1, "an Else with no If is an error");
        check(contains(Builder_BufText(&out), "if (%varp1_x = 1) {\n    mes(\"open\");\n}\n"),
              "an If left open is closed at the end");
        Builder_BufFree(&out);
        /* Injection: a name with code in it is refused. */
        Builder_StepInit(&t->steps[0], BUILDER_STEP_GIVE);
        snprintf(t->steps[0].a, BUILDER_NAME_MAX, "%s", "coins, 1); inv_clear(inv");
        Builder_IssuesClear(&issues);
        check(Builder_Validate(model, &issues) >= 1, "a field that would write code is refused");
        /* The door preset owns op1. */
        Builder_ModelInit(model, BUILDER_SUBJECT_LOC, "x_door");
        model->server.door = BUILDER_DOOR_CLOSED;
        snprintf(model->server.door_other, BUILDER_NAME_MAX, "%s", "x_door_open");
        Builder_ModelTriggerAdd(model, 1, 0, "");
        Builder_IssuesClear(&issues);
        check(Builder_Validate(model, &issues) >= 1, "a door preset with its own op1 is an error");
    }
    free(model);
}

/* ---- lint ---------------------------------------------------------------- */

static void
test_lint(void)
{
    struct Builder_Issues issues;

    Builder_IssuesClear(&issues);
    check(Builder_LintText("Hello, <displayname>.", "t", &issues), "the helper is clean");
    check(Builder_LintText("He said <text_gender(\"he\", \"she\")>.", "t", &issues),
          "a quote inside <...> is clean");
    check(!Builder_LintText("Welcome back, Blurite\xc2\xa0Rock.", "t", &issues),
          "a recorded display name (U+00A0) is refused");
    check(!Builder_LintText("He said \"hi\".", "t", &issues), "a bare quote is refused");
    check(!Builder_LintText("line\nbreak", "t", &issues), "a line break is refused");
    check(!Builder_LintText("<col=ff0000 unclosed", "t", &issues), "an unclosed tag is refused");
    check(issues.errors == 4, "each finding is one error");
}

/* ---- conflicts ----------------------------------------------------------- */

static void
test_conflicts(void)
{
    struct Builder_Model* model = model_new();
    struct Builder_Issues issues;
    struct Builder_ExistingTrigger rows[] = {
        { "oploc1,viking_crate", "server/scripts/areas/area_rellekka/scripts/rellekka.rs2" },
        { "oploc2,viking_crate", "server/scripts/edited/viking_crate/scripts/viking_crate.rs2" },
        { "oploc1,viking_crate_2", "server/scripts/areas/x.rs2" },
    };

    fixture_loc(model);
    Builder_IssuesClear(&issues);
    check(Builder_FindConflicts(model, rows, 3, &issues) == 1,
          "a hand-authored [oploc1] elsewhere conflicts; the builder's own script and another subject do not");
    free(model);
}

/* ---- splices ------------------------------------------------------------- */

static uint32_t g_rng = 12345;

static uint32_t
rng(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}

static void
random_text(
    struct Builder_Buf* out,
    size_t length)
{
    static const char k_alphabet[] = "abc def\n[]=_xyz0123";
    for( size_t i = 0; i < length; i++ )
        Builder_BufPut(out, &k_alphabet[rng() % (sizeof(k_alphabet) - 1)], 1);
}

static void
test_splices(void)
{
    int all_ok = 1;
    int sizes_ok = 1;
    int undo_ok = 1;
    int cases = 0;

    for( int round = 0; round < 400; round++ )
    {
        struct Builder_Buf old_text;
        struct Builder_Buf new_text;
        struct Builder_Buf work;
        struct Editor_Cmd* commands;
        int count;
        size_t old_length = rng() % (round < 50 ? 64 : 6000);
        size_t cut;
        size_t remove;

        Builder_BufInit(&old_text);
        Builder_BufInit(&new_text);
        Builder_BufInit(&work);
        random_text(&old_text, old_length);
        /* new = old with a random region replaced by random text */
        cut = old_length ? rng() % (old_length + 1) : 0;
        remove = old_length - cut ? rng() % (old_length - cut + 1) : 0;
        Builder_BufPut(&new_text, Builder_BufText(&old_text), cut);
        random_text(&new_text, rng() % (round % 3 == 0 ? 4000 : 80));
        Builder_BufPut(&new_text, Builder_BufText(&old_text) + cut + remove, old_length - cut - remove);

        count = Builder_TextSplices(EDITOR_DOC_TEXT, "rs2:edited/x/x.rs2", Builder_BufText(&old_text),
                                    old_text.length, Builder_BufText(&new_text), new_text.length, NULL, 0);
        commands = malloc(sizeof(*commands) * (size_t)(count ? count : 1));
        assert(commands);
        Builder_TextSplices(EDITOR_DOC_TEXT, "rs2:edited/x/x.rs2", Builder_BufText(&old_text),
                            old_text.length, Builder_BufText(&new_text), new_text.length, commands, count);
        Builder_BufPut(&work, Builder_BufText(&old_text), old_text.length);
        for( int i = 0; i < count; i++ )
        {
            if( commands[i].u.text_splice.before_len + commands[i].u.text_splice.after_len
                > EDITOR_CMD_SPLICE_MAX )
                sizes_ok = 0;
            if( !Builder_TextSpliceApply(&work, &commands[i]) )
                all_ok = 0;
        }
        if( work.length != new_text.length || memcmp(Builder_BufText(&work), Builder_BufText(&new_text), work.length) != 0 )
            all_ok = 0;
        /* Undo: the inverse of each, newest first. */
        for( int i = count - 1; i >= 0; i-- )
        {
            struct Editor_Cmd inverse = commands[i];
            uint32_t b = commands[i].u.text_splice.before_len;
            uint32_t a = commands[i].u.text_splice.after_len;
            memcpy(inverse.u.text_splice.text, commands[i].u.text_splice.text + b, a);
            memcpy(inverse.u.text_splice.text + a, commands[i].u.text_splice.text, b);
            inverse.u.text_splice.before_len = a;
            inverse.u.text_splice.after_len = b;
            if( !Builder_TextSpliceApply(&work, &inverse) )
                undo_ok = 0;
        }
        if( work.length != old_text.length || memcmp(Builder_BufText(&work), Builder_BufText(&old_text), work.length) != 0 )
            undo_ok = 0;
        cases++;
        free(commands);
        Builder_BufFree(&old_text);
        Builder_BufFree(&new_text);
        Builder_BufFree(&work);
    }
    check(all_ok, "400 random edits: the splices turn the old text into the new");
    check(sizes_ok, "every splice fits one command");
    check(undo_ok, "the inverses, newest first, restore the old text");
    check(Builder_TextSplices(EDITOR_DOC_TEXT, "rs2:a.rs2", "same", 4, "same", 4, NULL, 0) == 0,
          "equal texts need no splice");
    (void)cases;
}

/* ---- shell --------------------------------------------------------------- */

static struct Editor_Doc*
doc_new(void)
{
    struct Editor_Doc* doc = malloc(sizeof(*doc));
    assert(doc);
    Editor_DocInit(doc, NULL);
    return doc;
}

/** Apply `count` commands to a CFG doc holding `block`, check the result
 *  parses to `want`, undo all, check the bytes are back. */
static int
shell_roundtrip(
    struct Editor_Doc* doc,
    const char* doc_key,
    const char* block,
    size_t length,
    const struct Builder_Shell* want,
    int* out_count)
{
    struct Editor_DocHandle handle;
    struct Editor_Cmd* commands;
    struct Builder_Shell got;
    int count;
    int ok = 1;

    if( !Editor_DocOpen(doc, EDITOR_DOC_CFG, doc_key, &handle) )
        return 0;
    Editor_TextDocSet(handle.text, block, length);
    count = Builder_ShellCommands(doc_key, block, length, want, NULL, 0);
    *out_count = count;
    commands = malloc(sizeof(*commands) * (size_t)(count ? count : 1));
    assert(commands);
    Builder_ShellCommands(doc_key, block, length, want, commands, count);
    for( int i = 0; i < count && ok; i++ )
        ok = Editor_CmdApply(doc, &commands[i], EDITOR_CMD_FORWARD);
    if( ok )
    {
        Editor_DocFind(doc, EDITOR_DOC_CFG, doc_key, &handle);
        Builder_ShellParse(&got, want->subject, handle.text->text, handle.text->length);
        ok = got.slot_count == want->slot_count && strcmp(got.var, want->var) == 0
             && got.var_kind == want->var_kind;
        for( int i = 0; ok && i < got.slot_count; i++ )
            ok = strcmp(got.slots[i].child, want->slots[i].child) == 0;
    }
    for( int i = count - 1; i >= 0 && ok; i-- )
        ok = Editor_CmdApply(doc, &commands[i], EDITOR_CMD_INVERSE);
    if( ok )
    {
        Editor_DocFind(doc, EDITOR_DOC_CFG, doc_key, &handle);
        ok = handle.text->length == length && memcmp(handle.text->text, block, length) == 0;
    }
    free(commands);
    return ok;
}

static void
shell_sweep(
    const char* content,
    enum Builder_Subject subject,
    struct Editor_Doc* doc)
{
    char path[1024];
    size_t size;
    char* text;
    struct Editor_CfgFile file;
    int shells = 0;
    int noop_ok = 0;
    int resolve_ok = 1;
    int edits = 0;
    int edits_ok = 0;
    char what[160];
    const char* type = subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";

    snprintf(path, sizeof(path), "%s/configs/all.%s", content, type);
    text = slurp(path, &size);
    check(text != NULL, "all.<loc|npc> reads (read-only)");
    if( !text )
        return;
    Editor_CfgFileIndex(&file, text, size);
    for( int b = 0; b < file.block_count; b++ )
    {
        const struct Editor_CfgBlockRef* ref = &file.blocks[b];
        struct Builder_Shell shell;
        char key[EDITOR_DOC_KEY_MAX];
        int count;

        if( !Builder_ShellParse(&shell, subject, text + ref->start, ref->end - ref->start) )
            continue;
        shells++;
        if( ref->name_length + 5 >= sizeof(key) )
            continue;
        snprintf(key, sizeof(key), "%s:%.*s", type, (int)ref->name_length, text + ref->name_start);
        count = Builder_ShellCommands(key, text + ref->start, ref->end - ref->start, &shell, NULL, 0);
        if( count == 0 )
            noop_ok++;
        else if( count - 0 > 0 && shells - noop_ok <= 3 )
        {
            struct Editor_Cmd shown[8];
            int n = Builder_ShellCommands(key, text + ref->start, ref->end - ref->start, &shell, shown, 8);
            fprintf(stderr, "      %s: %d command(s) for an untouched shell\n", key, count);
            for( int i = 0; i < n && i < 8; i++ )
                fprintf(stderr, "        %s: '%s' -> '%s'\n", shown[i].u.cfg_field.key,
                        shown[i].u.cfg_field.before, shown[i].u.cfg_field.after);
        }
        /* Resolution follows VarPManager_ResolveTransform: value N -> slot N
         * for N < count-1, everything else the last. */
        if( Builder_ShellResolve(&shell, 0) != (shell.slot_count > 1 ? 0 : shell.slot_count - 1)
            || Builder_ShellResolve(&shell, -1) != shell.slot_count - 1
            || Builder_ShellResolve(&shell, shell.slot_count + 5) != shell.slot_count - 1 )
            resolve_ok = 0;

        /* Every 211th shell: a real edit, applied and undone byte-exact. */
        if( shells % 211 == 0 && shell.slot_count < BUILDER_SHELL_SLOTS_MAX - 1
            && ref->name_length < EDITOR_CMD_NAME_MAX )
        {
            struct Builder_Shell edited = shell;
            int n;
            switch( (shells / 211) % 3 )
            {
            case 0:
                Builder_ShellInsertSlot(&edited, 0, "-1");
                break;
            case 1:
                snprintf(edited.slots[0].child, BUILDER_NAME_MAX, "%s",
                         Builder_ShellSlotHidden(&edited.slots[0]) ? edited.name : "-1");
                break;
            default:
                if( edited.slot_count > 1 )
                    Builder_ShellRemoveSlot(&edited, edited.slot_count - 1);
                else
                    Builder_ShellInsertSlot(&edited, 1, "-1");
                break;
            }
            edits++;
            if( shell_roundtrip(doc, key, text + ref->start, ref->end - ref->start, &edited, &n) && n > 0 )
                edits_ok++;
        }
    }
    snprintf(what, sizeof(what), "all.%s: %d shells, each reads back to no commands (%d did)", type,
             shells, noop_ok);
    check(shells > 100 && noop_ok == shells, what);
    snprintf(what, sizeof(what), "all.%s: every shell resolves values the way the client does", type);
    check(resolve_ok, what);
    snprintf(what, sizeof(what), "all.%s: %d sampled edits apply and undo byte-exact (%d did)", type, edits,
             edits_ok);
    check(edits > 0 && edits_ok == edits, what);
    printf("  shell sweep all.%s: %d shells, %d no-op, %d edits round-tripped\n", type, shells, noop_ok,
           edits_ok);
    Editor_CfgFileFree(&file);
    free(text);
}

static void
test_shell(const char* content)
{
    static const char k_block[] = "[ed_shell]\nname=Thing\nmultivarbit=varb346_golem_a\nmultivarp=default\n"
                                  "multinpc1=-1\nmultinpc2=ed_child_a\nmultinpc4=ed_child_b\n"
                                  "multinpc5=ed_child_c\nop1=Talk-to\n\n";
    static const char k_plain[] = "[ed_plain]\nname=Rock\nmultivarbit=default\nmultivarp=default\n"
                                  "multiloc=default\nmultidefaultnone=default\nop1=Mine\n\n";
    struct Builder_Shell shell;
    struct Builder_Shell edited;
    struct Editor_Doc* doc = doc_new();
    char summary[160];
    char label[32];
    int count;

    check(Builder_ShellParse(&shell, BUILDER_SUBJECT_NPC, k_block, strlen(k_block)), "a multinpc is a shell");
    check(shell.slot_count == 5 && shell.var_kind == BUILDER_SHELL_VAR_VARBIT
              && strcmp(shell.var, "varb346_golem_a") == 0,
          "1-based slots up to the highest stated, and the varbit");
    check(Builder_ShellSlotHidden(&shell.slots[0]) && Builder_ShellSlotHidden(&shell.slots[2]),
          "-1 and a skipped slot are hidden");
    check(Builder_ShellResolveChild(&shell, 0) == NULL
              && strcmp(Builder_ShellResolveChild(&shell, 1), "ed_child_a") == 0
              && strcmp(Builder_ShellResolveChild(&shell, 3), "ed_child_b") == 0
              && strcmp(Builder_ShellResolveChild(&shell, 4), "ed_child_c") == 0
              && strcmp(Builder_ShellResolveChild(&shell, 99), "ed_child_c") == 0
              && strcmp(Builder_ShellResolveChild(&shell, -1), "ed_child_c") == 0,
          "value N shows slot N+1; past the list (and negative) the last");
    Builder_ShellSlotValueLabel(&shell, 4, label, sizeof(label));
    check(strcmp(label, "default (4+)") == 0, "the last row is marked as the default");
    Builder_ShellSummary(&shell, summary, sizeof(summary));
    check(strcmp(summary, "hidden at 0, visible at 1, hidden at 2, visible from 3") == 0,
          "the summary says when it shows");
    check(Builder_ShellCommands("npc:ed_shell", k_block, strlen(k_block), &shell, NULL, 0) == 0,
          "an untouched shell (with a skipped slot) gives no commands");

    edited = shell;
    snprintf(edited.slots[2].child, BUILDER_NAME_MAX, "%s", "ed_child_x");
    Builder_ShellInsertSlot(&edited, 5, "-1");
    check(shell_roundtrip(doc, "npc:ed_shell", k_block, strlen(k_block), &edited, &count) && count == 2,
          "filling a skipped slot and adding a hidden default: two field edits, undone exactly");

    edited = shell;
    edited.var_kind = BUILDER_SHELL_VAR_VARP;
    snprintf(edited.var, sizeof(edited.var), "%s", "varp7_mock_quest_progress");
    Builder_ShellRemoveSlot(&edited, 4);
    check(shell_roundtrip(doc, "npc:ed_shell", k_block, strlen(k_block), &edited, &count) && count == 3,
          "switching to a varp clears the varbit and dropping the last slot removes its line");

    Builder_ShellParse(&shell, BUILDER_SUBJECT_LOC, k_plain, strlen(k_plain));
    check(shell.slot_count == 0 && strcmp(shell.name, "ed_plain") == 0, "a plain loc is not a shell");
    edited = shell;
    edited.var_kind = BUILDER_SHELL_VAR_VARBIT;
    snprintf(edited.var, sizeof(edited.var), "%s", "varb346_golem_a");
    Builder_ShellInsertSlot(&edited, 0, "ed_rock_full");
    Builder_ShellInsertSlot(&edited, 1, "-1");
    check(shell_roundtrip(doc, "loc:ed_plain", k_plain, strlen(k_plain), &edited, &count) && count == 4,
          "a plain loc becomes a shell (var, two slots, the bare multiloc line goes) and back");

    /* Every real shell needs the content tree; a checkout without
     * OSRS-Content runs the synthetic half above alone. */
    if( g_have_content )
    {
        shell_sweep(content, BUILDER_SUBJECT_LOC, doc);
        shell_sweep(content, BUILDER_SUBJECT_NPC, doc);
    }
    else
        printf("  shell: the sweep over all.loc / all.npc skipped (no content tree at %s)\n", content);
    Editor_DocFree(doc);
    free(doc);
}

/* ---- preview ------------------------------------------------------------- */

static void
test_preview(void)
{
    struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
    struct Builder_PreviewVar back[BUILDER_PREVIEW_MAX];
    int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
    int kind = 0;
    int id = 0;
    int count = 0;
    int n;

    check(Builder_VarNameId("varb346_golem_a", &kind, &id) && kind == BUILDER_PREVIEW_VARBIT && id == 346,
          "a varbit's id comes from its name");
    check(Builder_VarNameId("varp7_mock_quest_progress", &kind, &id) && kind == BUILDER_PREVIEW_VARP && id == 7,
          "a varp's too");
    check(!Builder_VarNameId("cooksquest", &kind, &id), "an unprefixed name has no id");
    for( int i = 0; i < 7; i++ )
        count = Builder_PreviewSet(vars, count, BUILDER_PREVIEW_VARP, 100 + i, i);
    count = Builder_PreviewSet(vars, count, BUILDER_PREVIEW_VARP, 106, 42);
    check(count == BUILDER_PREVIEW_MAX && vars[count - 1].value == 42 && vars[0].id == 102,
          "the preview keeps the newest five and updates in place");
    n = Builder_PreviewEncode(vars, count, values, TORIRSMAPED_STATE_VALUES_MAX);
    check(n == 16 && n <= TORIRSMAPED_STATE_VALUES_MAX, "five entries fill the state field exactly");
    check(Builder_PreviewDecode(values, n, back, BUILDER_PREVIEW_MAX) == count
              && memcmp(back, vars, sizeof(vars[0]) * (size_t)count) == 0,
          "the preview decodes to what was encoded");
    values[1] = 9;
    check(Builder_PreviewDecode(values, n, back, BUILDER_PREVIEW_MAX) == 0, "a bad kind is refused");
}

/* ---- the authority ------------------------------------------------------- */

struct mirror
{
    struct EditorHost* host;
    struct Editor_Doc* doc;
    int applied;
    int refused;
};

static void
mirror_on_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    struct mirror* mirror = user_data;
    (void)seq;
    Editor_CmdApply(mirror->doc, command, direction ? EDITOR_CMD_INVERSE : EDITOR_CMD_FORWARD);
}

static void
mirror_on_saved(
    void* user_data,
    int count,
    const struct Editor_DocRef* rows)
{
    struct mirror* mirror = user_data;
    for( int i = 0; i < count; i++ )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(mirror->doc, rows[i].kind, rows[i].key, &handle) )
            Editor_DocHandleClearDirty(&handle);
    }
}

static void
mirror_drain(struct mirror* mirror)
{
    struct EditorHostMapEdFacts sink;
    memset(&sink, 0, sizeof(sink));
    sink.user_data = mirror;
    sink.on_cmd = mirror_on_cmd;
    sink.on_saved = mirror_on_saved;
    Editor_HostMapEdDrainFacts(mirror->host, &sink);
}

static int
mirror_apply(
    void* user_data,
    const struct Editor_Cmd* command)
{
    struct mirror* mirror = user_data;
    int ok = Editor_HostMapEdCmd(mirror->host, command) == EDITOR_HOST_OK;
    if( ok )
        mirror->applied++;
    else
    {
        mirror->refused++;
        if( getenv("BUILDER_TEST_VERBOSE") )
            fprintf(stderr, "      refused: %s kind %d offset %u before %u after %u\n", command->doc_key,
                    (int)command->kind, command->u.text_splice.offset, command->u.text_splice.before_len,
                    command->u.text_splice.after_len);
    }
    mirror_drain(mirror);
    return ok;
}

static void
mirror_stroke(
    void* user_data,
    int begin)
{
    struct mirror* mirror = user_data;
    Editor_HostMapEdStroke(mirror->host, begin);
}

static const char*
mirror_current(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    size_t* out_length)
{
    struct mirror* mirror = user_data;
    struct Editor_DocHandle handle;

    *out_length = 0;
    mirror_drain(mirror);
    if( !Editor_DocFind(mirror->doc, kind, key, &handle) || !Editor_DocHandleLoaded(&handle) )
    {
        struct EditorHost_DocText text;
        if( Editor_HostMapEdDocOpen(mirror->host, kind, key, &text) != EDITOR_HOST_OK )
            return NULL;
        if( !Editor_DocOpen(mirror->doc, kind, key, &handle) || !handle.text )
        {
            Editor_HostDocTextFree(&text);
            return NULL;
        }
        Editor_TextDocSet(handle.text, text.parts[0].data ? text.parts[0].data : "",
                          text.parts[0].data ? text.parts[0].size : 0);
        Editor_HostDocTextFree(&text);
    }
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

static int
mirror_save(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key)
{
    struct mirror* mirror = user_data;
    int result = Editor_HostMapEdSave(mirror->host, kind, key);
    mirror_drain(mirror);
    return result;
}

static int
fake_record(
    void* user_data,
    enum Builder_Subject subject,
    const char* name,
    int id,
    struct Builder_RecordInfo* out)
{
    (void)user_data;
    (void)id;
    memset(out, 0, sizeof(*out));
    if( strcmp(name, "no_such_npc") == 0 )
        return 0;
    out->found = 1;
    out->id = 1;
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(out->ops[0], sizeof(out->ops[0]), "%s",
             subject == BUILDER_SUBJECT_NPC ? "Talk-to" : "Search");
    return 1;
}

static int
fake_triggers(
    void* user_data,
    const char* subject,
    struct Builder_ExistingList* out)
{
    (void)user_data;
    memset(out, 0, sizeof(*out));
    if( strcmp(subject, "taken_npc") == 0 )
    {
        snprintf(out->names[0], sizeof(out->names[0]), "%s", "opnpc1,taken_npc");
        snprintf(out->files[0], sizeof(out->files[0]), "%s", "server/scripts/areas/a.rs2");
        out->rows[0].name = out->names[0];
        out->rows[0].file = out->files[0];
        out->count = 1;
    }
    return 1;
}

static int g_state_key;
static int32_t g_state_values[TORIRSMAPED_STATE_VALUES_MAX];
static int g_state_count;

static int
fake_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    (void)user_data;
    g_state_key = (int)key;
    g_state_count = count;
    memcpy(g_state_values, values, sizeof(values[0]) * (size_t)count);
    return 1;
}

/* The registry's contract (editor_ui.h), as far as the builder panels use
 * it. The real editor_ui.c registers every tools-panels module, which this
 * test does not link. */
int
Editor_UiAddPanel(
    struct EditorUi* ui,
    const struct EditorUiPanel* panel)
{
    assert(ui);
    assert(panel);
    assert(ui->panel_count < EDITOR_UI_PANELS_MAX);
    ui->panels[ui->panel_count] = *panel;
    return ui->panel_count++;
}

struct EditorUiPanel*
Editor_UiFindPanel(
    struct EditorUi* ui,
    const char* name)
{
    for( int i = 0; i < ui->panel_count; i++ )
    {
        if( strcmp(ui->panels[i].name, name) == 0 )
            return &ui->panels[i];
    }
    return NULL;
}

void*
Editor_UiPanelSelf(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(ui, name);
    return panel ? panel->self : NULL;
}

void
Editor_UiLog(
    struct EditorUi* ui,
    const char* format,
    ...)
{
    va_list args;
    (void)ui;
    if( !getenv("BUILDER_TEST_VERBOSE") )
        return;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}

/* The status sentence (editor_ui.h): the last one, for the checks. */
static char g_status[512];

void
Editor_UiStatus(
    struct EditorUi* ui,
    const char* format,
    ...)
{
    va_list args;
    (void)ui;
    va_start(args, format);
    vsnprintf(g_status, sizeof(g_status), format, args);
    va_end(args);
    if( getenv("BUILDER_TEST_VERBOSE") )
        fprintf(stderr, "status: %s\n", g_status);
}

/* The Problems seam (edpanel_problems_api.h): strong definitions here
 * replace builder_panels.c's weak console default, as the Problems panel's
 * will, so the test sees what the builder reports. */
static int g_problem_count;
static char g_problem_source[EDITOR_PROBLEM_SOURCE_MAX];
static Editor_ProblemOpenFn g_problem_open;
static void* g_problem_user;

void
Editor_Problems_Report(
    struct EditorUi* ui,
    const char* source,
    const struct Editor_Problem* rows,
    int count,
    Editor_ProblemOpenFn open,
    void* user)
{
    (void)ui;
    (void)rows;
    snprintf(g_problem_source, sizeof(g_problem_source), "%s", source);
    g_problem_count = count;
    g_problem_open = open;
    g_problem_user = user;
}

void
Editor_Problems_Clear(
    struct EditorUi* ui,
    const char* source)
{
    (void)ui;
    if( strcmp(source, g_problem_source) == 0 )
    {
        g_problem_count = 0;
        g_problem_open = NULL;
    }
}

static const char*
mirror_text(
    struct mirror* mirror,
    const char* key,
    size_t* length)
{
    return mirror_current(mirror, EDITOR_DOC_TEXT, key, length);
}

static int
find_widget(
    struct ToriRSChrome* chrome,
    int panel,
    const char* label)
{
    for( int i = 0; i < chrome->widget_count; i++ )
    {
        const struct ToriRSChromeWidget* widget = &chrome->widgets[i];
        if( widget->panel == panel
            && (strcmp(widget->label, label) == 0 || strcmp(widget->text, label) == 0) )
            return i;
    }
    return -1;
}

static void
test_authority(const char* tmp)
{
    char content[1024];
    char path[1200];
    struct EditorHost host;
    struct mirror mirror;
    struct Builder_Submit submit;
    struct Builder_Model* model = model_new();
    struct Builder_Issues issues;
    struct Builder_Buf expect;
    char key[EDITOR_DOC_KEY_MAX];
    const char* text;
    size_t length = 0;
    size_t disk_size = 0;
    char* disk;
    int before;

    snprintf(content, sizeof(content), "%s/content", tmp);
    snprintf(path, sizeof(path), "%s/maps", content);
    mkdir_p(path);
    snprintf(path, sizeof(path), "%s/server/scripts", content);
    mkdir_p(path);
    snprintf(path, sizeof(path), "%s/configs", content);
    mkdir_p(path);
    {
        static const char k_all_loc[] = "// all.loc (test)\n\n[ed_other]\nname=Other\n\n"
                                        "[ed_test_crate]\nname=Crate\nop1=Search\nop2=hidden\n\n";
        snprintf(path, sizeof(path), "%s/configs/all.loc", content);
        spit(path, k_all_loc, sizeof(k_all_loc) - 1);
    }

    memset(&host, 0, sizeof(host));
    check(Editor_HostOpenMapEdEmbed(&host, content, NULL) == 1, "the embedded authority opens a temp tree");
    memset(&mirror, 0, sizeof(mirror));
    mirror.host = &host;
    mirror.doc = doc_new();
    submit.user_data = &mirror;
    submit.apply = mirror_apply;
    submit.stroke = mirror_stroke;
    submit.current = mirror_current;

    fixture_npc(model, "port_roberts_bar_patron");
    Builder_IssuesClear(&issues);
    check(Builder_Regenerate(model, &submit, &issues) == BUILDER_SYNC_APPLIED,
          "a regeneration goes through the authority as TEXT_SPLICEs");
    {
        struct Builder_Buf script;
        size_t ini_length;
        char* ini = emit_ini(model, &ini_length);
        int want;
        Builder_BufInit(&script);
        Builder_EmitScript(model, &script, &issues);
        want = Builder_TextSplices(EDITOR_DOC_TEXT, "rs2:a.rs2", "", 0, ini, ini_length, NULL, 0)
               + Builder_TextSplices(EDITOR_DOC_TEXT, "rs2:a.rs2", "", 0, script.data, script.length, NULL, 0);
        check(mirror.refused == 0 && mirror.applied == want && want >= 4,
              "every splice was accepted: a new file is several 1 KB inserts");
        Builder_BufFree(&script);
        free(ini);
    }

    Builder_FileKey(BUILDER_SUBJECT_NPC, model->name, BUILDER_FILE_SCRIPT, key, sizeof(key));
    text = mirror_text(&mirror, key, &length);
    Builder_BufInit(&expect);
    Builder_EmitScript(model, &expect, &issues);
    check(text && length == expect.length && memcmp(text, expect.data, length) == 0,
          "the mirror's script is the emitter's text");
    Builder_BufFree(&expect);
    {
        struct EditorHost_DocText authority;
        check(Editor_HostMapEdDocOpen(&host, EDITOR_DOC_TEXT, key, &authority) == EDITOR_HOST_OK
                  && authority.parts[0].size == length
                  && memcmp(authority.parts[0].data, text, length) == 0 && (authority.dirty & 1),
              "the authority holds the same text, unsaved");
        Editor_HostDocTextFree(&authority);
    }
    Builder_FileKey(BUILDER_SUBJECT_NPC, model->name, BUILDER_FILE_MODEL, key, sizeof(key));
    text = mirror_text(&mirror, key, &length);
    {
        struct Builder_Model* back = model_new();
        char error[160];
        check(text && Builder_ModelParseIni(back, text, length, error, sizeof(error))
                  && back->node_count == model->node_count,
              "the model document reads back into the model");
        free(back);
    }

    /* Nothing is on disk before Save; Save writes all four files. */
    snprintf(path, sizeof(path), "%s/server/scripts/edited/%s/scripts/%s.rs2", content, model->name,
             model->name);
    disk = slurp(path, &disk_size);
    check(disk == NULL, "nothing is written before a save");
    free(disk);
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
    {
        Builder_FileKey(BUILDER_SUBJECT_NPC, model->name, (enum Builder_File)f, key, sizeof(key));
        mirror_save(&mirror, EDITOR_DOC_TEXT, key);
    }
    disk = slurp(path, &disk_size);
    Builder_FileKey(BUILDER_SUBJECT_NPC, model->name, BUILDER_FILE_SCRIPT, key, sizeof(key));
    text = mirror_text(&mirror, key, &length);
    check(disk && disk_size == length && memcmp(disk, text, length) == 0,
          "Save writes the script under server/scripts/edited/<subject>/scripts/");
    free(disk);
    snprintf(path, sizeof(path), "%s/server/scripts/edited/%s/%s.interaction", content, model->name,
             model->name);
    disk = slurp(path, &disk_size);
    check(disk && disk_size > 0, "...and the model beside it");
    free(disk);

    /* A one-line change regenerates as a small stroke; undo takes back the
     * model and the script together. */
    before = mirror.applied;
    snprintf(Builder_ModelNode(model, "thanks")->text, BUILDER_TEXT_MAX, "%s", "Cheers.");
    check(Builder_Regenerate(model, &submit, &issues) == BUILDER_SYNC_APPLIED, "an edit regenerates");
    check(mirror.applied - before == 2, "one changed line is one splice per file it touches (model, script)");
    text = mirror_text(&mirror, key, &length);
    check(text && strstr(text, "~chatplayer(\"Cheers.\");") != NULL, "the edit is in the script");
    check(Editor_HostMapEdUndo(&host) == 2, "undo reverts the regeneration as one stroke");
    mirror_drain(&mirror);
    text = mirror_text(&mirror, key, &length);
    check(text && strstr(text, "~chatplayer(\"Thanks.\");") != NULL, "...back to the old line");
    check(Editor_HostMapEdRedo(&host) == 2, "redo re-applies it");
    mirror_drain(&mirror);

    /* A stale regeneration (the text moved under it) is refused. */
    {
        struct Editor_Cmd stale;
        Builder_TextSplices(EDITOR_DOC_TEXT, key, "XXXX", 4, "YYYY", 4, &stale, 1);
        check(!mirror_apply(&mirror, &stale), "a splice against text that is not there is refused");
    }

    /* Detach: the model empties, the headers go, the scripts stay. */
    check(Builder_Detach(BUILDER_SUBJECT_NPC, model->name, &submit) == BUILDER_SYNC_APPLIED, "detach applies");
    text = mirror_text(&mirror, key, &length);
    check(text && length > 0 && !Builder_HasHeader(text, length)
              && strstr(text, "[opnpc1,port_roberts_bar_patron]") != NULL,
          "the script keeps its code without the header");
    Builder_FileKey(BUILDER_SUBJECT_NPC, model->name, BUILDER_FILE_MODEL, key, sizeof(key));
    mirror_text(&mirror, key, &length);
    check(length == 0, "the model document is empty");
    check(Editor_HostMapEdUndo(&host) > 0, "detach undoes");
    mirror_drain(&mirror);

    /* ---- the panels over the same authority and the real chrome --------- */
    {
        static struct ToriRSChrome chrome;
        static struct EditorUi ui;
        struct Builder_Services services;
        struct Builder_PanelView view;
        struct EditorUiPanel* panel;
        int widget;

        ToriRSChrome_Init(&chrome);
        ToriRSChrome_SetSurface(&chrome, 1280, 900);
        memset(&ui, 0, sizeof(ui));
        ui.chrome = &chrome;
        ui.visible = 1;
        memset(&services, 0, sizeof(services));
        services.user_data = &mirror;
        services.submit = submit;
        services.save = mirror_save;
        services.record = fake_record;
        services.triggers = fake_triggers;
        services.state_set = fake_state;
        Builder_LocPanelRegister(&ui, &services);
        Builder_NpcPanelRegister(&ui, &services);
        Builder_ShellPanelRegister(&ui, &services);
        check(ui.panel_count == 3 && Editor_UiFindPanel(&ui, BUILDER_PANEL_LOC)
                  && Editor_UiFindPanel(&ui, BUILDER_PANEL_NPC) && Editor_UiFindPanel(&ui, BUILDER_PANEL_SHELL),
              "the three panels register on the registry");
        ToriRSChrome_Build(&chrome);

        check(Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "port_roberts_bar_patron"),
              "the npc builder opens its subject");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.model && view.model->node_count == model->node_count && !view.read_only,
              "...reading the saved model back (re-opening never parses the script)");

        check(!Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "no_such_npc"),
              "a subject the catalog does not have is refused");
        check(Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "taken_npc"), "a taken subject opens");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.read_only == 0 && view.issues->count >= 1,
              "...naming the hand-authored trigger it cannot add");
        {
            struct Builder_Model* taken = model_new();
            fixture_npc(taken, "taken_npc");
            Builder_PanelSetModel(&ui, BUILDER_PANEL_NPC, taken);
            Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "taken_npc");
            Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
            check(view.read_only == 1, "a subject whose op1 is hand-authored elsewhere is read-only");
            free(taken);
        }

        /* Drive a real widget: pick op1 in the trigger dropdown and press
         * Add trigger on the loc builder. */
        check(Builder_PanelOpenSubject(&ui, BUILDER_PANEL_LOC, "ed_test_crate"), "the loc builder opens");
        panel = Editor_UiFindPanel(&ui, BUILDER_PANEL_LOC);
        widget = find_widget(&chrome, panel->chrome_panel, "Add trigger");
        check(widget >= 0 && panel->activate(&ui, panel->self, widget), "Add trigger is the panel's");
        Builder_PanelInspect(&ui, BUILDER_PANEL_LOC, &view);
        check(view.model && view.model->trigger_count == 1 && view.model->triggers[0].op == 1
                  && strcmp(view.model->triggers[0].label, "Search") == 0,
              "pressing it adds [oploc1] labelled with the cache's op1");
        widget = find_widget(&chrome, panel->chrome_panel, "Add step");
        check(widget >= 0 && panel->activate(&ui, panel->self, widget), "Add step is the panel's");
        Builder_PanelInspect(&ui, BUILDER_PANEL_LOC, &view);
        check(view.model->triggers[0].step_count == 1, "...and adds the step to the selected trigger");
        Builder_FileKey(BUILDER_SUBJECT_LOC, "ed_test_crate", BUILDER_FILE_SCRIPT, key, sizeof(key));
        text = mirror_text(&mirror, key, &length);
        check(text && strstr(text, "[oploc1,ed_test_crate]\nmes(\"\");") != NULL,
              "each panel edit regenerates the script through the authority");
        check(Builder_PanelSaveAll(&ui, BUILDER_PANEL_LOC), "the panel saves");

        /* Renaming op1 is a CFG_FIELD on the cache record, not a script edit. */
        ToriRSChrome_SetText(&chrome, find_widget(&chrome, panel->chrome_panel, "Menu text (the cache's opN)"),
                             "Rummage");
        widget = find_widget(&chrome, panel->chrome_panel, "Rename the op in the cache");
        check(widget >= 0 && panel->activate(&ui, panel->self, widget), "Rename is the panel's");
        text = mirror_current(&mirror, EDITOR_DOC_CFG, "loc:ed_test_crate", &length);
        check(text && strstr(text, "op1=Rummage\n") && strstr(text, "op2=hidden\n"),
              "renaming the op changes exactly the record's op1= line");
        Builder_PanelInspect(&ui, BUILDER_PANEL_LOC, &view);
        check(strcmp(view.model->triggers[0].label, "Rummage") == 0, "...and the trigger's label follows");

        /* The shell editor publishes the preview value. */
        {
            static const char k_shell[] = "[ed_shell_loc]\nmultivarbit=varb346_golem_a\nmultivarp=-1\n"
                                          "multiloc1=ed_a\nmultiloc2=-1\nmultiloc3=ed_b\n";
            struct EditorUiPanel* sp = Editor_UiFindPanel(&ui, BUILDER_PANEL_SHELL);
            struct Editor_DocHandle handle;
            struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
            int name_widget = find_widget(&chrome, sp->chrome_panel, "Name");
            int open_widget = find_widget(&chrome, sp->chrome_panel, "Open");
            int slider = find_widget(&chrome, sp->chrome_panel, "Preview value");

            /* The record's CFG document, as the authority would answer it. */
            Editor_DocOpen(mirror.doc, EDITOR_DOC_CFG, "loc:ed_shell_loc", &handle);
            Editor_TextDocSet(handle.text, k_shell, strlen(k_shell));
            ToriRSChrome_SetText(&chrome, name_widget, "ed_shell_loc");
            check(sp->activate(&ui, sp->self, open_widget) && Builder_ShellPanelShell(&ui)
                      && Builder_ShellPanelShell(&ui)->slot_count == 3,
                  "the shell editor opens a multiloc");
            ToriRSChrome_SliderSet(&chrome, slider, 1);
            sp->activate(&ui, sp->self, slider);
            check(g_state_key == TORIRSMAPED_STATE_VAR_PREVIEW
                      && Builder_PreviewDecode(g_state_values, g_state_count, vars, BUILDER_PREVIEW_MAX) == 1
                      && vars[0].kind == BUILDER_PREVIEW_VARBIT && vars[0].id == 346 && vars[0].value == 1,
                  "moving the preview publishes STATE_VAR_PREVIEW for the shell's varbit");
            /* Live: a drag's value (no release yet) goes out on the tick. */
            g_state_key = 0;
            sp->tick(&ui, sp->self);
            check(g_state_key == 0, "a tick with the knob where it was publishes nothing");
            ToriRSChrome_SliderSet(&chrome, slider, 2);
            sp->tick(&ui, sp->self);
            check(g_state_key == TORIRSMAPED_STATE_VAR_PREVIEW
                      && Builder_PreviewDecode(g_state_values, g_state_count, vars, BUILDER_PREVIEW_MAX) == 1
                      && vars[0].value == 2,
                  "scrubbing the knob publishes each value it passes, before the release");
            {
                int32_t heard[4] = { 1, BUILDER_PREVIEW_VARBIT, 346, 0 };
                sp->on_state(&ui, sp->self, TORIRSMAPED_STATE_VAR_PREVIEW, heard, 4);
                g_state_key = 0;
                sp->tick(&ui, sp->self);
                check(ToriRSChrome_SliderValue(&chrome, slider) == 0 && g_state_key == 0,
                      "another window's preview moves the knob without echoing it back");
            }
        }
        for( int i = 0; i < ui.panel_count; i++ )
            ui.panels[i].destroy(&ui, ui.panels[i].self);
    }

    Editor_HostClose(&host);
    Editor_DocFree(mirror.doc);
    free(mirror.doc);
    free(model);
}

/* ---- graph: ports, links, layout, pos= (workbench 6.6) ------------------- */

static void
test_graph(void)
{
    struct Builder_Model* model = model_new();
    struct Builder_Model* back = model_new();
    struct Builder_Model* shuffled = model_new();
    unsigned char mask[BUILDER_NODES_MAX];
    char error[160];
    size_t length;
    char* ini;
    /* Breadth-first from `hello`, ports in order, a column per depth. */
    static const struct
    {
        const char* id;
        int column;
        int row;
    } k_layout[] = {
        { "hello", 0, 0 }, { "ask", 1, 0 },    { "beer", 2, 0 },  { "work", 2, 1 },   { "bye", 2, 2 },
        { "pay", 3, 0 },   { "job", 3, 1 },    { "nowork", 3, 2 }, { "end", 3, 3 },  { "served", 4, 0 },
        { "start", 4, 1 }, { "thanks", 5, 0 },
    };

    fixture_npc(model, "graph_npc");

    /* Ports: a choice's options, a branch's arms then its default, none on
     * an End, one (next) on the rest. */
    check(Builder_NodePortCount(Builder_ModelNode(model, "ask")) == 3, "a 3-option choice has 3 ports");
    check(Builder_NodePortCount(Builder_ModelNode(model, "work")) == 2, "a 1-arm branch has its arm and its default");
    check(Builder_NodePortCount(Builder_ModelNode(model, "end")) == 0, "an End has no port");
    check(Builder_NodePortCount(Builder_ModelNode(model, "hello")) == 1, "a line has one port, its next");
    check(strcmp(Builder_NodePortNextConst(Builder_ModelNode(model, "work"), 1), "nowork") == 0,
          "a branch's last port is its default (next)");
    check(Builder_ModelLink(model, "bye", 0, "thanks") && strcmp(Builder_ModelNode(model, "bye")->next, "thanks") == 0,
          "linking a port writes the target's id");
    check(!Builder_ModelLink(model, "bye", 0, "nobody") && strcmp(Builder_ModelNode(model, "bye")->next, "thanks") == 0,
          "a link to no node is refused and changes nothing");
    check(Builder_ModelLink(model, "bye", 0, "end"), "...and links back");

    /* Reach and orphans. */
    check(Builder_ModelReach(model, "work", mask) == 11 && mask[Builder_ModelNodeIndex(model, "job")]
              && !mask[Builder_ModelNodeIndex(model, "hello")],
          "the reach of a node is everything after it, and not what is before");
    check(Builder_ModelOrphans(model, mask) == 0, "the fixture has no orphan");
    check(Builder_ModelLink(model, "ask", 1, ""), "unlinking a port");
    check(Builder_ModelOrphans(model, mask) == 4 && mask[Builder_ModelNodeIndex(model, "work")]
              && mask[Builder_ModelNodeIndex(model, "job")] && !mask[Builder_ModelNodeIndex(model, "thanks")],
          "...orphans the branch and what only it reaches, not the tail reached another way");
    check(Builder_ModelLink(model, "ask", 1, "work"), "relinked");

    /* Auto-layout: left to right by depth, the rows in the order a reader
     * meets the nodes, whatever the positions were. */
    check(Builder_ModelAutoLayout(model) > 0, "auto-layout moves the fixture's nodes");
    {
        int all = 1;
        for( size_t i = 0; i < sizeof(k_layout) / sizeof(k_layout[0]); i++ )
        {
            const struct Builder_Node* n = Builder_ModelNodeConst(model, k_layout[i].id);
            int const x = BUILDER_LAYOUT_ORIGIN + k_layout[i].column * BUILDER_LAYOUT_COL_W;
            int const y = BUILDER_LAYOUT_ORIGIN + k_layout[i].row * BUILDER_LAYOUT_ROW_H;
            if( !n || n->x != x || n->y != y )
            {
                all = 0;
                fprintf(stderr, "      %s at %d,%d, want %d,%d\n", k_layout[i].id, n ? n->x : -1, n ? n->y : -1, x, y);
            }
        }
        check(all, "every node sits at its depth's column and its reading order's row");
    }
    check(Builder_ModelAutoLayout(model) == 0, "a second auto-layout moves nothing (stable)");
    *shuffled = *model;
    g_rng = 99;
    for( int i = 0; i < shuffled->node_count; i++ )
    {
        shuffled->nodes[i].x = (int)(rng() % 900);
        shuffled->nodes[i].y = (int)(rng() % 400);
    }
    Builder_ModelAutoLayout(shuffled);
    {
        int same = 1;
        for( int i = 0; i < model->node_count; i++ )
            same &= shuffled->nodes[i].x == model->nodes[i].x && shuffled->nodes[i].y == model->nodes[i].y;
        check(same, "a hand layout lays out to the same picture (a pure function of the links)");
    }
    /* An orphan follows as a root of its own, below its depth's column. */
    {
        struct Builder_Node* lone = node(shuffled, "lone", BUILDER_NODE_NPC, "Who said that?", "end");
        (void)lone;
        Builder_ModelAutoLayout(shuffled);
        lone = Builder_ModelNode(shuffled, "lone");
        check(lone->x == BUILDER_LAYOUT_ORIGIN && lone->y == BUILDER_LAYOUT_ORIGIN + BUILDER_LAYOUT_ROW_H,
              "an orphan is a root: column 0, under the start");
        check(Builder_ModelNode(shuffled, "hello")->x == BUILDER_LAYOUT_ORIGIN
                  && Builder_ModelNode(shuffled, "end")->x == BUILDER_LAYOUT_ORIGIN + 3 * BUILDER_LAYOUT_COL_W,
              "...and the start's walk keeps every column it had");
    }

    /* pos= round-trips: the hand layout survives a save and a re-open. */
    model->nodes[3].x = 517;
    model->nodes[3].y = 0;
    ini = emit_ini(model, &length);
    check(strstr(ini, "pos=517,0\n") != NULL, "the .interaction file carries pos= per node");
    check(Builder_ModelParseIni(back, ini, length, error, sizeof(error)), "...and reads back");
    {
        int same = back->node_count == model->node_count;
        for( int i = 0; same && i < model->node_count; i++ )
            same = strcmp(back->nodes[i].id, model->nodes[i].id) == 0 && back->nodes[i].x == model->nodes[i].x
                   && back->nodes[i].y == model->nodes[i].y;
        check(same, "every node's position is the one it was saved at");
    }
    free(ini);
    {
        static const char k_bad[] = "[interaction]\nversion=1\nsubject=npc\nname=x\n[dialogue]\nstart=a\n"
                                    "[node,a]\nkind=end\npos=10\n";
        check(!Builder_ModelParseIni(back, k_bad, sizeof(k_bad) - 1, error, sizeof(error))
                  && strstr(error, "pos") != NULL,
              "a damaged pos= is refused with its line");
    }
    free(model);
    free(back);
    free(shuffled);
}

/* ---- chat: the line rule and its page breaks ------------------------------ */

/** An independent greedy wrap (words by single spaces, a line as wide as
 *  its words and the spaces between them), for text with no tags,
 *  hyphens or hard breaks and no word wider than the box. Returns the
 *  source offsets where each line starts. */
static int
greedy_lines(
    const char* text,
    const struct Builder_ChatMetrics* metrics,
    int* starts,
    int max)
{
    int count = 0;
    int width = 0;
    int at = 0;

    starts[count++] = 0;
    while( text[at] )
    {
        int end = at;
        int word = 0;
        while( text[end] && text[end] != ' ' )
            word += metrics->advance[(unsigned char)text[end++]];
        if( width > 0 && width + metrics->advance[' '] + word > metrics->width && count < max )
        {
            starts[count++] = at;
            width = word;
        }
        else
            width += (width > 0 ? metrics->advance[' '] : 0) + word;
        at = text[end] ? end + 1 : end;
    }
    return count;
}

static void
test_chat(void)
{
    static struct Builder_ChatLayout layout;
    struct Builder_ChatMetrics m;
    int breaks[8];
    char line[BUILDER_CHAT_TEXT_MAX];

    /* A synthetic face: letters 6 px, a space 3, the box 30 px (five
     * letters), two lines a page. */
    memset(&m, 0, sizeof(m));
    for( int c = 0; c < 256; c++ )
        m.advance[c] = 6;
    m.advance[' '] = 3;
    m.width = 30;
    m.page_lines = 2;
    m.player = "Bob";

    Builder_ChatLayoutText("aaa aaa aaa aaa aaa", &m, &layout);
    check(layout.line_count == 5 && layout.page_count == 3, "a word per line: five lines, three pages of two");
    check(Builder_ChatPageBreaks(&layout, breaks, 8) == 2 && breaks[0] == 8 && breaks[1] == 16,
          "the client pages at the third and fifth words (source offsets 8 and 16)");
    Builder_ChatLineText(&layout, 0, line, sizeof(line));
    check(strcmp(line, "aaa") == 0 && layout.lines[0].width == 18, "a line ends before the space it breaks at");

    Builder_ChatLayoutText("aa aa aa", &m, &layout);
    check(layout.line_count == 2 && layout.lines[1].source_start == 6 && layout.lines[0].width == 27,
          "two words fit a line (6+6+3+6+6 = 27 <= 30); the third breaks");
    Builder_ChatLayoutText("aaaaa", &m, &layout);
    check(layout.line_count == 1, "a line exactly the box's width is one line");
    Builder_ChatLayoutText("aa|aa<br>aa", &m, &layout);
    check(layout.line_count == 3 && layout.lines[1].source_start == 3 && layout.lines[2].source_start == 9,
          "| and <br> break hard");
    Builder_ChatLayoutText("<col=ff0000>aaaaa</col>", &m, &layout);
    check(layout.line_count == 1, "tags are widthless");
    Builder_ChatLayoutText("aa-aaaa", &m, &layout);
    check(layout.line_count == 2 && layout.lines[1].source_start == 3, "a hyphen breaks after itself");
    Builder_ChatLayoutText("{player} aa aa", &m, &layout);
    check(layout.line_count == 2 && layout.lines[1].source_start == 9,
          "{player} measures as the stand-in name; the break maps back to the source");
    Builder_ChatLayoutText("", &m, &layout);
    check(layout.line_count == 1 && layout.page_count == 1, "an empty text is one empty line, as split_init has it");

    /* Fidelity: the script carries the page exactly as it was laid out --
     * hard breaks kept, {player} as <displayname> (which the compiler makes
     * the name the stand-in measured for) -- and the model keeps it too. */
    {
        struct Builder_Model* model = model_new();
        struct Builder_Model* back = model_new();
        struct Builder_Issues issues;
        struct Builder_Buf script;
        char error[160];
        size_t length;
        char* ini;
        static const char k_page[] = "{player}, look.|The tower.<br>Go on.";

        fixture_npc(model, "page_npc");
        snprintf(Builder_ModelNode(model, "hello")->text, BUILDER_TEXT_MAX, "%s", k_page);
        Builder_IssuesClear(&issues);
        Builder_BufInit(&script);
        Builder_EmitScript(model, &script, &issues);
        check(strstr(Builder_BufText(&script), "\"<displayname>, look.|The tower.<br>Go on.\"") != NULL,
              "the emitted page keeps its hard breaks where the layout put them");
        Builder_BufFree(&script);
        ini = emit_ini(model, &length);
        check(Builder_ModelParseIni(back, ini, length, error, sizeof(error))
                  && strcmp(Builder_ModelNode(back, "hello")->text, k_page) == 0,
              "...and the .interaction keeps the page byte for byte");
        free(ini);
        free(model);
        free(back);
    }
    check(Builder_ChatPageBreaks(&layout, breaks, 8) == 0, "...and no page break");

    /* The baked p12_full face, the editor's chat box: the page breaks fall
     * where an independent greedy wrap over the same advances puts the
     * fifth, ninth, ... line. */
    Builder_ChatMetricsBaked(&m);
    check(m.width == BUILDER_CHAT_WIDTH_DEFAULT && m.page_lines == BUILDER_CHAT_PAGE_LINES_DEFAULT
              && m.advance['a'] > 0 && m.advance['W'] > m.advance['i'],
          "the baked metrics are p12_full's advances in the chat box");
    {
        static const char k_text[] =
            "The guards say the old tower north of the river has been empty for years, but every night "
            "somebody lights a lamp in its top window and every morning the lamp is out again. Nobody in "
            "the village will go near it. Would you take a look, and tell me what you find there? I will "
            "make it worth your while, I promise you that much at least.";
        int starts[BUILDER_CHAT_LINES_MAX];
        int count = greedy_lines(k_text, &m, starts, BUILDER_CHAT_LINES_MAX);
        int n;
        int same;

        Builder_ChatLayoutText(k_text, &m, &layout);
        same = layout.line_count == count;
        for( int i = 0; same && i < count; i++ )
            same = layout.lines[i].source_start == starts[i];
        check(same, "the line rule breaks where a greedy word wrap does at 380 px of p12_full");
        check(layout.page_count >= 2, "...and this page runs onto a second");
        n = Builder_ChatPageBreaks(&layout, breaks, 8);
        check(n == layout.page_count - 1 && breaks[0] == starts[m.page_lines],
              "the first page break is the fifth line's start");
        for( int i = 0; i < layout.line_count; i++ )
            check(layout.lines[i].width <= m.width, "no line is wider than the box");
    }
}

/* ---- lint ----------------------------------------------------------------- */

static int
lint_var_known(
    void* user_data,
    const char* name)
{
    (void)user_data;
    return strcmp(name, "varp7_mock_quest_progress") == 0;
}

static int
lint_proc_known(
    void* user_data,
    const char* name)
{
    (void)user_data;
    return strcmp(name, "chatplayer") == 0;
}

static int
lint_cannot_say(
    void* user_data,
    const char* name)
{
    (void)user_data;
    (void)name;
    return -1;
}

static int
lint_find(
    const struct Builder_LintRows* rows,
    enum Builder_LintKind kind,
    int node,
    int trigger)
{
    for( int i = 0; i < rows->count; i++ )
    {
        if( rows->rows[i].kind == kind && rows->rows[i].node == node && rows->rows[i].trigger == trigger )
            return i;
    }
    return -1;
}

static void
test_lint_rows(void)
{
    struct Builder_Model* model = model_new();
    struct Builder_LintRows* rows = malloc(sizeof(*rows));
    struct Builder_LintNames names;
    struct Builder_Node* n;
    int at;

    assert(rows);
    fixture_npc(model, "lint_npc");
    memset(&names, 0, sizeof(names));
    names.var_known = lint_var_known;
    names.proc_known = lint_proc_known;
    names.player_name = "Zezima";
    check(Builder_Lint(model, &names, rows) == 0 && rows->unchecked == 0, "the fixture lints clean");

    snprintf(Builder_ModelNode(model, "hello")->text, BUILDER_TEXT_MAX, "%s", "Welcome back, zezima!");
    snprintf(Builder_ModelNode(model, "job")->text, BUILDER_TEXT_MAX, "%s", "Thanks\xc2\xa0Zezima.");
    Builder_ModelNode(model, "ask")->arm_count = 1;
    snprintf(Builder_ModelNode(model, "work")->obj, BUILDER_NAME_MAX, "%s", "varp8_no_such_var");
    n = node(model, "lost", BUILDER_NODE_STEP, "", "");
    Builder_StepInit(&n->step, BUILDER_STEP_CALL);
    snprintf(n->step.a, sizeof(n->step.a), "%s", "no_such_proc");
    {
        struct Builder_Trigger* t = Builder_ModelTriggerAdd(model, 2, 0, "");
        step(t, BUILDER_STEP_SET_VAR, "varb9_nope", "1", "", "");
    }
    Builder_Lint(model, &names, rows);
    at = lint_find(rows, BUILDER_LINT_PLAYER_NAME, Builder_ModelNodeIndex(model, "hello"), -1);
    check(at >= 0 && rows->rows[at].error && strcmp(rows->rows[at].where, "node hello") == 0,
          "the session's own name in a line is a literal player name, at its node");
    check(lint_find(rows, BUILDER_LINT_PLAYER_NAME, Builder_ModelNodeIndex(model, "job"), -1) >= 0,
          "a U+00A0 (a recorded display name) is one too");
    check(lint_find(rows, BUILDER_LINT_ONE_ARM, Builder_ModelNodeIndex(model, "ask"), -1) >= 0,
          "a one-option choice is flagged");
    check(lint_find(rows, BUILDER_LINT_UNKNOWN_VAR, Builder_ModelNodeIndex(model, "work"), -1) >= 0,
          "a branch on a var no record names is flagged");
    at = lint_find(rows, BUILDER_LINT_UNKNOWN_VAR, -1, model->trigger_count - 1);
    check(at >= 0 && rows->rows[at].step == 0 && strcmp(rows->rows[at].where, "op2 step 1") == 0,
          "...and so is a trigger step's, at its step");
    check(lint_find(rows, BUILDER_LINT_UNKNOWN_PROC, Builder_ModelNodeIndex(model, "lost"), -1) >= 0,
          "a call to a proc the tree does not declare is flagged");
    at = lint_find(rows, BUILDER_LINT_ORPHAN, Builder_ModelNodeIndex(model, "lost"), -1);
    check(at >= 0 && !rows->rows[at].error, "an unreached node is an orphan (a warning)");
    check(lint_find(rows, BUILDER_LINT_ORPHAN, Builder_ModelNodeIndex(model, "beer"), -1) < 0,
          "the one-arm choice still reaches its option");
    check(lint_find(rows, BUILDER_LINT_ORPHAN, Builder_ModelNodeIndex(model, "work"), -1) >= 0,
          "...and what it no longer leads to is orphaned");

    memset(&names, 0, sizeof(names));
    Builder_Lint(model, &names, rows);
    check(rows->unchecked == ((1u << BUILDER_LINT_UNKNOWN_VAR) | (1u << BUILDER_LINT_UNKNOWN_PROC))
              && lint_find(rows, BUILDER_LINT_UNKNOWN_VAR, Builder_ModelNodeIndex(model, "work"), -1) < 0,
          "without a catalog the var and proc rows are not checked, and the lint says so");
    names.var_known = lint_cannot_say;
    names.proc_known = lint_proc_known;
    Builder_Lint(model, &names, rows);
    check(rows->unchecked == (1u << BUILDER_LINT_UNKNOWN_VAR)
              && lint_find(rows, BUILDER_LINT_UNKNOWN_VAR, Builder_ModelNodeIndex(model, "work"), -1) < 0
              && lint_find(rows, BUILDER_LINT_UNKNOWN_PROC, Builder_ModelNodeIndex(model, "lost"), -1) >= 0,
          "a catalog that cannot say (-1) leaves vars unchecked, not unknown; procs still checked");
    free(rows);
    free(model);
}

/* ---- the dialogue graph on the NPC builder (workbench 6.6) ---------------- */

/** One GRAPH event to the panel, as the chrome would queue it. */
static void
graph_send(
    struct EditorUi* ui,
    struct EditorUiPanel* panel,
    int widget,
    int kind,
    int x,
    int y,
    int value)
{
    struct ToriRSChromeEvent event;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.widget = widget;
    event.x = x;
    event.y = y;
    event.value = value;
    if( kind != TORIRS_CHROME_EVENT_GRAPH_KEY )
        event.key = ToriRSChrome_GraphKeyAt(ui->chrome, widget, x, y);
    panel->event(ui, panel->self, &event);
}

/** The well's items of `kind` with `color`, counted. */
static int
graph_items(
    const struct ToriRSChrome* chrome,
    int widget,
    int kind,
    uint32_t color)
{
    const struct ToriRSChromeWidget* w = &chrome->widgets[widget];
    int count = 0;
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* item = &chrome->graph_items[w->graph_first + i];
        count += item->kind == kind && item->color == color;
    }
    return count;
}

static int
name_known_fake(
    void* user_data,
    int kind,
    const char* name)
{
    (void)user_data;
    if( kind == BUILDER_NAME_VAR )
        return strcmp(name, "varp7_mock_quest_progress") == 0;
    return strcmp(name, "chatplayer") == 0;
}

static void
test_graph_panel(const char* tmp)
{
    char content[1024];
    char path[1200];
    struct EditorHost host;
    struct mirror mirror;
    struct Builder_Submit submit;
    struct Builder_Model* model = model_new();
    struct Builder_PanelView view;
    static struct ToriRSChrome chrome;
    static struct EditorUi ui;
    struct Builder_Services services;
    struct EditorUiPanel* panel;
    char key[EDITOR_DOC_KEY_MAX];
    const char* text;
    size_t length = 0;
    int graph;

    snprintf(content, sizeof(content), "%s/content_graph", tmp);
    snprintf(path, sizeof(path), "%s/maps", content);
    mkdir_p(path);
    snprintf(path, sizeof(path), "%s/server/scripts", content);
    mkdir_p(path);
    snprintf(path, sizeof(path), "%s/configs", content);
    mkdir_p(path);
    memset(&host, 0, sizeof(host));
    check(Editor_HostOpenMapEdEmbed(&host, content, NULL) == 1, "the embedded authority opens the graph's tree");
    memset(&mirror, 0, sizeof(mirror));
    mirror.host = &host;
    mirror.doc = doc_new();
    submit.user_data = &mirror;
    submit.apply = mirror_apply;
    submit.stroke = mirror_stroke;
    submit.current = mirror_current;

    ToriRSChrome_Init(&chrome);
    ToriRSChrome_SetSurface(&chrome, 1280, 900);
    memset(&ui, 0, sizeof(ui));
    ui.chrome = &chrome;
    ui.visible = 1;
    Editor_HotkeysInit(&ui.hotkeys);
    memset(&services, 0, sizeof(services));
    services.user_data = &mirror;
    services.submit = submit;
    services.save = mirror_save;
    services.record = fake_record;
    services.triggers = fake_triggers;
    services.name_known = name_known_fake;
    Builder_NpcPanelRegister(&ui, &services);
    panel = Editor_UiFindPanel(&ui, BUILDER_PANEL_NPC);
    ToriRSChrome_Build(&chrome);

    check(Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "graph_npc"), "the npc builder opens graph_npc");
    fixture_npc(model, "graph_npc");
    check(Builder_PanelSetModel(&ui, BUILDER_PANEL_NPC, model), "...and takes the fixture's dialogue");
    Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
    graph = view.w_graph;
    check(graph >= 0 && view.w_pages >= 0 && view.w_preview >= 0, "the dialogue has its graph, pages and preview wells");
    ToriRSChrome_PanelSetVisible(&chrome, panel->chrome_panel, 1);
    panel->tick(&ui, panel->self);
    ToriRSChrome_Build(&chrome);

    /* The buttons wear their chords. */
    {
        int dialogue = chrome.widgets[graph].panel;
        int layout = find_widget(&chrome, dialogue, "Auto-layout");
        int add = find_widget(&chrome, dialogue, "New node");
        int del = find_widget(&chrome, dialogue, "Delete node");
        check(layout >= 0 && strstr(chrome.widgets[layout].chord, "L") != NULL
                  && strstr(chrome.widgets[layout].chord, "trl") != NULL,
              "Auto-layout wears Ctrl+L");
        check(add >= 0 && strcmp(chrome.widgets[add].chord, "N") == 0, "New node wears N");
        check(del >= 0 && strstr(chrome.widgets[del].chord, "Del") != NULL, "Delete node wears Delete");
    }

    /* A port dragged onto a node links it: bye's port to thanks. */
    {
        const struct Builder_Node* bye = Builder_ModelNodeConst(view.model, "bye");
        const struct Builder_Node* thanks = Builder_ModelNodeConst(view.model, "thanks");
        int const px = bye->x + 60;
        int const py = bye->y + 26;
        check(ToriRSChrome_GraphKeyAt(&chrome, graph, px, py) >= 1000, "a node's port is a keyed item of the well");
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, px, py, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, thanks->x + 10, thanks->y + 10, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(graph_items(&chrome, graph, TORIRS_CHROME_GRAPH_LINE, 0xffd24a) == 1, "the link follows the pointer");
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, thanks->x + 10, thanks->y + 10, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(strcmp(Builder_ModelNodeConst(view.model, "bye")->next, "thanks") == 0, "dropping it on a node links the port");
        check(strstr(g_status, "Linked bye port 1 to thanks") != NULL, "...and says so");
        Builder_FileKey(BUILDER_SUBJECT_NPC, "graph_npc", BUILDER_FILE_SCRIPT, key, sizeof(key));
        text = mirror_text(&mirror, key, &length);
        check(text && strstr(text, "~mesbox(\"The patron goes back to polishing a glass.\");") != NULL,
              "the link regenerates the script through the authority");
    }

    /* Dropped on empty space: the chip row opens there; a chip makes the
     * node and links it. */
    {
        const struct Builder_Node* bye = Builder_ModelNodeConst(view.model, "bye");
        int const px = bye->x + 60;
        int const py = bye->y + 26;
        int count = view.model->node_count;
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, px, py, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, 20, 230, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, 20, 230, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.chips_open && view.model->node_count == count, "a port dropped on empty space opens the chip row");
        check(ToriRSChrome_GraphKeyAt(&chrome, graph, 20 + 4 * 54 + 5, 235) == 5000 + BUILDER_NODE_CHOICE,
              "the chips are keyed by kind, at the drop");
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, 20 + 2 * 54 + 5, 235, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, 20 + 2 * 54 + 5, 235, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(!view.chips_open && view.model->node_count == count + 1
                  && view.model->nodes[count].kind == BUILDER_NODE_MESBOX && view.model->nodes[count].x == 20
                  && view.model->nodes[count].y == 230,
              "the Mesbox chip makes a mesbox where the port was dropped");
        check(strcmp(Builder_ModelNodeConst(view.model, "bye")->next, view.model->nodes[count].id) == 0
                  && view.sel_node == count,
              "...linked from that port, and selected");
        /* Escape closes an open row. */
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, px, py, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, 400, 230, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, 400, 230, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_KEY, 0, 0, -TORIRS_CHROME_KEY_ESCAPE);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(!view.chips_open && view.model->node_count == count + 1, "Escape closes the chip row, making nothing");
    }

    /* Shift+drag moves the subtree; a plain drag the node alone. */
    {
        const struct Builder_Node* work = Builder_ModelNodeConst(view.model, "work");
        int const wx = work->x;
        int const wy = work->y;
        int const jx = Builder_ModelNodeConst(view.model, "job")->x;
        int const hx = Builder_ModelNodeConst(view.model, "hello")->x;
        ui.shift_held = 1;
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, wx + 30, wy + 10, 0);
        ui.shift_held = 0;
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, wx + 30 + 40, wy + 10 + 7, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, wx + 30 + 40, wy + 10 + 7, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(Builder_ModelNodeConst(view.model, "work")->x == wx + 40
                  && Builder_ModelNodeConst(view.model, "work")->y == wy + 7
                  && Builder_ModelNodeConst(view.model, "job")->x == jx + 40,
              "Shift+drag moves the node and everything after it");
        check(Builder_ModelNodeConst(view.model, "hello")->x == hx, "...and not what comes before it");
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_PRESS, wx + 40 + 30, wy + 7 + 10, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_DRAG, wx + 30, wy + 10, 0);
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_RELEASE, wx + 30, wy + 10, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(Builder_ModelNodeConst(view.model, "work")->x == wx && Builder_ModelNodeConst(view.model, "job")->x == jx + 40,
              "a plain drag moves the node alone");
        Builder_FileKey(BUILDER_SUBJECT_NPC, "graph_npc", BUILDER_FILE_MODEL, key, sizeof(key));
        text = mirror_text(&mirror, key, &length);
        {
            char want[32];
            snprintf(want, sizeof(want), "pos=%d,%d\n", jx + 40, Builder_ModelNodeConst(view.model, "job")->y);
            check(text && strstr(text, want) != NULL, "the moved positions are in the .interaction as pos=");
        }
    }

    /* Ctrl+L: the hotkey lays the graph out (it refused before 6.6). */
    {
        struct Builder_Model* expect = model_new();
        int same = 1;
        ToriRSChrome_FocusWidget(&chrome, graph, 0);
        check(Builder_GraphHotkey(&ui, EDITOR_HK_GRAPH_AUTO_LAYOUT), "Ctrl+L is the graph's");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        *expect = *view.model;
        Builder_ModelAutoLayout(expect);
        for( int i = 0; i < expect->node_count; i++ )
            same &= expect->nodes[i].x == view.model->nodes[i].x && expect->nodes[i].y == view.model->nodes[i].y;
        check(same && strstr(g_status, "Auto-layout:") != NULL, "...and lays it out by depth, saying how");
        free(expect);
    }

    /* The tree is the same model: a row selects the node, a node its row. */
    {
        int row = -1;
        int index = Builder_ModelNodeIndex(view.model, "job");
        for( int i = 0; i < chrome.widget_count && row < 0; i++ )
            if( chrome.widgets[i].kind == TORIRS_CHROME_W_TREE && strncmp(chrome.widgets[i].label, "job ", 4) == 0 )
                row = i;
        check(row >= 0 && panel->activate(&ui, panel->self, row), "the outline has a row for every node");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.sel_node == index, "selecting the row selects the node in the graph");
        check(graph_items(&chrome, graph, TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0xffd24a) == 1,
              "...which draws its selection ring");
        Builder_PanelSelectNode(&ui, Builder_ModelNodeIndex(view.model, "nowork"));
        row = -1;
        for( int i = 0; i < chrome.widget_count && row < 0; i++ )
            if( chrome.widgets[i].kind == TORIRS_CHROME_W_TREE && chrome.widgets[i].checked
                && strncmp(chrome.widgets[i].label, "nowork ", 7) == 0 )
                row = i;
        check(row >= 0, "selecting the node selects its outline row");
    }

    /* The selected page: the text wraps at the line rule, a page break is
     * a thin rule, and the preview shows the page. */
    {
        int node_text = -1;
        int index = Builder_ModelNodeIndex(view.model, "hello");
        struct EditorUiPanel* p = panel;
        for( int i = 0; i < chrome.widget_count && node_text < 0; i++ )
            if( strncmp(chrome.widgets[i].label, "Text ({player}", 14) == 0 )
                node_text = i;
        Builder_PanelSelectNode(&ui, index);
        ToriRSChrome_SetText(&chrome, node_text,
                             "The guards say the old tower north of the river has been empty for years, but "
                             "every night somebody lights a lamp up there, and every morning it is out again.");
        p->tick(&ui, p->self);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.layout->line_count >= 2, "the typed text is laid out at the chat box's line rule as it is typed");
        /* A one-line field holds 191 bytes, under a page at 380 px; hard
         * breaks make the fifth line. */
        ToriRSChrome_SetText(&chrome, node_text,
                             "{player}, the old tower is empty.|Yet a lamp burns there each night.|"
                             "And each morning it is out.|Go and look.|Tell me what you find there.");
        p->tick(&ui, p->self);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.layout->line_count == 5 && view.layout->page_count == 2, "a fifth line runs onto a second page");
        check(graph_items(&chrome, view.w_pages, TORIRS_CHROME_GRAPH_RECT, 0x7a8ad8) == 1,
              "...and the pages well draws one page-break rule");
        graph_send(&ui, panel, view.w_pages, TORIRS_CHROME_EVENT_GRAPH_PRESS, 10, 2 + 4 * 16 + 4, 0);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.sel_page == 1, "a click on the second page selects it");
        {
            int second = 0;
            int first = 0;
            const struct ToriRSChromeWidget* w = &chrome.widgets[view.w_preview];
            for( int i = 0; i < w->graph_count; i++ )
            {
                const struct ToriRSChromeGraphItem* item = &chrome.graph_items[w->graph_first + i];
                if( item->kind != TORIRS_CHROME_GRAPH_TEXT || item->color != 0x000000 )
                    continue;
                second += strcmp(item->text, "Tell me what you find there.") == 0;
                first += strncmp(item->text, BUILDER_CHAT_PLAYER_STANDIN, 12) == 0;
            }
            check(second == 1 && first == 0, "the preview draws that page's line, not the first page's");
        }
    }

    /* Lint rows: they flow to Problems, and a click goes to the node. */
    {
        struct Builder_Model* edited = model_new();
        int lost;
        struct Builder_Node* n;
        *edited = *view.model;
        n = Builder_ModelNodeAdd(edited, BUILDER_NODE_STEP);
        Builder_StepInit(&n->step, BUILDER_STEP_CALL);
        snprintf(n->step.a, sizeof(n->step.a), "%s", "no_such_proc");
        /* The longest id a node may have: its caption must still fit. */
        snprintf(n->id, sizeof(n->id), "%s", "stray_call_with_a_long_name_xyz");
        n->x = 600;
        n->y = 200;
        lost = edited->node_count - 1;
        g_problem_count = -1;
        check(Builder_PanelSetModel(&ui, BUILDER_PANEL_NPC, edited), "an edit with a stray call");
        free(edited);
        {
            const struct ToriRSChromeWidget* w = &chrome.widgets[graph];
            int found = 0;
            for( int i = 0; i < w->graph_count; i++ )
            {
                const struct ToriRSChromeGraphItem* item = &chrome.graph_items[w->graph_first + i];
                if( item->kind != TORIRS_CHROME_GRAPH_TEXT || item->x != 604 || item->y != 217 )
                    continue;
                found = 1;
                check(ToriRSChrome_MeasureText(chrome.theme.font_row, 1, item->text) <= 120 - 8
                          && strstr(item->text, "...") != NULL,
                      "a caption wider than its node is measured and ends in an ellipsis inside the box");
            }
            check(found, "the long-named node has its caption");
        }
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        {
            int orphan_row = -1;
            int proc_row = -1;
            for( int i = 0; i < view.lint->count; i++ )
            {
                if( view.lint->rows[i].node == lost && view.lint->rows[i].kind == BUILDER_LINT_ORPHAN )
                    orphan_row = i;
                if( view.lint->rows[i].node == lost && view.lint->rows[i].kind == BUILDER_LINT_UNKNOWN_PROC )
                    proc_row = i;
            }
            /* `end` is an orphan too: the chip's mesbox took bye's port and
             * leads nowhere. */
            check(orphan_row >= 0 && proc_row >= 0 && view.lint->count == 3 && g_problem_count == 3
                      && strcmp(g_problem_source, "npcbuilder:graph_npc") == 0,
                  "its lint rows (the stray node's orphan and proc, end's orphan) go to Problems as the subject's "
                  "source");
            check(g_problem_open != NULL, "...with a click-to-node");
            Builder_PanelSelectNode(&ui, -1);
            if( g_problem_open )
                g_problem_open(&ui, g_problem_user, orphan_row);
            Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
            check(view.sel_node == lost, "a Problems row's click selects its node");
            Builder_PanelSelectNode(&ui, -1);
            check(Builder_PanelOpenLint(&ui, proc_row), "the builder's own lint list opens a row too");
        }
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.sel_node == lost, "...at the same node");
    }

    /* Delete: from the focused well; the sentence names the orphans. */
    {
        int count;
        Builder_PanelSelectNode(&ui, Builder_ModelNodeIndex(view.model, "work"));
        count = view.model->node_count;
        ToriRSChrome_FocusWidget(&chrome, -1, 0);
        check(!Builder_GraphHotkey(&ui, EDITOR_HK_EDIT_DELETE),
              "Delete without the graph's focus is not the graph's (the World's delete runs)");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.model->node_count == count, "...and no node was deleted");
        {
            struct Editor_Chord chord;
            memset(&chord, 0, sizeof(chord));
            chord.key = TORIRSK_DELETE;
            check(Editor_HotkeysMatch(&ui.hotkeys, chord, EDITOR_HK_CTX_L) == EDITOR_HK_EDIT_DELETE,
                  "Delete is edit.delete's chord (the graph takes the chord from that row)");
        }
        graph_send(&ui, panel, graph, TORIRS_CHROME_EVENT_GRAPH_KEY, 0, 0, -TORIRS_CHROME_KEY_DELETE);
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.model->node_count == count - 1 && !Builder_ModelNodeConst(view.model, "work"),
              "Delete in the well deletes the selected node");
        check(strstr(g_status, "Deleted work (Branch on var)") != NULL && strstr(g_status, "job") != NULL
                  && strstr(g_status, "nowork") != NULL && strstr(g_status, "orphaned") != NULL,
              "...and the sentence names what it orphaned");
        check(Editor_HostMapEdUndo(&host) > 0, "the delete undoes as one step");
        mirror_drain(&mirror);
        check(Builder_PanelOpenSubject(&ui, BUILDER_PANEL_NPC, "graph_npc"), "re-opened after the undo");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(Builder_ModelNodeConst(view.model, "work") && view.model->node_count == count,
              "...the node is back, with its links");
    }

    /* N: a new node from the hotkey; the loc builder has no graph. */
    {
        int count = view.model->node_count;
        check(Builder_GraphHotkey(&ui, EDITOR_HK_GRAPH_NEW_NODE), "N is the graph's");
        Builder_PanelInspect(&ui, BUILDER_PANEL_NPC, &view);
        check(view.model->node_count == count + 1 && view.sel_node == count, "...and adds a selected node");
    }

    panel->destroy(&ui, panel->self);
    Editor_HostClose(&host);
    Editor_DocFree(mirror.doc);
    free(mirror.doc);
    free(model);
}

/* ---- compile ------------------------------------------------------------- */

static void
put_model(
    struct Builder_Ssc* ssc,
    const struct Builder_Model* model)
{
    struct Builder_Issues issues;
    struct Builder_Buf buf;
    char path[EDITOR_DOC_KEY_MAX];

    Builder_IssuesClear(&issues);
    Builder_BufInit(&buf);
    Builder_EmitScript(model, &buf, &issues);
    Builder_FilePath(model->subject, model->name, BUILDER_FILE_SCRIPT, path, sizeof(path));
    check(Builder_SscPut(ssc, path, Builder_BufText(&buf), buf.length), "the generated script is staged");
    Builder_BufFree(&buf);
    Builder_BufInit(&buf);
    Builder_EmitConfig(model, &buf, &issues);
    Builder_FilePath(model->subject, model->name, BUILDER_FILE_CONFIG, path, sizeof(path));
    Builder_SscPut(ssc, path, Builder_BufText(&buf), buf.length);
    Builder_BufFree(&buf);
}

static void
test_compile(
    const char* content,
    const char* tmp,
    const char* sscompile)
{
    struct Builder_Ssc* ssc = malloc(sizeof(*ssc));
    struct Builder_SscResult* result = malloc(sizeof(*result));
    struct Builder_Model* npc = model_new();
    struct Builder_Model* loc = model_new();
    char work[1024];
    char inside[1200];
    char script_path[EDITOR_DOC_KEY_MAX];
    char loc_path[EDITOR_DOC_KEY_MAX];

    assert(ssc);
    assert(result);
    snprintf(inside, sizeof(inside), "%s/server/scripts/build_maped_probe", content);
    check(!Builder_SscInit(ssc, sscompile, content, inside), "the runner refuses a work dir inside the content tree");
    snprintf(work, sizeof(work), "%s/ssc", tmp);
    check(Builder_SscInit(ssc, sscompile, content, work), "the runner takes a work dir under tmp");

    fixture_npc(npc, "port_roberts_bar_patron");
    fixture_loc(loc);
    check(Builder_SscStage(ssc) && ssc->last_copied + ssc->last_kept > 1000, "the stage mirrors what sscompile reads");
    printf("  stage: %d copied, %d kept, %d dropped\n", ssc->last_copied, ssc->last_kept, ssc->last_dropped);
    put_model(ssc, npc);
    put_model(ssc, loc);
    Builder_FilePath(npc->subject, npc->name, BUILDER_FILE_SCRIPT, script_path, sizeof(script_path));
    Builder_FilePath(loc->subject, loc->name, BUILDER_FILE_SCRIPT, loc_path, sizeof(loc_path));

    Builder_SscRun(ssc, BUILDER_SSC_COMPILE, result);
    printf("  sscompile: status %d, exit %d, %.2f s, %s\n", (int)result->status, result->exit_code,
           result->seconds, result->summary);
    for( int i = 0; i < result->diag_count; i++ )
    {
        if( !result->diags[i].warning )
            fprintf(stderr, "      %s:%d: %s\n", result->diags[i].file, result->diags[i].line,
                    result->diags[i].message);
    }
    check(result->status == BUILDER_SSC_OK, "the tree with a generated Talk-to and a crate compiles");
    check(Builder_SscErrorsFor(result, script_path) == 0 && Builder_SscErrorsFor(result, loc_path) == 0,
          "no error names a generated file");

    /* Re-staging keeps everything: nothing changed in the tree. */
    check(Builder_SscStage(ssc) && ssc->last_copied == 0 && ssc->last_dropped == 0,
          "a second stage copies nothing");

    /* A broken model: the error names the generated file and its line. */
    {
        struct Builder_Trigger* t = Builder_ModelTrigger(loc, 1, 0, "");
        struct Builder_Step s;
        Builder_StepInit(&s, BUILDER_STEP_CALL);
        snprintf(s.a, sizeof(s.a), "%s", "ed_no_such_proc_anywhere");
        Builder_TriggerStepAdd(t, &s);
        put_model(ssc, loc);
        Builder_SscRun(ssc, BUILDER_SSC_COMPILE, result);
        printf("  broken: status %d, %.2f s, %d diagnostic(s)\n", (int)result->status, result->seconds,
               result->diag_count);
        check(result->status == BUILDER_SSC_FAILED && Builder_SscErrorsFor(result, loc_path) >= 1,
              "an unknown proc fails the compile with an error on the generated script");
        {
            int located = 0;
            for( int i = 0; i < result->diag_count; i++ )
                located |= strcmp(result->diags[i].file, loc_path) == 0 && result->diags[i].line > 3;
            check(located, "...at a line inside the generated triggers");
        }
        Builder_TriggerStepRemove(t, t->step_count - 1);
        put_model(ssc, loc);
    }

    Builder_SscRun(ssc, BUILDER_SSC_DRY_RUN, result);
    check(result->status == BUILDER_SSC_OK, "--dry-run runs (it lists stale units, compiles nothing)");

    {
        char parse_stage[] = "/s/scripts";
        static const char k_output[] =
            "sscompile: /s/scripts/edited/a/scripts/a.rs2:12: warning: 'inv' names two\n"
            "/s/scripts/edited/a/scripts/a.rs2:14: unknown proc 'x'\n"
            "lanes: 0 of 3 compiled\n"
            "sscompile: 1 compiled, 3183 reused, 0 removed; 5 scripts\n";
        memset(result, 0, sizeof(*result));
        Builder_SscParseOutput(parse_stage, k_output, sizeof(k_output) - 1, result);
        check(result->diag_count == 2 && result->diags[0].warning && result->diags[0].line == 12
                  && strcmp(result->diags[1].file, "edited/a/scripts/a.rs2") == 0
                  && strcmp(result->diags[1].message, "unknown proc 'x'") == 0
                  && strncmp(result->summary, "1 compiled", 10) == 0,
              "compiler output parses into located diagnostics and the summary");
    }
    free(ssc);
    free(result);
    free(npc);
    free(loc);
}

int
main(
    int argc,
    char** argv)
{
    const char* content;
    const char* tmp;
    const char* sscompile;
    const char* golden_dir;
    const char* only;

    if( argc != 5 )
    {
        fprintf(stderr, "usage: builder_test <content-dir> <tmp-dir> <sscompile> <golden-dir>\n");
        return 2;
    }
    content = argv[1];
    tmp = argv[2];
    sscompile = argv[3];
    golden_dir = argv[4];
    only = getenv("BUILDER_TEST_ONLY");
    mkdir_p(tmp);
    {
        char probe[1200];
        struct stat st;
        snprintf(probe, sizeof(probe), "%s/configs/all.loc", content);
        g_have_content = stat(probe, &st) == 0;
        if( !g_have_content )
            printf("no content tree at %s: the shell sweep and the compile section are skipped\n", content);
    }

#define SECTION(name, call)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if( !only || strcmp(only, name) == 0 )                                                     \
        {                                                                                          \
            int before_ = g_failed;                                                                \
            call;                                                                                  \
            printf("%-10s %s\n", name, g_failed == before_ ? "ok" : "FAILED");                     \
        }                                                                                          \
    } while( 0 )

    SECTION("ini", test_ini());
    SECTION("golden", test_golden(golden_dir));
    SECTION("dialogue", test_dialogue());
    SECTION("lint", test_lint());
    SECTION("conflicts", test_conflicts());
    SECTION("splices", test_splices());
    SECTION("shell", test_shell(content));
    SECTION("preview", test_preview());
    SECTION("authority", test_authority(tmp));
    SECTION("graph", test_graph());
    SECTION("chat", test_chat());
    SECTION("lintrows", test_lint_rows());
    SECTION("graphpanel", test_graph_panel(tmp));
    if( g_have_content )
        SECTION("compile", test_compile(content, tmp, sscompile));
    else
        printf("%-10s skipped (no content tree)\n", "compile");
#undef SECTION

    printf("builder_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
