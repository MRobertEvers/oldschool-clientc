#ifndef SRC_GAME_RS_PREFS_H
#define SRC_GAME_RS_PREFS_H

#include "asyncio.h"
#include "game/rs_cs2_host.h"

/*
 * Device-local settings that outlive a launch.
 *
 * The audio panel's four volumes are client state, not account state: nothing
 * sends them to a server and no VARP packet carries them back, so without a
 * file on disk every launch starts at the boot seed and the player's choice is
 * lost. That is what "the music setting does not save" is -- the value was
 * never anywhere but this process's memory.
 *
 * The reference does the same thing with the same scope: `class79`
 * (preferences<N>.dat) holds the master/music/effects/area volumes beside the
 * rest of the client-side options, and every setter calls the save. Account
 * settings -- the ones a script writes as a varp -- travel the other way and
 * are the server save's business (see net/mock/mock230_save.c); this file must
 * not duplicate them, or a stale local copy would fight the server's.
 *
 * What is stored is the CS2 option surface: the three arrays behind
 * CLIENT/GAME/DEVICEOPTION_GET/SET. Volumes live there (game options 7/8/9,
 * device option 19), which is why storing the arrays rather than four named
 * volumes is not over-generalisation -- it is the same table the panel reads
 * back, so any option a later panel row sets persists without new code here.
 *
 * The mute icons write varps (168/169/872/3796) rather than options; the App
 * mirrors those into the option arrays through RS_CS2Host_SyncAudioVarp before
 * a capture sees them, so both halves of the panel land in the same place.
 */

struct RS_Prefs
{
    /** Complete value set, never sparse: an option the file did not mention
     *  holds RS_CS2Host_OptionDefault, so a comparison against the host is a
     *  plain element-wise one and a default that changes later reaches saves
     *  that predate it. Indexed by enum RS_CS2OptionKind. Only the entries
     *  RS_CS2Host_OptionPersists accepts ever reach the file. */
    int options[RS_CS2_OPTION_KIND_COUNT][RS_CS2_OPTION_MAX];
    /**
     * The window mode a script last chose as the default (SETDEFAULTWINDOWMODE
     * 5309, in the CS2 `windowmode` domain: 1 fixed, 2 resizable).
     *
     * Device state in the reference too — `class79` carries it beside the
     * volumes, and its constructor defaults it to 2. Distinct from the *live*
     * window mode, which the shell owns because it owns the window, and from
     * the server-side layout mode, which is an account setting.
     */
    int default_window_mode;
};

/**
 * Where preferences are read from and written to.
 *
 * `TORIRS_PREFS` overrides the path; setting it to the empty string turns
 * persistence off entirely, which is what a test that must not touch the
 * working directory wants. Returns NULL when off.
 */
char const*
RS_Prefs_Path(void);

/** Every option at its fresh-client value. Also the state after a failed load. */
void
RS_Prefs_Defaults(struct RS_Prefs* prefs);

/**
 * Read a file's bytes over `prefs`.
 *
 * `prefs` is filled with defaults first, so anything the file did not state
 * keeps its fresh-client value and a partial or hand-edited file still loads.
 * Returns 1 when something was parsed, 0 for no bytes at all.
 *
 * Bytes rather than a path because the file arrives through the IO queue —
 * this half is pure and the platform owns the disk (see CreateTask_PrefsLoad).
 */
int
RS_Prefs_Decode(struct RS_Prefs* prefs, void const* data, int size);

/**
 * Render `prefs` as the file's bytes, malloc'd into `*out_data`; the caller
 * frees. Options still at their default are left out. Returns 1 on success.
 */
int
RS_Prefs_Encode(struct RS_Prefs const* prefs, void** out_data, int* out_size);

/**
 * Read the preferences file into `prefs`, through the IO queue.
 *
 * A missing file is not a failure — it is a first launch, and `prefs` is left
 * holding defaults. Await this during boot, before anything reads an option.
 */
struct ToriRS_Task*
CreateTask_PrefsLoad(struct RS_Prefs* prefs, char const* path);

/**
 * Write a snapshot of `prefs` out, through the IO queue.
 *
 * Takes a copy at creation rather than borrowing: the App keeps mutating its
 * copy as the player drags a slider, and a task that read it at completion
 * would write a state nobody asked to be saved.
 */
struct ToriRS_Task*
CreateTask_PrefsSave(struct RS_Prefs const* prefs, char const* path);

/** Push every stored option into the host's option tables. */
void
RS_Prefs_ApplyToHost(struct RS_Prefs const* prefs, struct RS_CS2Host* host);

/** Pull the host's option tables back. Returns 1 when anything differed, which
 *  is the only thing that makes a write worth doing. */
int
RS_Prefs_CaptureFromHost(struct RS_Prefs* prefs, struct RS_CS2Host const* host);

#endif
