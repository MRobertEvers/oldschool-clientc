#include "builder_lint.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* const k_lint_labels[BUILDER_LINT_KIND_COUNT] = {
    "Literal player name", "Orphan node", "One-arm choice", "Unknown var", "Unknown proc",
};

const char*
Builder_LintKindLabel(enum Builder_LintKind kind)
{
    assert((int)kind >= 0);
    assert(kind < BUILDER_LINT_KIND_COUNT);
    return k_lint_labels[kind];
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 7, 8)))
#endif
static void
lint_add(
    struct Builder_LintRows* out,
    enum Builder_LintKind kind,
    int error,
    int node,
    int trigger,
    int step,
    const char* format,
    ...)
{
    struct Builder_LintRow* row;
    va_list args;

    if( out->count >= BUILDER_LINT_ROWS_MAX )
    {
        out->dropped++;
        return;
    }
    row = &out->rows[out->count++];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    row->error = error;
    row->node = node;
    row->trigger = trigger;
    row->step = step;
    va_start(args, format);
    vsnprintf(row->message, sizeof(row->message), format, args);
    va_end(args);
}

/** The rows added since `before` get `where`. */
static void
lint_where(
    struct Builder_LintRows* out,
    int before,
    const char* where)
{
    for( int i = before; i < out->count; i++ )
        snprintf(out->rows[i].where, sizeof(out->rows[i].where), "%s", where);
}

/** `name` as a whole word of `text`, case-insensitively. */
static int
lint_has_word(
    const char* text,
    const char* name)
{
    size_t const length = strlen(name);

    for( const char* at = text; *at; at++ )
    {
        size_t i;
        if( at != text && (isalnum((unsigned char)at[-1]) || at[-1] == '_') )
            continue;
        for( i = 0; i < length && at[i]; i++ )
        {
            if( tolower((unsigned char)at[i]) != tolower((unsigned char)name[i]) )
                break;
        }
        if( i == length && !isalnum((unsigned char)at[length]) && at[length] != '_' )
            return 1;
    }
    return 0;
}

static void
lint_text(
    const struct Builder_LintNames* names,
    struct Builder_LintRows* out,
    int node,
    int trigger,
    int step,
    const char* text)
{
    for( const unsigned char* at = (const unsigned char*)text; *at; at++ )
    {
        if( (at[0] == 0xC2 && at[1] == 0xA0) || at[0] == 0xA0 )
        {
            lint_add(out, BUILDER_LINT_PLAYER_NAME, 1, node, trigger, step,
                     "a U+00A0 is how a recorded player's name arrives: write {player}");
            return;
        }
    }
    if( names->player_name && names->player_name[0] && lint_has_word(text, names->player_name) )
        lint_add(out, BUILDER_LINT_PLAYER_NAME, 1, node, trigger, step,
                 "'%s' is this session's player: write {player}, every player reads it", names->player_name);
}

static void
lint_var(
    const struct Builder_LintNames* names,
    struct Builder_LintRows* out,
    int node,
    int trigger,
    int step,
    const char* var)
{
    int known;

    if( !var[0] || !names->var_known )
        return;
    known = names->var_known(names->user_data, var);
    if( known < 0 )
        out->unchecked |= 1u << BUILDER_LINT_UNKNOWN_VAR;
    else if( known == 0 )
        lint_add(out, BUILDER_LINT_UNKNOWN_VAR, 1, node, trigger, step, "no varp or varbit is named %s", var);
}

