#ifndef SRC_SERVERSCRIPT_SSVM_PROVIDER_H
#define SRC_SERVERSCRIPT_SSVM_PROVIDER_H

/*
 * The compiled-script container: script.dat + script.idx, plus the two indices
 * every lookup goes through.
 *
 *   script.idx   i32 entry_count; i32 size[i]   (0 = no script at that id)
 *   script.dat   i32 entry_count; i32 compiler_version; bodies back-to-back
 *
 * The .dat has no per-entry offsets — bodies are concatenated and the .idx
 * sizes are the only way to find each one. So a single wrong size silently
 * desynchronises every script after it, and the load asserts that the running
 * offset ends exactly at the end of the file. That one check catches the whole
 * failure class.
 *
 * Lookup mirrors the reference: by trigger with a type -> category -> global
 * fallback, or by the script's full "[trigger,subject]" name. Name-addressed
 * scripts (proc, label, queue, timer, softtimer, walktrigger) carry lookup_key
 * -1 and are resolved by id at the call site, so they are deliberately absent
 * from the trigger index.
 */

#include "ssvm_script.h"

#include <stddef.h>
#include <stdint.h>

/** The only compiler output this reader understands. LostCity bumps it on any
 *  format change, so a mismatch is a hard error rather than a best effort. */
#define SSVM_COMPILER_VERSION 27

struct SSVM_NameEntry
{
    uint64_t hash;
    int32_t id;
};

struct SSVM_KeyEntry
{
    uint64_t key;
    int32_t id;
};

struct SSVM_Provider
{
    /** [count]. Absent slots are zeroed; `op_count == 0` marks them. */
    struct SSVM_Script* scripts;
    int32_t count;
    /** Slots that actually held a script. */
    int32_t loaded;
    int32_t compiler_version;

    /** Sorted for binary search. A hash collision falls back to strcmp, so the
     *  index is exact rather than probabilistic. */
    struct SSVM_NameEntry* by_name;
    int32_t by_name_count;

    /** Sorted by the 64-bit lookup key. */
    struct SSVM_KeyEntry* by_key;
    int32_t by_key_count;
    /** Scripts whose key was already taken. Non-zero means content declared the
     *  same trigger+subject twice; the first one wins. */
    int32_t duplicate_keys;
};

/** Loads `<dir>/script.dat` and `<dir>/script.idx`. */
int
SSVM_ProviderLoadDir(
    struct SSVM_Provider* provider,
    const char* dir,
    struct SSVM_Error* err);

/** Both buffers are copied out of, not retained. */
int
SSVM_ProviderLoadMem(
    struct SSVM_Provider* provider,
    const uint8_t* dat,
    size_t dat_length,
    const uint8_t* idx,
    size_t idx_length,
    struct SSVM_Error* err);

void
SSVM_ProviderFree(struct SSVM_Provider* provider);

/** NULL for an out-of-range id or an absent slot. */
const struct SSVM_Script*
SSVM_ProviderGet(
    const struct SSVM_Provider* provider,
    int32_t id);

/** `name` includes the brackets: "[proc,playerwalk3]". */
const struct SSVM_Script*
SSVM_ProviderGetByName(
    const struct SSVM_Provider* provider,
    const char* name);

/**
 * Resolve a trigger, most specific first: exact type, then category, then the
 * global form. Pass -1 for a subject that does not apply.
 */
const struct SSVM_Script*
SSVM_ProviderGetByTrigger(
    const struct SSVM_Provider* provider,
    int trigger,
    int32_t type,
    int32_t category);

/** One exact lookup, no fallback: type if given, else category, else global. */
const struct SSVM_Script*
SSVM_ProviderGetByTriggerSpecific(
    const struct SSVM_Provider* provider,
    int trigger,
    int32_t type,
    int32_t category);

#endif
