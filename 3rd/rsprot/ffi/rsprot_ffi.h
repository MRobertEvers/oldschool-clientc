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

#endif
