#include "builder_panels.h"

#include "builder_shell.h"
#include "editor/editor_cfg.h"
#include "editor/editor_ui.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared --------------------------------------------------------------- */

#define BP_ISSUE_ROWS 8
#define BP_OUTLINE_MAX (BUILDER_TRIGGERS_MAX * (BUILDER_STEPS_MAX + 1) + BUILDER_NODES_MAX + 4)
#define BP_NODE_W 120
#define BP_NODE_H 26
#define BP_GRAPH_H 260

/* Borrowed by the dropdowns for the widgets' whole life: static. */
static const char* const k_trigger_options[] = {
    "op1", "op2", "op3", "op4", "op5", "ap1", "ap2", "ap3", "ap4", "ap5", "use-on (op)", "use-on (ap)",
};
#define BP_TRIGGER_OPTION_COUNT 12

static const char* k_step_options[BUILDER_STEP_KIND_COUNT];
static const char* k_node_options[BUILDER_NODE_KIND_COUNT];
static int k_options_ready;

static const char* const k_door_options[] = { "no door", "door (closed)", "door (opened)" };
static const char* const k_ladder_options[] = { "no ladder",          "climb up",
                                                "climb down",         "climb (ask)",
                                                "climb up (ladder)",  "climb down (ladder)",
                                                "climb (ask, ladder)" };
static const char* const k_subject_options[] = { "loc", "npc" };
static const char* const k_var_options[] = { "varbit", "varp" };

/** What fields A, B, C and Text mean for each step kind ("" hides it). */
static const char* const k_step_fields[BUILDER_STEP_KIND_COUNT][4] = {
    { "", "", "", "Message" },
    { "Var", "Compare (= ! < > <= >=)", "Value", "" },
    { "Var", "Compare (= ! < > <= >=)", "Value", "" },
    { "", "", "", "" },
    { "", "", "", "" },
    { "Var", "Value", "", "" },
    { "Obj", "Count", "", "" },
    { "Obj", "Count", "", "" },
    { "Obj", "Count", "", "Said when missing" },
    { "Stat", "Level", "", "Said when too low" },
    { "Interface", "", "", "" },
    { "Target (iface:comp)", "Interface", "Mode", "" },
    { "Seq", "", "", "" },
    { "Synth", "", "", "" },
    { "Coord (level_mx_mz_lx_lz)", "", "", "" },
    { "Ticks", "", "", "" },
    { "Loc", "Ticks", "", "" },
    { "Proc", "Arguments", "", "" },
    { "Label", "", "", "" },
    { "", "", "", "RuneScript (\\n between lines)" },
};

static void
options_init(void)
{
    if( k_options_ready )
        return;
    for( int i = 0; i < BUILDER_STEP_KIND_COUNT; i++ )
        k_step_options[i] = Builder_StepKindLabel((enum Builder_StepKind)i);
    for( int i = 0; i < BUILDER_NODE_KIND_COUNT; i++ )
        k_node_options[i] = Builder_NodeKindLabel((enum Builder_NodeKind)i);
    k_options_ready = 1;
}

static void
copy_text(
    char* out,
    size_t out_size,
    const char* text)
{
    snprintf(out, out_size, "%s", text ? text : "");
}

/* ---- the builder panel ---------------------------------------------------- */

struct builder_panel
{
    enum Builder_Subject subject;
    struct Builder_Services services;
    struct Builder_Ssc ssc;
    int ssc_ready;

    struct Builder_Model* model;
    int has_model;
    int read_only;
    struct Builder_RecordInfo record;
    struct Builder_Issues issues;
    struct Builder_SscResult* check;
    char status[192];

    int sel_trigger;
    int sel_step;
    int sel_node;

    int p_main;
    int p_outline;
    int p_dialogue;

    int w_subject, w_open, w_fromsel, w_status;
    int w_trigger_kind, w_use_obj, w_trigger_add, w_trigger_remove, w_trigger_dialogue;
    int w_menu_text, w_rename_op;
    int w_step_kind, w_step_field[4], w_step_add, w_step_apply, w_step_remove, w_step_up,
        w_step_down;
    int w_category, w_door, w_door_other, w_ladder, w_server_apply;
    int w_regen, w_check, w_save, w_detach;
    int w_issue[BP_ISSUE_ROWS];

    int outline_rows[BP_OUTLINE_MAX];
    int outline_trigger[BP_OUTLINE_MAX];
    int outline_step[BP_OUTLINE_MAX];
    int outline_node[BP_OUTLINE_MAX];
    int outline_count;

    int w_graph, w_node_kind, w_node_add, w_node_remove, w_node_start;
    int w_node_text, w_node_expr, w_node_next, w_node_speaker_npc, w_node_speaker_name, w_node_obj,
        w_node_row, w_node_quest, w_node_apply;
    int w_node_arm[BUILDER_ARMS_MAX];
    int dragging;
    int drag_dx;
    int drag_dy;
};

static void
bp_status(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* text)
{
    copy_text(self->status, sizeof(self->status), text);
    ToriRSChrome_SetText(ui->chrome, self->w_status, self->status);
    Editor_UiLog(ui, "%s builder: %s", self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", text);
}

static void
bp_show_issues(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    for( int i = 0; i < BP_ISSUE_ROWS; i++ )
    {
        char line[TORIRS_CHROME_INPUT_MAX];
        if( i < self->issues.count )
        {
            const struct Builder_Issue* issue = &self->issues.items[i];
            snprintf(line, sizeof(line), "%s %s: %s",
                     issue->severity == BUILDER_ISSUE_ERROR ? "error" : "warning", issue->where,
                     issue->message);
        }
        else if( i == 0 )
            snprintf(line, sizeof(line), "%s", self->has_model ? "no issues" : "");
        else
            line[0] = '\0';
        ToriRSChrome_SetText(ui->chrome, self->w_issue[i], line);
        ToriRSChrome_SetHidden(ui->chrome, self->w_issue[i], line[0] == '\0');
    }
}

static void
bp_set_editable(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int off = !self->has_model || self->read_only;
    const int widgets[] = {
        self->w_trigger_add, self->w_trigger_remove, self->w_trigger_dialogue, self->w_step_add,
        self->w_step_apply,  self->w_step_remove,    self->w_step_up,          self->w_step_down,
        self->w_server_apply, self->w_regen,         self->w_detach,          self->w_rename_op,
    };

    for( size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++ )
    {
        if( widgets[i] >= 0 )
            ToriRSChrome_SetDisabled(ui->chrome, widgets[i], off);
    }
    if( self->w_node_apply >= 0 )
    {
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_apply, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_add, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_remove, off);
        ToriRSChrome_SetDisabled(ui->chrome, self->w_node_start, off);
    }
}

static void
step_summary(
    const struct Builder_Step* step,
    char* out,
    size_t out_size)
{
    const char* label = Builder_StepKindLabel(step->kind);

    switch( step->kind )
    {
    case BUILDER_STEP_MES:
        snprintf(out, out_size, "%s: %s", label, step->text);
        break;
    case BUILDER_STEP_IF_VAR:
    case BUILDER_STEP_ELSE_IF_VAR:
        snprintf(out, out_size, "%s %%%s %s %s", label, step->a, step->b, step->c);
        break;
    case BUILDER_STEP_RAW:
        snprintf(out, out_size, "%s (%zu bytes)", label, strlen(step->text));
        break;
    default:
        snprintf(out, out_size, "%s %s %s %s", label, step->a, step->b, step->c);
        break;
    }
}

static void
trigger_label(
    const struct builder_panel* self,
    const struct Builder_Trigger* trigger,
    char* out,
    size_t out_size)
{
    const char* word = self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";

    if( trigger->op == BUILDER_OP_AI )
        snprintf(out, out_size, "[%s] combat", trigger->ai);
    else if( trigger->op == 0 )
        snprintf(out, out_size, "[%s%su] with %s%s", trigger->approach ? "ap" : "op", word,
                 trigger->use_obj, trigger->dialogue ? " -> dialogue" : "");
    else
        snprintf(out, out_size, "[%s%s%d] %s%s", trigger->approach ? "ap" : "op", word, trigger->op,
                 trigger->label, trigger->dialogue ? " -> dialogue" : "");
}

static void
outline_push(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* label,
    int depth,
    int expander,
    int trigger,
    int step,
    int node)
{
    int widget;

    if( self->outline_count >= BP_OUTLINE_MAX )
        return;
    widget = ToriRSChrome_TreeRow(ui->chrome, self->p_outline, label, depth, expander);
    if( widget < 0 )
        return;
    self->outline_rows[self->outline_count] = widget;
    self->outline_trigger[self->outline_count] = trigger;
    self->outline_step[self->outline_count] = step;
    self->outline_node[self->outline_count] = node;
    if( (trigger >= 0 && trigger == self->sel_trigger && step == self->sel_step && node < 0)
        || (node >= 0 && node == self->sel_node) )
        ToriRSChrome_TreeSelect(ui->chrome, widget);
    self->outline_count++;
}

/** The dialogue as a tree: depth-first from the start, a node reached a
 *  second time shown as a jump. The accessible form of the GRAPH. */
