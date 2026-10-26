#ifndef RSPROT_FFI_H
#define RSPROT_FFI_H

/*
 * rsprot_ffi -- the library behind a C ABI a foreign host can bind (JNA, ctypes):
 * the per-revision prot tables, and any packet decoded by its codec into a
 * trace of named values (rsprot_exec.h, RsprotTrace).
 *
 * Built as a shared library from the library and the host executor
 * (src/net/exec/pkt_exec.c, pkt_codec_storage.c). Every call is reentrant per
 * thread: message storage is thread-local.
 */

#include <stdint.h>

#define RSPROT_FFI_ABI 2

/** RSPROT_FFI_ABI, for a binding to check what it loaded. */
int rsprot_ffi_abi(void);

/** The revisions the tables cover (inclusive), and whether `revision` is one. */
int rsprot_ffi_revision_first(void);
int rsprot_ffi_revision_last(void);

/**
 * Row `i` of `revision`'s server (`client` 0) or client (1) prot table: its name,
 * opcode (-1 for a zone sub-prot), size (>= 0, or -1 var byte, -2 var short) and
 * codec name (NULL: resolve by the name). Returns 0, or -1 past the table.
 */
int rsprot_ffi_prot(int revision, int client, int i, const char **name, int *opcode, int *size,
                    const char **codec);

/** The UPDATE_ZONE_PARTIAL_ENCLOSED sub-prot at `ordinal`, or NULL. */
const char *rsprot_ffi_zone_prot(int revision, int ordinal);

/*
 * Decode one payload with `prot`'s codec at `revision` (`client` 0 server, 1
 * client), the prot named as a table row names it (its codec field honoured).
 * The trace goes into `out` (`cap` bytes); `consumed` gets the bytes the codec
 * read, which a zone sub-packet needs (the rest of `data` belongs to the next).
 *
 * Returns the trace's length; -1 no codec carries `prot` at `revision`; -2 the
 * codec failed (`err` says why); -3 the trace did not fit `cap`.
 * `whole` 1: also fail (-2) when bytes are left over.
 */
int rsprot_ffi_decode(int revision, int client, const char *prot, const uint8_t *data, int len,
                      int whole, uint8_t *out, int cap, int *consumed, char *err, int err_cap);

/*
 * rsprot_ffi_decode, the trace holding each field's DECODED value
 * (rsprot_exec_trace_final): the message as its struct holds it, which is what
 * a host builds its own model from.
 */
int rsprot_ffi_decode_fields(int revision, int client, const char *prot, const uint8_t *data,
                             int len, int whole, uint8_t *out, int cap, int *consumed, char *err,
                             int err_cap);

/*
 * The chat Huffman codec (src/rsprot_compression.h). Load the table -- the
 * cache's 256 bytes, one bit length per byte value -- once, before any decode;
 * it is read-only after. Returns 0, or -1 for a table that is not 256 bytes.
 */
int rsprot_ffi_huffman_load(const uint8_t *table, int len);

/*
 * Decodes one Huffman-coded string, as the wire carries it (a smart length, then
 * the codewords), into `out` as Cp1252 bytes (not terminated). Returns its length;
 * -1 no table is loaded, -2 the string runs past `data` or does not fit `cap`.
 */
int rsprot_ffi_huffman_decode(const uint8_t *data, int len, uint8_t *out, int cap);

/*
 * One sub-packet of an UPDATE_ZONE_PARTIAL_ENCLOSED payload at `data`: its
 * ordinal byte, then the zone prot it names at `revision` (`*prot_out`, valid
 * for the library's lifetime) decoded with its codec. `*consumed` is how far it
 * reached, the ordinal included. Returns as rsprot_ffi_decode; -1 also for an
 * ordinal the revision has no zone prot for.
 */
int rsprot_ffi_zone_decode(int revision, const uint8_t *data, int len, uint8_t *out, int cap,
                           int *consumed, const char **prot_out, char *err, int err_cap);

