#ifndef SRC_ENGINE_WORLD_BUILDER_WORLD_LOAD_STATE_H
#define SRC_ENGINE_WORLD_BUILDER_WORLD_LOAD_STATE_H

/*
 * Whether there is a world, whether a load has been tried, whether one is
 * running with the viewport blanked, and whether the server asked for it.
 *
 * Four facts, each moved by a named event:
 *
 *   - active: a load has completed. Nothing here clears it: a later load
 *     that does not complete leaves the world that was there.
 *
 *   - attempted: a load has been started, so the lazy first-sight load
 *     (a viewport appearing on an offline boot) is not queued again every
 *     frame for a map that fails. The editor forgets it before reloading
 *     squares, and a window that holds no world sets it without loading.
 *
 *   - inflight: a load is running with the viewport blanked. A local load
 *     and a blocking rebuild raise it when they begin; a background
 *     rebuild leaves the old scene up and raises it only if it stalls on
 *     reads only the platform can answer. The load's finish lowers it, and so
 *     does a cancelled job once it is idle.
 *
 *   - server_driven: the load was a REBUILD, so its finish owes the server a
 *     MAP_BUILD_COMPLETE. A finish that completed clears it just before
 *     sending the ack, a load that did not complete drops it, and so does a
 *     cancelled job, which built nothing.
 *
 * This does not know what a load is, which squares it reads, when it lands,
 * whether it completed, or what to send or print. The caller says when each
 * event happens and reads the facts back.
 */

struct WorldLoadState
{
    /** 1 once a load has completed. */
    int active;
    /** 1 once a load has been started; the editor forgets it to reload. */
    int attempted;
    /** 1 while a load runs with the viewport blanked. */
    int inflight;
    /** 1 while the running load is a REBUILD whose finish acks the server. */
    int server_driven;
};

/** A load is started that is not the server's: an offline, hotkey or editor
 *  load. Attempted, and running with the viewport blanked. */
void
WorldLoadState_BeginLocal(struct WorldLoadState* state);

/** A REBUILD was accepted. Attempted and server-driven; the viewport is
 *  blanked unless `background` (the old scene stays up while a world job
 *  gathers). */
void
WorldLoadState_BeginRebuild(
    struct WorldLoadState* state,
    int background);

/** A committed background rebuild is run once more as if the server had asked
 *  again (TORIRS_WORLD_JOB_REPEAT): server-driven, the viewport kept up. */
void
WorldLoadState_BeginRepeat(struct WorldLoadState* state);

/** A background rebuild is waiting on reads only the platform can answer:
 *  the viewport shows "Loading" as a blocking rebuild's would. */
void
WorldLoadState_Stall(struct WorldLoadState* state);

/** The running load has finished, complete or not: the viewport is no longer
 *  blanked. Says nothing about whether it completed. */
void
WorldLoadState_Land(struct WorldLoadState* state);

/** The load that landed completed: there is a world. */
void
WorldLoadState_Complete(struct WorldLoadState* state);

/** A server-driven load completed and its MAP_BUILD_COMPLETE is about to be
 *  sent: nothing is owed any more. */
void
WorldLoadState_Acknowledged(struct WorldLoadState* state);

/** The load that landed did not complete: nothing is acknowledged. */
void
WorldLoadState_Fail(struct WorldLoadState* state);

/** A background rebuild was cancelled. Nothing was built, so nothing is
 *  acknowledged; the viewport is unblanked only if the job is now idle
 *  (`job_idle`). */
void
WorldLoadState_Cancel(
    struct WorldLoadState* state,
    int job_idle);

/** Mark a load as tried without starting one: a window that holds no world. */
void
WorldLoadState_Attempt(struct WorldLoadState* state);

/** Forget that a load was tried, so the next begin is a fresh one: the editor
 *  reloading the squares it shows. */
void
WorldLoadState_ForgetAttempt(struct WorldLoadState* state);

/** 1 once a load has completed. */
int
WorldLoadState_Active(struct WorldLoadState const* state);

int
WorldLoadState_Attempted(struct WorldLoadState const* state);

/** 1 while a load runs with the viewport blanked. */
int
WorldLoadState_Inflight(struct WorldLoadState const* state);

/** 1 while the running or last-begun load owes the server an ack. */
int
WorldLoadState_ServerDriven(struct WorldLoadState const* state);

/** 1 while a server-driven load runs with the viewport blanked: the frame
 *  waits on the server's scene instead of drawing the old one. */
int
WorldLoadState_ServerLoadBlanking(struct WorldLoadState const* state);

#endif /* SRC_ENGINE_WORLD_BUILDER_WORLD_LOAD_STATE_H */