static void
outline_dialogue(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* id,
    int depth,
    unsigned char* seen)
{
    const struct Builder_Model* model = self->model;

    while( id[0] && depth < 24 )
    {
        int index = -1;
        const struct Builder_Node* node;
        char line[TORIRS_CHROME_LABEL_MAX];

        for( int i = 0; i < model->node_count; i++ )
        {
            if( strcmp(model->nodes[i].id, id) == 0 )
                index = i;
        }
        if( index < 0 )
            return;
        node = &model->nodes[index];
        if( seen[index] )
        {
            snprintf(line, sizeof(line), "-> %s", node->id);
            outline_push(ui, self, line, depth, TORIRS_CHROME_TREE_LEAF, -1, -1, index);
            return;
        }
        seen[index] = 1;
        snprintf(line, sizeof(line), "%s %s: %s", node->id, Builder_NodeKindLabel(node->kind),
                 node->kind == BUILDER_NODE_BRANCH ? node->obj : node->text);
        outline_push(ui, self, line, depth,
                     node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH
                         ? TORIRS_CHROME_TREE_OPEN
                         : TORIRS_CHROME_TREE_LEAF,
                     -1, -1, index);
        if( node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH )
        {
            for( int a = 0; a < node->arm_count; a++ )
            {
                if( node->kind == BUILDER_NODE_CHOICE )
                    snprintf(line, sizeof(line), "option %d: %s", a + 1, node->arms[a].text);
                else
                    snprintf(line, sizeof(line), "if %s %s %s", node->obj, node->arms[a].cmp,
                             node->arms[a].value);
                outline_push(ui, self, line, depth + 1, TORIRS_CHROME_TREE_OPEN, -1, -1, index);
                outline_dialogue(ui, self, node->arms[a].next, depth + 2, seen);
            }
            if( node->kind == BUILDER_NODE_BRANCH && node->next[0] )
            {
                outline_push(ui, self, "otherwise", depth + 1, TORIRS_CHROME_TREE_OPEN, -1, -1, index);
                outline_dialogue(ui, self, node->next, depth + 2, seen);
            }
            return;
        }
        if( node->kind == BUILDER_NODE_END )
            return;
        id = node->next;
    }
}

static void
bp_rebuild_outline(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    ToriRSChrome_PanelClearWidgets(ui->chrome, self->p_outline);
    self->outline_count = 0;
    if( !self->has_model )
    {
        ToriRSChrome_Label(ui->chrome, self->p_outline, "Open a subject to see its triggers.");
        return;
    }
    for( int t = 0; t < self->model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &self->model->triggers[t];
        trigger_label(self, trigger, line, sizeof(line));
        outline_push(ui, self, line, 0,
                     trigger->step_count && !trigger->dialogue ? TORIRS_CHROME_TREE_OPEN
                                                               : TORIRS_CHROME_TREE_LEAF,
                     t, -1, -1);
        if( trigger->dialogue )
            continue;
        for( int s = 0; s < trigger->step_count; s++ )
        {
            char summary[TORIRS_CHROME_LABEL_MAX];
            step_summary(&trigger->steps[s], summary, sizeof(summary));
            snprintf(line, sizeof(line), "%d. %s", s + 1, summary);
            outline_push(ui, self, line, 1, TORIRS_CHROME_TREE_LEAF, t, s, -1);
        }
    }
    if( self->model->node_count > 0 )
    {
        unsigned char seen[BUILDER_NODES_MAX];
        memset(seen, 0, sizeof(seen));
        outline_push(ui, self, "Dialogue", 0, TORIRS_CHROME_TREE_OPEN, -1, -1, -1);
        outline_dialogue(ui, self, self->model->start, 1, seen);
    }
}

static uint32_t
node_color(enum Builder_NodeKind kind)
{
    static const uint32_t k_colors[BUILDER_NODE_KIND_COUNT] = {
        0x3a5f8a, 0x3a7a4a, 0x6a6a6a, 0x6a5a3a, 0x8a6a2a, 0x7a3a7a, 0x4a4a6a, 0x8a3a3a, 0x2a2a2a,
    };
    return k_colors[kind];
}

static void
graph_edge(
    struct EditorUi* ui,
    struct builder_panel* self,
    const struct Builder_Node* from,
    const char* to_id,
    int port,
    int ports)
{
    const struct Builder_Node* to;
    int x0;

    if( !to_id[0] )
        return;
    to = Builder_ModelNodeConst(self->model, to_id);
    if( !to )
        return;
    x0 = from->x + (BP_NODE_W * (port + 1)) / (ports + 1);
    ToriRSChrome_GraphLine(ui->chrome, self->w_graph, x0, from->y + BP_NODE_H, to->x + BP_NODE_W / 2,
                           to->y, 0xc8c8c8);
}

static void
bp_redraw_graph(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    if( self->w_graph < 0 )
        return;
    ToriRSChrome_GraphBegin(ui->chrome, self->w_graph);
    if( self->has_model )
    {
        const struct Builder_Model* model = self->model;
        for( int n = 0; n < model->node_count; n++ )
        {
            const struct Builder_Node* node = &model->nodes[n];
            int ports = node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH
                            ? node->arm_count + (node->kind == BUILDER_NODE_BRANCH ? 1 : 0)
                            : 1;
            for( int a = 0; a < node->arm_count && node->kind != BUILDER_NODE_STEP; a++ )
                graph_edge(ui, self, node, node->arms[a].next, a, ports);
            if( node->kind != BUILDER_NODE_CHOICE && node->kind != BUILDER_NODE_END )
                graph_edge(ui, self, node, node->next, ports - 1, ports);
        }
        for( int n = 0; n < model->node_count; n++ )
        {
            const struct Builder_Node* node = &model->nodes[n];
            char label[TORIRS_CHROME_GRAPH_TEXT_MAX];

            ToriRSChrome_GraphRect(ui->chrome, self->w_graph, node->x, node->y, BP_NODE_W, BP_NODE_H,
                                   node_color(node->kind), 1, n + 1);
            if( n == self->sel_node )
                ToriRSChrome_GraphRect(ui->chrome, self->w_graph, node->x - 2, node->y - 2,
                                       BP_NODE_W + 4, BP_NODE_H + 4, 0xffd24a, 0, 0);
            if( strcmp(node->id, model->start) == 0 )
                ToriRSChrome_GraphRect(ui->chrome, self->w_graph, node->x - 4, node->y + 4, 3,
                                       BP_NODE_H - 8, 0x4aff7a, 1, 0);
            snprintf(label, sizeof(label), "%s %s", node->id, Builder_NodeKindToken(node->kind));
            ToriRSChrome_GraphText(ui->chrome, self->w_graph, node->x + 4, node->y + 17, 0xffffff,
                                   label);
        }
    }
    ToriRSChrome_GraphEnd(ui->chrome, self->w_graph);
}

static void
bp_show_step_fields(
    struct EditorUi* ui,
    struct builder_panel* self,
    enum Builder_StepKind kind)
{
    for( int f = 0; f < 4; f++ )
    {
        const char* label = k_step_fields[kind][f];
        ToriRSChrome_SetLabel(ui->chrome, self->w_step_field[f], label[0] ? label : "-");
        ToriRSChrome_SetHidden(ui->chrome, self->w_step_field[f], label[0] == '\0');
    }
}

static void
bp_load_step(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    const struct Builder_Step* step;
    const struct Builder_Trigger* trigger;

    if( !self->has_model || self->sel_trigger < 0 || self->sel_trigger >= self->model->trigger_count )
        return;
    trigger = &self->model->triggers[self->sel_trigger];
    if( self->sel_step < 0 || self->sel_step >= trigger->step_count )
        return;
    step = &trigger->steps[self->sel_step];
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->w_step_kind, (int)step->kind);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[0], step->a);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[1], step->b);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[2], step->c);
    ToriRSChrome_SetText(ui->chrome, self->w_step_field[3], step->text);
    bp_show_step_fields(ui, self, step->kind);
}

static void
bp_read_step(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Step* step)
{
    int kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_step_kind);
    const char* text;

    if( kind < 0 || kind >= BUILDER_STEP_KIND_COUNT )
        kind = BUILDER_STEP_MES;
    memset(step, 0, sizeof(*step));
    step->kind = (enum Builder_StepKind)kind;
    copy_text(step->a, sizeof(step->a), ToriRSChrome_Text(ui->chrome, self->w_step_field[0]));
    copy_text(step->b, sizeof(step->b), ToriRSChrome_Text(ui->chrome, self->w_step_field[1]));
    copy_text(step->c, sizeof(step->c), ToriRSChrome_Text(ui->chrome, self->w_step_field[2]));
    /* RAW is one input line: "\n" stands for a line break in it. */
    text = ToriRSChrome_Text(ui->chrome, self->w_step_field[3]);
    if( step->kind == BUILDER_STEP_RAW )
    {
        size_t out = 0;
        for( const char* at = text ? text : ""; *at && out + 1 < sizeof(step->text); at++ )
        {
            if( at[0] == '\\' && at[1] == 'n' )
            {
                step->text[out++] = '\n';
                at++;
            }
            else
                step->text[out++] = *at;
        }
        step->text[out] = '\0';
    }
    else
        copy_text(step->text, sizeof(step->text), text);
}

static void
bp_load_node(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    const struct Builder_Node* node;
    struct ToriRSChrome* chrome = ui->chrome;

    if( self->w_graph < 0 || !self->has_model || self->sel_node < 0
        || self->sel_node >= self->model->node_count )
        return;
    node = &self->model->nodes[self->sel_node];
    ToriRSChrome_DropdownSetSelected(chrome, self->w_node_kind, (int)node->kind);
    ToriRSChrome_SetText(chrome, self->w_node_text,
                         node->kind == BUILDER_NODE_STEP ? node->step.text : node->text);
    ToriRSChrome_SetText(chrome, self->w_node_expr, node->expr);
    ToriRSChrome_SetText(chrome, self->w_node_next, node->next);
    ToriRSChrome_SetText(chrome, self->w_node_speaker_npc, node->speaker_npc);
    ToriRSChrome_SetText(chrome, self->w_node_speaker_name, node->speaker_name);
    ToriRSChrome_SetText(chrome, self->w_node_obj,
                         node->kind == BUILDER_NODE_STEP ? node->step.a : node->obj);
    ToriRSChrome_SetText(chrome, self->w_node_row,
                         node->kind == BUILDER_NODE_QUEST && node->quest == BUILDER_QUEST_START
                             ? node->value
                             : node->row);
    ToriRSChrome_SetText(chrome, self->w_node_quest,
                         node->kind == BUILDER_NODE_STEP
                             ? Builder_StepKindToken(node->step.kind)
                             : (node->quest == BUILDER_QUEST_COMPLETE ? "complete" : "start"));
    for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
    {
        char arm[TORIRS_CHROME_INPUT_MAX] = "";
        if( a < node->arm_count )
        {
            if( node->kind == BUILDER_NODE_CHOICE )
                snprintf(arm, sizeof(arm), "%s|%s", node->arms[a].text, node->arms[a].next);
            else
                snprintf(arm, sizeof(arm), "%s|%s|%s", node->arms[a].cmp, node->arms[a].value,
                         node->arms[a].next);
        }
        ToriRSChrome_SetText(chrome, self->w_node_arm[a], arm);
    }
}

