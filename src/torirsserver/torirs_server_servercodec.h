#ifndef TORIRSSERVER_SERVERCODEC_H
#define TORIRSSERVER_SERVERCODEC_H

/*
 * The server band of a config record: which struct member each band field lands in.
 *
 * ## What the band is for
 *
 * A cache npc record says what the *client* needs — models, name, size,
 * animations. It says nothing about hitpoints, respawn rate or hunt mode,
 * because no client opcode exists for them. A cache loc record says what a door
 * looks like and nothing about which loc it opens into. Those reached the server
 * by being re-parsed out of text at every boot, which is the problem
 * `docs/CONTENT_ARCHITECTURE.md` §3.5 describes: two tools write a derived cache
 * and they do not compose.
 *
 * `cachepack` writes a client cache and a *server pack*; the server reads both.
 * The client record seeds every field it knows, then the band overrides each field
 * it STATES — which is what makes "absent" and "present and zero" different
 * states, the distinction a text overlay cannot express.
 *
 * ## Whose code is what
 *
 * The bytes are not this file's. The band codec is the rscache library's
 * (`rscache_band.h`), driven by the field register (`rscache_register.h`, the
 * tree's `fields/<type>.ini`): the register says which opcode and width each field
 * has, and the codec records presence per field as it reads — decode marks every
 * field it read, encode writes exactly the marked ones. cachepack writes with the
 * same codec from the same register, so the two ends cannot disagree about a width
 * or an opcode; there is only one copy of either.
 *
 * What a field MEANS is the server's, and it is all this file holds: per type, a
 * binding table of `field name -> offsetof member, sizeof member`, injected into
 * the codec (RSCache_BandBindingApply). The library knows no field.
 *
 * There used to be a hand-kept opcode/width table here as well, with an encoder
 * that decided what to write by comparing each value against an engine-defaults
 * record — "equal to the default" read as "not stated". That is the value-as-
 * presence bug the shared codec removes: a record that states its default value
 * on purpose (`attackrate=4` over a seed of 6) lost the statement. No encoder
 * lives here now; a writer states fields in an `RSCache_BandRecord` and calls
 * `RSCache_BandEncode`.
 *
 * ## The agreement is checked at load
 *
 * `ToriRSServer_ServerCheck` holds a type's bindings to the register both ways: a
 * binding naming a field the register gives no band home, a member narrower than
 * the field's wire width, and a band field the register declares that no binding
 * receives (cachepack would write it and the server would drop it). Any one is a
 * startup error (`torirs_server_content.c`).
 */

#include "torirs_server_content.h"

#include "rscache_band.h"
#include "rscache_register.h"

#include <stddef.h>
#include <stdint.h>

/**
 * One record type's band, as the server reads it.
 *
 * `name` is the register's own spelling — `npc` resolves `fields/npc.ini` — so no
 * second table maps one to the other.
 */
/** What an obj band applies to: the obj's id, and how many requirements its
 *  `levelrequire` list set (the apply calls ToriRSServer_ObjRequireSet). */
struct ToriRSServerObjBand
{
    int obj_id;
    int levelrequire;
};

struct ToriRSServerBandType
{
    const char* name;
    const struct RSCache_BandBinding* bindings;
    int binding_count;
    /** `sizeof` the record struct, so a caller can hold one without naming it. */
    size_t record_size;
};

/**
 * Every type with a server band.
 *
 * Exposed as a list rather than as a lookup per type so the load check and the
 * test can *iterate*: a type added here is checked against its register without
 * anyone remembering to add it to the check.
 */
const struct ToriRSServerBandType*
ToriRSServer_ServerTypes(int* out_count);

/** By register name (`npc`, `loc`), or NULL. */
const struct ToriRSServerBandType*
ToriRSServer_ServerTypeFor(const char* name);

/**
 * Hold `type`'s bindings to `reg`: the register's own consistency
 * (RSCache_RegisterCheck), every binding against the band
 * (RSCache_BandBindingCheck), and every band field against the bindings. Reports
 * each problem on stderr; returns the count.
 */
int
ToriRSServer_ServerCheck(
    const struct ToriRSServerBandType* type,
    const struct RSCache_Register* reg);

/**
 * Decode a band stream into `record` and apply its stated fields over `object`.
 *
 * `object` is expected to arrive already seeded — from the cache record and the
 * engine defaults — and this overrides only the fields the stream states. That is
 * the precedence rule: **the server pack wins for any field present; the seed
 * supplies every field absent.** `record` keeps what was stated, for a caller that
 * needs to tell the two apart.
 *
 * Returns the bytes consumed, or -1 at an opcode the register does not declare
 * (its payload width is unknown, so nothing past it can be read). On -1 nothing
 * is applied: a stream this build cannot read whole is not half-applied.
 */
int
ToriRSServer_ServerDecode(
    const struct ToriRSServerBandType* type,
    const struct RSCache_Register* reg,
    struct RSCache_BandRecord* record,
    void* object,
    const uint8_t* src,
    int size);

#endif
