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

/** Record every texture id an already-built ToriDraw model's faces reference.
 *
 *  ToriDraw_ModelFromToriRS reports what the *source* model asked for, which is
 *  not the whole story: face_textures also gets written by paths that never see
 *  a ToriRS_Model (the terrain tile builder constructs its model field by field)
 *  and gets rewritten after conversion by ToriDraw_ModelRetexture (loc recolour
 *  pairs, loc/npc/spotanim retexture pairs). Those ids are new to the registry
 *  and nothing else will ever report them, so every such path must call this —
 *  otherwise the texture is never requested and the raster skips the face
 *  forever. Merge and copy are exempt: their ids come from parts already noted.
 */
void
ToriDraw_ModelNoteTextureWants(const struct ToriDraw_Model* model);

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