/** Fill `node` from the node form. Returns 0 (and logs) on a malformed arm. */
static int
bp_read_node(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Node* node)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int kind = ToriRSChrome_DropdownSelected(chrome, self->w_node_kind);
    const char* quest = ToriRSChrome_Text(chrome, self->w_node_quest);

    if( kind < 0 || kind >= BUILDER_NODE_KIND_COUNT )
        return 0;
    node->kind = (enum Builder_NodeKind)kind;
    copy_text(node->expr, sizeof(node->expr), ToriRSChrome_Text(chrome, self->w_node_expr));
    copy_text(node->next, sizeof(node->next), ToriRSChrome_Text(chrome, self->w_node_next));
    copy_text(node->speaker_npc, sizeof(node->speaker_npc),
              ToriRSChrome_Text(chrome, self->w_node_speaker_npc));
    copy_text(node->speaker_name, sizeof(node->speaker_name),
              ToriRSChrome_Text(chrome, self->w_node_speaker_name));
    copy_text(node->row, sizeof(node->row), ToriRSChrome_Text(chrome, self->w_node_row));
    if( node->kind == BUILDER_NODE_STEP )
    {
        /* A dialogue step: Quest field names the step kind, Obj is its first
         * field, Text its text. */
        enum Builder_StepKind step_kind = Builder_StepKindParse(quest ? quest : "");
        if( step_kind == BUILDER_STEP_KIND_COUNT )
            step_kind = BUILDER_STEP_SET_VAR;
        if( node->step.kind != step_kind )
            Builder_StepInit(&node->step, step_kind);
        copy_text(node->step.a, sizeof(node->step.a), ToriRSChrome_Text(chrome, self->w_node_obj));
        copy_text(node->step.text, sizeof(node->step.text),
                  ToriRSChrome_Text(chrome, self->w_node_text));
    }
    else
    {
        copy_text(node->text, sizeof(node->text), ToriRSChrome_Text(chrome, self->w_node_text));
        copy_text(node->obj, sizeof(node->obj), ToriRSChrome_Text(chrome, self->w_node_obj));
        node->quest = quest && strcmp(quest, "complete") == 0 ? BUILDER_QUEST_COMPLETE
                                                              : BUILDER_QUEST_START;
        if( node->kind == BUILDER_NODE_QUEST && node->quest == BUILDER_QUEST_START )
        {
            copy_text(node->value, sizeof(node->value), node->row);
            node->row[0] = '\0';
        }
    }
    if( node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH )
    {
        node->arm_count = 0;
        for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
        {
            char arm[TORIRS_CHROME_INPUT_MAX];
            char* parts[3] = { NULL, NULL, NULL };
            int count = 0;
            struct Builder_Arm* out;

            copy_text(arm, sizeof(arm), ToriRSChrome_Text(chrome, self->w_node_arm[a]));
            if( arm[0] == '\0' )
                continue;
            parts[count++] = arm;
            for( char* at = arm; *at && count < 3; at++ )
            {
                if( *at == '|' )
                {
                    *at = '\0';
                    parts[count++] = at + 1;
                }
            }
            out = &node->arms[node->arm_count];
            memset(out, 0, sizeof(*out));
            if( node->kind == BUILDER_NODE_CHOICE )
            {
                if( count != 2 )
                {
                    Editor_UiLog(ui, "npc builder: option %d must be 'text|next node'", a + 1);
                    return 0;
                }
                copy_text(out->text, sizeof(out->text), parts[0]);
                copy_text(out->next, sizeof(out->next), parts[1]);
            }
            else
            {
                if( count != 3 )
                {
                    Editor_UiLog(ui, "npc builder: arm %d must be 'cmp|value|next node'", a + 1);
                    return 0;
                }
                copy_text(out->cmp, sizeof(out->cmp), parts[0]);
                copy_text(out->value, sizeof(out->value), parts[1]);
                copy_text(out->next, sizeof(out->next), parts[2]);
            }
            node->arm_count++;
        }
    }
    else
        node->arm_count = 0;
    return 1;
}

static void
bp_refresh(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    bp_rebuild_outline(ui, self);
    bp_redraw_graph(ui, self);
    bp_show_issues(ui, self);
    bp_set_editable(ui, self);
}

/** The model changed: regenerate every derived file through the authority,
 *  as one stroke. */
static int
bp_commit(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    enum Builder_SyncResult result;

    assert(self->has_model);
    Builder_IssuesClear(&self->issues);
    if( self->read_only )
    {
        bp_status(ui, self, "read-only: the edit was not applied");
        bp_refresh(ui, self);
        return 0;
    }
    if( !self->services.submit.apply )
    {
        Builder_Validate(self->model, &self->issues);
        bp_status(ui, self, "no session: the model is not saved anywhere");
        bp_refresh(ui, self);
        return 0;
    }
    result = Builder_Regenerate(self->model, &self->services.submit, &self->issues);
    if( result == BUILDER_SYNC_REFUSED )
        bp_status(ui, self, "the authority refused the regeneration (someone else edited the files); re-open");
    else
    {
        char line[192];
        snprintf(line, sizeof(line), "%s: %s (%d error(s), %d warning(s))", self->model->name,
                 result == BUILDER_SYNC_APPLIED ? "regenerated" : "unchanged", self->issues.errors,
                 self->issues.count - self->issues.errors);
        bp_status(ui, self, line);
    }
    bp_refresh(ui, self);
    return result != BUILDER_SYNC_REFUSED;
}

