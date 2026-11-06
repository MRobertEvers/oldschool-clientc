#ifndef SRC_EDITOR_BUILDER_BUILDER_MODEL_H
#define SRC_EDITOR_BUILDER_BUILDER_MODEL_H

/**
 * The interaction builders' model (docs/MAPED_DESIGN.html #locbuilder,
 * #npcbuilder): one loc's or one npc's interactions as structured data, the
 * record the builder reads back when it re-opens. The scripts are DERIVED
 * from it (builder_emit.h) and never parsed back.
 *
 * Where it lives. Everything the builder writes sits in a directory it owns,
 * under the content tree's server scripts:
 *
 *   server/scripts/edited/<subject>/<subject>.interaction   this model (INI)
 *   server/scripts/edited/<subject>/scripts/<subject>.rs2   triggers, labels
 *   server/scripts/edited/<subject>/configs/<subject>.<loc|npc>
 *                                                           server fields
 *   server/scripts/edited/<subject>/configs/<subject>.dbrow maplink rows
 *
 * All four are TEXT documents ("rs2:edited/<subject>/..."), edited through
 * the authority as TEXT_SPLICE strokes (builder_text.h); the builder writes no
 * file itself. The server loader reads none of the .interaction files (it
 * reads only the extensions it knows), and sscompile only walks `.rs2`.
 *
 * THE INI. One `[interaction]` block, then one block per part:
 *
 *   [interaction]                 version=1, subject=loc|npc, name=<record>
 *   [server]                      category=, param=<name>,<value> (repeated),
 *                                 door=closed|opened,<other loc>,
 *                                 ladder=up|down|unqualified[,ladder],
 *                                 maplink=<src coord>|<dest coord>|<dir>
 *   [op<N>]  [ap<N>]              one trigger, N 1..5: label=, dialogue=1,
 *                                 step=<kind>|<field>|... (repeated, in order)
 *   [opu,<obj>]  [apu,<obj>]      a use-on entry; "*" for any held obj
 *   [ai,<trigger>]                an npc ai trigger (ai_opplayer2, ...): the
 *                                 encounter builder's Combat templates (6.7)
 *   [dialogue]                    start=<node id>
 *   [node,<id>]                   one dialogue node (Builder_NodeKind)
 *
 * Values are one line: '\' escapes '\', '|' and a line break ("\n"). The
 * parser accepts what the emitter writes and refuses everything else, so a
 * hand-damaged model is reported rather than half-read.
 */

#include <stddef.h>

/** A record, var, obj or proc name: the tree has varbit and loc names past
 *  63 bytes (80 varbits, 47 locs), so names hold what a CFG_FIELD value can. */
#define BUILDER_NAME_MAX 128
/** A SUBJECT is shorter: its name is spelled twice in its files' document
 *  keys ("rs2:edited/<s>/configs/<s>.interaction" must fit 127 bytes). */
#define BUILDER_SUBJECT_MAX 48
#define BUILDER_TEXT_MAX 512
#define BUILDER_STEPS_MAX 48
#define BUILDER_TRIGGERS_MAX 16
#define BUILDER_NODES_MAX 128
#define BUILDER_ARMS_MAX 5
#define BUILDER_PARAMS_MAX 16
#define BUILDER_MAPLINKS_MAX 8
/** Node ids: [a-z0-9_], what a label name is made of. */
#define BUILDER_NODE_ID_MAX 32
/** An ai trigger's name ("ai_opplayer2"), the terminator included. */
#define BUILDER_AI_NAME_MAX 32
/** Builder_Trigger.op of an ai trigger. */
#define BUILDER_OP_AI 6

enum Builder_Subject
{
    BUILDER_SUBJECT_LOC = 0,
    BUILDER_SUBJECT_NPC,
};

/**
 * The step vocabulary (design §11): the shapes the tree already uses most,
 * each a template with typed fields. Fields a, b, c are names or numbers;
 * `text` is the prose a message carries, or the verbatim code of RAW.
 *
 *   kind           a           b          c          text
 *   MES                                              the message
 *   IF_VAR         var         cmp        value
 *   ELSE_IF_VAR    var         cmp        value
 *   ELSE
 *   END                                              (closes an IF_VAR)
 *   SET_VAR        var         value
 *   GIVE           obj         count
 *   TAKE           obj         count
 *   REQUIRE_OBJ    obj         count                 said when it is missing
 *   REQUIRE_LEVEL  stat        level                 said when it is too low
 *   OPEN_MAIN      interface
 *   OPEN_SUB       target      interface  mode       (target = iface:comp)
 *   ANIM           seq
 *   SOUND          synth
 *   TELEPORT       coord                             (level_mx_mz_lx_lz)
 *   DELAY          ticks
 *   LOC_SWAP       loc         ticks
 *   CALL           proc        args
 *   JUMP           label
 *   RAW                                              verbatim RuneScript
 *
 * A var is named as the tree names it, `varp<id>_<name>` / `varb<id>_<name>`
 * (editor_vars.h), and is written `%<name>`. `cmp` is one of = ! < > <= >=.
 */
