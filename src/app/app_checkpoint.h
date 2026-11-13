#ifndef SRC_APP_APP_CHECKPOINT_H
#define SRC_APP_APP_CHECKPOINT_H

/*
 * The App's own session state, as checkpoint domains (replay/replay_checkpoint.h).
 *
 * Most of what a session packet writes lives in a module of its own (varps,
 * invs, the UI tree, the CS2 host, chat), and that module copies it. What is
 * left is held by the App directly: plain stores (stats, social, the UI slots,
 * the idk design, the held RUNCLIENTSCRIPTs), the interface stores that
 * outlive a mount (IF_SETTEXT / SETHIDE / SETCOLOUR / SETEVENTS, the head and
 * player-model requests, the detached mount records), the camera script, a
 * row of server-set scalars (hint arrow, player ops, multiway, interaction
 * modes, minimap state, the reboot timer, ...), and the music track. Each
 * group is one domain here; the domain table is in replay_checkpoint.c.
 *
 * Every function asserts its arguments. The take functions copy; the restore
 * functions put a copy back and leave invalidation of what READS the state to
 * App_CheckpointAfterRestore, called once after every domain is back.
 */

#include <stddef.h>

struct App;

/* The plain stores and the scalars: one generic shape, a list of App fields
 * copied by value (app_checkpoint.c lists them). `which` is an enum
 * AppCheckpointFields value. */
enum AppCheckpointFields
{
    APP_CKPT_FIELDS_STATS = 0,
    APP_CKPT_FIELDS_SOCIAL,
    APP_CKPT_FIELDS_SLOTS,
    APP_CKPT_FIELDS_IDK,
    APP_CKPT_FIELDS_CS2QUEUE,
    APP_CKPT_FIELDS_CAMERA,
    APP_CKPT_FIELDS_SCALARS,
    APP_CKPT_FIELDS_COUNT
};

void*
App_CheckpointFieldsTake(
    struct App const* app,
    int which);

void
App_CheckpointFieldsRestore(
    struct App* app,
    int which,
    void const* cp);

/** Accepts NULL. */
void
App_CheckpointFieldsFree(void* cp);

size_t
App_CheckpointFieldsBytes(void const* cp);

int
App_CheckpointFieldsEqual(
    void const* a,
    void const* b,
    char* why,
    int why_cap);

/* The interface stores. A store that was up to date with the tree when it
 * was taken is up to date with the restored tree (its applied generation is
 * re-keyed), so the restore must come AFTER the UI tree's. */
void*
App_CheckpointIfStoresTake(struct App const* app);

void
App_CheckpointIfStoresRestore(
    struct App* app,
    void const* cp);

/** Accepts NULL. */
void
App_CheckpointIfStoresFree(void* cp);

size_t
App_CheckpointIfStoresBytes(void const* cp);

int
App_CheckpointIfStoresEqual(
    void const* a,
    void const* b,
    char* why,
    int why_cap);

/* The music track the server last asked for (MIDI_SONG / _STOP /
 * _WITHSECONDARY): a restore asks the player for it when it differs. */
void*
App_CheckpointMusicTake(struct App const* app);

void
App_CheckpointMusicRestore(
    struct App* app,
    void const* cp);

/** Accepts NULL. */
void
App_CheckpointMusicFree(void* cp);

size_t
App_CheckpointMusicBytes(void const* cp);

int
App_CheckpointMusicEqual(
    void const* a,
    void const* b,
    char* why,
    int why_cap);

/**
 * After every domain is back: drop what pointed into the state the restore
 * freed (the emit list, hover, focus, drag, the minimenu, the retain gate and
 * the frame binder's memory of the tree), and ask for a relayout, a CS1 pass
 * and a redraw.
 */
void
App_CheckpointAfterRestore(struct App* app);

#endif
