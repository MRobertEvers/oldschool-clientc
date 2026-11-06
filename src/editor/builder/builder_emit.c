#include "builder_emit.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- issues -------------------------------------------------------------- */

void
Builder_IssuesClear(struct Builder_Issues* issues)
{
    assert(issues);
    memset(issues, 0, sizeof(*issues));
}

void
Builder_IssueAdd(
    struct Builder_Issues* issues,
    enum Builder_IssueSeverity severity,
    const char* where,
    const char* format,
    ...)
{
    struct Builder_Issue* issue;
    va_list args;

    assert(issues);
    assert(where);
    assert(format);

    if( severity == BUILDER_ISSUE_ERROR )
        issues->errors++;
    if( issues->count >= BUILDER_ISSUES_MAX )
    {
        issues->dropped++;
        return;
    }
    issue = &issues->items[issues->count++];
    issue->severity = severity;
    snprintf(issue->where, sizeof(issue->where), "%s", where);
    va_start(args, format);
    vsnprintf(issue->message, sizeof(issue->message), format, args);
    va_end(args);
}

/* ---- lint ---------------------------------------------------------------- */

int
Builder_LintText(
    const char* text,
    const char* where,
    struct Builder_Issues* issues)
{
    int depth = 0;
    int errors = 0;

    assert(text);
    assert(where);
    assert(issues);

    for( const unsigned char* at = (const unsigned char*)text; *at; at++ )
    {
        if( (at[0] == 0xC2 && at[1] == 0xA0) || at[0] == 0xA0 )
        {
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "U+00A0 is how a recorded player's display name arrives; write "
                             "<displayname> (or {player}) instead");
            errors++;
            break;
        }
    }
    for( const char* at = text; *at; at++ )
    {
        if( *at == '<' )
            depth++;
        else if( *at == '>' && depth > 0 )
            depth--;
        else if( *at == '"' && depth == 0 )
        {
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "a '\"' outside <...> would end the string (RuneScript strings have "
                             "no escapes)");
            errors++;
            break;
        }
        else if( *at == '\n' || *at == '\r' )
        {
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "a line break cannot be in a string; the chat box pages the text");
            errors++;
            break;
        }
    }
    if( depth > 0 )
    {
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "'<' without its '>'");
        errors++;
    }
    return errors == 0;
}

/* ---- operands ------------------------------------------------------------ */

/** A name, number, coord or ^constant: what may stand bare in an argument
 *  list. Anything else could close the call and write code. */
static int
operand_valid(const char* text)
{
    if( text[0] == '\0' )
        return 0;
    for( const char* at = text; *at; at++ )
    {
        char c = *at;
        if( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '_' || c == ':' || c == '^' || c == '-' || c == '$' || c == '%') )
            return 0;
    }
    return 1;
}

static int
identifier_valid(const char* text)
{
    if( text[0] == '\0' )
        return 0;
    for( const char* at = text; *at; at++ )
    {
        char c = *at;
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

static int
cmp_valid(const char* cmp)
{
    static const char* const k_cmps[] = { "=", "!", "<", ">", "<=", ">=" };
    for( size_t i = 0; i < sizeof(k_cmps) / sizeof(k_cmps[0]); i++ )
    {
        if( strcmp(cmp, k_cmps[i]) == 0 )
            return 1;
    }
    return 0;
}

/** Call arguments: one line, no statement or block punctuation outside a
 *  string. */
static int
args_valid(const char* text)
{
    int quoted = 0;
    for( const char* at = text; *at; at++ )
    {
        if( *at == '"' )
            quoted = !quoted;
        else if( *at == '\n' || *at == '\r' )
            return 0;
        else if( !quoted && (*at == ';' || *at == '{' || *at == '}' || *at == '[' || *at == ']') )
            return 0;
    }
    return !quoted;
}

/* ---- validation ---------------------------------------------------------- */

static void
check_operand(
    struct Builder_Issues* issues,
    const char* where,
    const char* what,
    const char* value)
{
    if( !operand_valid(value) )
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "%s '%s' is not a name or number",
                         what, value);
}

static void
check_identifier(
    struct Builder_Issues* issues,
    const char* where,
    const char* what,
    const char* value)
{
    if( !identifier_valid(value) )
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "%s '%s' is not a name", what, value);
}

static void
check_step(
    const struct Builder_Step* step,
    const char* where,
    int in_dialogue,
    struct Builder_Issues* issues)
{
    switch( step->kind )
    {
    case BUILDER_STEP_MES:
        Builder_LintText(step->text, where, issues);
        break;
    case BUILDER_STEP_IF_VAR:
    case BUILDER_STEP_ELSE_IF_VAR:
        if( in_dialogue )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "a dialogue branches with a Branch node, not an if step");
        check_identifier(issues, where, "var", step->a);
        if( !cmp_valid(step->b) )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "comparison '%s' is not one of = ! < > <= >=",
                             step->b);
        check_operand(issues, where, "value", step->c);
        break;
    case BUILDER_STEP_ELSE:
    case BUILDER_STEP_END:
        if( in_dialogue )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "a dialogue branches with a Branch node, not an if step");
        break;
    case BUILDER_STEP_SET_VAR:
        check_identifier(issues, where, "var", step->a);
        check_operand(issues, where, "value", step->b);
        break;
    case BUILDER_STEP_GIVE:
    case BUILDER_STEP_TAKE:
        check_identifier(issues, where, "obj", step->a);
        check_operand(issues, where, "count", step->b);
        break;
    case BUILDER_STEP_REQUIRE_OBJ:
        check_identifier(issues, where, "obj", step->a);
        check_operand(issues, where, "count", step->b);
        Builder_LintText(step->text, where, issues);
        break;
    case BUILDER_STEP_REQUIRE_LEVEL:
        check_identifier(issues, where, "stat", step->a);
        check_operand(issues, where, "level", step->b);
        Builder_LintText(step->text, where, issues);
        break;
    case BUILDER_STEP_OPEN_MAIN:
        check_identifier(issues, where, "interface", step->a);
        break;
    case BUILDER_STEP_OPEN_SUB:
        check_operand(issues, where, "target", step->a);
        check_identifier(issues, where, "interface", step->b);
        check_operand(issues, where, "mode", step->c);
        break;
    case BUILDER_STEP_ANIM:
        check_identifier(issues, where, "seq", step->a);
        break;
    case BUILDER_STEP_SOUND:
        check_identifier(issues, where, "synth", step->a);
        break;
    case BUILDER_STEP_TELEPORT:
        check_operand(issues, where, "coord", step->a);
        break;
    case BUILDER_STEP_DELAY:
        check_operand(issues, where, "ticks", step->a);
        break;
    case BUILDER_STEP_LOC_SWAP:
        check_identifier(issues, where, "loc", step->a);
        check_operand(issues, where, "ticks", step->b);
        break;
    case BUILDER_STEP_CALL:
        check_identifier(issues, where, "proc", step->a);
        if( !args_valid(step->b) )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "arguments must be one line with no ; { } [ ] outside a string");
        break;
    case BUILDER_STEP_JUMP:
        check_identifier(issues, where, "label", step->a);
        break;
    case BUILDER_STEP_RAW:
        break;
    default:
        assert(0 && "check_step: unknown step kind");
        break;
    }
}

