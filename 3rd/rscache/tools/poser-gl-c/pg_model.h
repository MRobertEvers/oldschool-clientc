#ifndef POSER_GL_C_PG_MODEL_H
#define POSER_GL_C_PG_MODEL_H

/*
 * poser-gl's ModelDefinition, and the two things it does: merge several models
 * into one body, and bend that body with a frame's transformations.
 */

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Model;

struct PG_ModelDef
{
    int id;
    int vertex_count;
    int face_count;
    int priority;

    int* vertices_x;
    int* vertices_y;
    int* vertices_z;

    int* face_a;
    int* face_b;
    int* face_c;

    /**
     * Vertex indices per bone label, built once by pg_model_compute_groups.
     * `groups[label]` holds `group_counts[label]` vertex indices.
     */
    int** groups;
    int* group_counts;
    int group_count;

    /** Per-vertex bone label, consumed and released by pg_model_compute_groups. */
    int* vertex_skins;

    /** Rest pose, captured the first time a transformation runs. */
    int* orig_x;
    int* orig_y;
    int* orig_z;

    uint16_t* face_colors;
    uint8_t* face_alphas;          /* nullable */
    uint8_t* face_priorities;      /* nullable */
    uint8_t* face_render_types;    /* nullable */
};

struct PG_ModelDef*
pg_model_new(void);

/**
 * Adapt an rscache model.
 *
 * Applies the version-13+ 4x vertex scale-down the reference clients do and
 * rscache deliberately does not (it keeps the wire values so encode stays
 * byte-exact). Skipping it draws 643-era models four times life size.
 */
struct PG_ModelDef*
pg_model_from_rscache(const struct RSCache_Model* src, int id);

void
pg_model_free(struct PG_ModelDef* def);

/** poser-gl ModelMerger.merge — OSRS 179 deob, vertices deduplicated by position. */
struct PG_ModelDef*
pg_model_merge(struct PG_ModelDef** defs, int count);

/** Build `groups` from `vertex_skins`, then release the skins. Idempotent. */
void
pg_model_compute_groups(struct PG_ModelDef* def);

/** Replace every face whose colour is `from` with `to`. */
void
pg_model_recolour(struct PG_ModelDef* def, uint16_t from, uint16_t to);

/**
 * Apply one transformation, a port of the client's animate().
 *
 * `type` is 0 origin / 1 translate / 2 rotate / 3 scale; `labels` is the frame
 * map's bone group for this transform. The origin transform writes module state
 * that the following rotate and scale read, so these must be applied in frame
 * order — that pivot is the whole reason the order matters.
 */
void
pg_model_animate(
    struct PG_ModelDef* def,
    int type,
    const int* labels,
    int label_count,
    int dx,
    int dy,
    int dz);

/** Restore the rest pose and clear the pivot, before applying a keyframe. */
void
pg_model_reset_transformations(struct PG_ModelDef* def);

/**
 * Summed positions of every vertex the given bone labels own, and how many there
 * were. This is what a joint's on-screen position is derived from: a bone group
 * has no position of its own, only the vertices it moves.
 *
 * The sum and the count come back separately rather than as a mean because the
 * caller folds the frame's own offset into the numerator before dividing, which
 * is what the reference does and is not the same as adding it to the mean.
 * Returns zero when the labels match nothing — the same condition that makes an
 * origin transform fall back to an absolute pivot.
 */
int
pg_model_label_vertex_sum(
    const struct PG_ModelDef* def,
    const int* labels,
    int label_count,
    float out_sum[3]);

/** The pivot the last origin transform computed. */
void
pg_model_anim_offset(int* x, int* y, int* z);

#endif
