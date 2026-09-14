#ifndef SRC_ENGINE_TORIDRAW_ELEMENT_ANIM_H
#define SRC_ENGINE_TORIDRAW_ELEMENT_ANIM_H

/**
 * Binding an animation to a scene element, and deciding whether one can pose
 * anything at all.
 *
 * Two shapes of animation reach a scene element and they are posed by
 * different code: a classic frame/framemap track, and a skeletal (Animaya)
 * matrix palette. The element carries flags saying which, and setting those
 * flags is the whole of the binding -- get it wrong and the model is posed by
 * the animator that has no data for it, which draws the bind pose forever
 * rather than failing.
 *
 * A third shape exists and must never be bound: the empty sentinel a failed
 * load leaves behind in the registry. It is a registered animation with no
 * frames, and it is why "is this animation resident" is not the same question
 * as "can this animation pose a model".
 */

#include "toridraw_animation.h"
#include "toridraw_scene.h"

#include <stdbool.h>

/**
 * Can this animation actually pose a model?
 *
 * True for a classic track with both its frames and its base, and for a
 * skeletal palette. False for the empty sentinel a failed load leaves in the
 * registry -- which is registered, so a residency check says yes, and carries
 * nothing to pose with.
 */
bool
ToriDraw_ElementAnimPlayable(struct ToriDraw_Animation const* anim);

/**
 * Point a scene element at an animation, selecting the pose path.
 *
 * Skeletal sequences carry no bones, so the element is flagged for
 * ToriDraw_ModelAnimateSkeletal instead of the frame animator. NULL clears the
 * binding, which is how an element stops animating.
 */
void
ToriDraw_ElementSetAnim(
    struct ToriDraw_SceneElement* element,
    struct ToriDraw_Animation* anim);

#endif /* SRC_ENGINE_TORIDRAW_ELEMENT_ANIM_H */