enum Builder_StepKind
{
    BUILDER_STEP_MES = 0,
    BUILDER_STEP_IF_VAR,
    BUILDER_STEP_ELSE_IF_VAR,
    BUILDER_STEP_ELSE,
    BUILDER_STEP_END,
    BUILDER_STEP_SET_VAR,
    BUILDER_STEP_GIVE,
    BUILDER_STEP_TAKE,
    BUILDER_STEP_REQUIRE_OBJ,
    BUILDER_STEP_REQUIRE_LEVEL,
    BUILDER_STEP_OPEN_MAIN,
    BUILDER_STEP_OPEN_SUB,
    BUILDER_STEP_ANIM,
    BUILDER_STEP_SOUND,
    BUILDER_STEP_TELEPORT,
    BUILDER_STEP_DELAY,
    BUILDER_STEP_LOC_SWAP,
    BUILDER_STEP_CALL,
    BUILDER_STEP_JUMP,
    BUILDER_STEP_RAW,
    BUILDER_STEP_KIND_COUNT
};

struct Builder_Step
{
    enum Builder_StepKind kind;
    char a[BUILDER_NAME_MAX];
    char b[BUILDER_NAME_MAX];
    char c[BUILDER_NAME_MAX];
    char text[BUILDER_TEXT_MAX];
};

/**
 * One trigger: `[op<N>,subject]` / `[ap<N>,subject]`, or a use-on entry.
 * Use-on entries of one subject share one `[oplocu]`/`[opnpcu]` trigger: the
 * emitter writes them as an `if (last_useitem = <obj>)` chain, the `*` entry
 * (any obj) as its tail, else the tree's default message.
 */
struct Builder_Trigger
{
    /** 1..5, 0 for a use-on entry, BUILDER_OP_AI for an ai trigger
     *  (`[<ai>,<npc>]`, its steps the body; approach and use_obj unused). */
    int op;
    /** BUILDER_OP_AI: the trigger, "ai_opplayer2", "ai_applayer2", ... */
    char ai[BUILDER_AI_NAME_MAX];
    /** 1 = ap (approach), 0 = op (operate when adjacent). */
    int approach;
    /** Use-on: the held obj, "*" = any. */
    char use_obj[BUILDER_NAME_MAX];
    /** The menu text this op is (the cache record's opN), shown as the
     *  trigger's label. Informational: renaming is a CFG_FIELD on the cache
     *  record, never this field. */
    char label[BUILDER_NAME_MAX];
    /** 1 = this op runs the dialogue graph; its steps are ignored. */
    int dialogue;
    struct Builder_Step steps[BUILDER_STEPS_MAX];
    int step_count;
};

/**
 * The dialogue graph's nodes, one-to-one onto the procs the tree uses by the
 * thousand (design §12):
 *
 *   NPC        ~chatnpc_anim(^chat_<expr>, "text") / ~chatnpc("text"), or
 *              ~chatnpc_specific[_anim]("<name>", <npc>, ...) when a speaker
 *              is given
 *   PLAYER     ~chatplayer_anim(^chat_<expr>, "text") / ~chatplayer("text")
 *   MESBOX     ~mesbox("text")
 *   OBJBOX     ~objbox(<obj>, "text")
 *   CHOICE     def_int $choice_<id> = ~p_choice<N>[_header](...); one arm per
 *              option (2..5)
 *   BRANCH     if (%<var> <cmp> <value>) { } else if ... else { default }
 *   STEP       one step of the loc builder's vocabulary
 *   QUEST      start (the progress var's first write) or ~quest_complete_rewards
 *   END        return;
 *
 * Text carries the player's name as `<displayname>` (the tree's helper) or
 * the `{player}` token, which the emitter writes as `<displayname>`; never a
 * literal name (Builder_LintText).
 */