static void
check_steps(
    const struct Builder_Trigger* trigger,
    const char* trigger_where,
    struct Builder_Issues* issues)
{
    int depth = 0;
    int seen_else[BUILDER_STEPS_MAX];
    char where[64];

    for( int s = 0; s < trigger->step_count; s++ )
    {
        const struct Builder_Step* step = &trigger->steps[s];

        snprintf(where, sizeof(where), "%s step %d", trigger_where, s + 1);
        check_step(step, where, 0, issues);
        if( step->kind == BUILDER_STEP_IF_VAR )
            seen_else[depth++] = 0;
        else if( step->kind == BUILDER_STEP_ELSE_IF_VAR || step->kind == BUILDER_STEP_ELSE )
        {
            if( depth == 0 )
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "%s with no open If var",
                                 Builder_StepKindLabel(step->kind));
            else if( seen_else[depth - 1] )
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "%s after Else",
                                 Builder_StepKindLabel(step->kind));
            else if( step->kind == BUILDER_STEP_ELSE )
                seen_else[depth - 1] = 1;
        }
        else if( step->kind == BUILDER_STEP_END )
        {
            if( depth == 0 )
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "End if with no open If var");
            else
                depth--;
        }
    }
    if( depth > 0 )
        Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, trigger_where,
                         "%d If var left open; the script closes it at the end", depth);
}

static void
trigger_where(
    const struct Builder_Trigger* trigger,
    char* out,
    size_t out_size)
{
    if( trigger->op == BUILDER_OP_AI )
        snprintf(out, out_size, "%s", trigger->ai);
    else if( trigger->op == 0 )
        snprintf(out, out_size, "%su %s", trigger->approach ? "ap" : "op", trigger->use_obj);
    else
        snprintf(out, out_size, "%s%d", trigger->approach ? "ap" : "op", trigger->op);
}

static void
check_port(
    const struct Builder_Model* model,
    const char* where,
    const char* next,
    struct Builder_Issues* issues)
{
    if( next[0] && !Builder_ModelNodeConst(model, next) )
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "leads to node '%s', which does not exist",
                         next);
}

static void
check_node(
    const struct Builder_Model* model,
    const struct Builder_Node* node,
    struct Builder_Issues* issues)
{
    char where[64];

    snprintf(where, sizeof(where), "node %s", node->id);
    switch( node->kind )
    {
    case BUILDER_NODE_NPC:
        if( model->subject == BUILDER_SUBJECT_LOC && node->speaker_npc[0] == '\0' )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where,
                             "a loc has no npc to speak: give the line a speaker");
        if( node->speaker_npc[0] )
        {
            check_identifier(issues, where, "speaker npc", node->speaker_npc);
            Builder_LintText(node->speaker_name, where, issues);
        }
        /* fall through */
    case BUILDER_NODE_PLAYER:
        if( node->expr[0] )
            check_identifier(issues, where, "expression", node->expr);
        /* fall through */
    case BUILDER_NODE_MESBOX:
        Builder_LintText(node->text, where, issues);
        if( node->text[0] == '\0' )
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, where, "an empty page");
        break;
    case BUILDER_NODE_OBJBOX:
        check_identifier(issues, where, "obj", node->obj);
        Builder_LintText(node->text, where, issues);
        break;
    case BUILDER_NODE_CHOICE:
        if( node->arm_count < 2 || node->arm_count > 5 )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "a choice has 2 to 5 options, not %d",
                             node->arm_count);
        Builder_LintText(node->text, where, issues);
        for( int a = 0; a < node->arm_count; a++ )
        {
            char arm_where[64];
            snprintf(arm_where, sizeof(arm_where), "node %s option %d", node->id, a + 1);
            Builder_LintText(node->arms[a].text, arm_where, issues);
            check_port(model, arm_where, node->arms[a].next, issues);
        }
        if( node->next[0] )
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, where,
                             "a choice continues through its options; its next is ignored");
        return;
    case BUILDER_NODE_BRANCH:
        check_identifier(issues, where, "var", node->obj);
        if( node->arm_count < 1 || node->arm_count > BUILDER_ARMS_MAX )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "a branch has 1 to %d arms",
                             BUILDER_ARMS_MAX);
        for( int a = 0; a < node->arm_count; a++ )
        {
            char arm_where[64];
            snprintf(arm_where, sizeof(arm_where), "node %s arm %d", node->id, a + 1);
            if( !cmp_valid(node->arms[a].cmp) )
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, arm_where,
                                 "comparison '%s' is not one of = ! < > <= >=", node->arms[a].cmp);
            check_operand(issues, arm_where, "value", node->arms[a].value);
            check_port(model, arm_where, node->arms[a].next, issues);
        }
        break;
    case BUILDER_NODE_STEP:
        check_step(&node->step, where, 1, issues);
        break;
    case BUILDER_NODE_QUEST:
        if( node->quest == BUILDER_QUEST_COMPLETE )
        {
            check_identifier(issues, where, "quest row", node->row);
            check_identifier(issues, where, "reward icon", node->obj);
            Builder_LintText(node->text, where, issues);
        }
        else
        {
            check_identifier(issues, where, "progress var", node->obj);
            check_operand(issues, where, "first stage", node->value);
        }
        break;
    case BUILDER_NODE_END:
        if( node->next[0] )
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, where, "an End has no next; it is ignored");
        return;
    default:
        assert(0 && "check_node: unknown node kind");
        return;
    }
    check_port(model, where, node->next, issues);
}

