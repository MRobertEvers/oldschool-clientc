#include "engine/toridraw_element_anim.h"

#include <assert.h>
#include <stddef.h>

bool
ToriDraw_ElementAnimPlayable(struct ToriDraw_Animation const* anim)
{
    assert(anim);
    if( anim->frame_count <= 0 )
        return false;
    return (anim->frames && anim->base) || anim->skeletal != NULL;
}

void
ToriDraw_ElementSetAnim(
    struct ToriDraw_SceneElement* element,
    struct ToriDraw_Animation* anim)
{
    assert(element);
    element->animation = anim;
    element->is_skeletal = anim && anim->skeletal != NULL;
    element->skeletal_animation = anim ? anim->skeletal : NULL;
    element->skeletal_play_frames = element->is_skeletal ? anim->frame_count : 0;
}
