#include "builder_model.h"

#include "builder_text.h"
#include "editor/editor_cfg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- names --------------------------------------------------------------- */

static const char* const k_step_tokens[BUILDER_STEP_KIND_COUNT] = {
    "mes",       "if_var",      "elif_var",  "else",          "end",   "set_var", "give",
    "take",      "require_obj", "require_level", "open_main", "open_sub", "anim", "sound",
    "teleport",  "delay",       "loc_swap",  "call",          "jump",  "raw",
};

static const char* const k_step_labels[BUILDER_STEP_KIND_COUNT] = {
    "Message",       "If var",         "Else if var", "Else",         "End if",
    "Set var",       "Give obj",       "Take obj",    "Require obj",  "Require level",
    "Open interface", "Open sub-interface", "Play anim", "Play sound", "Teleport",
    "Delay",         "Loc swap",       "Call proc",   "Jump to label", "Raw RuneScript",
};

static const char* const k_node_tokens[BUILDER_NODE_KIND_COUNT] = {
    "npc", "player", "mesbox", "objbox", "choice", "branch", "step", "quest", "end",
};

static const char* const k_node_labels[BUILDER_NODE_KIND_COUNT] = {
    "NPC says", "Player says", "Message box", "Item box", "Choice",
    "Branch on var", "Step", "Quest hook", "End",
};

static const char* const k_quest_tokens[BUILDER_QUEST_ACTION_COUNT] = { "start", "complete" };

const char*
Builder_StepKindToken(enum Builder_StepKind kind)
{
    assert((int)kind >= 0 && kind < BUILDER_STEP_KIND_COUNT);
    return k_step_tokens[kind];
}

const char*
Builder_StepKindLabel(enum Builder_StepKind kind)
{
    assert((int)kind >= 0 && kind < BUILDER_STEP_KIND_COUNT);
    return k_step_labels[kind];
}

enum Builder_StepKind
Builder_StepKindParse(const char* token)
{
    assert(token);
    for( int i = 0; i < BUILDER_STEP_KIND_COUNT; i++ )
    {
        if( strcmp(token, k_step_tokens[i]) == 0 )
            return (enum Builder_StepKind)i;
    }
    return BUILDER_STEP_KIND_COUNT;
}

const char*
Builder_NodeKindToken(enum Builder_NodeKind kind)
{
    assert((int)kind >= 0 && kind < BUILDER_NODE_KIND_COUNT);
    return k_node_tokens[kind];
}

const char*
Builder_NodeKindLabel(enum Builder_NodeKind kind)
{
    assert((int)kind >= 0 && kind < BUILDER_NODE_KIND_COUNT);
    return k_node_labels[kind];
}

enum Builder_NodeKind
Builder_NodeKindParse(const char* token)
{
    assert(token);
    for( int i = 0; i < BUILDER_NODE_KIND_COUNT; i++ )
    {
        if( strcmp(token, k_node_tokens[i]) == 0 )
            return (enum Builder_NodeKind)i;
    }
    return BUILDER_NODE_KIND_COUNT;
}

int
Builder_NameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= BUILDER_SUBJECT_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