int
Builder_Validate(
    const struct Builder_Model* model,
    struct Builder_Issues* issues)
{
    int errors_before;
    int dialogue_ops = 0;
    char where[64];

    assert(model);
    assert(issues);

    errors_before = issues->errors;
    if( !Builder_NameValid(model->name) )
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "subject", "'%s' is not a record name",
                         model->name);

    for( int t = 0; t < model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &model->triggers[t];
        trigger_where(trigger, where, sizeof(where));
        if( trigger->op == 0 && strcmp(trigger->use_obj, "*") != 0 )
            check_identifier(issues, where, "held obj", trigger->use_obj);
        if( trigger->op == BUILDER_OP_AI && model->subject != BUILDER_SUBJECT_NPC )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, where, "an ai trigger is an npc's; a loc has none");
        if( trigger->dialogue )
        {
            dialogue_ops++;
            if( trigger->step_count > 0 )
                Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, where,
                                 "runs the dialogue; its %d step(s) are not emitted",
                                 trigger->step_count);
        }
        else
            check_steps(trigger, where, issues);
    }

    if( dialogue_ops > 0 && model->node_count == 0 )
        Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "dialogue", "an op runs the dialogue, which has no nodes");
    if( model->node_count > 0 && dialogue_ops == 0 )
        Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, "dialogue",
                         "no op runs the dialogue; it is emitted as a label only");
    for( int n = 0; n < model->node_count; n++ )
        check_node(model, &model->nodes[n], issues);

    {
        const struct Builder_Server* server = &model->server;
        if( server->category[0] )
            check_identifier(issues, "server", "category", server->category);
        for( int p = 0; p < server->param_count; p++ )
        {
            check_identifier(issues, "server", "param", server->params[p].name);
            if( !args_valid(server->params[p].value) || server->params[p].value[0] == '\0' )
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "server", "param %s has no value",
                                 server->params[p].name);
        }
        if( model->subject == BUILDER_SUBJECT_NPC
            && (server->door != BUILDER_DOOR_NONE || server->ladder != BUILDER_LADDER_NONE
                || server->maplink_count) )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "server", "doors and ladders are locs");
        if( server->door != BUILDER_DOOR_NONE && server->ladder != BUILDER_LADDER_NONE )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "server",
                             "a loc is a door or a ladder, not both (one category)");
        if( (server->door != BUILDER_DOOR_NONE || server->ladder != BUILDER_LADDER_NONE)
            && server->category[0] )
            Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "server",
                             "the door/ladder preset sets the category; clear category=%s",
                             server->category);
        if( server->door != BUILDER_DOOR_NONE )
            check_identifier(issues, "server", "door's other loc", server->door_other);
        for( int m = 0; m < server->maplink_count; m++ )
        {
            check_operand(issues, "server", "maplink src", server->maplinks[m].src);
            check_operand(issues, "server", "maplink dest", server->maplinks[m].dest);
        }
        if( server->maplink_count && server->ladder == BUILDER_LADDER_NONE )
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, "server",
                             "maplink rows without the ladder preset: nothing reads them for this loc");
        if( server->door != BUILDER_DOOR_NONE || server->ladder != BUILDER_LADDER_NONE )
        {
            for( int t = 0; t < model->trigger_count; t++ )
            {
                if( model->triggers[t].op == 1 && !model->triggers[t].approach )
                    Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, "op1",
                                     "the %s preset handles op1 by category; a [oploc1] here "
                                     "takes it over",
                                     server->door != BUILDER_DOOR_NONE ? "door" : "ladder");
            }
        }
    }
    return issues->errors - errors_before;
}

/* ---- the emitter --------------------------------------------------------- */

struct emitter
{
    const struct Builder_Model* model;
    struct Builder_Buf* out;
    struct Builder_Issues* issues;
    char prefix[BUILDER_NAME_MAX + 8];
    int indeg[BUILDER_NODES_MAX];
    int reachable[BUILDER_NODES_MAX];
    int nesting;
};

static const char*
subject_word(enum Builder_Subject subject)
{
    return subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";
}

static void
put_header(
    const struct Builder_Model* model,
    struct Builder_Buf* out)
{
    char path[EDITOR_DOC_KEY_MAX];

    Builder_FilePath(model->subject, model->name, BUILDER_FILE_MODEL, path, sizeof(path));
    Builder_BufPrintf(out, BUILDER_HEADER_PREFIX "%s builder from %s.\n", subject_word(model->subject),
                      path);
    Builder_BufPuts(out, "// The builder regenerates this file from that model: edit the model, not\n"
                         "// this file. Detach in the builder to keep this file as hand-authored.\n");
}

static void
indent(
    struct Builder_Buf* out,
    int depth)
{
    for( int i = 0; i < depth; i++ )
        Builder_BufPuts(out, "    ");
}

/** A literal's text: `{player}` becomes the tree's display-name helper. */
static void
put_text(
    struct Builder_Buf* out,
    const char* text)
{
    static const char k_token[] = "{player}";
    const char* at = text;

    Builder_BufPuts(out, "\"");
    for( ;; )
    {
        const char* hit = strstr(at, k_token);
        if( !hit )
            break;
        Builder_BufPut(out, at, (size_t)(hit - at));
        Builder_BufPuts(out, "<displayname>");
        at = hit + sizeof(k_token) - 1;
    }
    Builder_BufPuts(out, at);
    Builder_BufPuts(out, "\"");
}

