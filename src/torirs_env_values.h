#ifndef SRC_TORIRS_ENV_VALUES_H
#define SRC_TORIRS_ENV_VALUES_H

/**
 * Runtime knobs that carry a VALUE, not just presence.
 *
 * torirs_env.h answers "is this knob set" for the dozens of switches whose
 * whole meaning is being set at all. The handful here are different: each one
 * has a small grammar, and getting that grammar wrong is silent. A spawn
 * argument that fails to parse falls back to a built-in id and spawns the
 * wrong thing; a map-square list that half-parses loads half a world; a field
 * of view outside the projection's domain mirrors the scene.
 *
 * So they are parsers, and they take their text as an argument. Reading the
 * environment is the caller's half -- which is what makes these testable at
 * all, since the environment is process-wide and read once.
 *
 * Every one of them is total: any input returns something usable, and the
 * caller is never asked to distinguish "parsed 0" from "did not parse".
 */

#include <stdbool.h>

/**
 * The value of `<name>=<number>` in a comma-separated argument list.
 *
 * `args` is the spelling a debug hotkey carries: `id=3106,height=92`. A clause
 * that is not this name, or whose number does not consume the whole clause, is
 * skipped rather than rejected -- the list is shared by several readers, each
 * looking for its own key. A later clause with the same name wins, which is
 * what a person editing the tail of a string expects.
 *
 * Negative numbers are not accepted: every id and count this reads is
 * non-negative, and `-1` in one of these strings has always been a typo rather
 * than a sentinel. Returns `fallback` when the name is absent.
 *
 * NULL or empty `args` is the ordinary case (no arguments given), not an error.
 */
int
ToriRS_EnvNamedArg(
    char const* args,
    char const* name,
    int fallback);

/**
 * The same, with an environment variable that outranks the list.
 *
 * The env value is taken whole, in any base strtol accepts, and is NOT subject
 * to the non-negative rule above: it is the operator speaking directly, and a
 * deliberate -1 there is how several of these knobs are switched off.
 */
int
ToriRS_EnvNamedArgOrEnv(
    char const* args,
    char const* name,
    char const* env_name,
    int fallback);

/**
 * A `;`-separated list of `x,z` map-square pairs, into `out_chunks` as
 * consecutive x,z ints.
 *
 * Returns the pair count, or 0 if ANY of it failed to parse -- trailing text
 * after the last pair included, and a list LONGER than `max_pairs` included.
 * Never a partial list: half a world meshed because the second pair had a typo
 * is invisible in the frame and reads as "the renderer got faster", so the
 * caller keeps its own default and says so instead.
 *
 * Truncating to the cap would be the same silent failure wearing a different
 * hat, which is why an over-long list is refused rather than clipped.
 */
int
ToriRS_EnvChunkList(
    char const* spec,
    int* out_chunks,
    int max_pairs);

/**
 * Whether `id` appears in a comma-separated list of decimal ids.
 *
 * The walk consumes one comma after each number and otherwise leaves the
 * cursor where the number ended, so a space-separated list reads the same as a
 * comma-separated one (the number parse skips leading whitespace) but a
 * DOUBLED separator stops it: "1,,2" contains 1 and not 2. Typed by hand and
 * read once at boot, so a stalled walk shows up immediately as "my id did
 * nothing" rather than silently later.
 *
 * An empty list contains nothing -- which is how `TORIRS_ZBUFFER_NPCS=`
 * switches the feature off for every npc, as distinct from leaving the
 * variable unset and letting each npc's config decide.
 */
bool
ToriRS_EnvIdListHas(
    char const* list,
    int id);

/** `ToriRS_EnvScaleMode` returns this when the knob asks for the legacy
 *  constant instead of a recomputed scale. */
#define TORIRS_ENV_SCALE_OFF (-1)
/** ...and this when it wants the scale recomputed, which is the default. */
#define TORIRS_ENV_SCALE_AUTO 0

/**
 * The wedge scale knob: `off`/`0` for the legacy constant, `auto`/`1` to
 * recompute, or an explicit scale of 8 or more.
 *
 * Anything else -- including a number below 8, which would collapse the
 * projection -- reads as auto. NULL and empty read as auto too, so an unset
 * knob and a knob set to nothing agree.
 */
int
ToriRS_EnvScaleMode(char const* text);

/**
 * A field-of-view override, clamped into [fov_min, fov_max], or -1 when the
 * text does not name one.
 *
 * Clamped rather than rejected: an out-of-domain angle does not fail, it
 * mirrors the world, and a silently mirrored scene is far harder to recognise
 * than a slightly narrower one.
 */
int
ToriRS_EnvFovOverride(
    char const* text,
    int fov_min,
    int fov_max);

#endif /* SRC_TORIRS_ENV_VALUES_H */
