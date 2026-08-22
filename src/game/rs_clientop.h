#ifndef RS_CLIENTOP_H
#define RS_CLIENTOP_H

/*
 * CLIENTOP_* (6700..6709): right-click rows the CLIENT owns.
 *
 * A cache script installs one with a slot, a label and a clientscript:
 *
 *     _6708(1, "Mark tile", 4762);   // CLIENTOP_TILE_SET
 *     _6700(1, "Tag", 6688);         // CLIENTOP_NPC_SET
 *
 * and removes it with the DEL form when the setting behind it is switched off.
 * The row goes on the minimenu for its subject kind; picking it runs the named
 * script, and WHILE THAT SCRIPT RUNS the context ops answer with the subject:
 *
 *     [clientscript,script4762]                  // "Mark tile"
 *     if (_7038(_6950, 6, 1) = true) {           //   _6950 = the tile's coord
 *         highlight_tile_off(_6950, 6, 1);
 *     } else {
 *         highlight_tile_on(_6950, 6, 1);
 *     }
 *
 * That is the missing half of the Activities settings. The HIGHLIGHT_* groups
 * were already being set up off their varbits; nothing was ever put IN them,
 * because the scripts that do the putting get their subject from here. See
 * NXT_CLIENT_PLUGINS.md and rs_highlight.h.
 *
 * Nothing here includes an engine header, so it links into a test on its own.
 */

#include <stdbool.h>

/** Kinds of subject a client op can hang off -- one SET/DEL opcode pair each. */
enum RS_ClientOpKind
{
    RS_CLIENTOP_NPC = 0,
    RS_CLIENTOP_LOC,
    RS_CLIENTOP_OBJ,
    RS_CLIENTOP_PLAYER,
    RS_CLIENTOP_TILE,
    RS_CLIENTOP_KIND_COUNT
};

/**
 * Slots per kind.
 *
 * The cache uses 1..6 (clientscript 4624's developer ops go that high); slot 0
 * is never used by anything, and is kept addressable rather than special-cased
 * so a script that asks for it gets a slot instead of a silent no-op.
 */
#define RS_CLIENTOP_SLOT_MAX 8

/** Bytes of a row's label. The longest the cache installs is
 *  "Highlight-all always-on-top" at 27. */
#define RS_CLIENTOP_LABEL_MAX 40

/** Bytes of a subject's name, matching the entity snapshots it comes from. */
#define RS_CLIENTOP_NAME_MAX 64

/** Bytes of a MINIMENU_ENTRY string. A menu row, not a name: it carries the op
 *  and the coloured target together (see mouseover_op), so it is sized like the
 *  menu model it is copied from (UITREE_MINIMENU_OPTION_LEN). */
#define RS_CLIENTOP_MENU_TEXT_MAX 128

struct RS_ClientOpSlot
{
    bool set;
    char label[RS_CLIENTOP_LABEL_MAX];
    int script_id;
};

/**
 * What the client op currently being dispatched is about.
 *
 * Filled by whoever runs the script, cleared when it returns. Outside a
 * dispatch every field reads as absent, because a script asking about a client
 * op while none is running is asking about nothing -- and answering with the
 * LAST one would silently mark whatever was clicked before.
 */
struct RS_ClientOpContext
{
    /** enum RS_ClientOpKind, or -1 when no op is being dispatched. */
    int kind;
    /**
     * The clientscript this context belongs to, and the ONLY script allowed to
     * read it.
     *
     * A client op's script does not run where it is dispatched: RS_CS2_RunScript
     * queues a task and the runner executes it during the frame's settle, some
     * way below the click. So the context cannot be scoped by a
     * begin/run/clear bracket around the call -- the clear would land before
     * the script ever started, and every context op would read -1.
     *
     * It is scoped by IDENTITY instead: whoever answers a context op checks
     * that the ROOT frame of the running script is this id. That is exact for
     * this family, because a client op's script is only ever run as a client
     * op -- clientscript 4762 ("Mark tile") and 6688 ("Tag") are named by
     * nothing else in the cache -- and it survives yields, nesting and the
     * task queue without either side having to know about them.
     */
    int script_id;
    /**
     * The npc's uid, as `_6751` reports it and as `highlight_npc_on` keys on.
     *
     * This client's uid IS the server slot. The reference's is not, but the
     * value never leaves the client: a script reads it from `_6751` and hands
     * it straight back to a HIGHLIGHT_NPC_* op, so the only requirement is
     * that this and the highlight resolver agree, and a server slot is the one
     * npc identity that is stable while it is on screen.
     */
    int uid;
    /** npc type / loc type / obj id. -1 for a tile and a player. */
    int type;
    /**
     * A loc's LAYER (0 wall / 1 wall decor / 2 ground / 3 ground decor); -1 for
     * every other kind.
     *
     * A tile can hold one loc per layer, so this is half of a loc's identity
     * and not a detail: the scripted-entity-overlay store keys on
     * (coord, layer), and a door and the ground decor under it would otherwise
     * be the same subject. See World_LocShapeToLayer.
     */
    int layer;
    /** Packed absolute coord, `plane << 28 | x << 14 | z`. -1 when absent. */
    int coord;
    /** The npc's, loc's, obj's or player's name; "" when absent. */
    char name[RS_CLIENTOP_NAME_MAX];
};