static int
node_index(
    const struct Builder_Model* model,
    const char* id)
{
    for( int i = 0; i < model->node_count; i++ )
    {
        if( strcmp(model->nodes[i].id, id) == 0 )
            return i;
    }
    return -1;
}

static void emit_chain(struct emitter* e, const char* id, int depth, int root);

static void
emit_step(
    struct emitter* e,
    const struct Builder_Step* step,
    int* depth)
{
    struct Builder_Buf* out = e->out;

    switch( step->kind )
    {
    case BUILDER_STEP_MES:
        indent(out, *depth);
        Builder_BufPuts(out, "mes(");
        put_text(out, step->text);
        Builder_BufPuts(out, ");\n");
        break;
    case BUILDER_STEP_IF_VAR:
        indent(out, *depth);
        Builder_BufPrintf(out, "if (%%%s %s %s) {\n", step->a, step->b, step->c);
        (*depth)++;
        break;
    case BUILDER_STEP_ELSE_IF_VAR:
        assert(*depth > 0);
        (*depth)--;
        indent(out, *depth);
        Builder_BufPrintf(out, "} else if (%%%s %s %s) {\n", step->a, step->b, step->c);
        (*depth)++;
        break;
    case BUILDER_STEP_ELSE:
        assert(*depth > 0);
        (*depth)--;
        indent(out, *depth);
        Builder_BufPuts(out, "} else {\n");
        (*depth)++;
        break;
    case BUILDER_STEP_END:
        assert(*depth > 0);
        (*depth)--;
        indent(out, *depth);
        Builder_BufPuts(out, "}\n");
        break;
    case BUILDER_STEP_SET_VAR:
        indent(out, *depth);
        Builder_BufPrintf(out, "%%%s = %s;\n", step->a, step->b);
        break;
    case BUILDER_STEP_GIVE:
        indent(out, *depth);
        Builder_BufPrintf(out, "inv_add(inv, %s, %s);\n", step->a, step->b);
        break;
    case BUILDER_STEP_TAKE:
        indent(out, *depth);
        Builder_BufPrintf(out, "inv_del(inv, %s, %s);\n", step->a, step->b);
        break;
    case BUILDER_STEP_REQUIRE_OBJ:
    case BUILDER_STEP_REQUIRE_LEVEL:
        indent(out, *depth);
        if( step->kind == BUILDER_STEP_REQUIRE_OBJ )
            Builder_BufPrintf(out, "if (inv_total(inv, %s) < %s) {\n", step->a, step->b);
        else
            Builder_BufPrintf(out, "if (stat(%s) < %s) {\n", step->a, step->b);
        if( step->text[0] )
        {
            indent(out, *depth + 1);
            Builder_BufPuts(out, "mes(");
            put_text(out, step->text);
            Builder_BufPuts(out, ");\n");
        }
        indent(out, *depth + 1);
        Builder_BufPuts(out, "return;\n");
        indent(out, *depth);
        Builder_BufPuts(out, "}\n");
        break;
    case BUILDER_STEP_OPEN_MAIN:
        indent(out, *depth);
        Builder_BufPrintf(out, "if_openmain(%s);\n", step->a);
        break;
    case BUILDER_STEP_OPEN_SUB:
        indent(out, *depth);
        Builder_BufPrintf(out, "if_opensub(%s, %s, %s);\n", step->a, step->b, step->c);
        break;
    case BUILDER_STEP_ANIM:
        indent(out, *depth);
        Builder_BufPrintf(out, "anim(%s, 0);\n", step->a);
        break;
    case BUILDER_STEP_SOUND:
        indent(out, *depth);
        Builder_BufPrintf(out, "sound_synth(%s, 1, 0);\n", step->a);
        break;
    case BUILDER_STEP_TELEPORT:
        indent(out, *depth);
        Builder_BufPrintf(out, "p_telejump(%s);\n", step->a);
        break;
    case BUILDER_STEP_DELAY:
        indent(out, *depth);
        Builder_BufPrintf(out, "p_delay(%s);\n", step->a);
        break;
    case BUILDER_STEP_LOC_SWAP:
        indent(out, *depth);
        Builder_BufPrintf(out, "loc_change(%s, %s);\n", step->a, step->b);
        break;
    case BUILDER_STEP_CALL:
        indent(out, *depth);
        if( step->b[0] )
            Builder_BufPrintf(out, "~%s(%s);\n", step->a, step->b);
        else
            Builder_BufPrintf(out, "~%s;\n", step->a);
        break;
    case BUILDER_STEP_JUMP:
        indent(out, *depth);
        Builder_BufPrintf(out, "@%s;\n", step->a);
        break;
    case BUILDER_STEP_RAW:
    {
        const char* at = step->text;
        while( *at )
        {
            const char* end = strchr(at, '\n');
            size_t length = end ? (size_t)(end - at) : strlen(at);
            if( length > 0 )
            {
                indent(out, *depth);
                Builder_BufPut(out, at, length);
            }
            Builder_BufPuts(out, "\n");
            if( !end )
                break;
            at = end + 1;
        }
        break;
    }
    default:
        assert(0 && "emit_step: unknown step kind");
        break;
    }
}

static void
emit_steps(
    struct emitter* e,
    const struct Builder_Trigger* trigger)
{
    int depth = 0;

    for( int s = 0; s < trigger->step_count; s++ )
    {
        const struct Builder_Step* step = &trigger->steps[s];
        /* An Else/End with nothing open would close the trigger's own
         * block; validation reports it, the emitter skips it. */
        if( (step->kind == BUILDER_STEP_ELSE || step->kind == BUILDER_STEP_ELSE_IF_VAR
             || step->kind == BUILDER_STEP_END)
            && depth == 0 )
            continue;
        emit_step(e, step, &depth);
    }
    while( depth > 0 )
    {
        depth--;
        indent(e->out, depth);
        Builder_BufPuts(e->out, "}\n");
    }
}

