#ifndef RSCACHE_TOOLS_CACHE_WRITE_H
#define RSCACHE_TOOLS_CACHE_WRITE_H

#include "port_plan.h"

/**
 * Commit a port plan into a destination cache directory.
 * Destination must already be a copy of the target cache (or the target itself
 * when the caller accepts append-and-orphan growth).
 *
 * Returns 0 on success.
 */
int
tool_port_commit_dat2(
    struct Tool_Dat2Cache* src,
    struct Tool_Dat2Cache* dst,
    const char* dst_directory,
    struct Tool_PortPlan* plan,
    int emit_bas);

int
tool_port_commit_dat1(
    struct Tool_Dat2Cache* src_dat2, /* nullable when source is dat1 */
    struct Tool_Dat1Cache* src_dat1, /* nullable when source is dat2 */
    struct Tool_Dat1Cache* dst,
    const char* dst_directory,
    struct Tool_PortPlan* plan);

/** Recursively copy a cache directory to `out_dir`. Returns 0 on success. */
int
tool_copy_cache_dir(
    const char* src_dir,
    const char* out_dir);

/* Same-id-if-free helpers for planning. */
int tool_dat2_model_id_free(struct Tool_Dat2Cache* c, int id);
int tool_dat2_framemap_id_free(struct Tool_Dat2Cache* c, int id);
int tool_dat2_anim_archive_id_free(struct Tool_Dat2Cache* c, int id);
int tool_dat2_seq_id_free(struct Tool_Dat2Cache* c, int id);
int tool_dat2_npc_id_free(struct Tool_Dat2Cache* c, int id);
int tool_dat2_bas_id_free(struct Tool_Dat2Cache* c, int id);

/* Dat1 destination helpers for planning. */
int tool_dat1_npc_count(struct Tool_Dat1Cache* c);
int tool_dat1_seq_count(struct Tool_Dat1Cache* c);
int tool_dat1_model_id_free(struct Tool_Dat1Cache* c, int id);
int tool_dat1_anim_archive_id_free(struct Tool_Dat1Cache* c, int id);
/** Highest global frame id across all ANIMATIONS archives, or -1 if none. */
int tool_dat1_max_frame_id(struct Tool_Dat1Cache* c);

#endif