enum Builder_NodeKind
{
    BUILDER_NODE_NPC = 0,
    BUILDER_NODE_PLAYER,
    BUILDER_NODE_MESBOX,
    BUILDER_NODE_OBJBOX,
    BUILDER_NODE_CHOICE,
    BUILDER_NODE_BRANCH,
    BUILDER_NODE_STEP,
    BUILDER_NODE_QUEST,
    BUILDER_NODE_END,
    BUILDER_NODE_KIND_COUNT
};

/**
 * The quest hooks as the tree defines them. The tree has no start helper: a
 * quest starts when its progress var leaves 0 (the journal and the quest list
 * read that var), so START is that write. COMPLETE is the helper 250 quests
 * end with. Later stages are SET_VAR steps.
 */
enum Builder_QuestAction
{
    /** %<obj> = <value>; -- the progress var to its first stage. */
    BUILDER_QUEST_START = 0,
    /** ~quest_complete_rewards(<row>, "<text>", <obj>) -- the scroll. */
    BUILDER_QUEST_COMPLETE,
    BUILDER_QUEST_ACTION_COUNT
};

/** One out-port of a CHOICE (an option) or a BRANCH (an arm). */
struct Builder_Arm
{
    /** CHOICE: the option text. BRANCH: unused. */
    char text[BUILDER_TEXT_MAX];
    /** BRANCH: the comparison and the value (a number or ^constant). */
    char cmp[4];
    char value[BUILDER_NAME_MAX];
    /** The node this port leads to; "" = the dialogue ends here. */
    char next[BUILDER_NODE_ID_MAX];
};

struct Builder_Node
{
    char id[BUILDER_NODE_ID_MAX];
    enum Builder_NodeKind kind;
    /** NPC/PLAYER/MESBOX/OBJBOX: the page text. CHOICE: the optional title.
     *  QUEST COMPLETE: the rewards ("a|b|c", the scroll's lines). */
    char text[BUILDER_TEXT_MAX];
    /** NPC/PLAYER: the ^chat_<expr> suffix ("neutral", "happy"); "" = none. */
    char expr[BUILDER_NAME_MAX];
    /** NPC: speaker override -- the npc record and the name it shows. */
    char speaker_npc[BUILDER_NAME_MAX];
    char speaker_name[BUILDER_NAME_MAX];
    /** OBJBOX: the obj. QUEST COMPLETE: the reward icon. QUEST START and
     *  BRANCH: the var. */
    char obj[BUILDER_NAME_MAX];
    /** QUEST COMPLETE: the quest's dbrow. */
    char row[BUILDER_NAME_MAX];
    /** QUEST START: the first stage (a number or ^constant). */
    char value[BUILDER_NAME_MAX];
    enum Builder_QuestAction quest;
    /** STEP: the step. */
    struct Builder_Step step;
    /** CHOICE: options 2..5. BRANCH: arms, then `next` is the default. */
    struct Builder_Arm arms[BUILDER_ARMS_MAX];
    int arm_count;
    /** The linear successor (NPC..STEP, QUEST), or BRANCH's default. "" =
     *  the dialogue ends. */
    char next[BUILDER_NODE_ID_MAX];
    /** Where the GRAPH well draws it, in well pixels. Layout only. */
    int x;
    int y;
};

enum Builder_DoorState
{
    BUILDER_DOOR_NONE = 0,
    BUILDER_DOOR_CLOSED,
    BUILDER_DOOR_OPENED,
};

enum Builder_LadderDir
{
    BUILDER_LADDER_NONE = 0,
    BUILDER_LADDER_UP,
    BUILDER_LADDER_DOWN,
    BUILDER_LADDER_UNQUALIFIED,
};

struct Builder_Param
{
    char name[BUILDER_NAME_MAX];
    char value[BUILDER_NAME_MAX];
};

/** A `maplink` dbrow (ladders_stairs/configs/maplink.dbtable): pressing the
 *  subject at `src` lands on `dest`. Coords are level_mx_mz_lx_lz. */
struct Builder_Maplink
{
    char src[BUILDER_NAME_MAX];
    char dest[BUILDER_NAME_MAX];
    int dir;
};

/** The server-overlay half: what `configs/<subject>.<loc|npc>` states. */
struct Builder_Server
{
    char category[BUILDER_NAME_MAX];
    struct Builder_Param params[BUILDER_PARAMS_MAX];
    int param_count;
    /** Door preset (design §11): category door_closed / door_opened plus
     *  param=next_loc_stage,<door_other> -- two field edits, no script. */
    enum Builder_DoorState door;
    char door_other[BUILDER_NAME_MAX];
    /** Ladder preset: category climb_<dir>[_ladder]; the generic handler in
     *  ladders_stairs/ moves the player, a maplink row says where to. */
    enum Builder_LadderDir ladder;
    int ladder_animated;
    struct Builder_Maplink maplinks[BUILDER_MAPLINKS_MAX];
    int maplink_count;
};