static int
bp_open(
    struct EditorUi* ui,
    struct builder_panel* self,
    const char* name)
{
    char key[EDITOR_DOC_KEY_MAX];
    char error[160];
    const char* text = NULL;
    size_t length = 0;
    struct Builder_ExistingList* existing;

    if( !Builder_NameValid(name) )
    {
        bp_status(ui, self, "a subject is a record name: [a-z0-9_]");
        return 0;
    }
    memset(&self->record, 0, sizeof(self->record));
    if( self->services.record
        && !self->services.record(self->services.user_data, self->subject, name, -1, &self->record) )
    {
        char line[160];
        snprintf(line, sizeof(line), "no %s named %s in the catalog",
                 self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", name);
        bp_status(ui, self, line);
        return 0;
    }

    self->has_model = 1;
    self->read_only = 0;
    self->sel_trigger = -1;
    self->sel_step = -1;
    self->sel_node = -1;
    Builder_IssuesClear(&self->issues);
    Builder_FileKey(self->subject, name, BUILDER_FILE_MODEL, key, sizeof(key));
    if( self->services.submit.current )
        text = self->services.submit.current(self->services.user_data, EDITOR_DOC_TEXT, key, &length);
    if( text && length > 0 )
    {
        if( !Builder_ModelParseIni(self->model, text, length, error, sizeof(error))
            || strcmp(self->model->name, name) != 0 || self->model->subject != self->subject )
        {
            char line[192];
            Builder_ModelInit(self->model, self->subject, name);
            self->read_only = 1;
            snprintf(line, sizeof(line), "%s: the model does not read (%s); read-only", name, error);
            bp_status(ui, self, line);
            bp_refresh(ui, self);
            return 1;
        }
    }
    else
    {
        char script_key[EDITOR_DOC_KEY_MAX];
        const char* script = NULL;
        size_t script_length = 0;

        Builder_ModelInit(self->model, self->subject, name);
        /* A script in the builder's directory without a model is a detached
         * one: hand-authored, and a regeneration would overwrite it. */
        Builder_FileKey(self->subject, name, BUILDER_FILE_SCRIPT, script_key, sizeof(script_key));
        if( self->services.submit.current )
            script = self->services.submit.current(self->services.user_data, EDITOR_DOC_TEXT,
                                                   script_key, &script_length);
        if( script && script_length > 0 && !Builder_HasHeader(script, script_length) )
        {
            self->read_only = 1;
            bp_status(ui, self, "this subject's script is hand-authored (detached); open the script");
            bp_refresh(ui, self);
            return 1;
        }
    }

    existing = ToriMem_Alloc(ToriMem_Editor(), sizeof(*existing));
    assert(existing);
    memset(existing, 0, sizeof(*existing));
    if( self->services.triggers
        && self->services.triggers(self->services.user_data, name, existing) )
    {
        if( Builder_FindConflicts(self->model, existing->rows, existing->count, &self->issues) > 0 )
            self->read_only = 1;
        /* Triggers the model does not have yet would also collide the moment
         * they are added: name the ops a hand-authored script already owns. */
        for( int i = 0; i < existing->count; i++ )
        {
            char own[EDITOR_DOC_KEY_MAX];
            size_t own_length;
            size_t file_length = strlen(existing->files[i]);
            const char* comma = strchr(existing->names[i], ',');
            Builder_FilePath(self->subject, name, BUILDER_FILE_SCRIPT, own, sizeof(own));
            own_length = strlen(own);
            if( !comma || strcmp(comma + 1, name) != 0 )
                continue;
            if( file_length >= own_length
                && strcmp(existing->files[i] + file_length - own_length, own) == 0 )
                continue;
            Builder_IssueAdd(&self->issues, BUILDER_ISSUE_WARNING, existing->names[i],
                             "hand-authored in %s; the builder cannot add it", existing->files[i]);
        }
    }
    else
        Builder_IssueAdd(&self->issues, BUILDER_ISSUE_WARNING, "subject",
                         "no catalog: duplicate triggers are not checked until Check");
    ToriMem_Free(ToriMem_Editor(), existing);

    {
        char line[192];
        snprintf(line, sizeof(line), "%s%s: %d trigger(s), %d node(s)%s", name,
                 self->record.display[0] ? "" : "", self->model->trigger_count,
                 self->model->node_count, self->read_only ? " -- read-only (see issues)" : "");
        bp_status(ui, self, line);
    }
    ToriRSChrome_SetText(ui->chrome, self->w_subject, name);
    bp_refresh(ui, self);
    return 1;
}

static int
bp_check(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct Builder_Buf texts[BUILDER_FILE_COUNT];
    struct Builder_Issues scratch;
    int errors = 0;
    char line[192];

    if( !self->has_model )
    {
        bp_status(ui, self, "open a subject first");
        return 0;
    }
    if( !self->ssc_ready )
    {
        bp_status(ui, self, "Check needs the compile paths (Builder_PanelsSetCompile)");
        return 0;
    }
    Builder_IssuesClear(&scratch);
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
        Builder_BufInit(&texts[f]);
    Builder_EmitScript(self->model, &texts[BUILDER_FILE_SCRIPT], &scratch);
    Builder_EmitConfig(self->model, &texts[BUILDER_FILE_CONFIG], &scratch);
    Builder_EmitDbrow(self->model, &texts[BUILDER_FILE_DBROW], &scratch);
    for( int f = BUILDER_FILE_SCRIPT; f < BUILDER_FILE_COUNT; f++ )
    {
        char path[EDITOR_DOC_KEY_MAX];
        Builder_FilePath(self->subject, self->model->name, (enum Builder_File)f, path, sizeof(path));
        Builder_SscPut(&self->ssc, path, Builder_BufText(&texts[f]), texts[f].length);
        Builder_BufFree(&texts[f]);
    }
    Builder_SscStage(&self->ssc);
    Builder_SscRun(&self->ssc, BUILDER_SSC_COMPILE, self->check);

    {
        char script[EDITOR_DOC_KEY_MAX];
        Builder_FilePath(self->subject, self->model->name, BUILDER_FILE_SCRIPT, script, sizeof(script));
        for( int i = 0; i < self->check->diag_count; i++ )
        {
            const struct Builder_SscDiag* diag = &self->check->diags[i];
            char where[64];
            if( strncmp(diag->file, "edited/", 7) != 0
                || strncmp(diag->file + 7, self->model->name, strlen(self->model->name)) != 0 )
                continue;
            snprintf(where, sizeof(where), "line %d", diag->line);
            Builder_IssueAdd(&self->issues, diag->warning ? BUILDER_ISSUE_WARNING : BUILDER_ISSUE_ERROR,
                             where, "sscompile: %s", diag->message);
            if( !diag->warning )
                errors++;
        }
        (void)script;
    }
    if( self->check->status == BUILDER_SSC_OK )
        snprintf(line, sizeof(line), "compiles (%.1f s)", self->check->seconds);
    else if( self->check->status == BUILDER_SSC_FAILED )
        snprintf(line, sizeof(line), "sscompile failed: %d error(s) in %s's files; log %s", errors,
                 self->model->name, self->check->log_path);
    else
        snprintf(line, sizeof(line), "sscompile could not run (%s)",
                 self->check->status == BUILDER_SSC_UNSUPPORTED ? "not on this platform"
                                                               : "no binary or stage");
    bp_status(ui, self, line);
    bp_show_issues(ui, self);
    return self->check->status == BUILDER_SSC_OK;
}

static int
bp_save(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    int saved = 0;
    int failed = 0;
    char line[160];

    if( !self->has_model || !self->services.save )
    {
        bp_status(ui, self, self->has_model ? "no session to save through" : "open a subject first");
        return 0;
    }
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        int result;
        Builder_FileKey(self->subject, self->model->name, (enum Builder_File)f, key, sizeof(key));
        result = self->services.save(self->services.user_data, EDITOR_DOC_TEXT, key);
        if( result > 0 )
            saved++;
        else if( result < 0 )
            failed++;
    }
    snprintf(line, sizeof(line), "saved %d file(s)%s", saved, failed ? ", some refused" : "");
    bp_status(ui, self, line);
    return failed == 0;
}

static int
bp_detach(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    if( !self->has_model || self->read_only || !self->services.submit.apply )
    {
        bp_status(ui, self, "nothing to detach");
        return 0;
    }
    if( Builder_Detach(self->subject, self->model->name, &self->services.submit) == BUILDER_SYNC_REFUSED )
    {
        bp_status(ui, self, "the authority refused the detach; re-open");
        return 0;
    }
    self->read_only = 1;
    bp_status(ui, self, "detached: the scripts are hand-authored from here on");
    bp_refresh(ui, self);
    return 1;
}

static struct Builder_Trigger*
bp_selected_trigger(struct builder_panel* self)
{
    if( !self->has_model || self->sel_trigger < 0 || self->sel_trigger >= self->model->trigger_count )
        return NULL;
    return &self->model->triggers[self->sel_trigger];
}

/**
 * Rename op N (design §11 "the form"): a CFG_FIELD on the cache record's
 * `opN=`, which changes the menu the CLIENT shows -- a cache pack, not a
 * script, delivers it, and the status says so. The trigger's label follows.
 */
static int
bp_rename_op(
    struct EditorUi* ui,
    struct builder_panel* self,
    struct Builder_Trigger* trigger)
{
    char doc_key[EDITOR_DOC_KEY_MAX];
    char field[8];
    const char* text;
    const char* want;
    size_t length = 0;
    struct Editor_CfgBlock block;
    struct Editor_Cmd command;
    int line;
    int ok;

    if( !trigger || trigger->op == 0 || trigger->op == BUILDER_OP_AI )
    {
        bp_status(ui, self, "select an op trigger (op1..op5 / ap1..ap5) first");
        return 0;
    }
    want = ToriRSChrome_Text(ui->chrome, self->w_menu_text);
    if( !want || strchr(want, '\n') || strlen(want) >= EDITOR_CMD_VALUE_MAX )
        return 0;
    snprintf(doc_key, sizeof(doc_key), "%s:%s", self->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc",
             self->model->name);
    snprintf(field, sizeof(field), "op%d", trigger->op);
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, doc_key, &length);
    if( !text || length == 0 )
    {
        bp_status(ui, self, "the cache record is not in configs/all.<loc|npc>");
        return 0;
    }
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, text, length);
    line = Editor_CfgBlockFieldFind(&block, field, 0);
    Editor_CmdInit(&command, EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
    command.has_before = 1;
    command.has_after = 1;
    snprintf(command.u.cfg_field.key, sizeof(command.u.cfg_field.key), "%s", field);
    if( line >= 0 )
    {
        size_t value_length = 0;
        const char* value = Editor_CfgBlockValue(&block, line, &value_length);
        if( value_length >= sizeof(command.u.cfg_field.before) )
            value_length = sizeof(command.u.cfg_field.before) - 1;
        memcpy(command.u.cfg_field.before, value, value_length);
        command.u.cfg_field.before[value_length] = '\0';
    }
    Editor_CfgBlockFree(&block);
    snprintf(command.u.cfg_field.after, sizeof(command.u.cfg_field.after), "%s", want);
    if( strcmp(command.u.cfg_field.before, command.u.cfg_field.after) == 0 )
        return 1;
    ok = self->services.submit.apply(self->services.user_data, &command);
    if( !ok )
    {
        bp_status(ui, self, "the authority refused the rename; re-open the subject");
        return 0;
    }
    copy_text(trigger->label, sizeof(trigger->label), want);
    snprintf(self->record.ops[trigger->op - 1], sizeof(self->record.ops[0]), "%s", want);
    bp_commit(ui, self);
    bp_status(ui, self, "renamed: the client's menu changes with the next client cache pack");
    return 1;
}

