#ifndef SRC_RENDER_SKYBOX_SLOT_H
#define SRC_RENDER_SKYBOX_SLOT_H

/*
 * The CAM_SKYBOX model: which one the server wants, which one has been asked
 * for from the cache, and which one is built and drawn.
 *
 * Each is kept as the model id + 1, so 0 is "none". Wanting a new skybox
 * forgets the request, so a model that is not resident is asked for once per
 * want and not once per frame. The built model is kept until another one is
 * built over it: wanting a different skybox leaves the old one drawn until
 * the new one is ready. Wanting none drops the built model at once.
 *
 * The slot holds the built model's pointer but never frees it. Where a model
 * leaves the slot it is handed back, and the caller frees it.
 *
 * This does not know about a cache, a task queue, a scene or textures. The
 * caller asks whether a build or a request is due, checks what is resident,
 * builds the model, queues the load, and passes the result in.
 */

struct ToriDraw_Model;

struct SkyboxSlot
{
    /** CAM_SKYBOX: the model id + 1, 0 for none (deob client.java field1294). */
    int id_plus1;
    /** What has been built and asked for, as above. */
    int built_plus1;
    int requested_plus1;
    struct ToriDraw_Model* model;
};

/**
 * The server wants model `model_id`, or none when it is negative. Forgets
 * the request. Wanting none also empties the slot: the model it held is
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

/** A build is due and the wanted model has not been asked for yet. */
int
SkyboxSlot_NeedsRequest(struct SkyboxSlot const* slot);

/** The wanted model has been asked for. */
void
SkyboxSlot_NoteRequested(struct SkyboxSlot* slot);

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
