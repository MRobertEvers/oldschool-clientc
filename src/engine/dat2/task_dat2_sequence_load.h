#ifndef ENGINE_DAT2_TASK_DAT2_SEQUENCE_LOAD_H
#define ENGINE_DAT2_TASK_DAT2_SEQUENCE_LOAD_H

struct ToriRS_Task;
struct CacheProvider;
struct ToriDraw_Scene;

/*
 * Async: load a classic sequence (seq config -> per-frame animation archives ->
 * framemaps), assemble a ToriDraw_Animation, and register it in `scene` keyed by
 * seq_id (ToriDraw_SceneAnimationAdd). No-op if seq_id is already present.
 * WASM-safe (all cache fetches go through ToriRS_IO). Returns NULL if the
 * sequence is already loaded or seq_id is invalid.
 */
struct ToriRS_Task*
CreateTask_Dat2SequenceLoad(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id);

#endif