/**
 * NEVER a stack local in a hot path: about 300 KB (the dialogue graph's
 * text dominates). Tests and panels hold theirs on the heap or static.
 */
struct Builder_Model
{
    enum Builder_Subject subject;
    /** The loc / npc record, as configs/all.<loc|npc> names it. */
    char name[BUILDER_NAME_MAX];
    struct Builder_Server server;
    struct Builder_Trigger triggers[BUILDER_TRIGGERS_MAX];
    int trigger_count;
    char start[BUILDER_NODE_ID_MAX];
    struct Builder_Node nodes[BUILDER_NODES_MAX];
    int node_count;
};

/* ---- the model ----------------------------------------------------------- */

void
Builder_ModelInit(
    struct Builder_Model* model,
    enum Builder_Subject subject,
    const char* name);

/** 1 for a record name the tree can name a directory and a trigger after:
 *  [a-z0-9_], 1..BUILDER_SUBJECT_MAX-1 bytes. */
int
Builder_NameValid(const char* name);

/** The trigger for (op, approach) -- or a use-on entry for `use_obj` when op
 *  is 0 -- or NULL. */
struct Builder_Trigger*
Builder_ModelTrigger(
    struct Builder_Model* model,
    int op,
    int approach,
    const char* use_obj);

/** The trigger, added (empty) when the model has none. NULL when full. */
struct Builder_Trigger*
Builder_ModelTriggerAdd(
    struct Builder_Model* model,
    int op,
    int approach,
    const char* use_obj);

/** 1 for an npc ai trigger name: "ai_" then [a-z0-9_], under
 *  BUILDER_AI_NAME_MAX bytes. */
int
Builder_AiTriggerValid(const char* ai);

/** The ai trigger `ai` (Builder_AiTriggerValid), or NULL. */
struct Builder_Trigger*
Builder_ModelAiTrigger(
    struct Builder_Model* model,
    const char* ai);

/** The ai trigger, added (empty) when the model has none. NULL when full. */
struct Builder_Trigger*
Builder_ModelAiTriggerAdd(
    struct Builder_Model* model,
    const char* ai);

void
Builder_ModelTriggerRemove(
    struct Builder_Model* model,
    int index);

/** Append a step. Returns its index, or -1 when the trigger is full. */
int
Builder_TriggerStepAdd(
    struct Builder_Trigger* trigger,
    const struct Builder_Step* step);

void
Builder_TriggerStepRemove(
    struct Builder_Trigger* trigger,
    int index);

/** Move step `from` to position `to` (both < step_count). */
void
Builder_TriggerStepMove(
    struct Builder_Trigger* trigger,
    int from,
    int to);

/** A step of `kind` with the template's default fields filled. */
void
Builder_StepInit(
    struct Builder_Step* step,
    enum Builder_StepKind kind);

/** The node named `id`, or NULL. */
struct Builder_Node*
Builder_ModelNode(
    struct Builder_Model* model,
    const char* id);

const struct Builder_Node*
Builder_ModelNodeConst(
    const struct Builder_Model* model,
    const char* id);

/** Add a node of `kind` with a fresh id ("n<k>"). NULL when full. The first
 *  node added becomes the start when there is none. */
struct Builder_Node*
Builder_ModelNodeAdd(
    struct Builder_Model* model,
    enum Builder_NodeKind kind);

/** Remove node `id` and every port that led to it (those ports end the
 *  dialogue instead). */
void
Builder_ModelNodeRemove(
    struct Builder_Model* model,
    const char* id);

/* ---- the graph (workbench 6.6: ports, links, layout) ---------------------
 *
 * A node's OUT-PORTS, in the order the GRAPH well draws them left to right:
 *
 *   CHOICE      one per option (0 .. arm_count-1); `next` is not a port
 *   BRANCH      one per arm, then the default (port arm_count = `next`)
 *   END         none
 *   any other   one: port 0 = `next`
 *
 * Linking writes the target's id into that port; an empty target unlinks.
 */

/** How many out-ports `node` has. */
int
Builder_NodePortCount(const struct Builder_Node* node);

