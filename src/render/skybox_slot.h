#ifndef SRC_RENDER_SKYBOX_SLOT_H
#define SRC_RENDER_SKYBOX_SLOT_H

/*
 * The CAM_SKYBOX model: which one the server wants, and which one is built
 * and drawn.
 *
 * Each is kept as the model id + 1, so 0 is "none". Whether the wanted model
 * has been asked for is not here: the caller wants it through the assets
 * table (ASSET_KIND_SKYBOX) every frame it is not resident, and the table
 * holds the one load -- a newer skybox replacing an older one still loading,
 * and a failed load retried after a back-off (a request flag here asked once
 * per want and never again). The built model is kept until another one is
 * built over it: wanting a different skybox leaves the old one drawn until
 * the new one is ready. Wanting none drops the built model at once.
 *
 * The slot holds the built model's pointer but never frees it. Where a model
 * leaves the slot it is handed back, and the caller frees it.
 *
 * This does not know about a cache, a task queue, a scene or textures. The
 * caller asks whether a build is due, checks what is resident, builds the
 * model or wants it, and passes the result in.
 */

struct ToriDraw_Model;

struct SkyboxSlot
{
    /** CAM_SKYBOX: the model id + 1, 0 for none (deob client.java field1294). */
    int id_plus1;
    /** What has been built, as above. */
    int built_plus1;
    struct ToriDraw_Model* model;
};

/**
 * The server wants model `model_id`, or none when it is negative. Wanting
 * none also empties the slot: the model it held is
 * returned for the caller to free, NULL when there was none.
 */
struct ToriDraw_Model*
SkyboxSlot_Want(
    struct SkyboxSlot* slot,
    int model_id);

/** A skybox is wanted and the one built (if any) is not it. */
int
SkyboxSlot_NeedsBuild(struct SkyboxSlot const* slot);

/** The wanted model id. Only meaningful while one is wanted. */
int
SkyboxSlot_WantedId(struct SkyboxSlot const* slot);

/**
 * `model` is the wanted skybox, built. Returns the model it replaces, NULL
 * when there was none, for the caller to free.
 */
struct ToriDraw_Model*
SkyboxSlot_NoteBuilt(
    struct SkyboxSlot* slot,
    struct ToriDraw_Model* model);

/** The model to draw, NULL for none. */
struct ToriDraw_Model*
SkyboxSlot_Model(struct SkyboxSlot const* slot);

#endif