static void
lint_step(
    const struct Builder_LintNames* names,
    struct Builder_LintRows* out,
    int node,
    int trigger,
    int index,
    const struct Builder_Step* step)
{
    switch( step->kind )
    {
    case BUILDER_STEP_MES:
    case BUILDER_STEP_REQUIRE_OBJ:
    case BUILDER_STEP_REQUIRE_LEVEL:
        lint_text(names, out, node, trigger, index, step->text);
        break;
    case BUILDER_STEP_IF_VAR:
    case BUILDER_STEP_ELSE_IF_VAR:
    case BUILDER_STEP_SET_VAR:
        lint_var(names, out, node, trigger, index, step->a);
        break;
    case BUILDER_STEP_CALL:
    {
        int known;
        if( !step->a[0] || !names->proc_known )
            break;
        known = names->proc_known(names->user_data, step->a);
        if( known < 0 )
            out->unchecked |= 1u << BUILDER_LINT_UNKNOWN_PROC;
        else if( known == 0 )
            lint_add(out, BUILDER_LINT_UNKNOWN_PROC, 1, node, trigger, index, "no [proc,%s] in the tree", step->a);
        break;
    }
    default:
        break;
    }
}

int
Builder_Lint(
    const struct Builder_Model* model,
    const struct Builder_LintNames* names,
    struct Builder_LintRows* out)
{
    unsigned char orphans[BUILDER_NODES_MAX];

    assert(model);
    assert(names);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( !names->var_known )
        out->unchecked |= 1u << BUILDER_LINT_UNKNOWN_VAR;
    if( !names->proc_known )
        out->unchecked |= 1u << BUILDER_LINT_UNKNOWN_PROC;

    (void)Builder_ModelOrphans(model, orphans);
    for( int n = 0; n < model->node_count; n++ )
    {
        const struct Builder_Node* node = &model->nodes[n];
        char where[64];
        int before = out->count;

        snprintf(where, sizeof(where), "node %s", node->id);
        if( orphans[n] )
            lint_add(out, BUILDER_LINT_ORPHAN, 0, n, -1, -1,
                     "%s %s is not reached from the start (%s): it is emitted as nothing", node->id,
                     Builder_NodeKindLabel(node->kind), model->start[0] ? model->start : "none");
        switch( node->kind )
        {
        case BUILDER_NODE_NPC:
            lint_text(names, out, n, -1, -1, node->speaker_name);
            /* fall through */
        case BUILDER_NODE_PLAYER:
        case BUILDER_NODE_MESBOX:
        case BUILDER_NODE_OBJBOX:
            lint_text(names, out, n, -1, -1, node->text);
            break;
        case BUILDER_NODE_CHOICE:
            if( node->arm_count < 2 )
                lint_add(out, BUILDER_LINT_ONE_ARM, 1, n, -1, -1,
                         "a choice with %d option%s is no choice: add one, or use a line", node->arm_count,
                         node->arm_count == 1 ? "" : "s");
            lint_text(names, out, n, -1, -1, node->text);
            for( int a = 0; a < node->arm_count; a++ )
                lint_text(names, out, n, -1, -1, node->arms[a].text);
            break;
        case BUILDER_NODE_BRANCH:
            lint_var(names, out, n, -1, -1, node->obj);
            break;
        case BUILDER_NODE_STEP:
            lint_step(names, out, n, -1, -1, &node->step);
            break;
        case BUILDER_NODE_QUEST:
            if( node->quest == BUILDER_QUEST_START )
                lint_var(names, out, n, -1, -1, node->obj);
            else
                lint_text(names, out, n, -1, -1, node->text);
            break;
        default:
            break;
        }
        lint_where(out, before, where);
    }
    for( int t = 0; t < model->trigger_count; t++ )
    {
        const struct Builder_Trigger* trigger = &model->triggers[t];
        if( trigger->dialogue )
            continue;
        for( int s = 0; s < trigger->step_count; s++ )
        {
            char where[64];
            int before = out->count;
            if( trigger->op == BUILDER_OP_AI )
                snprintf(where, sizeof(where), "%s step %d", trigger->ai, s + 1);
            else if( trigger->op == 0 )
                snprintf(where, sizeof(where), "%su %s step %d", trigger->approach ? "ap" : "op",
                         trigger->use_obj, s + 1);
            else
                snprintf(where, sizeof(where), "%s%d step %d", trigger->approach ? "ap" : "op", trigger->op,
                         s + 1);
            lint_step(names, out, -1, t, s, &trigger->steps[s]);
            lint_where(out, before, where);
        }
    }
    return out->count;
}