/** The port's target field (an id, "" = the dialogue ends). `port` must be
 *  below Builder_NodePortCount. */
char*
Builder_NodePortNext(
    struct Builder_Node* node,
    int port);

const char*
Builder_NodePortNextConst(
    const struct Builder_Node* node,
    int port);

/** Link `from`'s port to node `to` ("" unlinks). Returns 1, or 0 when `to`
 *  names no node (nothing changes). */
int
Builder_ModelLink(
    struct Builder_Model* model,
    const char* from,
    int port,
    const char* to);

/** The index of node `id`, or -1. */
int
Builder_ModelNodeIndex(
    const struct Builder_Model* model,
    const char* id);

/**
 * Mark every node reachable from `id` through out-ports, `id` included:
 * mask[i] = 1 for node i (BUILDER_NODES_MAX bytes, cleared here). Returns
 * how many were marked; 0 when `id` names no node.
 */
int
Builder_ModelReach(
    const struct Builder_Model* model,
    const char* id,
    unsigned char* mask);

/** Mark the ORPHANS -- the nodes the start cannot reach (every node when
 *  there is no start). Returns how many. */
int
Builder_ModelOrphans(
    const struct Builder_Model* model,
    unsigned char* mask);

/** The auto-layout's grid: a column per depth, a row per node in it. */
#define BUILDER_LAYOUT_ORIGIN 10
#define BUILDER_LAYOUT_COL_W 150
#define BUILDER_LAYOUT_ROW_H 44

/**
 * Lay the graph out left to right by depth (Ctrl+L): a breadth-first walk
 * from the start, ports in port order, puts each node in the column of the
 * depth it is first reached at and the next free row of that column. The
 * orphans follow as roots of their own walks, in model order, below what is
 * already in each column. A pure function of the links: the same graph lays
 * out the same way whatever its current positions (stable), so a hand
 * layout survives only until the next Ctrl+L. Returns how many nodes moved.
 */
int
Builder_ModelAutoLayout(struct Builder_Model* model);

/* ---- names --------------------------------------------------------------- */

/** "mes", "if_var", ... -- the token the INI writes. */
const char*
Builder_StepKindToken(enum Builder_StepKind kind);

/** "Message", "If var", ... -- what the step list shows. */
const char*
Builder_StepKindLabel(enum Builder_StepKind kind);

/** BUILDER_STEP_KIND_COUNT when the token names no kind. */
enum Builder_StepKind
Builder_StepKindParse(const char* token);

const char*
Builder_NodeKindToken(enum Builder_NodeKind kind);

const char*
Builder_NodeKindLabel(enum Builder_NodeKind kind);

enum Builder_NodeKind
Builder_NodeKindParse(const char* token);

/* ---- paths and document keys --------------------------------------------- */

enum Builder_File
{
    /** edited/<s>/<s>.interaction */
    BUILDER_FILE_MODEL = 0,
    /** edited/<s>/scripts/<s>.rs2 */
    BUILDER_FILE_SCRIPT,
    /** edited/<s>/configs/<s>.loc | .npc */
    BUILDER_FILE_CONFIG,
    /** edited/<s>/configs/<s>.dbrow */
    BUILDER_FILE_DBROW,
    BUILDER_FILE_COUNT
};

/**
 * The file's path under server/scripts ("edited/<s>/scripts/<s>.rs2"), and
 * its TEXT document key ("rs2:" + that path). Returns 0 when it does not
 * fit, which a valid name never hits.
 */
int
Builder_FilePath(
    enum Builder_Subject subject,
    const char* name,
    enum Builder_File file,
    char* out,
    size_t out_size);

int
Builder_FileKey(
    enum Builder_Subject subject,
    const char* name,
    enum Builder_File file,
    char* out,
    size_t out_size);

/* ---- the INI ------------------------------------------------------------- */

/**
 * Write the model as `.interaction` text into `out` (NULL to measure).
 * Returns the bytes the whole text needs (excluding the terminator); writes
 * at most `out_size - 1` and terminates.
 */
size_t
Builder_ModelEmitIni(
    const struct Builder_Model* model,
    char* out,
    size_t out_size);

/**
 * Read `.interaction` text into `model`. Returns 1, or 0 with a message in
 * `error` ("line 12: unknown step kind 'foo'") when the text is not one the
 * emitter could have written. An empty text is 0 ("no model": a detached
 * builder's file).
 */
int
Builder_ModelParseIni(
    struct Builder_Model* model,
    const char* text,
    size_t length,
    char* error,
    size_t error_size);

#endif