static int
node_id_valid(const char* id)
{
    size_t length = strlen(id);

    if( length == 0 || length >= BUILDER_NODE_ID_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char c = id[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

static void
copy_field(
    char* out,
    size_t out_size,
    const char* text)
{
    snprintf(out, out_size, "%s", text);
}

/* ---- the model ----------------------------------------------------------- */

void
Builder_ModelInit(
    struct Builder_Model* model,
    enum Builder_Subject subject,
    const char* name)
{
    assert(model);
    assert(name);
    assert(subject == BUILDER_SUBJECT_LOC || subject == BUILDER_SUBJECT_NPC);

    memset(model, 0, sizeof(*model));
    model->subject = subject;
    copy_field(model->name, sizeof(model->name), name);
}

static int
trigger_matches(
    const struct Builder_Trigger* trigger,
    int op,
    int approach,
    const char* use_obj)
{
    if( trigger->op != op )
        return 0;
    if( op == 0 )
        return strcmp(trigger->use_obj, use_obj) == 0;
    return trigger->approach == approach;
}

struct Builder_Trigger*
Builder_ModelTrigger(
    struct Builder_Model* model,
    int op,
    int approach,
    const char* use_obj)
{
    assert(model);
    assert(op >= 0 && op <= 5);
    assert(op != 0 || use_obj);

    for( int i = 0; i < model->trigger_count; i++ )
    {
        if( trigger_matches(&model->triggers[i], op, approach, use_obj) )
            return &model->triggers[i];
    }
    return NULL;
}

struct Builder_Trigger*
Builder_ModelTriggerAdd(
    struct Builder_Model* model,
    int op,
    int approach,
    const char* use_obj)
{
    struct Builder_Trigger* trigger = Builder_ModelTrigger(model, op, approach, use_obj);

    if( trigger )
        return trigger;
    if( model->trigger_count >= BUILDER_TRIGGERS_MAX )
        return NULL;
    trigger = &model->triggers[model->trigger_count++];
    memset(trigger, 0, sizeof(*trigger));
    trigger->op = op;
    trigger->approach = approach ? 1 : 0;
    if( op == 0 )
        copy_field(trigger->use_obj, sizeof(trigger->use_obj), use_obj);
    return trigger;
}

int
Builder_AiTriggerValid(const char* ai)
{
    size_t length;

    assert(ai);
    length = strlen(ai);
    if( length <= 3 || length >= BUILDER_AI_NAME_MAX || strncmp(ai, "ai_", 3) != 0 )
        return 0;
    for( size_t i = 3; i < length; i++ )
        if( !((ai[i] >= 'a' && ai[i] <= 'z') || (ai[i] >= '0' && ai[i] <= '9') || ai[i] == '_') )
            return 0;
    return 1;
}

struct Builder_Trigger*
Builder_ModelAiTrigger(
    struct Builder_Model* model,
    const char* ai)
{
    assert(model);
    assert(ai);
    assert(Builder_AiTriggerValid(ai));

    for( int i = 0; i < model->trigger_count; i++ )
        if( model->triggers[i].op == BUILDER_OP_AI && strcmp(model->triggers[i].ai, ai) == 0 )
            return &model->triggers[i];
    return NULL;
}

struct Builder_Trigger*
Builder_ModelAiTriggerAdd(
    struct Builder_Model* model,
    const char* ai)
{
    struct Builder_Trigger* trigger = Builder_ModelAiTrigger(model, ai);

    if( trigger )
        return trigger;
    if( model->trigger_count >= BUILDER_TRIGGERS_MAX )
        return NULL;
    trigger = &model->triggers[model->trigger_count++];
    memset(trigger, 0, sizeof(*trigger));
    trigger->op = BUILDER_OP_AI;
    copy_field(trigger->ai, sizeof(trigger->ai), ai);
    return trigger;
}

void
Builder_ModelTriggerRemove(
    struct Builder_Model* model,
    int index)
{
    assert(model);
    assert(index >= 0 && index < model->trigger_count);

    memmove(&model->triggers[index], &model->triggers[index + 1],
            sizeof(model->triggers[0]) * (size_t)(model->trigger_count - index - 1));
    model->trigger_count--;
}

int
Builder_TriggerStepAdd(
    struct Builder_Trigger* trigger,
    const struct Builder_Step* step)
{
    assert(trigger);
    assert(step);

    if( trigger->step_count >= BUILDER_STEPS_MAX )
        return -1;
    trigger->steps[trigger->step_count] = *step;
    return trigger->step_count++;
}

void
Builder_TriggerStepRemove(
    struct Builder_Trigger* trigger,
    int index)
{
    assert(trigger);
    assert(index >= 0 && index < trigger->step_count);

    memmove(&trigger->steps[index], &trigger->steps[index + 1],
            sizeof(trigger->steps[0]) * (size_t)(trigger->step_count - index - 1));
    trigger->step_count--;
}

void
Builder_TriggerStepMove(
    struct Builder_Trigger* trigger,
    int from,
    int to)
{
    struct Builder_Step moved;

    assert(trigger);
    assert(from >= 0 && from < trigger->step_count);
    assert(to >= 0 && to < trigger->step_count);

    if( from == to )
        return;
    moved = trigger->steps[from];
    if( from < to )
        memmove(&trigger->steps[from], &trigger->steps[from + 1],
                sizeof(moved) * (size_t)(to - from));
    else
        memmove(&trigger->steps[to + 1], &trigger->steps[to], sizeof(moved) * (size_t)(from - to));
    trigger->steps[to] = moved;
}

void
Builder_StepInit(
    struct Builder_Step* step,
    enum Builder_StepKind kind)
{
    assert(step);
    assert((int)kind >= 0 && kind < BUILDER_STEP_KIND_COUNT);

    memset(step, 0, sizeof(*step));
    step->kind = kind;
    switch( kind )
    {
    case BUILDER_STEP_IF_VAR:
    case BUILDER_STEP_ELSE_IF_VAR:
        copy_field(step->b, sizeof(step->b), "=");
        copy_field(step->c, sizeof(step->c), "0");
        break;
    case BUILDER_STEP_SET_VAR:
        copy_field(step->b, sizeof(step->b), "0");
        break;
    case BUILDER_STEP_GIVE:
    case BUILDER_STEP_TAKE:
    case BUILDER_STEP_REQUIRE_OBJ:
        copy_field(step->b, sizeof(step->b), "1");
        break;
    case BUILDER_STEP_REQUIRE_LEVEL:
        copy_field(step->b, sizeof(step->b), "1");
        break;
    case BUILDER_STEP_OPEN_SUB:
        copy_field(step->c, sizeof(step->c), "0");
        break;
    case BUILDER_STEP_DELAY:
        copy_field(step->a, sizeof(step->a), "1");
        break;
    case BUILDER_STEP_LOC_SWAP:
        copy_field(step->b, sizeof(step->b), "50");
        break;
    default:
        break;
    }
}

struct Builder_Node*
Builder_ModelNode(
    struct Builder_Model* model,
    const char* id)
{
    assert(model);
    assert(id);
    for( int i = 0; i < model->node_count; i++ )
    {
        if( strcmp(model->nodes[i].id, id) == 0 )
            return &model->nodes[i];
    }
    return NULL;
}

const struct Builder_Node*
Builder_ModelNodeConst(
    const struct Builder_Model* model,
    const char* id)
{
    return Builder_ModelNode((struct Builder_Model*)model, id);
}

struct Builder_Node*
Builder_ModelNodeAdd(
    struct Builder_Model* model,
    enum Builder_NodeKind kind)
{
    struct Builder_Node* node;
    char id[BUILDER_NODE_ID_MAX];
    int k = model->node_count + 1;

    assert(model);
    assert((int)kind >= 0 && kind < BUILDER_NODE_KIND_COUNT);

    if( model->node_count >= BUILDER_NODES_MAX )
        return NULL;
    do
    {
        snprintf(id, sizeof(id), "n%d", k++);
    } while( Builder_ModelNode(model, id) );

    node = &model->nodes[model->node_count++];
    memset(node, 0, sizeof(*node));
    copy_field(node->id, sizeof(node->id), id);
    node->kind = kind;
    if( kind == BUILDER_NODE_NPC || kind == BUILDER_NODE_PLAYER )
        copy_field(node->expr, sizeof(node->expr), "neutral");
    if( kind == BUILDER_NODE_CHOICE )
        node->arm_count = 2;
    if( kind == BUILDER_NODE_BRANCH )
    {
        node->arm_count = 1;
        copy_field(node->arms[0].cmp, sizeof(node->arms[0].cmp), "=");
        copy_field(node->arms[0].value, sizeof(node->arms[0].value), "0");
    }
    if( kind == BUILDER_NODE_STEP )
        Builder_StepInit(&node->step, BUILDER_STEP_MES);
    if( model->start[0] == '\0' )
        copy_field(model->start, sizeof(model->start), node->id);
    return node;
}

void
Builder_ModelNodeRemove(
    struct Builder_Model* model,
    const char* id)
{
    char gone[BUILDER_NODE_ID_MAX];
    int index = -1;

    assert(model);
    assert(id);

    copy_field(gone, sizeof(gone), id);
    for( int i = 0; i < model->node_count; i++ )
    {
        if( strcmp(model->nodes[i].id, gone) == 0 )
            index = i;
    }
    if( index < 0 )
        return;
    memmove(&model->nodes[index], &model->nodes[index + 1],
            sizeof(model->nodes[0]) * (size_t)(model->node_count - index - 1));
    model->node_count--;
    for( int i = 0; i < model->node_count; i++ )
    {
        struct Builder_Node* node = &model->nodes[i];
        if( strcmp(node->next, gone) == 0 )
            node->next[0] = '\0';
        for( int a = 0; a < node->arm_count; a++ )
        {
            if( strcmp(node->arms[a].next, gone) == 0 )
                node->arms[a].next[0] = '\0';
        }
    }
    if( strcmp(model->start, gone) == 0 )
        copy_field(model->start, sizeof(model->start), model->node_count ? model->nodes[0].id : "");
}

/* ---- the graph ------------------------------------------------------------ */

int
Builder_NodePortCount(const struct Builder_Node* node)
{
    assert(node);
    switch( node->kind )
    {
    case BUILDER_NODE_CHOICE:
        return node->arm_count;
    case BUILDER_NODE_BRANCH:
        return node->arm_count + 1;
    case BUILDER_NODE_END:
        return 0;
    default:
        return 1;
    }
}

const char*
Builder_NodePortNextConst(
    const struct Builder_Node* node,
    int port)
{
    assert(node);
    assert(port >= 0);
    assert(port < Builder_NodePortCount(node));
    if( node->kind == BUILDER_NODE_CHOICE )
        return node->arms[port].next;
    if( node->kind == BUILDER_NODE_BRANCH && port < node->arm_count )
        return node->arms[port].next;
    return node->next;
}

char*
Builder_NodePortNext(
    struct Builder_Node* node,
    int port)
{
    return (char*)Builder_NodePortNextConst(node, port);
}

int
Builder_ModelNodeIndex(
    const struct Builder_Model* model,
    const char* id)
{
    assert(model);
    assert(id);
    for( int i = 0; i < model->node_count; i++ )
    {
        if( strcmp(model->nodes[i].id, id) == 0 )
            return i;
    }
    return -1;
}

int
Builder_ModelLink(
    struct Builder_Model* model,
    const char* from,
    int port,
    const char* to)
{
    struct Builder_Node* node;

    assert(model);
    assert(from);
    assert(to);
    node = Builder_ModelNode(model, from);
    assert(node);
    if( to[0] && !Builder_ModelNode(model, to) )
        return 0;
    copy_field(Builder_NodePortNext(node, port), BUILDER_NODE_ID_MAX, to);
    return 1;
}

int
Builder_ModelReach(
    const struct Builder_Model* model,
    const char* id,
    unsigned char* mask)
{
    int queue[BUILDER_NODES_MAX];
    int head = 0;
    int tail = 0;
    int first;

    assert(model);
    assert(id);
    assert(mask);
    memset(mask, 0, BUILDER_NODES_MAX);
    first = Builder_ModelNodeIndex(model, id);
    if( first < 0 )
        return 0;
    mask[first] = 1;
    queue[tail++] = first;
    while( head < tail )
    {
        const struct Builder_Node* node = &model->nodes[queue[head++]];
        int ports = Builder_NodePortCount(node);
        for( int p = 0; p < ports; p++ )
        {
            const char* next = Builder_NodePortNextConst(node, p);
            int to = next[0] ? Builder_ModelNodeIndex(model, next) : -1;
            if( to < 0 || mask[to] )
                continue;
            mask[to] = 1;
            queue[tail++] = to;
        }
    }
    return tail;
}

int
Builder_ModelOrphans(
    const struct Builder_Model* model,
    unsigned char* mask)
{
    unsigned char reach[BUILDER_NODES_MAX];
    int count = 0;

    assert(model);
    assert(mask);
    if( model->start[0] )
        (void)Builder_ModelReach(model, model->start, reach);
    else
        memset(reach, 0, sizeof(reach));
    memset(mask, 0, BUILDER_NODES_MAX);
    for( int i = 0; i < model->node_count; i++ )
    {
        if( reach[i] )
            continue;
        mask[i] = 1;
        count++;
    }
    return count;
}

int
Builder_ModelAutoLayout(struct Builder_Model* model)
{
    int depth[BUILDER_NODES_MAX];
    int rows[BUILDER_NODES_MAX];
    int queue[BUILDER_NODES_MAX];
    int moved = 0;

    assert(model);
    for( int i = 0; i < BUILDER_NODES_MAX; i++ )
    {
        depth[i] = -1;
        rows[i] = 0;
    }
    /* Roots: the start first, then every node no earlier walk reached, in
     * model order. Each walk is breadth-first, ports in port order, so the
     * columns are depths and the rows the order a reader meets the nodes. */
    for( int pass = -1; pass < model->node_count; pass++ )
    {
        int root = pass < 0 ? (model->start[0] ? Builder_ModelNodeIndex(model, model->start) : -1) : pass;
        int head = 0;
        int tail = 0;

        if( root < 0 || depth[root] >= 0 )
            continue;
        depth[root] = 0;
        queue[tail++] = root;
        while( head < tail )
        {
            int at = queue[head++];
            struct Builder_Node* node = &model->nodes[at];
            int ports = Builder_NodePortCount(node);
            int x = BUILDER_LAYOUT_ORIGIN + depth[at] * BUILDER_LAYOUT_COL_W;
            int y = BUILDER_LAYOUT_ORIGIN + rows[depth[at]] * BUILDER_LAYOUT_ROW_H;

            rows[depth[at]]++;
            if( node->x != x || node->y != y )
                moved++;
            node->x = x;
            node->y = y;
            for( int p = 0; p < ports; p++ )
            {
                const char* next = Builder_NodePortNextConst(node, p);
                int to = next[0] ? Builder_ModelNodeIndex(model, next) : -1;
                if( to < 0 || depth[to] >= 0 )
                    continue;
                /* A walk of N nodes is at most N - 1 deep: rows[] holds it. */
                depth[to] = depth[at] + 1;
                queue[tail++] = to;
            }
        }
    }
    return moved;
}

/* ---- paths --------------------------------------------------------------- */

int
Builder_FilePath(
    enum Builder_Subject subject,
    const char* name,
    enum Builder_File file,
    char* out,
    size_t out_size)
{
    int written = 0;

    assert(name);
    assert(out);
    assert(Builder_NameValid(name));

    switch( file )
    {
    case BUILDER_FILE_MODEL:
        written = snprintf(out, out_size, "edited/%s/%s.interaction", name, name);
        break;
    case BUILDER_FILE_SCRIPT:
        written = snprintf(out, out_size, "edited/%s/scripts/%s.rs2", name, name);
        break;
    case BUILDER_FILE_CONFIG:
        written = snprintf(out, out_size, "edited/%s/configs/%s.%s", name, name,
                           subject == BUILDER_SUBJECT_NPC ? "npc" : "loc");
        break;
    case BUILDER_FILE_DBROW:
        written = snprintf(out, out_size, "edited/%s/configs/%s.dbrow", name, name);
        break;
    default:
        assert(0 && "Builder_FilePath: unknown file");
        return 0;
    }
    return written > 0 && (size_t)written < out_size;
}

int
Builder_FileKey(
    enum Builder_Subject subject,
    const char* name,
    enum Builder_File file,
    char* out,
    size_t out_size)
{
    char path[EDITOR_DOC_KEY_MAX];
    int written;

    assert(out);
    if( !Builder_FilePath(subject, name, file, path, sizeof(path)) )
        return 0;
    written = snprintf(out, out_size, "rs2:%s", path);
    return written > 0 && (size_t)written < out_size;
}

/* ---- the INI: writing ---------------------------------------------------- */

static void
put_escaped(
    struct Builder_Buf* buf,
    const char* text)
{
    for( const char* at = text; *at; at++ )
    {
        if( *at == '\\' )
            Builder_BufPuts(buf, "\\\\");
        else if( *at == '|' )
            Builder_BufPuts(buf, "\\|");
        else if( *at == '\n' )
            Builder_BufPuts(buf, "\\n");
        else if( *at == '\r' )
            continue;
        else
            Builder_BufPut(buf, at, 1);
    }
}

static void
put_kv(
    struct Builder_Buf* buf,
    const char* key,
    const char* value)
{
    Builder_BufPrintf(buf, "%s=", key);
    put_escaped(buf, value);
    Builder_BufPuts(buf, "\n");
}

static void
put_step(
    struct Builder_Buf* buf,
    const char* key,
    const struct Builder_Step* step)
{
    Builder_BufPrintf(buf, "%s=%s|", key, Builder_StepKindToken(step->kind));
    put_escaped(buf, step->a);
    Builder_BufPuts(buf, "|");
    put_escaped(buf, step->b);
    Builder_BufPuts(buf, "|");
    put_escaped(buf, step->c);
    Builder_BufPuts(buf, "|");
    put_escaped(buf, step->text);
    Builder_BufPuts(buf, "\n");
}

static const char*
door_token(enum Builder_DoorState door)
{
    return door == BUILDER_DOOR_OPENED ? "opened" : "closed";
}

static const char* const k_ladder_tokens[] = { "", "up", "down", "unqualified" };

size_t
Builder_ModelEmitIni(
    const struct Builder_Model* model,
    char* out,
    size_t out_size)
{
    struct Builder_Buf buf;
    const struct Builder_Server* server;
    size_t length;

    assert(model);
    assert(out || out_size == 0);

    Builder_BufInit(&buf);
    server = &model->server;
    Builder_BufPrintf(&buf,
                      "// ToriRSEd %s builder model. The builder reads this file back; the\n"
                      "// scripts and configs beside it are generated from it and never parsed.\n",
                      model->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc");
    Builder_BufPuts(&buf, "[interaction]\nversion=1\n");
    Builder_BufPrintf(&buf, "subject=%s\n", model->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc");
    put_kv(&buf, "name", model->name);

    if( server->category[0] || server->param_count || server->door != BUILDER_DOOR_NONE
        || server->ladder != BUILDER_LADDER_NONE || server->maplink_count )
    {
        Builder_BufPuts(&buf, "\n[server]\n");
        if( server->category[0] )
            put_kv(&buf, "category", server->category);
        for( int i = 0; i < server->param_count; i++ )
        {
            Builder_BufPuts(&buf, "param=");
            put_escaped(&buf, server->params[i].name);
            Builder_BufPuts(&buf, ",");
            put_escaped(&buf, server->params[i].value);
            Builder_BufPuts(&buf, "\n");
        }
        if( server->door != BUILDER_DOOR_NONE )
        {
            Builder_BufPrintf(&buf, "door=%s,", door_token(server->door));
            put_escaped(&buf, server->door_other);
            Builder_BufPuts(&buf, "\n");
        }
        if( server->ladder != BUILDER_LADDER_NONE )
            Builder_BufPrintf(&buf, "ladder=%s%s\n", k_ladder_tokens[server->ladder],
                              server->ladder_animated ? ",ladder" : "");
        for( int i = 0; i < server->maplink_count; i++ )
        {
            Builder_BufPuts(&buf, "maplink=");
            put_escaped(&buf, server->maplinks[i].src);
            Builder_BufPuts(&buf, "|");
            put_escaped(&buf, server->maplinks[i].dest);
            Builder_BufPrintf(&buf, "|%d\n", server->maplinks[i].dir);
        }
    }

    for( int t = 0; t < model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &model->triggers[t];

        if( trigger->op == BUILDER_OP_AI )
            Builder_BufPrintf(&buf, "\n[ai,%s]\n", trigger->ai);
        else if( trigger->op == 0 )
        {
            Builder_BufPrintf(&buf, "\n[%s,", trigger->approach ? "apu" : "opu");
            put_escaped(&buf, trigger->use_obj);
            Builder_BufPuts(&buf, "]\n");
        }
        else
            Builder_BufPrintf(&buf, "\n[%s%d]\n", trigger->approach ? "ap" : "op", trigger->op);
        if( trigger->label[0] )
            put_kv(&buf, "label", trigger->label);
        if( trigger->dialogue )
            Builder_BufPuts(&buf, "dialogue=1\n");
        for( int s = 0; s < trigger->step_count; s++ )
            put_step(&buf, "step", &trigger->steps[s]);
    }

    if( model->node_count > 0 )
    {
        Builder_BufPuts(&buf, "\n[dialogue]\n");
        put_kv(&buf, "start", model->start);
    }
    for( int n = 0; n < model->node_count; n++ )
    {
        const struct Builder_Node* node = &model->nodes[n];

        Builder_BufPrintf(&buf, "\n[node,%s]\n", node->id);
        Builder_BufPrintf(&buf, "kind=%s\n", Builder_NodeKindToken(node->kind));
        if( node->text[0] )
            put_kv(&buf, "text", node->text);
        if( node->expr[0] )
            put_kv(&buf, "expr", node->expr);
        if( node->speaker_npc[0] )
            put_kv(&buf, "speaker_npc", node->speaker_npc);
        if( node->speaker_name[0] )
            put_kv(&buf, "speaker_name", node->speaker_name);
        if( node->obj[0] )
            put_kv(&buf, "obj", node->obj);
        if( node->row[0] )
            put_kv(&buf, "row", node->row);
        if( node->value[0] )
            put_kv(&buf, "value", node->value);
        if( node->kind == BUILDER_NODE_QUEST )
            Builder_BufPrintf(&buf, "quest=%s\n", k_quest_tokens[node->quest]);
        if( node->kind == BUILDER_NODE_STEP )
            put_step(&buf, "step", &node->step);
        for( int a = 0; a < node->arm_count; a++ )
        {
            const struct Builder_Arm* arm = &node->arms[a];
            if( node->kind == BUILDER_NODE_CHOICE )
            {
                Builder_BufPuts(&buf, "option=");
                put_escaped(&buf, arm->text);
            }
            else
            {
                Builder_BufPuts(&buf, "arm=");
                put_escaped(&buf, arm->cmp);
                Builder_BufPuts(&buf, "|");
                put_escaped(&buf, arm->value);
            }
            Builder_BufPuts(&buf, "|");
            put_escaped(&buf, arm->next);
            Builder_BufPuts(&buf, "\n");
        }
        if( node->next[0] )
            put_kv(&buf, "next", node->next);
        Builder_BufPrintf(&buf, "pos=%d,%d\n", node->x, node->y);
    }

    length = buf.length;
    if( out_size > 0 )
    {
        size_t copy = length < out_size - 1 ? length : out_size - 1;
        memcpy(out, Builder_BufText(&buf), copy);
        out[copy] = '\0';
    }
    Builder_BufFree(&buf);
    return length;
}

/* ---- the INI: reading ---------------------------------------------------- */

/** Split an escaped value at unescaped '|' into up to `max` unescaped
 *  fields of `field_size` bytes each. Returns the field count, or -1 when a
 *  field does not fit or an escape is unknown. */
static int
split_fields(
    const char* value,
    size_t length,
    char (*fields)[BUILDER_TEXT_MAX],
    int max)
{
    int count = 0;
    size_t out = 0;

    if( max <= 0 )
        return -1;
    fields[0][0] = '\0';
    count = 1;
    for( size_t i = 0; i < length; i++ )
    {
        char c = value[i];
        if( c == '\\' )
        {
            if( i + 1 >= length )
                return -1;
            c = value[++i];
            if( c == 'n' )
                c = '\n';
            else if( c != '\\' && c != '|' )
                return -1;
        }
        else if( c == '|' )
        {
            if( count >= max )
                return -1;
            fields[count - 1][out] = '\0';
            fields[count][0] = '\0';
            count++;
            out = 0;
            continue;
        }
        if( out + 1 >= BUILDER_TEXT_MAX )
            return -1;
        fields[count - 1][out++] = c;
    }
    fields[count - 1][out] = '\0';
    return count;
}

static int
set_small(
    char* out,
    size_t out_size,
    const char* value)
{
    if( strlen(value) >= out_size )
        return 0;
    copy_field(out, out_size, value);
    return 1;
}

static int
parse_step(
    struct Builder_Step* step,
    const char* value,
    size_t length)
{
    char fields[5][BUILDER_TEXT_MAX];
    int count = split_fields(value, length, fields, 5);
    enum Builder_StepKind kind;

    if( count != 5 )
        return 0;
    kind = Builder_StepKindParse(fields[0]);
    if( kind == BUILDER_STEP_KIND_COUNT )
        return 0;
    memset(step, 0, sizeof(*step));
    step->kind = kind;
    return set_small(step->a, sizeof(step->a), fields[1])
           && set_small(step->b, sizeof(step->b), fields[2])
           && set_small(step->c, sizeof(step->c), fields[3])
           && set_small(step->text, sizeof(step->text), fields[4]);
}

enum ini_section
{
    SECTION_NONE = 0,
    SECTION_INTERACTION,
    SECTION_SERVER,
    SECTION_TRIGGER,
    SECTION_DIALOGUE,
    SECTION_NODE,
};

int
Builder_ModelParseIni(
    struct Builder_Model* model,
    const char* text,
    size_t length,
    char* error,
    size_t error_size)
{
    size_t at = 0;
    int line_number = 0;
    enum ini_section section = SECTION_NONE;
    struct Builder_Trigger* trigger = NULL;
    struct Builder_Node* node = NULL;
    int have_interaction = 0;
    int have_subject = 0;
    char key[BUILDER_NAME_MAX];
    char fields[4][BUILDER_TEXT_MAX];

    assert(model);
    assert(text || length == 0);
    assert(error);
    assert(error_size > 0);

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        int fail_used_ = snprintf(error, error_size, "line %d: ", line_number);                    \
        if( fail_used_ > 0 && (size_t)fail_used_ < error_size )                                    \
            snprintf(error + fail_used_, error_size - (size_t)fail_used_, __VA_ARGS__);            \
        return 0;                                                                                  \
    } while( 0 )

    error[0] = '\0';
    memset(model, 0, sizeof(*model));
    if( length == 0 )
    {
        snprintf(error, error_size, "no model (an empty file is a detached builder)");
        return 0;
    }

    while( at < length )
    {
        struct Editor_CfgLine line;
        const char* content;
        const char* value;
        size_t value_length;

        at = Editor_CfgLineParse(text, length, at, &line);
        line_number++;
        content = text + line.start;
        if( line.kind == EDITOR_CFG_LINE_BLANK || line.kind == EDITOR_CFG_LINE_COMMENT )
            continue;
        if( line.kind == EDITOR_CFG_LINE_OTHER )
            FAIL("not a [section] or key=value line");

        if( line.kind == EDITOR_CFG_LINE_HEADER )
        {
            char name[BUILDER_TEXT_MAX];

            if( !line.closed || line.part >= sizeof(name) )
                FAIL("malformed section header");
            memcpy(name, content + 1, line.part);
            name[line.part] = '\0';
            trigger = NULL;
            node = NULL;
            if( strcmp(name, "interaction") == 0 )
            {
                section = SECTION_INTERACTION;
                have_interaction = 1;
            }
            else if( strcmp(name, "server") == 0 )
                section = SECTION_SERVER;
            else if( strcmp(name, "dialogue") == 0 )
                section = SECTION_DIALOGUE;
            else if( strncmp(name, "node,", 5) == 0 )
            {
                if( !node_id_valid(name + 5) )
                    FAIL("bad node id '%s'", name + 5);
                if( Builder_ModelNode(model, name + 5) )
                    FAIL("node '%s' stated twice", name + 5);
                if( model->node_count >= BUILDER_NODES_MAX )
                    FAIL("more than %d nodes", BUILDER_NODES_MAX);
                node = &model->nodes[model->node_count++];
                memset(node, 0, sizeof(*node));
                copy_field(node->id, sizeof(node->id), name + 5);
                node->kind = BUILDER_NODE_KIND_COUNT;
                section = SECTION_NODE;
            }
            else if( strncmp(name, "ai,", 3) == 0 )
            {
                if( !Builder_AiTriggerValid(name + 3) )
                    FAIL("bad ai trigger [%s]", name);
                if( Builder_ModelAiTrigger(model, name + 3) )
                    FAIL("[%s] stated twice", name);
                trigger = Builder_ModelAiTriggerAdd(model, name + 3);
                if( !trigger )
                    FAIL("more than %d triggers", BUILDER_TRIGGERS_MAX);
                section = SECTION_TRIGGER;
            }
            else
            {
                int approach;
                int op = 0;
                const char* rest;

                if( strncmp(name, "op", 2) == 0 )
                    approach = 0;
                else if( strncmp(name, "ap", 2) == 0 )
                    approach = 1;
                else
                    FAIL("unknown section [%s]", name);
                rest = name + 2;
                if( rest[0] >= '1' && rest[0] <= '5' && rest[1] == '\0' )
                    op = rest[0] - '0';
                else if( strncmp(rest, "u,", 2) == 0 && rest[2] != '\0'
                         && (strcmp(rest + 2, "*") == 0 || Builder_NameValid(rest + 2)) )
                    op = 0;
                else
                    FAIL("unknown section [%s]", name);
                if( Builder_ModelTrigger(model, op, approach, op == 0 ? rest + 2 : "") )
                    FAIL("[%s] stated twice", name);
                trigger = Builder_ModelTriggerAdd(model, op, approach, op == 0 ? rest + 2 : "");
                if( !trigger )
                    FAIL("more than %d triggers", BUILDER_TRIGGERS_MAX);
                section = SECTION_TRIGGER;
            }
            continue;
        }

        /* FIELD */
        if( line.part == 0 || line.part >= sizeof(key) )
            FAIL("bad key");
        memcpy(key, content, line.part);
        key[line.part] = '\0';
        value = content + line.part + 1;
        value_length = line.length - line.part - 1;

        if( section == SECTION_NONE )
            FAIL("key before [interaction]");

        if( section == SECTION_INTERACTION )
        {
            if( split_fields(value, value_length, fields, 1) != 1 )
                FAIL("bad value for %s", key);
            if( strcmp(key, "version") == 0 )
            {
                if( strcmp(fields[0], "1") != 0 )
                    FAIL("unsupported model version %s", fields[0]);
            }
            else if( strcmp(key, "subject") == 0 )
            {
                if( strcmp(fields[0], "loc") == 0 )
                    model->subject = BUILDER_SUBJECT_LOC;
                else if( strcmp(fields[0], "npc") == 0 )
                    model->subject = BUILDER_SUBJECT_NPC;
                else
                    FAIL("subject must be loc or npc");
                have_subject = 1;
            }
            else if( strcmp(key, "name") == 0 )
            {
                if( !Builder_NameValid(fields[0]) )
                    FAIL("bad record name '%s'", fields[0]);
                copy_field(model->name, sizeof(model->name), fields[0]);
            }
            else
                FAIL("unknown key %s in [interaction]", key);
            continue;
        }

        if( section == SECTION_SERVER )
        {
            struct Builder_Server* server = &model->server;

            if( strcmp(key, "category") == 0 )
            {
                if( split_fields(value, value_length, fields, 1) != 1
                    || !set_small(server->category, sizeof(server->category), fields[0]) )
                    FAIL("bad category");
            }
            else if( strcmp(key, "param") == 0 || strcmp(key, "door") == 0 )
            {
                char* comma;
                if( split_fields(value, value_length, fields, 1) != 1 )
                    FAIL("bad %s", key);
                comma = strchr(fields[0], ',');
                if( !comma )
                    FAIL("%s needs <name>,<value>", key);
                *comma = '\0';
                if( strcmp(key, "param") == 0 )
                {
                    struct Builder_Param* param;
                    if( server->param_count >= BUILDER_PARAMS_MAX )
                        FAIL("more than %d params", BUILDER_PARAMS_MAX);
                    param = &server->params[server->param_count++];
                    if( !set_small(param->name, sizeof(param->name), fields[0])
                        || !set_small(param->value, sizeof(param->value), comma + 1) )
                        FAIL("param too long");
                }
                else
                {
                    if( strcmp(fields[0], "closed") == 0 )
                        server->door = BUILDER_DOOR_CLOSED;
                    else if( strcmp(fields[0], "opened") == 0 )
                        server->door = BUILDER_DOOR_OPENED;
                    else
                        FAIL("door must be closed or opened");
                    if( !set_small(server->door_other, sizeof(server->door_other), comma + 1) )
                        FAIL("door loc too long");
                }
            }
            else if( strcmp(key, "ladder") == 0 )
            {
                char* comma;
                if( split_fields(value, value_length, fields, 1) != 1 )
                    FAIL("bad ladder");
                comma = strchr(fields[0], ',');
                server->ladder_animated = 0;
                if( comma )
                {
                    if( strcmp(comma + 1, "ladder") != 0 )
                        FAIL("ladder's second part must be 'ladder'");
                    *comma = '\0';
                    server->ladder_animated = 1;
                }
                server->ladder = BUILDER_LADDER_NONE;
                for( int i = 1; i <= BUILDER_LADDER_UNQUALIFIED; i++ )
                {
                    if( strcmp(fields[0], k_ladder_tokens[i]) == 0 )
                        server->ladder = (enum Builder_LadderDir)i;
                }
                if( server->ladder == BUILDER_LADDER_NONE )
                    FAIL("ladder must be up, down or unqualified");
            }
            else if( strcmp(key, "maplink") == 0 )
            {
                struct Builder_Maplink* link;
                char* end = NULL;
                if( split_fields(value, value_length, fields, 3) != 3 )
                    FAIL("maplink needs <src>|<dest>|<dir>");
                if( server->maplink_count >= BUILDER_MAPLINKS_MAX )
                    FAIL("more than %d maplinks", BUILDER_MAPLINKS_MAX);
                link = &server->maplinks[server->maplink_count++];
                if( !set_small(link->src, sizeof(link->src), fields[0])
                    || !set_small(link->dest, sizeof(link->dest), fields[1]) )
                    FAIL("maplink coord too long");
                link->dir = (int)strtol(fields[2], &end, 10);
                if( !end || *end != '\0' || fields[2][0] == '\0' )
                    FAIL("maplink dir must be a number");
            }
            else
                FAIL("unknown key %s in [server]", key);
            continue;
        }

        if( section == SECTION_TRIGGER )
        {
            assert(trigger);
            if( strcmp(key, "step") == 0 )
            {
                struct Builder_Step step;
                if( !parse_step(&step, value, value_length) )
                    FAIL("bad step");
                if( Builder_TriggerStepAdd(trigger, &step) < 0 )
                    FAIL("more than %d steps", BUILDER_STEPS_MAX);
            }
            else if( strcmp(key, "label") == 0 )
            {
                if( split_fields(value, value_length, fields, 1) != 1
                    || !set_small(trigger->label, sizeof(trigger->label), fields[0]) )
                    FAIL("bad label");
            }
            else if( strcmp(key, "dialogue") == 0 )
            {
                if( value_length != 1 || (value[0] != '0' && value[0] != '1') )
                    FAIL("dialogue must be 0 or 1");
                trigger->dialogue = value[0] == '1';
            }
            else
                FAIL("unknown key %s in a trigger", key);
            continue;
        }

        if( section == SECTION_DIALOGUE )
        {
            if( strcmp(key, "start") != 0 )
                FAIL("unknown key %s in [dialogue]", key);
            if( split_fields(value, value_length, fields, 1) != 1 || !node_id_valid(fields[0]) )
                FAIL("bad start node");
            copy_field(model->start, sizeof(model->start), fields[0]);
            continue;
        }

        assert(section == SECTION_NODE);
        assert(node);
        if( strcmp(key, "kind") == 0 )
        {
            if( split_fields(value, value_length, fields, 1) != 1 )
                FAIL("bad kind");
            node->kind = Builder_NodeKindParse(fields[0]);
            if( node->kind == BUILDER_NODE_KIND_COUNT )
                FAIL("unknown node kind '%s'", fields[0]);
        }
        else if( strcmp(key, "step") == 0 )
        {
            if( !parse_step(&node->step, value, value_length) )
                FAIL("bad step");
        }
        else if( strcmp(key, "option") == 0 || strcmp(key, "arm") == 0 )
        {
            int want = strcmp(key, "option") == 0 ? 2 : 3;
            struct Builder_Arm* arm;
            if( node->arm_count >= BUILDER_ARMS_MAX )
                FAIL("more than %d %ss", BUILDER_ARMS_MAX, key);
            if( split_fields(value, value_length, fields, want) != want )
                FAIL("bad %s", key);
            arm = &node->arms[node->arm_count++];
            memset(arm, 0, sizeof(*arm));
            if( want == 2 )
            {
                if( !set_small(arm->text, sizeof(arm->text), fields[0]) )
                    FAIL("option text too long");
            }
            else if( !set_small(arm->cmp, sizeof(arm->cmp), fields[0])
                     || !set_small(arm->value, sizeof(arm->value), fields[1]) )
                FAIL("bad arm");
            if( fields[want - 1][0] && !node_id_valid(fields[want - 1]) )
                FAIL("bad port target '%s'", fields[want - 1]);
            copy_field(arm->next, sizeof(arm->next), fields[want - 1]);
        }
        else if( strcmp(key, "pos") == 0 )
        {
            char* end = NULL;
            if( split_fields(value, value_length, fields, 1) != 1 )
                FAIL("bad pos");
            node->x = (int)strtol(fields[0], &end, 10);
            if( !end || *end != ',' )
                FAIL("pos must be x,y");
            node->y = (int)strtol(end + 1, &end, 10);
            if( !end || *end != '\0' )
                FAIL("pos must be x,y");
        }
        else if( strcmp(key, "quest") == 0 )
        {
            int found = 0;
            if( split_fields(value, value_length, fields, 1) != 1 )
                FAIL("bad quest");
            for( int q = 0; q < BUILDER_QUEST_ACTION_COUNT; q++ )
            {
                if( strcmp(fields[0], k_quest_tokens[q]) == 0 )
                {
                    node->quest = (enum Builder_QuestAction)q;
                    found = 1;
                }
            }
            if( !found )
                FAIL("quest must be start or complete");
        }
        else
        {
            char* target = NULL;
            size_t target_size = 0;

            if( strcmp(key, "text") == 0 )
                target = node->text, target_size = sizeof(node->text);
            else if( strcmp(key, "expr") == 0 )
                target = node->expr, target_size = sizeof(node->expr);
            else if( strcmp(key, "speaker_npc") == 0 )
                target = node->speaker_npc, target_size = sizeof(node->speaker_npc);
            else if( strcmp(key, "speaker_name") == 0 )
                target = node->speaker_name, target_size = sizeof(node->speaker_name);
            else if( strcmp(key, "obj") == 0 )
                target = node->obj, target_size = sizeof(node->obj);
            else if( strcmp(key, "row") == 0 )
                target = node->row, target_size = sizeof(node->row);
            else if( strcmp(key, "value") == 0 )
                target = node->value, target_size = sizeof(node->value);
            else if( strcmp(key, "next") == 0 )
                target = node->next, target_size = sizeof(node->next);
            else
                FAIL("unknown key %s in a node", key);
            if( split_fields(value, value_length, fields, 1) != 1
                || !set_small(target, target_size, fields[0]) )
                FAIL("bad %s", key);
            if( target == node->next && !node_id_valid(node->next) )
                FAIL("bad next node '%s'", node->next);
        }
    }

    if( !have_interaction || !have_subject || model->name[0] == '\0' )
    {
        snprintf(error, error_size, "missing [interaction] subject= or name=");
        return 0;
    }
    for( int n = 0; n < model->node_count; n++ )
    {
        if( model->nodes[n].kind == BUILDER_NODE_KIND_COUNT )
        {
            snprintf(error, error_size, "node %s has no kind", model->nodes[n].id);
            return 0;
        }
    }
    if( model->node_count > 0 && !Builder_ModelNode(model, model->start) )
    {
        snprintf(error, error_size, "[dialogue] start names no node");
        return 0;
    }
#undef FAIL
    return 1;
}
