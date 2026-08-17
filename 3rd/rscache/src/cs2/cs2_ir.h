/*
 * The decompiler's intermediate representation.
 *
 * Ported from RuneStar/cs2 `ir/` — Expression, Element, Variable, Instruction,
 * Function, Typing, Typings. Three deliberate departures from the Kotlin, all
 * forced by the absence of a garbage collector and of value-equality classes:
 *
 * 1. Every node lives in one arena (cs2_support.h) and nothing owns anything
 *    else, matching the reference's free-form graph rewiring.
 *
 * 2. Kotlin `data class` keys compare by value. `Variable` is one, so a varp
 *    with id 7 built in two places is one map key there and would be two
 *    pointers here. Variables are therefore *interned* per function set, which
 *    restores value equality as pointer equality. `Prototype` is the same case
 *    and is handled by being a closed enum (cs2_types.h) instead.
 *
 * 3. `Instruction.Label` is also a data class, and the reference exploits that:
 *    a branch holds a *separate* Label instance that its HashChain resolves to
 *    the one actually in the chain. Here the label pointer is resolved once,
 *    when labels are inserted, so a branch points at the real chain node.
 */
#ifndef RSCACHE_CS2_IR_H
#define RSCACHE_CS2_IR_H

#include "cs2_command.h"
#include "cs2_support.h"
#include "cs2_types.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_CS2_FunctionSet;

/* -------------------------------------------------------------------------
 * Values
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_Value
{
    enum RSCache_CS2_StackType stack_type;
    int int_value;
    /* Arena-owned; NUL-terminated windows-1252 bytes, exactly as the cache
     * stores them. */
    const char* string_value;
};

/* -------------------------------------------------------------------------
 * Variables
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_VarKind
{
    RSCACHE_CS2_VAR_VARP = 0,
    RSCACHE_CS2_VAR_VARBIT,
    RSCACHE_CS2_VAR_VARCINT,
    RSCACHE_CS2_VAR_VARCSTRING,
    RSCACHE_CS2_VAR_VARCLANSETTING,
    RSCACHE_CS2_VAR_VARCLAN,
    /* Locals, distinguished from globals by carrying a script id. */
    RSCACHE_CS2_VAR_STACKINT,
    RSCACHE_CS2_VAR_STACKSTRING,
    RSCACHE_CS2_VAR_INT,
    RSCACHE_CS2_VAR_STRING,
    RSCACHE_CS2_VAR_ARRAY,

    RSCACHE_CS2_VAR_KIND_COUNT,
};

struct RSCache_CS2_Variable
{
    enum RSCache_CS2_VarKind kind;
    /* 0 for globals. */
    int script;
    int id;
};

bool
RSCache_CS2_VarIsGlobal(enum RSCache_CS2_VarKind kind);

bool
RSCache_CS2_VarIsLocal(enum RSCache_CS2_VarKind kind);

/** True for the synthetic operand-stack slots the interpreter invents. */
bool
RSCache_CS2_VarIsStack(enum RSCache_CS2_VarKind kind);

enum RSCache_CS2_StackType
RSCache_CS2_VarStackType(enum RSCache_CS2_VarKind kind);

/* -------------------------------------------------------------------------
 * Event properties
 *
 * Ten magic operand values that a hook argument may carry instead of a real
 * value; the client substitutes the live mouse position, pressed key and so on
 * when the hook fires. They are recognised by their magic constant, so the
 * decompiler prints `event_mousex` where the bytecode holds Int.MIN_VALUE + 1.
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_EventProperty
{
    RSCACHE_CS2_EVENT_OPBASE = 0,
    RSCACHE_CS2_EVENT_MOUSEX,
    RSCACHE_CS2_EVENT_MOUSEY,
    RSCACHE_CS2_EVENT_COM,
    RSCACHE_CS2_EVENT_OPINDEX,
    RSCACHE_CS2_EVENT_COMSUBID,
    RSCACHE_CS2_EVENT_DROP,
    RSCACHE_CS2_EVENT_DROPSUBID,
    RSCACHE_CS2_EVENT_KEY,
    RSCACHE_CS2_EVENT_KEYCHAR,

    RSCACHE_CS2_EVENT_COUNT,
    RSCACHE_CS2_EVENT_NONE = -1,
};

/** Recognise a hook argument's magic value. RSCACHE_CS2_EVENT_NONE if ordinary. */
enum RSCache_CS2_EventProperty
RSCache_CS2_EventPropertyOf(const struct RSCache_CS2_Value* value);

