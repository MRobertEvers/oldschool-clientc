#ifndef TORIDRAW_MODEL_TRANSFORM_H
#define TORIDRAW_MODEL_TRANSFORM_H

#include "toridraw_types.h"

struct ToriDraw_Model*
ToriDraw_ModelCopy(struct ToriDraw_Model* src);

/** Move all geometry from src into a new model; src arrays are NULL (free shell with ToriDraw_ModelFree). */
struct ToriDraw_Model*
ToriDraw_ModelSteal(struct ToriDraw_Model* src);

struct ToriDraw_Model*
ToriDraw_ModelMerge(
    struct ToriDraw_Model** models,
    int model_count);

/** Merge models without computing bounds; caller lights and bounds the result. */
struct ToriDraw_Model*
ToriDraw_ModelNewMerge(
    struct ToriDraw_Model** models,
    int model_count);

void
ToriDraw_ModelRecolor(
    struct ToriDraw_Model* model,
    int color_src,
    int color_dst);

void
ToriDraw_ModelRetexture(
    struct ToriDraw_Model* model,
    int texture_src,
    int texture_dst);

void
ToriDraw_ModelMirror(struct ToriDraw_Model* model);

void
ToriDraw_ModelOrient(
    struct ToriDraw_Model* model,
    int orientation);

void
ToriDraw_ModelScale(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height);

/*
 * Record placement that must be applied AFTER every animation frame rather than
 * now: the resize, the quarter-turn orientation, and the offset. See
 * `post_transform` in struct ToriDraw_Model for why the two orders are not
 * interchangeable and what the canonical order is. Identity arguments
 * ((128,128,128), 0, (0,0,0)) clear their part of the record.
 *
 * A model that records placement must ALSO have its bind pose captured before
 * anything is applied (ToriDraw_ModelCaptureOriginalVertices), or the placed
 * geometry becomes the bind and every pose compounds.
 *
 * Callers still put the model into its placed state once themselves --
 * ToriDraw_ModelApplyPostTransforms -- because a model that is never animated
 * is never posed, and nothing else would ever apply it.
 */
void
ToriDraw_ModelSetPostResize(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height);

/** Quarter turns (0..3), the reference's rotate90 repeated. */
void
ToriDraw_ModelSetPostOrient(
    struct ToriDraw_Model* model,
    int quarter_turns);

void
ToriDraw_ModelSetPostOffset(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z);

/** Place the model's live vertices: orient, then resize, then translate.
 *  No-op with nothing recorded. Exactly once per pose -- see post_transform. */
void
ToriDraw_ModelApplyPostTransforms(struct ToriDraw_Model* model);

void
ToriDraw_ModelTranslate(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z);

void
ToriDraw_ModelSetBoundsCylinder(struct ToriDraw_Model* model);

#endif