static void
label_name(
    const struct emitter* e,
    const char* id,
    char* out,
    size_t out_size)
{
    if( strcmp(id, e->model->start) == 0 )
        snprintf(out, out_size, "%s_talk", e->prefix);
    else
        snprintf(out, out_size, "%s_%s", e->prefix, id);
}

static int
node_labeled(
    const struct emitter* e,
    int index)
{
    return strcmp(e->model->nodes[index].id, e->model->start) == 0 || e->indeg[index] >= 2;
}

static void
emit_node(
    struct emitter* e,
    const struct Builder_Node* node,
    int depth)
{
    struct Builder_Buf* out = e->out;

    switch( node->kind )
    {
    case BUILDER_NODE_NPC:
        indent(out, depth);
        if( node->speaker_npc[0] )
        {
            Builder_BufPuts(out, node->expr[0] ? "~chatnpc_specific_anim(" : "~chatnpc_specific(");
            put_text(out, node->speaker_name);
            Builder_BufPrintf(out, ", %s, ", node->speaker_npc);
            if( node->expr[0] )
                Builder_BufPrintf(out, "^chat_%s, ", node->expr);
        }
        else if( node->expr[0] )
            Builder_BufPrintf(out, "~chatnpc_anim(^chat_%s, ", node->expr);
        else
            Builder_BufPuts(out, "~chatnpc(");
        put_text(out, node->text);
        Builder_BufPuts(out, ");\n");
        break;
    case BUILDER_NODE_PLAYER:
        indent(out, depth);
        if( node->expr[0] )
            Builder_BufPrintf(out, "~chatplayer_anim(^chat_%s, ", node->expr);
        else
            Builder_BufPuts(out, "~chatplayer(");
        put_text(out, node->text);
        Builder_BufPuts(out, ");\n");
        break;
    case BUILDER_NODE_MESBOX:
        indent(out, depth);
        Builder_BufPuts(out, "~mesbox(");
        put_text(out, node->text);
        Builder_BufPuts(out, ");\n");
        break;
    case BUILDER_NODE_OBJBOX:
        indent(out, depth);
        Builder_BufPrintf(out, "~objbox(%s, ", node->obj);
        put_text(out, node->text);
        Builder_BufPuts(out, ");\n");
        break;
    case BUILDER_NODE_CHOICE:
    {
        int count = node->arm_count < 2 ? 2 : node->arm_count > 5 ? 5 : node->arm_count;
        int first = 1;

        indent(out, depth);
        Builder_BufPrintf(out, "def_int $choice_%s = ~p_choice%d%s(", node->id, count,
                          node->text[0] ? "_header" : "");
        for( int a = 0; a < count; a++ )
        {
            if( a > 0 )
                Builder_BufPuts(out, ", ");
            put_text(out, a < node->arm_count ? node->arms[a].text : "");
            Builder_BufPrintf(out, ", %d", a + 1);
        }
        if( node->text[0] )
        {
            Builder_BufPuts(out, ", ");
            put_text(out, node->text);
        }
        Builder_BufPuts(out, ");\n");
        for( int a = 0; a < count && a < node->arm_count; a++ )
        {
            if( node->arms[a].next[0] == '\0' )
                continue;
            indent(out, depth);
            Builder_BufPrintf(out, "%sif ($choice_%s = %d) {\n", first ? "" : "} else ", node->id,
                              a + 1);
            emit_chain(e, node->arms[a].next, depth + 1, 0);
            first = 0;
        }
        if( !first )
        {
            indent(out, depth);
            Builder_BufPuts(out, "}\n");
        }
        break;
    }
    case BUILDER_NODE_BRANCH:
    {
        int first = 1;

        for( int a = 0; a < node->arm_count; a++ )
        {
            const struct Builder_Arm* arm = &node->arms[a];
            indent(out, depth);
            Builder_BufPrintf(out, "%sif (%%%s %s %s) {\n", first ? "" : "} else ", node->obj,
                              arm->cmp, arm->value);
            emit_chain(e, arm->next, depth + 1, 0);
            first = 0;
        }
        if( node->next[0] )
        {
            if( first )
                emit_chain(e, node->next, depth, 0);
            else
            {
                indent(out, depth);
                Builder_BufPuts(out, "} else {\n");
                emit_chain(e, node->next, depth + 1, 0);
            }
        }
        if( !first )
        {
            indent(out, depth);
            Builder_BufPuts(out, "}\n");
        }
        break;
    }
    case BUILDER_NODE_STEP:
    {
        int step_depth = depth;
        /* A block step would open or close a brace the graph does not own;
         * validation refuses them in a dialogue, the emitter skips them. */
        if( node->step.kind == BUILDER_STEP_IF_VAR || node->step.kind == BUILDER_STEP_ELSE_IF_VAR
            || node->step.kind == BUILDER_STEP_ELSE || node->step.kind == BUILDER_STEP_END )
            break;
        emit_step(e, &node->step, &step_depth);
        break;
    }
    case BUILDER_NODE_QUEST:
        indent(out, depth);
        if( node->quest == BUILDER_QUEST_COMPLETE )
        {
            Builder_BufPrintf(out, "~quest_complete_rewards(%s, ", node->row);
            put_text(out, node->text);
            Builder_BufPrintf(out, ", %s);\n", node->obj);
        }
        else
            Builder_BufPrintf(out, "%%%s = %s;\n", node->obj, node->value);
        break;
    case BUILDER_NODE_END:
        indent(out, depth);
        Builder_BufPuts(out, "return;\n");
        break;
    default:
        assert(0 && "emit_node: unknown node kind");
        break;
    }
}

/** True when a node's own emission already ends the path (its ports carry
 *  the rest), so its `next` is not followed. */
static int
node_forks(const struct Builder_Node* node)
{
    return node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH
           || node->kind == BUILDER_NODE_END;
}