const char*
RSCache_CS2_EventPropertyLiteral(enum RSCache_CS2_EventProperty property);

/**
 * The constant a property is spelled as in the bytecode — the inverse of
 * RSCache_CS2_EventPropertyOf. `event_opbase` is a magic *string* and the other
 * nine are magic ints, which is why this answers with a Value rather than an
 * int. False for a property outside the table.
 */
bool
RSCache_CS2_EventPropertyMagic(
    enum RSCache_CS2_EventProperty property,
    struct RSCache_CS2_Value* out);

enum RSCache_CS2_ProtoId
RSCache_CS2_EventPropertyProto(enum RSCache_CS2_EventProperty property);

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_ExprKind
{
    /* Elements: exactly one stack slot each. */
    RSCACHE_CS2_EXPR_CONSTANT = 0,
    RSCACHE_CS2_EXPR_ACCESS,
    RSCACHE_CS2_EXPR_POINTER,
    RSCACHE_CS2_EXPR_EVENT_PROPERTY,
    /* Composites. */
    RSCACHE_CS2_EXPR_COMPOUND,
    RSCACHE_CS2_EXPR_OPERATION,
    RSCACHE_CS2_EXPR_PROC,
    RSCACHE_CS2_EXPR_CLIENTSCRIPT,
};

struct RSCache_CS2_Expr
{
    enum RSCache_CS2_ExprKind kind;

    /* CONSTANT */
    struct RSCache_CS2_Value value;

    /* ACCESS, POINTER */
    struct RSCache_CS2_Variable* variable;
    /**
     * POINTER, array arguments only: the expression this pointer replaced.
     *
     * `cs2_find_array_args_calls` rewrites a proc call's first argument into a
     * pointer at array 0 so the source reads `~sort($intarray0, ...)`. What the
     * bytecode actually pushed was the *caller's* handle — an ordinary
     * `push_string_local` of whichever slot the caller defined its array in,
     * which is not usually 0. Keeping it means a lowered call still passes the
     * caller's array rather than confidently passing the wrong one.
     */
    struct RSCache_CS2_Expr* pointer_source;
    /** ACCESS only: whether the slot's value was known when it was pushed. */
    bool has_value;

    /* EVENT_PROPERTY */
    enum RSCache_CS2_EventProperty event_property;

    /* COMPOUND — never holds a nested compound, and never holds exactly one
     * child (a one-element list *is* that element, per Expression(list)). */
    struct RSCache_CS2_Vec children;

    /* OPERATION, PROC, CLIENTSCRIPT */
    int opcode;
    int script_id;
    struct RSCache_CS2_Expr* arguments;
    /** CLIENTSCRIPT: the varp/stat/inv ids the hook re-fires on. */
    struct RSCache_CS2_Expr* triggers;
    /** CLIENTSCRIPT: target component, NULL for the cc_* (active) forms. */
    struct RSCache_CS2_Expr* component;
    bool dot;
    /**
     * CLIENTSCRIPT: the argument descriptor exactly as the bytecode spelled it.
     *
     * Kept because it cannot be rebuilt. The descriptor names each argument's
     * *type* — `I` for a component, `i` for a plain int — and both live on the
     * int stack, so regenerating it from the arguments' stack types produces a
     * different string and therefore different bytes. Arena-owned; NULL on a
     * node the interpreter did not build.
     */
    const char* hook_descriptor;

    /* OPERATION, PROC: what the command leaves on the stack. */
    enum RSCache_CS2_StackType* stack_types;
    int stack_type_count;

    /**
     * OPERATION: the instruction's int operand, exactly as decoded.
     *
     * For most commands this is the active-form flag `dot` already records, and
     * for the rest it is zero — but not for all of them. Opcodes 4123 and 4124
     * take no arguments and carry a *selector* in the operand, and the decoder
     * reads neither into the IR, so a lowering that assumed "operand is the dot
     * flag" rebuilt 22 scripts of cache.osrs239 as a different program.
     *
     * `has_raw_operand` distinguishes "decoded, and it was zero" from "this node
     * was built by a pass and has no instruction behind it".
     */
    int raw_operand;
    bool has_raw_operand;
};

