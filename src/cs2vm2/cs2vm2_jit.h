#ifndef CS2VM2_JIT_H
#define CS2VM2_JIT_H

/*
 * THE LOAD-TIME JIT: the array the interpreter's loop runs for a script,
 * made when the script loads, with JIT-section instructions where it could
 * prove more than the plain array says. Design: docs/CS2_JIT.md.
 *
 * What it is, in one paragraph. A script keeps its opcodes (canonical
 * instructions, enum CS2_Op), operands, strings and switch tables exactly as
 * they were decoded and translated. The loop runs a second array, op_count + 1
 * instructions long (script->run_ops). CS2VM2_ScriptPrepare always makes one:
 * the opcodes, END_OF_SCRIPT at op_count, and RUN_HANDLER where a FAST case
 * cannot be trusted (CS2VM2_ScriptBuildRunOps). The jit makes the other, an
 * OVERLAY: the same, except that at a pc where a fused group of two to four
 * instructions STARTS it holds the fused instruction, a declared-local
 * push/pop whose bounds were proven at load is its _DECLARED form, and a
 * constant string push is its _PRESENT form. Inner pcs of a group keep their
 * own instruction, so a branch into the middle of a group lands on a valid
 * entry and no pc is ever rewritten. While an overlay is attached it is the
 * array the loop runs.
 *
 * WHO CALLS WHAT. The VM side (this header, cs2vm2_jit.c) is pure: it builds,
 * serializes, validates, copies and frees an overlay. It does no IO, reads no
 * environment, takes no clock and logs nothing; it never yields for the jit.
 * The host side (src/engine/cs2_jit_cache.*, the two pack tasks) decides
 * whether to build or to attach a cached blob, asks the platform for the pack
 * through the IO queue, and writes it back behind the frame. CS2VM2_Ask stays
 * the VM's only door to the host; nothing here goes through it.
 *
 * WHERE IT HANGS. struct CS2VM2_Script gains one field (cs2vm2_script.h):
 *
 *     struct CS2VM2_Jit* jit;   NULL: no overlay, the loop runs prepared_ops
 *
 * CS2VM2_ScriptInit leaves it NULL (the memset). CS2VM2_ScriptFree calls
 * CS2VM2_JitFree. CS2VM2_ScriptCopy copies it when the source has one
 * (`if( src->jit ) CS2VM2_JitCopy(src, dst);`): the overlay holds indices
 * only, never a pointer into the script, so a copy's overlay is valid for the
 * copy. Every block is ToriMem_Script().
 *
 * WHEN IT MAY CHANGE. Build, attach and free happen while NO frame references
 * the script: in the load task between decode and CacheProvider_ClientScriptAdd,
 * in a test before the first PushCallScript, or at teardown. The VM cannot
 * check that cheaply, so it is the caller's contract. The loop reads
 * `frame->script->run_ops` whenever it enters a frame.
 *
 * WHAT IT MUST NEVER CHANGE. Same host requests, in the same order, with the
 * same arguments; same errors at the same pc with the same last_error_*;
 * same cycle count (a fused group counts every instruction in it, and is not
 * entered where the cycle limit would fall inside it); same yields. Every
 * fused group checks, before it touches anything, the stack depth and the
 * cycle budget its unfused instructions would have needed, and otherwise
 * leaves the pc where it is for the handler of the instruction there to run
 * it alone -- the handler is the reference for every instruction.
 *
 * WHAT A BAD BLOB CAN DO. Nothing. CS2VM2_JitAttachBlob re-proves every byte
 * of the overlay against the script it is being attached to (the shape at that
 * pc, every local index below the script's declared count, every branch target
 * inside the script) before it allocates anything; the checksum and the
 * fingerprint only decide WHICH rejection a bad blob gets. No byte sequence
 * that passes can make the loop index outside the script's arrays or a
 * frame's locals.
 *
 * NUMBERS THAT ARE PART OF THE FORMAT. A blob stores one byte per pc: the
 * position of the overlay's instruction in the JIT section
 * (tools/cs2_gen_opcodes/jit_instructions.py), 0 standing for the pc's own
 * plain instruction (CS2VM2_ScriptBuildRunOps); never a CS2_OP_ value, whose
 * numbering changes when cs2_opcode.h is regenerated (the dialect tag folds
 * CS2_OP_NUMBERING_HASH in, so that makes a stored blob stale, not wrong).
 * The JIT section's order, the fusion rules, the blob layout,
 * CS2VM2_JitHash64 and the fingerprint's field order are all frozen under
 * CS2VM2_JIT_FORMAT_VERSION. Changing any of them bumps
 * it, which turns every cached blob into a miss (REJECT_VERSION) -- never a
 * misread. test-cs2-jit-blob pins the hash and the fingerprint with golden
 * values so the bump cannot be forgotten.
 *
 * Conventions (CLAUDE.md): a NULL pointer or a contract violation is an
 * assert, one per condition; a script the jit declines (CS2VM2_JitBuild's
 * refusals) and a blob that fails validation (CS2VM2_JitAttachBlob's
 * rejections) are legitimate runtime states and are returned, never asserted.
 */

