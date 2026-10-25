#ifndef RSPROT_REGISTRY_H
#define RSPROT_REGISTRY_H

/*
 * Every packet codec under packets/, by prot name.
 *
 * The prot tables (gen/osrs<N>_prot.h) say which prot an opcode is at a
 * revision; this says which codec reads that prot there. Together they make a
 * revision's inbound path data: a client that knows the revision number frames
 * an opcode with the table and decodes it with
 * `rsprot_registry_codec(prot, RSPROT_REG_OUT, revision)`, and a NULL means the
 * one thing rsprot_version_pick's NULL means -- no layout for that prot at that
 * revision.
 *
 * Generated with the unity file (tools/rsprot_gen_codec.py, write_packets_unity),
 * so a packet file cannot exist without a row. Like the codecs themselves it
 * links only where an executor does: it lives in rsprot_packets_unity.c.
 *
 * Direction is RSProt's: OUT is server -> client (an encoder in RSProt), IN is
 * client -> server.
 */

#include "rsprot_exec.h"

typedef enum RsprotRegistryDir
{
    RSPROT_REG_OUT = 0,
    RSPROT_REG_IN = 1,
} RsprotRegistryDir;

typedef struct RsprotRegistryEntry
{
    const char *prot;
    RsprotRegistryDir dir;
    const RsprotVersionRange *ranges;
    const int *count;
} RsprotRegistryEntry;

extern const RsprotRegistryEntry rsprot_registry[];
extern const int rsprot_registry_count;

/** The entry for `prot` in `dir`, or NULL when no packet file carries it. */
const RsprotRegistryEntry *rsprot_registry_find(const char *prot, RsprotRegistryDir dir);

/** The codec for `prot` at `revision`, or NULL: no file, or no layout there. */
RsprotCodecFn rsprot_registry_codec(const char *prot, RsprotRegistryDir dir, int revision);

/**
 * rsprot_registry_codec, accepting either spelling of a prot RSProx renamed.
 *
 * RSProx calls a prot `X_V1` once an `X_V2` exists, at every revision including
 * the ones before the rename; RSProt keeps `X`. Both name the same wire -- the
 * overlay's gap fill only gives `X_V1` a row where RSProx's decoder is identical
 * to one RSProt already carries, and test-pktsweep would see two that
 * disagreed. So this tries `prot`, then `prot` without a trailing `_V1`, then
 * `prot` with one.
 */
RsprotCodecFn rsprot_registry_resolve(const char *prot, RsprotRegistryDir dir, int revision);

#endif