bool
RSCache_CS2_ExprIsElement(const struct RSCache_CS2_Expr* expr);

/**
 * Flatten to the list the reference's `asList` yields: a compound's children,
 * or a one-element view of anything else. `out_items` points into the
 * expression, or at `*single_slot` for the non-compound case.
 */
void
RSCache_CS2_ExprAsList(
    struct RSCache_CS2_Expr* expr,
    struct RSCache_CS2_Expr*** out_items,
    int* out_count,
    struct RSCache_CS2_Expr** single_slot);

int
RSCache_CS2_ExprStackTypeCount(const struct RSCache_CS2_Expr* expr);

enum RSCache_CS2_StackType
RSCache_CS2_ExprStackTypeAt(const struct RSCache_CS2_Expr* expr, int index);

/* -------------------------------------------------------------------------
 * Instructions
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_InsnKind
{
    RSCACHE_CS2_INSN_ASSIGNMENT = 0,
    RSCACHE_CS2_INSN_RETURN,
    RSCACHE_CS2_INSN_BRANCH,
    RSCACHE_CS2_INSN_SWITCH,
    RSCACHE_CS2_INSN_LABEL,
    RSCACHE_CS2_INSN_GOTO,
};

struct RSCache_CS2_Insn
{
    enum RSCache_CS2_InsnKind kind;

    struct RSCache_CS2_Insn* prev;
    struct RSCache_CS2_Insn* next;
    bool linked;

    /** ASSIGNMENT: the slots written. An empty compound for a bare statement. */
    struct RSCache_CS2_Expr* definitions;
    /** ASSIGNMENT, RETURN, BRANCH, SWITCH. */
    struct RSCache_CS2_Expr* expression;

    /** BRANCH: the label taken when the test passes. */
    struct RSCache_CS2_Insn* pass;
    /** GOTO: destination. */
    struct RSCache_CS2_Insn* label;
    /** LABEL: the instruction index it marks. */
    int label_id;

    /* SWITCH */
    int* case_keys;
    struct RSCache_CS2_Insn** case_labels;
    int case_count;

    /**
     * RETURN: an unreachable `goto` followed this return in the bytecode.
     *
     * Set by `cs2_remove_dead_code` as it deletes that goto, because it is the
     * only evidence of a distinction the source has to keep and the CFG cannot
     * see. `if (a) { return; } else if (b) { … }` and
     * `if (a) { return; } if (b) { … }` reconstruct from identical reachable
     * graphs; what tells them apart is that the first compiles a jump past the
     * else and the second does not, and that jump is unreachable, so the
     * dead-code pass removes it before the flow graph is built.
     *
     * Both spellings run the same. They compile to different bytes, and the
     * cache holds one of them.
     */
    bool dead_goto_follows;
    /**
     * RETURN: where that deleted `goto` pointed.
     *
     * `dead_goto_follows` is enough to reconstruct the *source*, which is all
     * the decompiler needed. Re-emitting the instruction needs its target too,
     * so `cs2_remove_dead_code` records it on the way past. Unreachable either
     * way — this exists so a lowered script can be compared byte for byte with
     * the one it came from.
     */
    struct RSCache_CS2_Insn* dead_goto_target;

    /**
     * RETURN: this is the script's closing return — the last instruction the
     * bytecode held.
     *
     * Marked at decode because after the fact it is not recognisable. A script
     * whose source ends in an explicit `return(0)` compiles two identical
     * `push 0; return` pairs, and if the second is unreachable the dead-code
     * pass deletes it and leaves a chain that *ends* in something that looks
     * exactly like an epilogue but is not the one. Comparing the tail said the
     * epilogue was still there for 196 scripts of cache.osrs239 that had lost
     * it, so the flag replaces the comparison.
     */
    bool is_epilogue_return;

    /* Set while decoding, before labels exist; resolved to pointers by
     * cs2_interp's label pass and meaningless afterwards. */
    int target_id;
    int* case_target_ids;
};

/**
 * A doubly-linked instruction list.
 *
 * The reference's HashChain also indexes elements so it can look up by value;
 * that indirection exists only to make its value-equal Label class usable as a
 * position, which resolving label pointers up front removes the need for.
 */
struct RSCache_CS2_Chain
{
    struct RSCache_CS2_Insn* first;
    struct RSCache_CS2_Insn* last;
    int count;
};

