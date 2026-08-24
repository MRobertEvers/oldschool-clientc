#ifndef TORIRS_GAME_PREVIEW_STATE_H
#define TORIRS_GAME_PREVIEW_STATE_H

#include <stddef.h>
#include <stdint.h>

struct VarCManager;
struct VarPManager;
struct RS_PlayerStats;

/* Binary preview-state packet limits. The producer and consumer both enforce
 * these so an opt-in headless preview cannot turn an id or string length into
 * an unbounded allocation. */
#define TORIRS_PREVIEW_STATE_MAX_BYTES (1024u * 1024u)
#define TORIRS_PREVIEW_STATE_MAX_RECORDS 4096u
#define TORIRS_PREVIEW_STATE_MAX_ID 65535
#define TORIRS_PREVIEW_STATE_MAX_STRING 4096u

enum ToriRSPreviewStateKind
{
    TORIRS_PREVIEW_STATE_VARP = 1,
    TORIRS_PREVIEW_STATE_VARBIT = 2,
    TORIRS_PREVIEW_STATE_VARC_INT = 3,
    TORIRS_PREVIEW_STATE_VARC_STRING = 4,
    TORIRS_PREVIEW_STATE_STAT = 5,
};

/**
 * Validate and atomically apply a CS2Dom preview-state packet.
 *
 * Returns 1 on success and 0 on failure. No value is changed unless the whole
 * packet is structurally valid and every referenced varbit exists. `error` is
 * always NUL-terminated when its capacity is non-zero.
 */
int
ToriRSPreviewState_ApplyBuffer(
    unsigned char const* bytes,
    size_t size,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RS_PlayerStats* stats,
    int* applied_count,
    uint32_t* applied_stat_mask,
    char* error,
    size_t error_capacity);

/** Read, validate and apply a bounded packet from an exact filesystem path. */
int
ToriRSPreviewState_ApplyFile(
    char const* path,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RS_PlayerStats* stats,
    int* applied_count,
    uint32_t* applied_stat_mask,
    char* error,
    size_t error_capacity);

#endif