/*
 * The cache's MINIMENU_TYPE (7100) numbering.
 *
 * Read off clientscript 5350, which is the mouseover highlighter: it bails on
 * 0 and 1, and branches to the npc / loc / obj / player highlighters on 2, 3,
 * 4 and 6. 5 is not reached by anything this cache runs and is not guessed at.
 */
#define RS_MINIMENU_TYPE_NONE 0
#define RS_MINIMENU_TYPE_COMPONENT 1
#define RS_MINIMENU_TYPE_NPC 2
#define RS_MINIMENU_TYPE_LOC 3
#define RS_MINIMENU_TYPE_OBJ 4
#define RS_MINIMENU_TYPE_PLAYER 6

struct RS_ClientOpState
{
    struct RS_ClientOpSlot slot[RS_CLIENTOP_KIND_COUNT][RS_CLIENTOP_SLOT_MAX];
    struct RS_ClientOpContext ctx;
    /**
     * What the pointer is over, in the same shape as a dispatch context.
     *
     * The `_67xx / _68xx / _69xx` getters are the CURRENT TARGET, not only a
     * client op's subject. Clientscript 5350 -- the cache's own "Highlight
     * entities on mouse-over" (setting 190) -- reads `_6751` and `_6802` with
     * no client op anywhere in sight, having first asked `_7100` what kind of
     * thing the pointer is on. So when no dispatch is live these fall back to
     * here.
     *
     * Published by the App each frame from the pick set. `kind` is -1 when the
     * pointer is over nothing.
     */
    struct RS_ClientOpContext mouseover;
    /**
     * The subject an opcode NAMED, per kind.
     *
     * The reference keeps one "active npc / loc / obj / tile / player" register
     * on its ScriptRunner, written both by a client-op dispatch and by ops that
     * go looking -- `LOC_FIND` (6803) is the one this cache leans on: every
     * static-overlay script opens with it, and the OVERLAY_LOC_* ops that
     * follow are about whatever it found.
     *
     * Kept beside the dispatch context rather than merged into it because the
     * two are scoped differently: a dispatch is gated on which script is
     * asking (see RS_ClientOpContext::script_id), and this is not gated at all
     * -- a script that just called LOC_FIND is entitled to the answer.
     *
     * `kind` is -1 for a register nothing has written.
     */
    struct RS_ClientOpContext active[RS_CLIENTOP_KIND_COUNT];
    /** RS_MINIMENU_TYPE_*, as `_7100` reports it. */
    int mouseover_type;
    /**
     * The mouseover row's op text and target text, as `_7101` yields them, and
     * how many ops the target offers (`_7110`).
     *
     * Both come from the ACTING ROW of the menu the pointer would open -- the
     * same scratch menu the hover line is composed from (app_hover_text_update)
     * -- and not from the pickset, which answers a different question ("what is
     * under the pointer") and can name an entity no row is about.
     *
     * The op carries the WHOLE row, target included, and the target is empty.
     * This port composes a row as one string ("<op> @col@ <target>", built in
     * rs_minimenu_world.c) and keeps no boundary between the halves, so there
     * is nothing to split on that would not be a guess. Every consumer joins
     * them straight back up -- clientscript 4726 builds "<option> <target>" and
     * hands that to 4728, which sizes its tooltip box from it -- so one string
     * in the first slot reaches the script exactly as the reference's two do.
     *
     * Leaving the op empty was not a missing verb only, it was a WIDTH bug: the
     * tooltip string arrived as " Willow tree", and parawidth measures words, so
     * it never counts a leading space -- while the draw path of a one-line box
     * does not auto-wrap and advances over it. The box came out one space
     * narrower than the text inside it, with the padding all on the left.
     */
    char mouseover_op[RS_CLIENTOP_MENU_TEXT_MAX];
    char mouseover_target[RS_CLIENTOP_MENU_TEXT_MAX];
    int mouseover_opcount;
    /** Is the right-click menu open (`_7108`)? */
    bool menu_open;
};