void
RSCache_CS2_ChainInit(struct RSCache_CS2_Chain* chain);

void
RSCache_CS2_ChainAddLast(struct RSCache_CS2_Chain* chain, struct RSCache_CS2_Insn* insn);

void
RSCache_CS2_ChainInsertBefore(
    struct RSCache_CS2_Chain* chain,
    struct RSCache_CS2_Insn* insn,
    struct RSCache_CS2_Insn* point);

void
RSCache_CS2_ChainInsertAfter(
    struct RSCache_CS2_Chain* chain,
    struct RSCache_CS2_Insn* insn,
    struct RSCache_CS2_Insn* point);

void
RSCache_CS2_ChainRemove(struct RSCache_CS2_Chain* chain, struct RSCache_CS2_Insn* insn);

/* -------------------------------------------------------------------------
 * Typings
 *
 * A typing is one inference variable: a stack slot's type and identifier, plus
 * edges to the typings it flows from, flows to, and is compared against. The
 * solver (cs2_dfa.c) walks those edges to a fixpoint.
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_Typing
{
    enum RSCache_CS2_StackType stack_type;
    /* RSCACHE_CS2_TYPE_NONE / NULL until solved. */
    enum RSCache_CS2_Type type;
    const char* identifier;
    bool freeze_type;
    bool freeze_identifier;

    /* Sets of struct RSCache_CS2_Typing*. */
    struct RSCache_CS2_Vec from;
    struct RSCache_CS2_Vec to;
    struct RSCache_CS2_Vec compare;
};

struct RSCache_CS2_TypingList
{
    struct RSCache_CS2_Typing** items;
    int count;
};

struct RSCache_CS2_Typings
{
    struct RSCache_CS2_Arena* arena;

    struct RSCache_CS2_Typing* event_properties[RSCACHE_CS2_EVENT_COUNT];
    struct RSCache_CS2_Typing* prototypes[RSCACHE_CS2_PROTO_COUNT_];

    /* Expr* (CONSTANT) -> Typing* */
    struct RSCache_CS2_Map constants;
    /* Variable* -> Typing* */
    struct RSCache_CS2_Map variables;
    /* Expr* (OPERATION) -> TypingList* */
    struct RSCache_CS2_Map operations;
    /* script id -> TypingList* */
    struct RSCache_CS2_IntMap returns;
    struct RSCache_CS2_IntMap args;

    /**
     * Every live typing, in creation order — the reference rebuilds this by
     * concatenating its maps, whose iteration order is Java's and not
     * reproducible. Kept as an explicit list so the solver's traversal order is
     * deterministic; the passes are written to converge to the same fixpoint
     * regardless, which the gold-corpus comparison is what actually checks.
     */
    struct RSCache_CS2_Vec all;
};

void
RSCache_CS2_TypingsInit(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Arena* arena);

void
RSCache_CS2_TypingsFree(struct RSCache_CS2_Typings* typings);

struct RSCache_CS2_Typing*
RSCache_CS2_TypingNew(struct RSCache_CS2_Typings* typings, enum RSCache_CS2_StackType stack_type);

/**
 * Pin a typing's type.
 *
 * Returns false when the type disagrees with the slot's stack type — an `int`
 * pinned onto a string slot, say. That means a command signature and the
 * bytecode describe different things, which is not recoverable, but it is
 * reported rather than asserted: a corpus contains scripts that reach it, and
 * one script's inconsistency should not take down a batch run.
 */
bool
RSCache_CS2_TypingFreezeType(struct RSCache_CS2_Typing* typing, enum RSCache_CS2_Type type);

void
RSCache_CS2_TypingFreezeIdentifier(struct RSCache_CS2_Typing* typing, const char* identifier);

/** As RSCache_CS2_TypingFreezeType, for a prototype's type and identifier. */
bool
RSCache_CS2_TypingFreezeProto(struct RSCache_CS2_Typing* typing, enum RSCache_CS2_ProtoId proto);

/**
 * Record that `from`'s value reaches `to`.
 *
 * Returns false when the two disagree about their stack type. That is not a
 * recoverable condition — it means an arity or a signature is wrong further up
 * — but it is reported rather than asserted, because the reference throws here
 * too and a corpus contains scripts that hit it. The caller turns it into a
 * failed decompile of that one script.
 */
bool
RSCache_CS2_TypingAssign(struct RSCache_CS2_Typing* from, struct RSCache_CS2_Typing* to);

