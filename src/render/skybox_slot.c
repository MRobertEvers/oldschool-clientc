#include "render/skybox_slot.h"

#include <assert.h>
#include <stddef.h>

struct ToriDraw_Model*
SkyboxSlot_Want(
    struct SkyboxSlot* slot,
    int model_id)
{
    struct ToriDraw_Model* old = NULL;

    assert(slot);
    slot->id_plus1 = model_id < 0 ? 0 : model_id + 1;
    slot->requested_plus1 = 0;
    if( slot->id_plus1 == 0 && slot->model )
    {
        old = slot->model;
        slot->model = NULL;
        slot->built_plus1 = 0;
    }
    return old;
}

int
SkyboxSlot_NeedsBuild(struct SkyboxSlot const* slot)
{
    assert(slot);
    return slot->id_plus1 != 0 && slot->built_plus1 != slot->id_plus1;
}

int
SkyboxSlot_WantedId(struct SkyboxSlot const* slot)
{
    assert(slot);
    return slot->id_plus1 - 1;
}

int
SkyboxSlot_NeedsRequest(struct SkyboxSlot const* slot)
{
    assert(slot);
    return SkyboxSlot_NeedsBuild(slot) && slot->requested_plus1 != slot->id_plus1;
}

void
SkyboxSlot_NoteRequested(struct SkyboxSlot* slot)
{
    assert(slot);
    slot->requested_plus1 = slot->id_plus1;
}

struct ToriDraw_Model*
SkyboxSlot_NoteBuilt(
    struct SkyboxSlot* slot,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_Model* old;

    assert(slot);
    assert(model);
    old = slot->model;
    slot->model = model;
    slot->built_plus1 = slot->id_plus1;
    return old;
}

struct ToriDraw_Model*
SkyboxSlot_Model(struct SkyboxSlot const* slot)
{
    assert(slot);
    return slot->model;
}