static int
bp_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct builder_panel* self = opaque;
    struct ToriRSChrome* chrome = ui->chrome;
    struct Builder_Trigger* trigger;

    if( widget == self->w_open || widget == self->w_subject )
    {
        bp_open(ui, self, ToriRSChrome_Text(chrome, self->w_subject));
        return 1;
    }
    if( widget == self->w_fromsel )
    {
        struct Builder_RecordInfo info;
        if( ui->tools.loc_count == 0 || !self->services.record )
        {
            bp_status(ui, self, "select a loc in the world first");
            return 1;
        }
        memset(&info, 0, sizeof(info));
        if( self->services.record(self->services.user_data, self->subject, "", ui->tools.locs[0].loc_id,
                                  &info) )
            bp_open(ui, self, info.name);
        else
            bp_status(ui, self, "the selected loc is not in the catalog");
        return 1;
    }
    if( widget == self->w_regen )
    {
        if( self->has_model )
            bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_check )
    {
        bp_check(ui, self);
        return 1;
    }
    if( widget == self->w_save )
    {
        bp_save(ui, self);
        return 1;
    }
    if( widget == self->w_detach )
    {
        bp_detach(ui, self);
        return 1;
    }
    if( widget == self->w_step_kind )
    {
        int kind = ToriRSChrome_DropdownSelected(chrome, self->w_step_kind);
        if( kind >= 0 && kind < BUILDER_STEP_KIND_COUNT )
            bp_show_step_fields(ui, self, (enum Builder_StepKind)kind);
        return 1;
    }
    for( int i = 0; i < self->outline_count; i++ )
    {
        if( widget != self->outline_rows[i] )
            continue;
        if( self->outline_node[i] >= 0 )
        {
            self->sel_node = self->outline_node[i];
            bp_load_node(ui, self);
            bp_redraw_graph(ui, self);
        }
        else if( self->outline_trigger[i] >= 0 )
        {
            self->sel_trigger = self->outline_trigger[i];
            self->sel_step = self->outline_step[i];
            bp_load_step(ui, self);
        }
        ToriRSChrome_TreeSelect(chrome, widget);
        return 1;
    }
    if( widget == self->w_node_kind || widget == self->w_trigger_kind || widget == self->w_door
        || widget == self->w_ladder )
        return 1;

    /* Everything below edits the model. */
    if( !self->has_model )
    {
        int ours = widget == self->w_trigger_add || widget == self->w_trigger_remove
                   || widget == self->w_step_add || widget == self->w_step_apply
                   || widget == self->w_step_remove || widget == self->w_step_up
                   || widget == self->w_step_down || widget == self->w_server_apply
                   || widget == self->w_trigger_dialogue || widget == self->w_node_add
                   || widget == self->w_node_remove || widget == self->w_node_apply
                   || widget == self->w_node_start || widget == self->w_rename_op;
        if( ours )
            bp_status(ui, self, "open a subject first");
        return ours;
    }

    if( widget == self->w_trigger_add )
    {
        int option = ToriRSChrome_DropdownSelected(chrome, self->w_trigger_kind);
        int op = option >= 0 && option < 10 ? option % 5 + 1 : 0;
        int approach = option >= 5 && option != 10;
        const char* use = ToriRSChrome_Text(chrome, self->w_use_obj);
        struct Builder_Trigger* added;

        if( option < 0 )
            return 1;
        if( op == 0 && (!use || !use[0]) )
        {
            bp_status(ui, self, "a use-on trigger needs the held obj (or *)");
            return 1;
        }
        added = Builder_ModelTriggerAdd(self->model, op, approach, op == 0 ? use : "");
        if( !added )
        {
            bp_status(ui, self, "the subject has as many triggers as the builder holds");
            return 1;
        }
        if( op > 0 && added->label[0] == '\0' )
            copy_text(added->label, sizeof(added->label), self->record.ops[op - 1]);
        self->sel_trigger = (int)(added - self->model->triggers);
        self->sel_step = -1;
        bp_commit(ui, self);
        return 1;
    }
    trigger = bp_selected_trigger(self);
    if( widget == self->w_trigger_remove )
    {
        if( trigger )
        {
            Builder_ModelTriggerRemove(self->model, self->sel_trigger);
            self->sel_trigger = -1;
            self->sel_step = -1;
            bp_commit(ui, self);
        }
        return 1;
    }
    if( widget == self->w_rename_op )
    {
        bp_rename_op(ui, self, trigger);
        return 1;
    }
    if( widget == self->w_trigger_dialogue )
    {
        if( trigger )
        {
            trigger->dialogue = ToriRSChrome_Checked(chrome, self->w_trigger_dialogue);
            if( trigger->dialogue && self->model->node_count == 0 )
            {
                struct Builder_Node* node = Builder_ModelNodeAdd(self->model, BUILDER_NODE_NPC);
                if( node )
                    copy_text(node->text, sizeof(node->text), "Hello, {player}.");
            }
            bp_commit(ui, self);
        }
        return 1;
    }
    if( widget == self->w_step_add || widget == self->w_step_apply )
    {
        struct Builder_Step step;
        if( !trigger )
        {
            bp_status(ui, self, "select a trigger first");
            return 1;
        }
        bp_read_step(ui, self, &step);
        if( widget == self->w_step_apply && self->sel_step >= 0 && self->sel_step < trigger->step_count )
            trigger->steps[self->sel_step] = step;
        else
        {
            int index = Builder_TriggerStepAdd(trigger, &step);
            if( index < 0 )
            {
                bp_status(ui, self, "the trigger has as many steps as the builder holds");
                return 1;
            }
            self->sel_step = index;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_step_remove || widget == self->w_step_up || widget == self->w_step_down )
    {
        if( !trigger || self->sel_step < 0 || self->sel_step >= trigger->step_count )
            return 1;
        if( widget == self->w_step_remove )
        {
            Builder_TriggerStepRemove(trigger, self->sel_step);
            self->sel_step = -1;
        }
        else
        {
            int to = self->sel_step + (widget == self->w_step_up ? -1 : 1);
            if( to < 0 || to >= trigger->step_count )
                return 1;
            Builder_TriggerStepMove(trigger, self->sel_step, to);
            self->sel_step = to;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_server_apply )
    {
        struct Builder_Server* server = &self->model->server;
        int door = ToriRSChrome_DropdownSelected(chrome, self->w_door);
        int ladder = ToriRSChrome_DropdownSelected(chrome, self->w_ladder);

        copy_text(server->category, sizeof(server->category),
                  ToriRSChrome_Text(chrome, self->w_category));
        server->door = door == 1 ? BUILDER_DOOR_CLOSED : door == 2 ? BUILDER_DOOR_OPENED
                                                                   : BUILDER_DOOR_NONE;
        copy_text(server->door_other, sizeof(server->door_other),
                  ToriRSChrome_Text(chrome, self->w_door_other));
        server->ladder = BUILDER_LADDER_NONE;
        server->ladder_animated = 0;
        if( ladder >= 1 && ladder <= 6 )
        {
            server->ladder = (enum Builder_LadderDir)((ladder - 1) % 3 + 1);
            server->ladder_animated = ladder >= 4;
        }
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_node_add )
    {
        int kind = ToriRSChrome_DropdownSelected(chrome, self->w_node_kind);
        struct Builder_Node* node;
        if( kind < 0 || kind >= BUILDER_NODE_KIND_COUNT )
            kind = BUILDER_NODE_NPC;
        node = Builder_ModelNodeAdd(self->model, (enum Builder_NodeKind)kind);
        if( !node )
        {
            bp_status(ui, self, "the dialogue has as many nodes as the builder holds");
            return 1;
        }
        node->x = 10 + (self->model->node_count - 1) % 4 * (BP_NODE_W + 16);
        node->y = 10 + (self->model->node_count - 1) / 4 * (BP_NODE_H + 30);
        if( self->sel_node >= 0 && self->sel_node < self->model->node_count - 1 )
        {
            struct Builder_Node* from = &self->model->nodes[self->sel_node];
            if( from->kind != BUILDER_NODE_CHOICE && from->kind != BUILDER_NODE_END
                && from->next[0] == '\0' )
                copy_text(from->next, sizeof(from->next), node->id);
        }
        self->sel_node = self->model->node_count - 1;
        bp_load_node(ui, self);
        bp_commit(ui, self);
        return 1;
    }
    if( widget == self->w_node_remove || widget == self->w_node_apply || widget == self->w_node_start )
    {
        struct Builder_Node* node;
        if( self->sel_node < 0 || self->sel_node >= self->model->node_count )
            return 1;
        node = &self->model->nodes[self->sel_node];
        if( widget == self->w_node_remove )
        {
            char id[BUILDER_NODE_ID_MAX];
            copy_text(id, sizeof(id), node->id);
            Builder_ModelNodeRemove(self->model, id);
            self->sel_node = -1;
        }
        else if( widget == self->w_node_start )
            copy_text(self->model->start, sizeof(self->model->start), node->id);
        else
        {
            struct Builder_Node edited = *node;
            if( !bp_read_node(ui, self, &edited) )
                return 1;
            *node = edited;
        }
        bp_commit(ui, self);
        return 1;
    }
    return 0;
}

static int
bp_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct builder_panel* self = opaque;

    if( self->w_graph < 0 || event->widget != self->w_graph )
        return 0;
    if( !self->has_model )
        return 1;
    switch( event->kind )
    {
    case TORIRS_CHROME_EVENT_GRAPH_PRESS:
        if( event->key > 0 && event->key <= self->model->node_count )
        {
            const struct Builder_Node* node = &self->model->nodes[event->key - 1];
            self->sel_node = event->key - 1;
            self->dragging = !self->read_only;
            self->drag_dx = event->x - node->x;
            self->drag_dy = event->y - node->y;
            bp_load_node(ui, self);
            bp_rebuild_outline(ui, self);
        }
        else
            self->sel_node = -1;
        bp_redraw_graph(ui, self);
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_DRAG:
        if( self->dragging && self->sel_node >= 0 && self->sel_node < self->model->node_count )
        {
            struct Builder_Node* node = &self->model->nodes[self->sel_node];
            node->x = event->x - self->drag_dx;
            node->y = event->y - self->drag_dy;
            if( node->x < 0 )
                node->x = 0;
            if( node->y < 0 )
                node->y = 0;
            bp_redraw_graph(ui, self);
        }
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_RELEASE:
        /* The layout is part of the model: one regeneration per drag. */
        if( self->dragging )
        {
            self->dragging = 0;
            bp_commit(ui, self);
        }
        return 1;
    case TORIRS_CHROME_EVENT_GRAPH_MENU:
        if( !self->read_only && event->key == 0 )
        {
            int kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_node_kind);
            struct Builder_Node* node =
                Builder_ModelNodeAdd(self->model, kind >= 0 && kind < BUILDER_NODE_KIND_COUNT
                                                      ? (enum Builder_NodeKind)kind
                                                      : BUILDER_NODE_NPC);
            if( node )
            {
                node->x = event->x;
                node->y = event->y;
                self->sel_node = self->model->node_count - 1;
                bp_load_node(ui, self);
                bp_commit(ui, self);
            }
        }
        return 1;
    default:
        return 1;
    }
}

static void
bp_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct builder_panel* self = opaque;
    int visible = ui->visible && ui->chrome->panels[self->p_main].visible;

    /* The outline and the dialogue follow the form's window. */
    if( ui->chrome->panels[self->p_outline].visible != visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->p_outline, visible);
    if( self->p_dialogue >= 0 && ui->chrome->panels[self->p_dialogue].visible != visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->p_dialogue, visible);
}