/**
 * Emit the path starting at node `id`. `root` is 1 for the first node of a
 * label body: that node is the label, so reaching it is emitting it. Any
 * other labeled node reached is a jump -- the shared tail is written once,
 * in its own label, and a back edge (a loop) is always one of these.
 */
static void
emit_chain(
    struct emitter* e,
    const char* id,
    int depth,
    int root)
{
    const struct Builder_Model* model = e->model;

    if( ++e->nesting > 64 )
    {
        Builder_IssueAdd(e->issues, BUILDER_ISSUE_ERROR, "dialogue", "nesting deeper than 64 nodes");
        e->nesting--;
        return;
    }
    while( id[0] )
    {
        int index = node_index(model, id);
        const struct Builder_Node* node;

        if( index < 0 )
            break; /* validation reported the dangling port */
        if( !root && node_labeled(e, index) )
        {
            char label[BUILDER_TRIGGER_NAME_MAX];
            label_name(e, id, label, sizeof(label));
            indent(e->out, depth);
            Builder_BufPrintf(e->out, "@%s;\n", label);
            break;
        }
        root = 0;
        node = &model->nodes[index];
        emit_node(e, node, depth);
        if( node_forks(node) )
            break;
        id = node->next;
    }
    e->nesting--;
}

static void
count_edges(
    struct emitter* e,
    const char* id)
{
    const struct Builder_Model* model = e->model;
    int index = id[0] ? node_index(model, id) : -1;
    const struct Builder_Node* node;

    if( index < 0 )
        return;
    e->indeg[index]++;
    if( e->reachable[index] )
        return;
    e->reachable[index] = 1;
    node = &model->nodes[index];
    if( node->kind == BUILDER_NODE_CHOICE || node->kind == BUILDER_NODE_BRANCH )
    {
        for( int a = 0; a < node->arm_count; a++ )
            count_edges(e, node->arms[a].next);
    }
    if( node->kind != BUILDER_NODE_CHOICE && node->kind != BUILDER_NODE_END )
        count_edges(e, node->next);
}

static void
emitter_init(
    struct emitter* e,
    const struct Builder_Model* model,
    struct Builder_Buf* out,
    struct Builder_Issues* issues)
{
    memset(e, 0, sizeof(*e));
    e->model = model;
    e->out = out;
    e->issues = issues;
    snprintf(e->prefix, sizeof(e->prefix), "ed_%s", model->name);
    if( model->node_count > 0 && model->start[0] )
    {
        int start = node_index(model, model->start);
        count_edges(e, model->start);
        /* The entry is reached once by the trigger, which is not a port. */
        if( start >= 0 )
            e->indeg[start]--;
    }
}

static const char*
trigger_word(
    const struct Builder_Model* model,
    const struct Builder_Trigger* trigger)
{
    (void)trigger;
    return model->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";
}

static int
has_use_entries(
    const struct Builder_Model* model,
    int approach)
{
    for( int t = 0; t < model->trigger_count; t++ )
    {
        if( model->triggers[t].op == 0 && model->triggers[t].approach == approach )
            return 1;
    }
    return 0;
}

static void
emit_use_trigger(
    struct emitter* e,
    int approach)
{
    const struct Builder_Model* model = e->model;
    const struct Builder_Trigger* any = NULL;

    Builder_BufPrintf(e->out, "\n[%s%su,%s]\n", approach ? "ap" : "op", trigger_word(model, NULL),
                      model->name);
    for( int t = 0; t < model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &model->triggers[t];
        struct Builder_Buf body;
        struct emitter inner;

        if( trigger->op != 0 || trigger->approach != approach )
            continue;
        if( strcmp(trigger->use_obj, "*") == 0 )
        {
            any = trigger;
            continue;
        }
        Builder_BufPrintf(e->out, "if (last_useitem = %s) {\n", trigger->use_obj);
        Builder_BufInit(&body);
        inner = *e;
        inner.out = &body;
        if( trigger->dialogue )
            Builder_BufPrintf(&body, "@%s_talk;\n", e->prefix);
        else
        {
            emit_steps(&inner, trigger);
            Builder_BufPuts(&body, "return;\n");
        }
        /* Indent the body one level: emit_steps writes from depth 0. */
        for( const char* at = Builder_BufText(&body); *at; )
        {
            const char* end = strchr(at, '\n');
            size_t length = end ? (size_t)(end - at) + 1 : strlen(at);
            if( length > 1 )
                Builder_BufPuts(e->out, "    ");
            Builder_BufPut(e->out, at, length);
            at += length;
        }
        Builder_BufFree(&body);
        Builder_BufPuts(e->out, "}\n");
    }
    if( any )
    {
        if( any->dialogue )
            Builder_BufPrintf(e->out, "@%s_talk;\n", e->prefix);
        else
            emit_steps(e, any);
    }
    else
        Builder_BufPuts(e->out, "~displaymessage(^dm_default);\n");
}

void
Builder_EmitScript(
    const struct Builder_Model* model,
    struct Builder_Buf* out,
    struct Builder_Issues* issues)
{
    struct emitter e;
    int wrote = 0;

    assert(model);
    assert(out);
    assert(issues);

    Builder_Validate(model, issues);
    emitter_init(&e, model, out, issues);

    for( int pass = 0; pass < 2; pass++ )
    {
        for( int op = 1; op <= 5; op++ )
        {
            const struct Builder_Trigger* trigger =
                Builder_ModelTrigger((struct Builder_Model*)model, op, pass, "");
            if( !trigger )
                continue;
            if( !wrote )
                put_header(model, out);
            wrote = 1;
            Builder_BufPrintf(out, "\n[%s%s%d,%s]\n", pass ? "ap" : "op", trigger_word(model, trigger),
                              op, model->name);
            if( trigger->dialogue )
                Builder_BufPrintf(out, "@%s_talk;\n", e.prefix);
            else
                emit_steps(&e, trigger);
        }
    }
    for( int approach = 0; approach < 2; approach++ )
    {
        if( !has_use_entries(model, approach) )
            continue;
        if( !wrote )
            put_header(model, out);
        wrote = 1;
        emit_use_trigger(&e, approach);
    }
    /* The ai triggers (the encounter builder's Combat templates): their
     * steps are the npc's swing. */
    for( int t = 0; t < model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &model->triggers[t];
        if( trigger->op != BUILDER_OP_AI )
            continue;
        if( !wrote )
            put_header(model, out);
        wrote = 1;
        Builder_BufPrintf(out, "\n[%s,%s]\n", trigger->ai, model->name);
        emit_steps(&e, trigger);
    }