#include "cs2vm2_dialect.h"
#include "cs2vm2_script.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bumped whenever a blob made by an older build could mean something else
 * to this one: the JIT section's order, the fusion rules, the blob layout, the
 * hash, the fingerprint's inputs, the dialect tag. 3: a byte is a position in
 * the JIT section (2 stored the loop's own code numbering). */
#define CS2VM2_JIT_FORMAT_VERSION 3u

/* The blob's first four bytes, 'C' 'J' 'I' 'T', read as a little-endian u32. */
#define CS2VM2_JIT_BLOB_MAGIC 0x54494A43u

/* The fixed header in front of a blob's per-pc bytes (layout in docs/CS2_JIT.md
 * "Blob"). A blob is exactly CS2VM2_JIT_BLOB_HEADER_SIZE + op_count bytes. */
#define CS2VM2_JIT_BLOB_HEADER_SIZE 48u

/* Seed of CS2VM2_JitFingerprint; also a part of the format. */
#define CS2VM2_JIT_FINGERPRINT_SEED 0x43533246505631ull /* "CS2FPV1" */

/* VM-owned. Its definition is in cs2vm2_jit_internal.h, which only the VM's
 * own files include; hosts hold a script, never one of these. */
struct CS2VM2_Jit;

/* Why CS2VM2_JitBuild gave a script no overlay. Not errors: the script runs
 * its prepared_ops. */
enum CS2VM2_JitBuildResult
{
    CS2VM2_JIT_BUILT = 0,
    /* op_count <= 0: nothing to run. */
    CS2VM2_JIT_REFUSED_EMPTY,
    /* A jump by operand (the FAST branches BRANCH, BRANCH_EQUALS ..
     * BRANCH_GREATER_THAN_OR_EQUALS, the long compares LONG_BRANCH_NOT ..
     * LONG_BRANCH_GREATER_THAN_OR_EQUALS, BRANCH_IF_ONE; the one list is
     * cs2vm2_op_jumps_by_operand), or a case of a SWITCH whose table
     * exists, targets a pc outside [0, op_count]. The overlay does not cover
     * such a script at all. */
    CS2VM2_JIT_REFUSED_BRANCH_RANGE,

    CS2VM2_JIT_BUILD_RESULT_COUNT
};

/* What CS2VM2_JitAttachBlob made of a blob. Everything but ATTACHED leaves
 * the script without an overlay and is, to the host, a cache miss: the host
 * builds one in memory instead and counts the reason. Checked in the order
 * docs/CS2_JIT.md "Validation" gives (TRUNCATED is tested twice: before the
 * header is read and again once op_count is known); the first failing check
 * names the result. */
enum CS2VM2_JitAttachResult
{
    CS2VM2_JIT_ATTACHED = 0,
    /* Shorter than the header, or shorter than the header plus op_count. */
    CS2VM2_JIT_REJECT_TRUNCATED,
    /* The first four bytes are not CS2VM2_JIT_BLOB_MAGIC. */
    CS2VM2_JIT_REJECT_MAGIC,
    /* Made under another CS2VM2_JIT_FORMAT_VERSION. */
    CS2VM2_JIT_REJECT_VERSION,
    /* Made for a script in another dialect (CS2VM2_JitDialectTag). */
    CS2VM2_JIT_REJECT_DIALECT,
    /* Made for another script id. */
    CS2VM2_JIT_REJECT_SCRIPT_ID,
    /* Made for another version of this script: the fingerprint, op_count or
     * local_int_count in the header differ from the script's. */
    CS2VM2_JIT_REJECT_STALE,
    /* The header's own size field, code length or total length disagree
     * with each other, or its flags are not 0 (v1 defines none); bytes after
     * the code count as this too. */
    CS2VM2_JIT_REJECT_SHAPE,
    /* The checksum over header and code does not match: the bytes changed
     * after they were written. */
    CS2VM2_JIT_REJECT_CHECKSUM,
    /* A byte is not one the builder could have placed at that pc for this
     * script (past the JIT section, wrong shape, a local at or above the
     * declared count, a branch out of range, a string push with no string).
     * The safety check; a blob with a valid checksum still goes through it. */
    CS2VM2_JIT_REJECT_ILLEGAL_CODE,
    /* The script itself is one CS2VM2_JitBuild refuses (empty, branch out
     * of range), so no blob can be valid for it. */
    CS2VM2_JIT_REJECT_UNJITTABLE,

    CS2VM2_JIT_ATTACH_RESULT_COUNT
};

/* A static description of one script's overlay, for the host's counters and
 * the cost bench. No clock in it: the host times the calls. */
struct CS2VM2_JitInfo
{
    int op_count;
    /* pcs whose instruction is GENERAL (or RUN_HANDLER): a handler runs them. */
    int general_pcs;
    /* pcs whose instruction is FAST: the loop's own case runs it. */
    int plain_pcs;
    /* pcs whose instruction is PUSH_INT_LOCAL_DECLARED or POP_INT_LOCAL_DECLARED. */
    int declared_local_pcs;
    /* pcs whose instruction is PUSH_CONSTANT_STRING_PRESENT. */
    int constant_string_pcs;
    /* pcs at which a fused group starts, and the instructions those groups
     * cover counted at their starting pc (a group of 4 adds 4). Inner pcs of
     * a group are counted again under whatever instruction they hold themselves. */
    int fused_group_pcs;
    int fused_group_ops;
    /* CS2VM2_JitBlobSize for this script. */
    size_t blob_size;
};

/*
 * The format's hash: 64 bits over `size` bytes, deterministic on every lane
 * (little-endian words, defined by docs/CS2_JIT.md "Hash"). Used for the
 * fingerprint, the blob checksum, and by the host for the pack index
 * checksum. Not cryptographic: it detects change, it does not resist an
 * adversary -- that is the structural check's job.
 *
 * `data` may be NULL only when `size` is 0.
 */
uint64_t
CS2VM2_JitHash64(
    void const* data,
    size_t size,
    uint64_t seed);

/*
 * The identity of one decoded script's content: every field of struct
 * CS2VM2_Script except script_id, dialect and jit -- the counts, the
 * signature, opcodes, int operands, long operands (and whether there are
 * any), string operands (each one's presence, length and bytes), and every
 * switch table's cases -- in the order docs/CS2_JIT.md "Fingerprint" gives.
 * Two scripts with the same fingerprint run the same overlay.
 *
 * One linear pass over the script. Asserts `script`; an empty script
 * (op_count 0) has a fingerprint like any other.
 */
uint64_t
CS2VM2_JitFingerprint(struct CS2VM2_Script const* script);

/*
 * A stable 32-bit name for a dialect, for blob headers and the pack name:
 * CS2VM2_JitHash64 over the dialect's name and its flags, folded to 32 bits.
 * NULL is the native dialect and answers what &CS2VM2_DIALECT_OSRS answers.
 * Not a contract violation: NULL is a documented sentinel on
 * CS2VM2_Script::dialect.
 */
uint32_t
CS2VM2_JitDialectTag(struct CS2VM2_Dialect const* dialect);

/*
 * Build the overlay for `script` and attach it (script->jit).
 *
 * One pass over op_count with a lookahead of three instructions, plus the
 * branch-range check; one ToriMem_Script allocation of 2 * (op_count + 1)
 * bytes and the header.
 * Does not compute the fingerprint (CS2VM2_JitSerialize is told it).
 *
 * Asserts `script`, that it has no overlay yet, and that its opcode and
 * int-operand arrays exist when op_count > 0. A refusal leaves script->jit
 * NULL and is returned; the script then runs its prepared_ops.
 */
enum CS2VM2_JitBuildResult
CS2VM2_JitBuild(struct CS2VM2_Script* script);

/*
 * How many bytes CS2VM2_JitSerialize writes for `script`:
 * CS2VM2_JIT_BLOB_HEADER_SIZE + op_count. Asserts `script` and script->jit.
 */
size_t
CS2VM2_JitBlobSize(struct CS2VM2_Script const* script);

/*
 * Write `script`'s overlay as a self-describing blob into `out`, which holds
 * exactly CS2VM2_JitBlobSize(script) bytes (asserted). `fingerprint` is
 * CS2VM2_JitFingerprint(script), which the caller computes once and also puts
 * in its pack index; a debug build asserts the two agree. Byte-for-byte
 * deterministic: the same script and overlay always give the same blob.
 *
 * Asserts `script`, script->jit and `out`.
 */
void
CS2VM2_JitSerialize(
    struct CS2VM2_Script const* script,
    uint64_t fingerprint,
    uint8_t* out,
    size_t out_size);

/*
 * Validate `blob` against `script` and, if it is good, attach the overlay it
 * carries. `fingerprint` is CS2VM2_JitFingerprint(script), computed by the
 * caller from THIS script (the host needed it to pick the pack entry). The
 * blob is only read; the overlay is copied out of it, so the caller may free
 * the blob as soon as this returns.
 *
 * The checks, in order, are the enum above; the last of them re-proves every
 * byte against the script (docs/CS2_JIT.md "Validation"), which is what
 * makes it impossible for any byte sequence to send the loop out of range. Cost: one pass over op_count, about what CS2VM2_JitBuild costs --
 * which is why the cache's worth has to be measured, not assumed.
 *
 * Asserts `script`, `blob`, and that the script has no overlay yet. A
 * zero-length blob is a legitimate input and is REJECT_TRUNCATED. Anything
 * but ATTACHED leaves script->jit NULL.
 */
enum CS2VM2_JitAttachResult
CS2VM2_JitAttachBlob(
    struct CS2VM2_Script* script,
    uint64_t fingerprint,
    uint8_t const* blob,
    size_t blob_size);

/*
 * Give `dst` a copy of `src`'s overlay (CS2VM2_ScriptCopy's half of the job).
 * Asserts both scripts, that `src` has an overlay, that `dst` has none, and
 * that the two have the same op_count. "Does the source have one?" is the
 * caller's test.
 */
void
CS2VM2_JitCopy(
    struct CS2VM2_Script const* src,
    struct CS2VM2_Script* dst);

/*
 * Free `script`'s overlay, set script->jit to NULL and point run_ops back at
 * prepared_ops. A deallocator: a script with no overlay is a no-op. Asserts
 * `script` itself.
 *
 * Debug builds also re-hash the script's constant strings here and assert
 * they still match what CS2VM2_JitBuild / AttachBlob saw: constants are
 * pushed by pointer, so a host or an opcode that wrote through one would have
 * changed the script for every later run (docs/CS2_JIT.md "Constant strings").
 */
void
CS2VM2_JitFree(struct CS2VM2_Script* script);

/*
 * The array a script runs when nothing more is proven about it, into
 * out[0 .. op_count], which holds op_count + 1: each pc's own instruction,
 * except RUN_HANDLER for a FAST int branch whose target is outside the
 * script (the loop runs a FAST branch with no range test), and END_OF_SCRIPT
 * at op_count. CS2VM2_ScriptPrepare makes prepared_ops with it; an overlay
 * holds the same instruction at every pc it proved nothing more about.
 */
void
CS2VM2_ScriptBuildRunOps(
    struct CS2VM2_Script const* script,
    uint16_t* out);

/* Describe `script`'s overlay. Asserts `script`, script->jit and `out`. */
void
CS2VM2_JitGetInfo(
    struct CS2VM2_Script const* script,
    struct CS2VM2_JitInfo* out);

/* Short fixed names for the counters and the report line ("built",
 * "refused_dialect", "attached", "reject_stale", ...). Assert the value is in
 * range. */
char const*
CS2VM2_JitBuildResultName(enum CS2VM2_JitBuildResult result);

char const*
CS2VM2_JitAttachResultName(enum CS2VM2_JitAttachResult result);

#endif /* CS2VM2_JIT_H */