static void
bp_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct builder_panel* self = opaque;

    /* The editor opened or closed: like the interface editor, a builder
     * starts (and stays) hidden until the View menu opens it, and closing the
     * editor closes it. The registry has just shown the form's window. */
    (void)visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_outline, 0);
    if( self->p_dialogue >= 0 )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->p_dialogue, 0);
}

static void
bp_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct builder_panel* self = opaque;
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->check);
    ToriMem_Free(ToriMem_Editor(), self->model);
    ToriMem_Free(ToriMem_Editor(), self);
}

static void
bp_build(
    struct EditorUi* ui,
    struct builder_panel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int npc = self->subject == BUILDER_SUBJECT_NPC;
    int p;

    options_init();
    self->p_main = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, npc ? 380 : 360,
                                         npc ? 60 : 40, 300, npc ? "NPC builder" : "Loc builder");
    assert(self->p_main >= 0);
    p = self->p_main;
    ToriRSChrome_PanelSetScrollable(chrome, p, 1);
    ToriRSChrome_PanelSetClosable(chrome, p, 1);
    self->w_subject = ToriRSChrome_TextInput(chrome, p, npc ? "NPC" : "Loc", "");
    self->w_open = ToriRSChrome_Button(chrome, p, "Open");
    self->w_fromsel = npc ? -1 : ToriRSChrome_Button(chrome, p, "From selection");
    self->w_status = ToriRSChrome_Label(chrome, p, "Open a subject.");

    ToriRSChrome_Heading(chrome, p, "Triggers");
    self->w_trigger_kind = ToriRSChrome_Dropdown(chrome, p, "Trigger", k_trigger_options,
                                                 BP_TRIGGER_OPTION_COUNT, 0);
    self->w_use_obj = ToriRSChrome_TextInput(chrome, p, "Held obj (use-on, * = any)", "");
    self->w_trigger_add = ToriRSChrome_Button(chrome, p, "Add trigger");
    self->w_trigger_remove = ToriRSChrome_Button(chrome, p, "Remove trigger");
    self->w_trigger_dialogue =
        npc ? ToriRSChrome_Checkbox(chrome, p, "Selected trigger runs the dialogue", 0) : -1;
    self->w_menu_text = ToriRSChrome_TextInput(chrome, p, "Menu text (the cache's opN)", "");
    self->w_rename_op = ToriRSChrome_Button(chrome, p, "Rename the op in the cache");
    ToriRSChrome_SetTooltip(chrome, self->w_rename_op, "Changes the client's menu: needs a cache pack");

    ToriRSChrome_Heading(chrome, p, "Step");
    self->w_step_kind =
        ToriRSChrome_Dropdown(chrome, p, "Kind", k_step_options, BUILDER_STEP_KIND_COUNT, 0);
    for( int f = 0; f < 4; f++ )
        self->w_step_field[f] = ToriRSChrome_TextInput(chrome, p, "-", "");
    self->w_step_add = ToriRSChrome_Button(chrome, p, "Add step");
    self->w_step_apply = ToriRSChrome_Button(chrome, p, "Apply to selected step");
    self->w_step_remove = ToriRSChrome_Button(chrome, p, "Remove step");
    self->w_step_up = ToriRSChrome_Button(chrome, p, "Move up");
    self->w_step_down = ToriRSChrome_Button(chrome, p, "Move down");
    bp_show_step_fields(ui, self, BUILDER_STEP_MES);

    ToriRSChrome_Heading(chrome, p, "Server fields");
    self->w_category = ToriRSChrome_TextInput(chrome, p, "Category", "");
    if( npc )
    {
        self->w_door = -1;
        self->w_door_other = -1;
        self->w_ladder = -1;
    }
    else
    {
        self->w_door = ToriRSChrome_Dropdown(chrome, p, "Door", k_door_options, 3, 0);
        self->w_door_other = ToriRSChrome_TextInput(chrome, p, "Door's other half", "");
        self->w_ladder = ToriRSChrome_Dropdown(chrome, p, "Ladder", k_ladder_options, 7, 0);
    }
    self->w_server_apply = ToriRSChrome_Button(chrome, p, "Apply server fields");

    ToriRSChrome_Separator(chrome, p);
    self->w_regen = ToriRSChrome_Button(chrome, p, "Regenerate");
    self->w_check = ToriRSChrome_Button(chrome, p, "Check (sscompile)");
    self->w_save = ToriRSChrome_Button(chrome, p, "Save");
    self->w_detach = ToriRSChrome_Button(chrome, p, "Detach");
    ToriRSChrome_SetTooltip(chrome, self->w_detach,
                            "Drop the model: the scripts become hand-authored");
    for( int i = 0; i < BP_ISSUE_ROWS; i++ )
    {
        self->w_issue[i] = ToriRSChrome_Label(chrome, p, "");
        ToriRSChrome_SetHidden(chrome, self->w_issue[i], 1);
    }

    self->p_outline = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, npc ? 690 : 670,
                                            npc ? 60 : 40, 260, npc ? "NPC outline" : "Loc outline");
    assert(self->p_outline >= 0);
    ToriRSChrome_PanelSetScrollable(chrome, self->p_outline, 1);

    self->p_dialogue = -1;
    self->w_graph = -1;
    self->w_node_kind = self->w_node_add = self->w_node_remove = self->w_node_start = -1;
    self->w_node_apply = -1;
    if( npc )
    {
        self->p_dialogue =
            ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 380, 420, 560, "Dialogue");
        assert(self->p_dialogue >= 0);
        p = self->p_dialogue;
        ToriRSChrome_PanelSetScrollable(chrome, p, 1);
        self->w_graph = ToriRSChrome_Graph(chrome, p, "Dialogue graph", BP_GRAPH_H);
        self->w_node_kind =
            ToriRSChrome_Dropdown(chrome, p, "Node kind", k_node_options, BUILDER_NODE_KIND_COUNT, 0);
        self->w_node_add = ToriRSChrome_Button(chrome, p, "Add node (after the selected one)");
        self->w_node_remove = ToriRSChrome_Button(chrome, p, "Remove node");
        self->w_node_start = ToriRSChrome_Button(chrome, p, "Make it the start");
        self->w_node_text = ToriRSChrome_TextInput(chrome, p, "Text ({player} = the player's name)", "");
        self->w_node_expr = ToriRSChrome_TextInput(chrome, p, "Expression (^chat_...)", "neutral");
        self->w_node_next = ToriRSChrome_TextInput(chrome, p, "Next node (or default arm)", "");
        self->w_node_speaker_npc = ToriRSChrome_TextInput(chrome, p, "Speaker npc (override)", "");
        self->w_node_speaker_name = ToriRSChrome_TextInput(chrome, p, "Speaker name", "");
        self->w_node_obj = ToriRSChrome_TextInput(chrome, p, "Obj / var / step field", "");
        self->w_node_row = ToriRSChrome_TextInput(chrome, p, "Quest row / first stage", "");
        self->w_node_quest = ToriRSChrome_TextInput(chrome, p, "Quest action / step kind", "");
        for( int a = 0; a < BUILDER_ARMS_MAX; a++ )
        {
            char label[48];
            snprintf(label, sizeof(label), "Port %d (text|next or cmp|value|next)", a + 1);
            self->w_node_arm[a] = ToriRSChrome_TextInput(chrome, p, label, "");
        }
        self->w_node_apply = ToriRSChrome_Button(chrome, p, "Apply to node");
    }
    ToriRSChrome_PanelSetVisible(chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(chrome, self->p_outline, 0);
    if( self->p_dialogue >= 0 )
        ToriRSChrome_PanelSetVisible(chrome, self->p_dialogue, 0);
    bp_refresh(ui, self);
}

static void
builder_register(
    struct EditorUi* ui,
    const struct Builder_Services* services,
    enum Builder_Subject subject)
{
    struct builder_panel* self;
    struct EditorUiPanel panel;

    assert(ui);
    assert(ui->chrome);
    assert(services);

    self = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self));
    assert(self);
    memset(self, 0, sizeof(*self));
    self->model = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->model));
    assert(self->model);
    self->check = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->check));
    assert(self->check);
    memset(self->check, 0, sizeof(*self->check));
    self->check->status = BUILDER_SSC_NOT_RUN;
    self->subject = subject;
    self->services = *services;
    self->sel_trigger = -1;
    self->sel_step = -1;
    self->sel_node = -1;
    Builder_ModelInit(self->model, subject, "none");
    bp_build(ui, self);

    memset(&panel, 0, sizeof(panel));
    panel.name = subject == BUILDER_SUBJECT_NPC ? BUILDER_PANEL_NPC : BUILDER_PANEL_LOC;
    panel.title = subject == BUILDER_SUBJECT_NPC ? "NPC builder" : "Loc builder";
    panel.self = self;
    panel.chrome_panel = self->p_main;
    panel.tick = bp_tick;
    panel.activate = bp_activate;
    panel.event = bp_event;
    panel.set_visible = bp_set_visible;
    panel.destroy = bp_destroy;
    Editor_UiAddPanel(ui, &panel);
}

void
Builder_LocPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    builder_register(ui, services, BUILDER_SUBJECT_LOC);
}

void
Builder_NpcPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    builder_register(ui, services, BUILDER_SUBJECT_NPC);
}

/* ---- the shell editor ----------------------------------------------------- */

struct shell_panel
{
    struct Builder_Services services;
    struct Builder_Shell shell;
    int has_shell;
    char doc_key[EDITOR_DOC_KEY_MAX];
    int sel_slot;
    struct Builder_PreviewVar preview[BUILDER_PREVIEW_MAX];
    int preview_count;

