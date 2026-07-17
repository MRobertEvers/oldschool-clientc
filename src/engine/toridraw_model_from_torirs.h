#ifndef TORIDRAW_MODEL_FROM_TORIRS_H
#define TORIDRAW_MODEL_FROM_TORIRS_H

struct ToriDraw_Model;
struct ToriRS_Model;

/** Deep-copy a neutral ToriRS_Model into a renderer-owned ToriDraw_Model instance.
 *  Caller owns the result (free with ToriDraw_ModelFree). Returns NULL on alloc failure. */
struct ToriDraw_Model*
ToriDraw_ModelFromToriRS(const struct ToriRS_Model* src);

#endif
