#ifndef TORIDRAW_MODEL_FROM_TORIRS_H
#define TORIDRAW_MODEL_FROM_TORIRS_H

struct ToriDraw_Model;
struct ToriRS_Model;

/** Deep-copy a neutral ToriRS_Model into a renderer-owned ToriDraw_Model instance.
 *  Caller owns the result (free with ToriDraw_ModelFree). Returns NULL on alloc failure.
 *
 *  Every model that reaches the scene — widget models, obj icons, chatheads,
 *  entity builds, world scenery — is built here, so this is also where the
 *  texture ids a model needs get recorded (see ToriDraw_ModelTextureWantsTake).
 */
struct ToriDraw_Model*
ToriDraw_ModelFromToriRS(const struct ToriRS_Model* src);

/** Drain the texture ids referenced by models converted since the last call, into
 *  out_ids (each id at most once). Returns how many were written.
 *
 *  This is how the host learns which textures to load: the alternative — sweeping
 *  every live scene element's every face once per tick to spot ids that are not
 *  resident yet — costs the whole world's geometry every tick, while a model's
 *  texture set is known exactly once, when the model is built. Ids the host
 *  decides against (already resident, load failed) are simply dropped by the
 *  host; re-converting the model re-reports them. */
int
ToriDraw_ModelTextureWantsTake(
    int* out_ids,
    int max_ids);

#endif