/*
 * REFLECTION_CHECK_REPLY's results (packets/reflection_check_reply_results.h),
 * read against the `count` checks the request carried (`check_ops`, its
 * opcodes), and the CRC that ends the payload verified (-2 on a mismatch).
 */
int rsprot_ffi_reflection_reply(const int32_t *check_ops, int count, const uint8_t *data, int len,
                                uint8_t *out, int cap, char *err, int err_cap);

/* --- the info streams ----------------------------------------------------
 *
 * Each read writes one trace into `out`: first what the stream says about its
 * entities, then -- from `*blocks_at` -- the extended-info blocks' reads by
 * name, each entity's opened by `#who` / `#flag` and each block by `#block`
 * (RsprotInfoList.trace). Returns the trace's length; -1 no reader carries the
 * prot at the revision, -2 the read stopped (`err` says why), -3 `out` is full.
 */

/** A PLAYER_INFO table (RsprotPlayerInfo), owned by the local player. */
void *rsprot_ffi_players_new(int local_index);
void rsprot_ffi_players_free(void *players);
void rsprot_ffi_players_reset(void *players, int local_index);
/** Seed the table from a GPI init block (the login REBUILD's, or RECONNECT's). */
void rsprot_ffi_players_init(void *players, const uint8_t *gpi, int len);

/** Parse a GPI init block: `*coord` the local player's absolute coordinate,
 *  `positions` (2048) every other slot's 18-bit rough position. Returns 1, or 0
 *  when the block is short. */
int rsprot_ffi_gpi(int local_index, const uint8_t *gpi, int len, int32_t *coord, int32_t *positions);

/** Seed the table from a parsed GPI init block (rsprot_ffi_gpi). */
void rsprot_ffi_players_seed(void *players, int32_t coord, const int32_t *positions);

/*
 * PLAYER_INFO. The first part is one record per slot the packet did more than
 * leave in low resolution: `index`, `type` (RsprotPlayerUpdate) and `coord` --
 * the slot's absolute coordinate, or for the two low-resolution types its
 * rough position (level << 28 | x << 14 | z, 8-bit x and z).
 */
int rsprot_ffi_player_info(void *players, int revision, const uint8_t *data, int len,
                           uint8_t *out, int cap, int *blocks_at, char *err, int err_cap);

/**
 * An npc type's recolour / retexture counts, which CUSTOM_TYPED's arrays take
 * their length from: for `type`, or -- when the read has not named the npc's
 * type (it was in view before this packet, and has not transformed in it) --
 * for the npc at `old_position` in the host's list from the previous packet.
 * Return 0 when unknown: the read stops there.
 */
typedef int (*RsprotFfiNpcLengths)(int old_position, int type, int head, int *recolours,
                                   int *retextures);

/*
 * NPC_INFO_SMALL / NPC_INFO_LARGE (`prot` its table name: NPC_INFO_LARGE_V5,
 * ...). The first part is the op list (rsprot_info.h), one `#op` entry (its
 * RsprotInfoOpKind) then its fields by name, the extended-info ops left out.
 */
int rsprot_ffi_npc_info(int revision, const char *prot, const uint8_t *data, int len,
                        RsprotFfiNpcLengths lengths, uint8_t *out, int cap, int *blocks_at,
                        char *err, int err_cap);

/** A player's appearance block (the PLAYER_INFO trace's `data` blob of an
 *  appearance `#block`), field by field: rsprot_info_appearance_trace. */
int rsprot_ffi_appearance(int revision, const uint8_t *data, int len, uint8_t *out, int cap,
                          char *err, int err_cap);

/** WORLDENTITY_INFO_V1..V7, the same way. */
int rsprot_ffi_worldentity_info(int revision, const char *prot, const uint8_t *data, int len,
                                uint8_t *out, int cap, int *blocks_at, char *err, int err_cap);

#endif