    int p_main;
    int p_slots;
    int w_kind, w_name, w_open, w_status, w_var_kind, w_var, w_default_none, w_summary;
    int w_new_name, w_copy;
    int w_child, w_set, w_hidden, w_insert, w_append, w_remove, w_apply;
    int w_preview, w_preview_label;
    int slot_rows[BUILDER_SHELL_SLOTS_MAX];
    int slot_row_count;
};

static void
sp_status(
    struct EditorUi* ui,
    struct shell_panel* self,
    const char* text)
{
    ToriRSChrome_SetText(ui->chrome, self->w_status, text);
    Editor_UiLog(ui, "shell editor: %s", text);
}

static void
sp_preview_label(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    char line[TORIRS_CHROME_INPUT_MAX];
    int value = ToriRSChrome_SliderValue(ui->chrome, self->w_preview);
    const char* child;

    if( !self->has_shell || self->shell.slot_count == 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->w_preview_label, "");
        return;
    }
    child = Builder_ShellResolveChild(&self->shell, value);
    snprintf(line, sizeof(line), "%s = %d shows %s (slot %d)", self->shell.var, value,
             child ? child : "nothing (hidden)", Builder_ShellResolve(&self->shell, value) + 1);
    ToriRSChrome_SetText(ui->chrome, self->w_preview_label, line);
}

static void
sp_rebuild(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    char summary[TORIRS_CHROME_INPUT_MAX];

    ToriRSChrome_PanelClearWidgets(ui->chrome, self->p_slots);
    self->slot_row_count = 0;
    if( !self->has_shell )
    {
        ToriRSChrome_Label(ui->chrome, self->p_slots, "Open a record to see its slots.");
        ToriRSChrome_SetText(ui->chrome, self->w_summary, "");
        return;
    }
    ToriRSChrome_Label(ui->chrome, self->p_slots, "value | child");
    for( int i = 0; i < self->shell.slot_count; i++ )
    {
        char value[32];
        char line[TORIRS_CHROME_LABEL_MAX];
        int widget;

        Builder_ShellSlotValueLabel(&self->shell, i, value, sizeof(value));
        snprintf(line, sizeof(line), "%s | %s", value,
                 Builder_ShellSlotHidden(&self->shell.slots[i]) ? "none (hidden)"
                                                                 : self->shell.slots[i].child);
        widget = ToriRSChrome_TreeRow(ui->chrome, self->p_slots, line, 0, TORIRS_CHROME_TREE_LEAF);
        self->slot_rows[self->slot_row_count++] = widget;
        if( i == self->sel_slot )
            ToriRSChrome_TreeSelect(ui->chrome, widget);
    }
    Builder_ShellSummary(&self->shell, summary, sizeof(summary));
    ToriRSChrome_SetText(ui->chrome, self->w_summary, summary);
    ToriRSChrome_SliderSetRange(ui->chrome, self->w_preview, 0,
                                self->shell.slot_count > 0 ? self->shell.slot_count : 1, 1);
    sp_preview_label(ui, self);
}

static int
sp_open(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    int kind = ToriRSChrome_DropdownSelected(ui->chrome, self->w_kind);
    const char* name = ToriRSChrome_Text(ui->chrome, self->w_name);
    enum Builder_Subject subject = kind == 1 ? BUILDER_SUBJECT_NPC : BUILDER_SUBJECT_LOC;
    const char* text;
    size_t length = 0;
    char line[160];
    int is_shell;

    if( !name || !Builder_NameValid(name) )
    {
        sp_status(ui, self, "a record name: [a-z0-9_]");
        return 0;
    }
    if( !self->services.submit.current )
    {
        sp_status(ui, self, "no session");
        return 0;
    }
    snprintf(self->doc_key, sizeof(self->doc_key), "%s:%s",
             subject == BUILDER_SUBJECT_NPC ? "npc" : "loc", name);
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, self->doc_key,
                                         &length);
    if( !text || length == 0 )
    {
        snprintf(line, sizeof(line), "no record %s", self->doc_key);
        sp_status(ui, self, line);
        self->has_shell = 0;
        sp_rebuild(ui, self);
        return 0;
    }
    is_shell = Builder_ShellParse(&self->shell, subject, text, length);
    self->has_shell = 1;
    self->sel_slot = -1;
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->w_var_kind,
                                     self->shell.var_kind == BUILDER_SHELL_VAR_VARP ? 1 : 0);
    ToriRSChrome_SetText(ui->chrome, self->w_var, self->shell.var);
    if( self->w_default_none >= 0 )
        ToriRSChrome_SetChecked(ui->chrome, self->w_default_none, self->shell.default_none);
    snprintf(line, sizeof(line), "%s: %s, %d slot(s)", self->doc_key,
             is_shell ? "a shell" : "not a shell yet", self->shell.slot_count);
    sp_status(ui, self, line);
    sp_rebuild(ui, self);
    return 1;
}

/** Copy to new...: the open record's definition under the typed name with
 *  a fresh id (one undo step); the copy becomes the open record. */
static void
sp_copy(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    const char* new_name = ToriRSChrome_Text(ui->chrome, self->w_new_name);
    const char* colon = strchr(self->doc_key, ':');
    char line[160];

    if( !self->has_shell || !colon )
    {
        sp_status(ui, self, "open a record first");
        return;
    }
    if( !new_name || !Builder_NameValid(new_name) )
    {
        sp_status(ui, self, "a new record name: [a-z0-9_]");
        return;
    }
    if( !self->services.copy_record )
    {
        sp_status(ui, self, "copying needs an editor server");
        return;
    }
    if( !self->services.copy_record(self->services.user_data, self->shell.subject, colon + 1, new_name) )
    {
        snprintf(line, sizeof(line), "%s was not copied to %s (see the console)", self->doc_key, new_name);
        sp_status(ui, self, line);
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->w_name, new_name);
    sp_open(ui, self);
}

static int
sp_apply(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    const char* text;
    size_t length = 0;
    struct Editor_Cmd* commands;
    int count;
    int applied = 0;
    char line[160];

    if( !self->has_shell || !self->services.submit.apply )
        return 0;
    if( self->shell.slot_count > 0 && self->shell.var_kind == BUILDER_SHELL_VAR_NONE )
    {
        sp_status(ui, self, "a shell needs its var");
        return 0;
    }
    if( self->shell.overflow )
    {
        sp_status(ui, self, "this table is longer than the editor holds; it is shown, not edited");
        return 0;
    }
    text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG, self->doc_key,
                                         &length);
    count = Builder_ShellCommands(self->doc_key, text ? text : "", length, &self->shell, NULL, 0);
    if( count == 0 )
    {
        sp_status(ui, self, "no change");
        return 1;
    }
    commands = ToriMem_Alloc(ToriMem_Editor(), sizeof(*commands) * (size_t)count);
    assert(commands);
    Builder_ShellCommands(self->doc_key, text ? text : "", length, &self->shell, commands, count);
    self->services.submit.stroke(self->services.user_data, 1);
    for( int i = 0; i < count; i++ )
    {
        if( !self->services.submit.apply(self->services.user_data, &commands[i]) )
            break;
        applied++;
    }
    self->services.submit.stroke(self->services.user_data, 0);
    ToriMem_Free(ToriMem_Editor(), commands);
    snprintf(line, sizeof(line), "%d of %d field edit(s) applied%s", applied, count,
             applied < count ? " -- refused; re-open" : "");
    sp_status(ui, self, line);
    return applied == count;
}

static void
sp_publish_preview(
    struct EditorUi* ui,
    struct shell_panel* self)
{
    int kind;
    int id;
    int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
    int count;

    if( !self->has_shell || !Builder_VarNameId(self->shell.var, &kind, &id) )
    {
        sp_status(ui, self, "the preview needs a varp<id>_ / varb<id>_ var name");
        return;
    }
    if( (kind == BUILDER_PREVIEW_VARP) != (self->shell.var_kind == BUILDER_SHELL_VAR_VARP) )
    {
        sp_status(ui, self, "the var's name and kind disagree");
        return;
    }
    self->preview_count = Builder_PreviewSet(self->preview, self->preview_count, kind, id,
                                             ToriRSChrome_SliderValue(ui->chrome, self->w_preview));
    count = Builder_PreviewEncode(self->preview, self->preview_count, values,
                                  TORIRSMAPED_STATE_VALUES_MAX);
    if( self->services.state_set )
        self->services.state_set(self->services.user_data, TORIRSMAPED_STATE_VAR_PREVIEW, values,
                                 count);
}