/** Every slot empty, no dispatch in progress. */
void RS_ClientOpReset(struct RS_ClientOpState* state);

void RS_ClientOpSet(
    struct RS_ClientOpState* state,
    enum RS_ClientOpKind kind,
    int slot,
    char const* label,
    int script_id);

void RS_ClientOpDel(struct RS_ClientOpState* state, enum RS_ClientOpKind kind, int slot);

/** The installed op, or NULL for an empty slot or an id out of range. */
struct RS_ClientOpSlot const* RS_ClientOpGet(
    struct RS_ClientOpState const* state,
    enum RS_ClientOpKind kind,
    int slot);

/** Apply one CLIENTOP_* opcode. False for an opcode outside the family. */
bool RS_ClientOpApply(
    struct RS_ClientOpState* state,
    int opcode,
    bool is_set,
    int slot,
    char const* label,
    int script_id);

/**
 * Write the active-subject register for one kind, or clear it with a NULL
 * `ctx`. `ctx->kind` must be `kind`.
 */
void RS_ClientOpActiveSet(
    struct RS_ClientOpState* state,
    enum RS_ClientOpKind kind,
    struct RS_ClientOpContext const* ctx);

/**
 * Which subject the getters for `kind` should answer about right now: the
 * dispatch when one is running in `running_script_id`, else the register an
 * opcode set, else the mouseover. NULL when there is none of the three.
 */
struct RS_ClientOpContext const* RS_ClientOpSubject(
    struct RS_ClientOpState const* state,
    enum RS_ClientOpKind kind,
    int running_script_id);

/** Replace the mouseover snapshot. `ctx` may have kind -1 for "nothing". */
void RS_ClientOpMouseoverSet(
    struct RS_ClientOpState* state,
    struct RS_ClientOpContext const* ctx,
    int minimenu_type);

/** Begin a dispatch: `ctx` is copied and the context ops answer from it until
 *  RS_ClientOpContextEnd. */
void RS_ClientOpContextBegin(
    struct RS_ClientOpState* state,
    struct RS_ClientOpContext const* ctx);
void RS_ClientOpContextEnd(struct RS_ClientOpState* state);

/**
 * Answer one context opcode.
 *
 * Which STACK the answer belongs on is the opcode's, not the value's, so this
 * says which it filled rather than leaving the caller to re-derive it: a name
 * getter sets `*out_str` (to "" when there is no name, never to NULL), and
 * every other one sets `*out_int` and leaves `*out_str` NULL. Pushing an int
 * where the script's frame was sized for a string desynchronises the string
 * stack for the rest of the script, so this is not a detail either side may
 * guess at.
 *
 * `running_script_id` is the ROOT frame's script id -- see
 * RS_ClientOpContext::script_id for why the answer is gated on it and not on a
 * bracket around the dispatch. Pass -1 from a caller that has no script.
 *
 * Both out-parameters are written on every true return. False for an opcode
 * that is not a context getter.
 */
bool RS_ClientOpContextRead(
    struct RS_ClientOpState const* state,
    int opcode,
    int running_script_id,
    int* out_int,
    char const** out_str);

/** "npc", "loc", "obj", "player", "tile". Never NULL. */
char const* RS_ClientOpKindName(enum RS_ClientOpKind kind);

/* Same packing as CS2's `coord` (3308) and rs_highlight.h: absolute tiles. */
#define RS_CLIENTOP_COORD(plane, x, z) (((plane) << 28) | ((x) << 14) | (z))

#endif /* RS_CLIENTOP_H */
