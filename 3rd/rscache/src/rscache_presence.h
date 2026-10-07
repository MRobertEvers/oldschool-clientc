#ifndef RSCACHE_PRESENCE_H
#define RSCACHE_PRESENCE_H

/*
 * Which fields of a decoded config record the stream actually stated.
 *
 * ## Why this exists
 *
 * A config record is an opcode stream: a field is either written (its opcode is
 * in the stream) or it is not, and "not" means the client applies its own
 * default. Those are two states, and before this every codec inferred them from
 * the field's VALUE -- `if( def->right_hand_item != -1 )` -- which makes a field
 * explicitly written with the default's value indistinguishable from an absent
 * one, and makes every encoder, decoder, unpacker and packer agree on what each
 * field's default is.
 *
 * They did not agree. The July 2026 seq unpacker skipped any field equal to 0,
 * and 0 is not "absent" for a held-item override: it is "hide the weapon". The
 * text lost the value, every bake from that text kept the wielded weapon in the
 * player's hand while woodcutting, and 1,705 sequences were wrong.
 *
 * So presence is recorded, not inferred: the decoder sets a bit for every field
 * whose opcode it reads, the encoder writes an opcode exactly when its bit is
 * set, and a caller that builds a record by hand states the fields it means.
 * The field VALUES still carry the client's defaults, so runtime readers that
 * only consult values are unaffected.
 *
 * ## Keying
 *
 * By field, never by opcode number. Opcode numbers move between codec versions
 * (a sequence's 13 is frame sounds before rev 226 and the maya id after), so each
 * datatype declares its own field enum and its decoders map whichever opcode the
 * era uses onto it.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** Fields per record. The widest datatype (loc) declares well under this. */
#define RSCACHE_PRESENCE_MAX_FIELDS 256

struct RSCache_Presence
{
    uint64_t bits[RSCACHE_PRESENCE_MAX_FIELDS / 64];
};

static inline void
RSCache_PresenceSet(
    struct RSCache_Presence* presence,
    int field)
{
    assert(presence);
    assert(field >= 0);
    assert(field < RSCACHE_PRESENCE_MAX_FIELDS);
    presence->bits[field >> 6] |= (uint64_t)1 << (field & 63);
}

static inline void
RSCache_PresenceClear(
    struct RSCache_Presence* presence,
    int field)
{
    assert(presence);
    assert(field >= 0);
    assert(field < RSCACHE_PRESENCE_MAX_FIELDS);
    presence->bits[field >> 6] &= ~((uint64_t)1 << (field & 63));
}

static inline bool
RSCache_PresenceHas(
    const struct RSCache_Presence* presence,
    int field)
{
    assert(presence);
    assert(field >= 0);
    assert(field < RSCACHE_PRESENCE_MAX_FIELDS);
    return (presence->bits[field >> 6] >> (field & 63)) & 1;
}

static inline void
RSCache_PresenceReset(struct RSCache_Presence* presence)
{
    assert(presence);
    memset(presence, 0, sizeof(*presence));
}

#endif