static int
sp_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct shell_panel* self = opaque;
    struct ToriRSChrome* chrome = ui->chrome;

    if( widget == self->w_open || widget == self->w_name )
    {
        sp_open(ui, self);
        return 1;
    }
    if( widget == self->w_new_name )
        return 1;
    if( widget == self->w_copy )
    {
        sp_copy(ui, self);
        return 1;
    }
    if( widget == self->w_kind || widget == self->w_var_kind )
        return 1;
    for( int i = 0; i < self->slot_row_count; i++ )
    {
        if( widget == self->slot_rows[i] )
        {
            self->sel_slot = i;
            ToriRSChrome_TreeSelect(chrome, widget);
            ToriRSChrome_SetText(chrome, self->w_child,
                                 Builder_ShellSlotHidden(&self->shell.slots[i]) ? ""
                                                                                : self->shell.slots[i].child);
            return 1;
        }
    }
    if( widget == self->w_preview )
    {
        sp_preview_label(ui, self);
        sp_publish_preview(ui, self);
        return 1;
    }
    if( widget != self->w_set && widget != self->w_hidden && widget != self->w_insert
        && widget != self->w_append && widget != self->w_remove && widget != self->w_apply
        && widget != self->w_default_none && widget != self->w_var )
        return 0;
    if( !self->has_shell )
    {
        sp_status(ui, self, "open a record first");
        return 1;
    }
    if( widget == self->w_var )
        return 1;
    if( widget == self->w_default_none )
    {
        self->shell.default_none = ToriRSChrome_Checked(chrome, self->w_default_none);
        return 1;
    }
    if( widget == self->w_set || widget == self->w_hidden )
    {
        const char* child = widget == self->w_hidden ? "-1" : ToriRSChrome_Text(chrome, self->w_child);
        if( self->sel_slot < 0 || self->sel_slot >= self->shell.slot_count )
        {
            sp_status(ui, self, "select a slot first");
            return 1;
        }
        if( !child || (strcmp(child, "-1") != 0 && !Builder_NameValid(child)) )
        {
            sp_status(ui, self, "a child is a record name, or Hidden");
            return 1;
        }
        copy_text(self->shell.slots[self->sel_slot].child, BUILDER_NAME_MAX, child);
    }
    else if( widget == self->w_insert || widget == self->w_append )
    {
        int at = widget == self->w_append || self->sel_slot < 0 ? self->shell.slot_count
                                                               : self->sel_slot;
        if( !Builder_ShellInsertSlot(&self->shell, at, "-1") )
        {
            sp_status(ui, self, "the shell has as many slots as the editor holds");
            return 1;
        }
        self->sel_slot = at;
    }
    else if( widget == self->w_remove )
    {
        if( self->sel_slot < 0 || self->sel_slot >= self->shell.slot_count )
            return 1;
        Builder_ShellRemoveSlot(&self->shell, self->sel_slot);
        self->sel_slot = -1;
    }
    else if( widget == self->w_apply )
    {
        const char* var = ToriRSChrome_Text(chrome, self->w_var);
        int var_kind = ToriRSChrome_DropdownSelected(chrome, self->w_var_kind);
        copy_text(self->shell.var, sizeof(self->shell.var), var);
        self->shell.var_kind = self->shell.var[0] == '\0' ? BUILDER_SHELL_VAR_NONE
                               : var_kind == 1            ? BUILDER_SHELL_VAR_VARP
                                                          : BUILDER_SHELL_VAR_VARBIT;
        sp_apply(ui, self);
        /* Read back what the authority now holds. */
        {
            size_t length = 0;
            const char* text = self->services.submit.current(self->services.user_data, EDITOR_DOC_CFG,
                                                             self->doc_key, &length);
            if( text )
                Builder_ShellParse(&self->shell, self->shell.subject, text, length);
        }
    }
    sp_rebuild(ui, self);
    return 1;
}

static void
sp_on_state(
    struct EditorUi* ui,
    void* opaque,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct shell_panel* self = opaque;
    struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
    int kind;
    int id;
    int n;

    if( key != TORIRSMAPED_STATE_VAR_PREVIEW )
        return;
    n = Builder_PreviewDecode(values, count, vars, BUILDER_PREVIEW_MAX);
    if( count > 0 && n == 0 && values[0] != 0 )
        return;
    memcpy(self->preview, vars, sizeof(vars[0]) * (size_t)n);
    self->preview_count = n;
    if( !self->has_shell || !Builder_VarNameId(self->shell.var, &kind, &id) )
        return;
    for( int i = 0; i < n; i++ )
    {
        if( vars[i].kind == kind && vars[i].id == id )
        {
            ToriRSChrome_SliderSet(ui->chrome, self->w_preview, vars[i].value);
            sp_preview_label(ui, self);
        }
    }
}

static void
sp_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct shell_panel* self = opaque;
    int visible = ui->visible && ui->chrome->panels[self->p_main].visible;

    if( ui->chrome->panels[self->p_slots].visible != visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->p_slots, visible);
}

static void
sp_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct shell_panel* self = opaque;

    /* Hidden until the View menu opens it, as the builders are. */
    (void)visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->p_slots, 0);
}

static void
sp_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Builder_ShellPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services)
{
    struct shell_panel* self;
    struct EditorUiPanel panel;
    struct ToriRSChrome* chrome;
    int p;

    assert(ui);
    assert(ui->chrome);
    assert(services);

    chrome = ui->chrome;
    self = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self));
    assert(self);
    memset(self, 0, sizeof(*self));
    self->services = *services;
    self->sel_slot = -1;

    self->p_main = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 400, 80, 280, "Shell editor");
    assert(self->p_main >= 0);
    p = self->p_main;
    ToriRSChrome_PanelSetScrollable(chrome, p, 1);
    ToriRSChrome_PanelSetClosable(chrome, p, 1);
    self->w_kind = ToriRSChrome_Dropdown(chrome, p, "Record", k_subject_options, 2, 0);
    self->w_name = ToriRSChrome_TextInput(chrome, p, "Name", "");
    self->w_open = ToriRSChrome_Button(chrome, p, "Open");
    self->w_status = ToriRSChrome_Label(chrome, p, "Open a multiloc or multinpc.");
    self->w_new_name = ToriRSChrome_TextInput(chrome, p, "New name", "");
    self->w_copy = ToriRSChrome_Button(chrome, p, "Copy to new...");
    ToriRSChrome_SetTooltip(chrome, self->w_copy, "This record under New name, with a fresh id");
    self->w_var_kind = ToriRSChrome_Dropdown(chrome, p, "Var kind", k_var_options, 2, 0);
    self->w_var = ToriRSChrome_TextInput(chrome, p, "Var (varb<id>_name / varp<id>_name)", "");
    self->w_default_none = ToriRSChrome_Checkbox(chrome, p, "No default loc (multidefaultnone)", 0);
    self->w_summary = ToriRSChrome_Label(chrome, p, "");
    ToriRSChrome_Heading(chrome, p, "Selected slot");
    self->w_child = ToriRSChrome_TextInput(chrome, p, "Child", "");
    self->w_set = ToriRSChrome_Button(chrome, p, "Set child");
    self->w_hidden = ToriRSChrome_Button(chrome, p, "Hidden (none)");
    self->w_insert = ToriRSChrome_Button(chrome, p, "Insert slot before");
    self->w_append = ToriRSChrome_Button(chrome, p, "Add slot at the end");
    self->w_remove = ToriRSChrome_Button(chrome, p, "Remove slot");
    self->w_apply = ToriRSChrome_Button(chrome, p, "Apply to the record");
    ToriRSChrome_Heading(chrome, p, "Preview");
    self->w_preview = ToriRSChrome_Slider(chrome, p, "Preview value", 0, 1, 0, 1);
    self->w_preview_label = ToriRSChrome_Label(chrome, p, "");
    ToriRSChrome_SetTooltip(chrome, self->w_preview,
                            "Every view resolves shells on this var with it");

    self->p_slots = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 690, 80, 240, "Shell slots");
    assert(self->p_slots >= 0);
    ToriRSChrome_PanelSetScrollable(chrome, self->p_slots, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->p_main, 0);
    ToriRSChrome_PanelSetVisible(chrome, self->p_slots, 0);
    sp_rebuild(ui, self);

    memset(&panel, 0, sizeof(panel));
    panel.name = BUILDER_PANEL_SHELL;
    panel.title = "Shell editor";
    panel.self = self;
    panel.chrome_panel = self->p_main;
    panel.tick = sp_tick;
    panel.activate = sp_activate;
    panel.set_visible = sp_set_visible;
    panel.on_state = sp_on_state;
    panel.destroy = sp_destroy;
    Editor_UiAddPanel(ui, &panel);
}

const struct Builder_Shell*
Builder_ShellPanelShell(struct EditorUi* ui)
{
    struct shell_panel* self = Editor_UiPanelSelf(ui, BUILDER_PANEL_SHELL);
    return self && self->has_shell ? &self->shell : NULL;
}

/* ---- the scripting surface ------------------------------------------------ */

static struct builder_panel*
find_builder(
    struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);
    if( strcmp(name, BUILDER_PANEL_LOC) != 0 && strcmp(name, BUILDER_PANEL_NPC) != 0 )
        return NULL;
    return Editor_UiPanelSelf(ui, name);
}

int
Builder_PanelsSetCompile(
    struct EditorUi* ui,
    const char* sscompile,
    const char* content_root,
    const char* work_dir)
{
    int ok = 1;
    const char* names[2] = { BUILDER_PANEL_LOC, BUILDER_PANEL_NPC };

    assert(ui);
    assert(sscompile);
    assert(content_root);
    assert(work_dir);
    for( int i = 0; i < 2; i++ )
    {
        struct builder_panel* self = find_builder(ui, names[i]);
        if( !self )
            continue;
        self->ssc_ready = Builder_SscInit(&self->ssc, sscompile, content_root, work_dir);
        ok = ok && self->ssc_ready;
    }
    return ok;
}

int
Builder_PanelInspect(
    struct EditorUi* ui,
    const char* name,
    struct Builder_PanelView* out)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(out);
    memset(out, 0, sizeof(*out));
    if( !self )
        return 0;
    out->model = self->has_model ? self->model : NULL;
    out->read_only = self->read_only;
    out->issues = &self->issues;
    out->check = self->check;
    out->status = self->status;
    return 1;
}

int
Builder_PanelOpenSubject(
    struct EditorUi* ui,
    const char* name,
    const char* subject)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(subject);
    if( !self )
        return 0;
    ToriRSChrome_SetText(ui->chrome, self->w_subject, subject);
    return bp_open(ui, self, subject);
}

int
Builder_PanelSetModel(
    struct EditorUi* ui,
    const char* name,
    const struct Builder_Model* model)
{
    struct builder_panel* self = find_builder(ui, name);

    assert(model);
    if( !self || !self->has_model || model->subject != self->subject
        || strcmp(model->name, self->model->name) != 0 )
        return 0;
    *self->model = *model;
    return bp_commit(ui, self);
}

int
Builder_PanelCheck(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_check(ui, self) : 0;
}

int
Builder_PanelSaveAll(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_save(ui, self) : 0;
}

int
Builder_PanelDetach(
    struct EditorUi* ui,
    const char* name)
{
    struct builder_panel* self = find_builder(ui, name);
    return self ? bp_detach(ui, self) : 0;
}