    if( model->node_count > 0 && node_index(model, model->start) >= 0 )
    {
        if( !wrote )
            put_header(model, out);
        wrote = 1;
        Builder_BufPrintf(out, "\n[label,%s_talk]\n", e.prefix);
        emit_chain(&e, model->start, 0, 1);
        for( int n = 0; n < model->node_count; n++ )
        {
            char label[BUILDER_TRIGGER_NAME_MAX];
            if( !e.reachable[n] || strcmp(model->nodes[n].id, model->start) == 0
                || !node_labeled(&e, n) )
                continue;
            label_name(&e, model->nodes[n].id, label, sizeof(label));
            Builder_BufPrintf(out, "\n[label,%s]\n", label);
            emit_chain(&e, model->nodes[n].id, 0, 1);
        }
        for( int n = 0; n < model->node_count; n++ )
        {
            char where[64];
            if( e.reachable[n] )
                continue;
            snprintf(where, sizeof(where), "node %s", model->nodes[n].id);
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, where,
                             "not reachable from the start; not emitted");
        }
    }
}

void
Builder_EmitConfig(
    const struct Builder_Model* model,
    struct Builder_Buf* out,
    struct Builder_Issues* issues)
{
    static const char* const k_ladder_categories[] = { "", "climb_up", "climb_down",
                                                       "climb_unqualified" };
    const struct Builder_Server* server;
    const char* category;
    char ladder_category[64];

    assert(model);
    assert(out);
    assert(issues);

    server = &model->server;
    category = server->category;
    if( server->door == BUILDER_DOOR_CLOSED )
        category = "door_closed";
    else if( server->door == BUILDER_DOOR_OPENED )
        category = "door_opened";
    else if( server->ladder != BUILDER_LADDER_NONE )
    {
        snprintf(ladder_category, sizeof(ladder_category), "%s%s", k_ladder_categories[server->ladder],
                 server->ladder_animated ? "_ladder" : "");
        category = ladder_category;
    }
    if( !category[0] && server->param_count == 0 && server->door == BUILDER_DOOR_NONE )
        return;

    put_header(model, out);
    Builder_BufPrintf(out, "\n[%s]\n", model->name);
    if( category[0] )
        Builder_BufPrintf(out, "category=%s\n", category);
    if( server->door != BUILDER_DOOR_NONE )
        Builder_BufPrintf(out, "param=next_loc_stage,%s\n", server->door_other);
    for( int p = 0; p < server->param_count; p++ )
    {
        if( server->door != BUILDER_DOOR_NONE && strcmp(server->params[p].name, "next_loc_stage") == 0 )
        {
            Builder_IssueAdd(issues, BUILDER_ISSUE_WARNING, "server",
                             "param next_loc_stage is the door preset's; the explicit one is dropped");
            continue;
        }
        Builder_BufPrintf(out, "param=%s,%s\n", server->params[p].name, server->params[p].value);
    }
}

void
Builder_EmitDbrow(
    const struct Builder_Model* model,
    struct Builder_Buf* out,
    struct Builder_Issues* issues)
{
    assert(model);
    assert(out);
    assert(issues);
    (void)issues;

    if( model->server.maplink_count == 0 )
        return;
    put_header(model, out);
    for( int m = 0; m < model->server.maplink_count; m++ )
    {
        const struct Builder_Maplink* link = &model->server.maplinks[m];
        Builder_BufPrintf(out,
                          "\n[ed_%s_maplink_%d]\ntable=maplink\ndata=src,%s\ndata=dest,%s\n"
                          "data=loc,%s\ndata=dir,%d\n",
                          model->name, m + 1, link->src, link->dest, model->name, link->dir);
    }
}

/* ---- header -------------------------------------------------------------- */

int
Builder_HasHeader(
    const char* text,
    size_t length)
{
    size_t prefix = sizeof(BUILDER_HEADER_PREFIX) - 1;

    assert(text || length == 0);
    return length >= prefix && memcmp(text, BUILDER_HEADER_PREFIX, prefix) == 0;
}

int
Builder_StripHeader(
    const char* text,
    size_t length,
    struct Builder_Buf* out)
{
    size_t at = 0;

    assert(out);
    if( !Builder_HasHeader(text, length) )
        return 0;
    for( int line = 0; line < BUILDER_HEADER_LINES && at < length; line++ )
    {
        if( line > 0 && (length - at < 2 || text[at] != '/' || text[at + 1] != '/') )
            break;
        while( at < length && text[at] != '\n' )
            at++;
        if( at < length )
            at++;
    }
    Builder_BufPut(out, text + at, length - at);
    return 1;
}

/* ---- trigger names ------------------------------------------------------- */

