#ifndef RSCACHE_TOOLS_LC_OUT_H
#define RSCACHE_TOOLS_LC_OUT_H

#include <stddef.h>

/*
 * Writing into a LostCity `content/` tree.
 *
 * Assets land where the build expects them:
 *   models/<name>.ob2          model geometry
 *   models/<name>.anim         animation frame archive (animset)
 *   maps/m<X>_<Z>.jm2          map square, text
 *   scripts/<area>/configs/    .npc / .seq / .spotanim / .loc / .flo, text
 *   pack/<type>.pack           id registry, rewritten by lc_packs_write
 *
 * Config text accumulates in memory and is written once at the end, because a
 * single `.npc` file holds every npc as a `[name]` section and the exporters
 * reach them in dependency order rather than file order.
 */

struct LC_Str
{
    char* data;
    size_t len;
    size_t cap;
};

void
lc_str_addf(
    struct LC_Str* str,
    const char* fmt,
    ...);

void
lc_str_free(struct LC_Str* str);

struct LC_Out
{
    /** LostCity content root, e.g. `<repo>/content`. */
    char content[1024];
    /** Config subdirectory under `scripts`, e.g. "areas/area_inferno". */
    char area[256];
    /** Basename for emitted config files, e.g. "inferno" -> `inferno.npc`. */
    char prefix[64];
    /** Report only; touch nothing on disk. */
    int dry_run;

    struct LC_Str npc_cfg;
    struct LC_Str seq_cfg;
    struct LC_Str spotanim_cfg;
    struct LC_Str loc_cfg;
    struct LC_Str flo_cfg;

    char** warnings;
    int warning_count;

    int files_written;
    long bytes_written;
};

int
lc_out_init(
    struct LC_Out* out,
    const char* content_dir,
    const char* area,
    const char* prefix,
    int dry_run);

/** Write `size` bytes to `<content>/<relpath>`, creating parent directories. */
int
lc_out_write_file(
    struct LC_Out* out,
    const char* relpath,
    const void* data,
    size_t size);

void
lc_out_warn(
    struct LC_Out* out,
    const char* fmt,
    ...);

/** Flush the accumulated config text. Does not write packs. */
int
lc_out_flush_configs(struct LC_Out* out);

void
lc_out_free(struct LC_Out* out);

#endif
