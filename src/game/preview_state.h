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
 * packet is structurally valid and every referenced varbit exists.
 * `applied_stat_mask`, when supplied, receives one bit per seeded skill so the
 * preview boot can dispatch only those post-registration stat listeners.
 * `error` is always NUL-terminated when its capacity is non-zero.
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

/**
 * What applying a preview-state file said, kept for the rest of the boot.
 *
 * `failed` is the verdict the headless caller turns into a non-zero process
 * exit once the boot returns. `stat_mask` has one bit per seeded skill, for
 * the post-build stat transmit; a file that cannot be opened or read leaves
 * it as it was, as ToriRSPreviewState_ApplyFile leaves its mask.
 */
struct ToriRSPreviewStateOutcome
{
    int failed;
    uint32_t stat_mask;
};

/** ToriRSPreviewState_ApplyFile, with its verdict and its stat mask kept in
 *  `outcome`. Returns what ApplyFile returns. */
int
ToriRSPreviewState_ApplyFileOutcome(
    char const* path,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RS_PlayerStats* stats,
    int* applied_count,
    struct ToriRSPreviewStateOutcome* outcome,
    char* error,
    size_t error_capacity);

/** The last apply into `outcome` failed. */
int
ToriRSPreviewState_OutcomeFailed(struct ToriRSPreviewStateOutcome const* outcome);

/** The skills the last apply into `outcome` seeded, one bit per skill id. */
uint32_t
ToriRSPreviewState_OutcomeStatMask(struct ToriRSPreviewStateOutcome const* outcome);

#endif