int
Builder_ScriptTriggerNames(
    const struct Builder_Model* model,
    char (*out)[BUILDER_TRIGGER_NAME_MAX],
    int max)
{
    int count = 0;
    const char* word;

    assert(model);
    assert(out || max == 0);

    word = model->subject == BUILDER_SUBJECT_NPC ? "npc" : "loc";
#define NAME_PUSH(...)                                                                             \
    do                                                                                             \
    {                                                                                              \
        if( count < max )                                                                          \
            snprintf(out[count], BUILDER_TRIGGER_NAME_MAX, __VA_ARGS__);                           \
        count++;                                                                                   \
    } while( 0 )

    for( int approach = 0; approach < 2; approach++ )
    {
        for( int op = 1; op <= 5; op++ )
        {
            if( Builder_ModelTrigger((struct Builder_Model*)model, op, approach, "") )
                NAME_PUSH("%s%s%d,%s", approach ? "ap" : "op", word, op, model->name);
        }
    }
    for( int approach = 0; approach < 2; approach++ )
    {
        if( has_use_entries(model, approach) )
            NAME_PUSH("%s%su,%s", approach ? "ap" : "op", word, model->name);
    }
    for( int t = 0; t < model->trigger_count; t++ )
    {
        if( model->triggers[t].op == BUILDER_OP_AI )
            NAME_PUSH("%s,%s", model->triggers[t].ai, model->name);
    }
    if( model->node_count > 0 )
    {
        struct emitter e;
        struct Builder_Issues scratch;
        Builder_IssuesClear(&scratch);
        emitter_init(&e, model, NULL, &scratch);
        NAME_PUSH("label,%s_talk", e.prefix);
        for( int n = 0; n < model->node_count; n++ )
        {
            if( e.reachable[n] && strcmp(model->nodes[n].id, model->start) != 0
                && node_labeled(&e, n) )
                NAME_PUSH("label,%s_%s", e.prefix, model->nodes[n].id);
        }
    }
#undef NAME_PUSH
    return count;
}

int
Builder_FindConflicts(
    const struct Builder_Model* model,
    const struct Builder_ExistingTrigger* existing,
    int existing_count,
    struct Builder_Issues* issues)
{
    char names[BUILDER_TRIGGERS_MAX + BUILDER_NODES_MAX + 4][BUILDER_TRIGGER_NAME_MAX];
    char own[EDITOR_DOC_KEY_MAX];
    int count;
    int conflicts = 0;
    size_t own_length;

    assert(model);
    assert(existing || existing_count == 0);
    assert(issues);

    count = Builder_ScriptTriggerNames(model, names, (int)(sizeof(names) / sizeof(names[0])));
    Builder_FilePath(model->subject, model->name, BUILDER_FILE_SCRIPT, own, sizeof(own));
    own_length = strlen(own);
    for( int i = 0; i < existing_count; i++ )
    {
        const char* file = existing[i].file ? existing[i].file : "";
        size_t file_length = strlen(file);

        if( file_length >= own_length && strcmp(file + file_length - own_length, own) == 0 )
            continue;
        for( int n = 0; n < count; n++ )
        {
            if( strcmp(existing[i].name, names[n]) == 0 )
            {
                Builder_IssueAdd(issues, BUILDER_ISSUE_ERROR, names[n],
                                 "already declared in %s; open that script (sscompile refuses a "
                                 "duplicate trigger)",
                                 file);
                conflicts++;
            }
        }
    }
    return conflicts;
}

/* ---- regenerating -------------------------------------------------------- */

enum Builder_SyncResult
Builder_Regenerate(
    const struct Builder_Model* model,
    const struct Builder_Submit* submit,
    struct Builder_Issues* issues)
{
    struct Builder_Buf texts[BUILDER_FILE_COUNT];
    enum Builder_SyncResult worst = BUILDER_SYNC_UNCHANGED;
    size_t ini_length;

    assert(model);
    assert(submit);
    assert(issues);

    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
        Builder_BufInit(&texts[f]);
    ini_length = Builder_ModelEmitIni(model, NULL, 0);
    {
        char* ini = ToriMem_Alloc(ToriMem_Editor(), ini_length + 1);
        assert(ini);
        Builder_ModelEmitIni(model, ini, ini_length + 1);
        Builder_BufPut(&texts[BUILDER_FILE_MODEL], ini, ini_length);
        ToriMem_Free(ToriMem_Editor(), ini);
    }
    Builder_EmitScript(model, &texts[BUILDER_FILE_SCRIPT], issues);
    Builder_EmitConfig(model, &texts[BUILDER_FILE_CONFIG], issues);
    Builder_EmitDbrow(model, &texts[BUILDER_FILE_DBROW], issues);

    submit->stroke(submit->user_data, 1);
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        enum Builder_SyncResult result;

        Builder_FileKey(model->subject, model->name, (enum Builder_File)f, key, sizeof(key));
        result = Builder_TextSync(submit, key, Builder_BufText(&texts[f]), texts[f].length, 1);
        if( result == BUILDER_SYNC_REFUSED )
        {
            worst = result;
            break;
        }
        if( result == BUILDER_SYNC_APPLIED )
            worst = result;
    }
    submit->stroke(submit->user_data, 0);
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
        Builder_BufFree(&texts[f]);
    return worst;
}

enum Builder_SyncResult
Builder_Detach(
    enum Builder_Subject subject,
    const char* name,
    const struct Builder_Submit* submit)
{
    enum Builder_SyncResult worst = BUILDER_SYNC_UNCHANGED;

    assert(name);
    assert(submit);
    assert(Builder_NameValid(name));

    submit->stroke(submit->user_data, 1);
    for( int f = 0; f < BUILDER_FILE_COUNT; f++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        const char* current;
        size_t length = 0;
        struct Builder_Buf stripped;
        enum Builder_SyncResult result;

        Builder_FileKey(subject, name, (enum Builder_File)f, key, sizeof(key));
        Builder_BufInit(&stripped);
        if( f != BUILDER_FILE_MODEL )
        {
            current = submit->current(submit->user_data, EDITOR_DOC_TEXT, key, &length);
            if( !current || !Builder_StripHeader(current, length, &stripped) )
            {
                Builder_BufFree(&stripped);
                continue;
            }
        }
        result = Builder_TextSync(submit, key, Builder_BufText(&stripped), stripped.length, 1);
        Builder_BufFree(&stripped);
        if( result == BUILDER_SYNC_REFUSED )
        {
            worst = result;
            break;
        }
        if( result == BUILDER_SYNC_APPLIED )
            worst = result;
    }
    submit->stroke(submit->user_data, 0);
    return worst;
}