/** False if the lists differ in length, or any pair disagrees. */
bool
RSCache_CS2_TypingAssignLists(
    const struct RSCache_CS2_TypingList* from,
    const struct RSCache_CS2_TypingList* to);

/** Record that the two are compared, so they must agree without either flowing. */
bool
RSCache_CS2_TypingCompare(struct RSCache_CS2_Typing* a, struct RSCache_CS2_Typing* b);

/** Unlink a typing from its neighbours. */
void
RSCache_CS2_TypingUnlink(struct RSCache_CS2_Typing* typing);

/**
 * Redirect every edge of `typing` onto `by`, then drop `typing`.
 *
 * False when `typing` has already been solved or the two disagree about their
 * stack type — replacing then would discard a decision rather than move it.
 */
bool
RSCache_CS2_TypingReplace(struct RSCache_CS2_Typing* typing, struct RSCache_CS2_Typing* by);

bool
RSCache_CS2_TypingReplaceLists(
    const struct RSCache_CS2_TypingList* typings,
    const struct RSCache_CS2_TypingList* by);

/* Lookups, each creating the typing on first use. */

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable);

/**
 * Look up an array variable's element typing, creating it with the supplied
 * stack bank on first use. Array variables represent their elements in the IR,
 * so their bank cannot be derived from RSCACHE_CS2_VAR_ARRAY alone.
 */
struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfArray(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable,
    enum RSCache_CS2_StackType element_stack);

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfProto(struct RSCache_CS2_Typings* typings, enum RSCache_CS2_ProtoId proto);

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfElement(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Expr* element);

/** Flattened typings of an arbitrary expression, in stack order. */
struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsOfExpr(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Expr* expr);

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsOfProtos(
    struct RSCache_CS2_Typings* typings,
    const enum RSCache_CS2_ProtoId* protos,
    int count);

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsReturns(
    struct RSCache_CS2_Typings* typings,
    int script_id,
    const enum RSCache_CS2_StackType* stack_types,
    int count);

/**
 * Typings of a script's arguments, allocating the argument variables on first
 * use. Int and string arguments are numbered independently, which is why the
 * caller passes the stack-type sequence rather than a count.
 */
struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsArgs(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_FunctionSet* fs,
    int script_id,
    const enum RSCache_CS2_StackType* stack_types,
    int count);

void
RSCache_CS2_TypingsRemoveVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable);

void
RSCache_CS2_TypingsRemoveConstant(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Expr* constant);

/** Drop the variable's typing without unlinking it — the reference's bare
 *  `typings.variables.remove(v)`, used where the edges must survive. */
void
RSCache_CS2_TypingsForgetVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable);

/* -------------------------------------------------------------------------
 * Functions
 * ---------------------------------------------------------------------- */

/**
 * One instruction of a script's value-producing epilogue.
 *
 * The epilogue is the run of pushes immediately before a script's closing
 * `return`, and it is not decoration: `RSCache_CS2_ScriptReturnTypes` reads it
 * to learn what the script leaves on the stack, which is how every *caller*
 * gets typed. It is also almost always unreachable, so the dead-code pass
 * deletes it — and a script lowered without it declares no return values and
 * silently mistypes everything that calls it.
 *
 * Kept verbatim rather than as types plus defaults, because a slot may be
 * produced by a zero-argument command (rev 239's opcode 63 is the empty-string
 * one) and not by a literal at all.
 */
struct RSCache_CS2_EpilogueOp
{
    int opcode;
    int operand;
    /** PUSH_CONSTANT_STRING only; arena-owned windows-1252 bytes. */
    const char* text;
};

struct RSCache_CS2_Function
{
    int id;
    /* Variable*, in declaration order. */
    struct RSCache_CS2_Vec arguments;
    struct RSCache_CS2_Chain instructions;
    enum RSCache_CS2_StackType* return_types;
    int return_type_count;
    /* Serialized facts which structured source cannot otherwise retain. */
    int original_local_int_count;
    int original_local_string_count;
    int original_int_argument_count;
    int original_string_argument_count;
    /** Generator may emit fingerprinted/serialized lossless comment metadata. */
    bool generate_lossless_metadata;
    bool preserve_frame_counts;
    int* return_default_values;
    bool* return_default_is_int_constant;
    /** The closing epilogue, verbatim; see RSCache_CS2_EpilogueOp. */
    struct RSCache_CS2_EpilogueOp* epilogue;
    int epilogue_count;
};

