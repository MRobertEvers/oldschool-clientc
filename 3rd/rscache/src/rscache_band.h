#ifndef RSCACHE_BAND_H
#define RSCACHE_BAND_H

/*
 * The server band: a record's fields that are not client cache opcodes, as an
 * opcode stream — `<opcode:u8> <payload>` pairs, terminated by 0 — encoded and
 * decoded from a field register (`rscache_register.h`) and nothing else.
 *
 * ## One codec, both ends
 *
 * cachepack writes the band and the game server reads it, and they used to do so
 * with two codecs that agreed only through tests: the writer emitted what the text
 * stated, the server's own encoder skipped any value equal to an engine default.
 * This is the one codec both use, with the client codecs' rule: presence is
 * recorded, never inferred from a value. Decode sets a field's bit for every
 * opcode it reads; encode writes exactly the fields whose bit is set.
 *
 * ## Knowledge-free
 *
 * The library knows no field. A record is an array of values indexed like the
 * register's band fields. What a field MEANS stays with its reader: the server
 * applies a decoded record to its own struct through a binding (field name ->
 * member) it builds at startup, RSCache_BandBindingApply, so nothing here names
 * `hitpoints` and nothing here includes the server's headers.
 */

#include "rscache_presence.h"
#include "rscache_register.h"

#include <stddef.h>
#include <stdint.h>

/** One element of a list tuple: `s` for a `string` element (owned by the
 *  record), `i` for every other type. */
struct RSCache_BandValue
{
    int32_t i;
    char* s;
};

/** A list field's tuples: `count` tuples of `arity` elements, row-major. */
struct RSCache_BandList
{
    int count;
    int arity;
    int capacity;
    struct RSCache_BandValue* items;
};

/** A decoded (or to-be-encoded) band record. Index i is the register's band
 *  field i (`reg->entries[i]`, i < `reg->band_count`): `values[i]` for an int
 *  field, `strings[i]` for a string field, `lists[i]` for a list field. The
 *  record owns its strings and lists (RSCache_BandRecordFree). */
struct RSCache_BandRecord
{
    struct RSCache_Presence present;
    int32_t values[RSCACHE_REGISTER_MAX];
    char* strings[RSCACHE_REGISTER_MAX];
    struct RSCache_BandList* lists[RSCACHE_REGISTER_MAX];
};

/** Initialise a record that owns nothing: no field stated. */
void
RSCache_BandRecordReset(struct RSCache_BandRecord* record);

/** Release a record's strings and lists and initialise it. Accepts NULL. */
void
RSCache_BandRecordFree(struct RSCache_BandRecord* record);

/** State string field `index` (copied). */
void
RSCache_BandRecordSetString(
    struct RSCache_BandRecord* record,
    int index,
    const char* value);

/** State list field `index` (with no tuples yet) and append one `arity`-element
 *  tuple to it; string elements are copied. `arity` must equal the field's
 *  `type_count`, and stays fixed for the list. Call with arity's tuple NULL and
 *  `arity` 0 to state the list with no tuples. */
void
RSCache_BandRecordAppendTuple(
    struct RSCache_BandRecord* record,
    int index,
    const struct RSCache_BandValue* tuple,
    int arity);

/** True if element `element` of list field `index` is a string. */
int
RSCache_BandElementIsString(
    const struct RSCache_Register* reg,
    int index,
    int element);

/** State band field `index` with `value`. */
void
RSCache_BandRecordSet(
    struct RSCache_BandRecord* record,
    int index,
    int32_t value);

/** The band index of the field named `name`, or -1 when it has no band home. */
int
RSCache_BandIndex(
    const struct RSCache_Register* reg,
    const char* name);

/** True if `value` fits band field `index`'s wire width. A writer must refuse a
 *  value that does not rather than truncate it into a different valid id. */
int
RSCache_BandFits(
    const struct RSCache_Register* reg,
    int index,
    int32_t value);

/** An upper bound on what RSCache_BandEncode writes for `record`. */
uint32_t
RSCache_BandEncodeBound(
    const struct RSCache_Register* reg,
    const struct RSCache_BandRecord* record);

/**
 * Write the stated fields, ascending by opcode, then the terminator. Every stated
 * value must fit its width (RSCache_BandFits) — an encoder handed one that does
 * not is a caller bug and asserts. Returns bytes written.
 */
uint32_t
RSCache_BandEncode(
    const struct RSCache_Register* reg,
    const struct RSCache_BandRecord* record,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Read a band stream into `record`, which must own nothing (fresh, Reset, or
 * Freed): it is initialised here. Returns the bytes consumed, or -1 at an
 * opcode the register does not declare (its payload width is unknown) or a
 * payload cut short. On -1 the record may hold what was read so far; Free it.
 */
int
RSCache_BandDecode(
    const struct RSCache_Register* reg,
    struct RSCache_BandRecord* record,
    const uint8_t* data,
    int size);

/* ---- binding a decoded record to a reader's own struct -------------------- */

/**
 * Where a band field lands in a reader's struct. An int field is stored into the
 * member at `offset`, `size` bytes (1, 2 or 4). A string or list field has no
 * single member to copy into, so its binding names `apply`, which receives the
 * decoded record and the field's band index and builds whatever the reader keeps
 * (a patrol route, a shop's stock table).
 */
struct RSCache_BandBinding
{
    const char* name;
    size_t offset;
    size_t size;
    void (*apply)(
        void* object,
        const struct RSCache_BandRecord* record,
        int index);
};

/**
 * Write every stated field of `record` that `bindings` names into `object`.
 * Returns how many were written. A band field with no binding is skipped —
 * the reader does not consume it — and a binding naming a field the register
 * does not declare in the band is a reader bug, reported by
 * RSCache_BandBindingCheck at startup rather than here.
 */
int
RSCache_BandBindingApply(
    const struct RSCache_Register* reg,
    const struct RSCache_BandBinding* bindings,
    int binding_count,
    const struct RSCache_BandRecord* record,
    void* object);

/** Report every binding whose field the register does not carry in the band, and
 *  every band field wider than its member. Returns the count of problems. */
int
RSCache_BandBindingCheck(
    const struct RSCache_Register* reg,
    const struct RSCache_BandBinding* bindings,
    int binding_count);

#endif