struct RSCache_CS2_FunctionSet
{
    struct RSCache_CS2_Arena arena;
    /* script id -> Function* */
    struct RSCache_CS2_IntMap functions;
    /* Decompilation order, so output is stable. */
    struct RSCache_CS2_IntVec function_ids;
    struct RSCache_CS2_Typings typings;
    /**
     * How each callee was reached — a proc call or a hook registration. Used to
     * name a script whose own name the cache does not carry.
     */
    struct RSCache_CS2_IntMap call_triggers;

    /* Variable interning; see the header comment. */
    struct RSCache_CS2_Vec interned_variables;
    struct RSCache_CS2_Map variable_index;
};

void
RSCache_CS2_FunctionSetInit(struct RSCache_CS2_FunctionSet* fs);

void
RSCache_CS2_FunctionSetFree(struct RSCache_CS2_FunctionSet* fs);

struct RSCache_CS2_Function*
RSCache_CS2_FunctionSetGet(struct RSCache_CS2_FunctionSet* fs, int script_id);

/** Interned: two calls with the same triple return the same pointer. */
struct RSCache_CS2_Variable*
RSCache_CS2_VarIntern(
    struct RSCache_CS2_FunctionSet* fs,
    enum RSCache_CS2_VarKind kind,
    int script,
    int id);

/* -------------------------------------------------------------------------
 * Expression constructors
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_Expr*
RSCache_CS2_ExprConstantInt(struct RSCache_CS2_Arena* arena, int value);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprConstantString(struct RSCache_CS2_Arena* arena, const char* value);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprAccess(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Variable* variable,
    const struct RSCache_CS2_Value* value);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprPointer(struct RSCache_CS2_Arena* arena, struct RSCache_CS2_Variable* variable);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprEventProperty(
    struct RSCache_CS2_Arena* arena,
    enum RSCache_CS2_EventProperty property);

/** Empty compound — the reference's `Expression()`. */
struct RSCache_CS2_Expr*
RSCache_CS2_ExprEmpty(struct RSCache_CS2_Arena* arena);

/**
 * Build from a list, applying the reference's two invariants: a single item is
 * returned as itself, and nested compounds are flattened.
 */
struct RSCache_CS2_Expr*
RSCache_CS2_ExprFromList(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* const* items,
    int count);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprOperation(
    struct RSCache_CS2_Arena* arena,
    const enum RSCache_CS2_StackType* stack_types,
    int stack_type_count,
    int opcode,
    struct RSCache_CS2_Expr* arguments,
    bool dot);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprProc(
    struct RSCache_CS2_Arena* arena,
    const enum RSCache_CS2_StackType* stack_types,
    int stack_type_count,
    int script_id,
    struct RSCache_CS2_Expr* arguments);

struct RSCache_CS2_Expr*
RSCache_CS2_ExprClientScript(
    struct RSCache_CS2_Arena* arena,
    int opcode,
    int script_id,
    struct RSCache_CS2_Expr* arguments,
    struct RSCache_CS2_Expr* triggers,
    bool dot,
    struct RSCache_CS2_Expr* component,
    const char* hook_descriptor);

/* -------------------------------------------------------------------------
 * Instruction constructors
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_Insn*
RSCache_CS2_InsnAssignment(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* definitions,
    struct RSCache_CS2_Expr* expression);

struct RSCache_CS2_Insn*
RSCache_CS2_InsnReturn(struct RSCache_CS2_Arena* arena, struct RSCache_CS2_Expr* expression);

struct RSCache_CS2_Insn*
RSCache_CS2_InsnBranch(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* expression,
    int target_id);

struct RSCache_CS2_Insn*
RSCache_CS2_InsnSwitch(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* expression,
    const int* keys,
    const int* target_ids,
    int count);

struct RSCache_CS2_Insn*
RSCache_CS2_InsnLabel(struct RSCache_CS2_Arena* arena, int label_id);

struct RSCache_CS2_Insn*
RSCache_CS2_InsnGoto(struct RSCache_CS2_Arena* arena, int target_id);

/** True for the instruction kinds that carry an `expression` field. */
bool
RSCache_CS2_InsnIsEvaluation(const struct RSCache_CS2_Insn* insn);

#endif
