/*
 * The CAM_SKYBOX slot (render/skybox_slot.h).
 *
 * A wanted model is asked for once per want, built once, kept drawn while a
 * different one is wanted and replaced the moment the new one is built;
 * wanting none hands the built model back at once. Every model that leaves
 * the slot is handed back exactly once.
 *
 * Run: make -C src test-skybox-slot
 */

#include "render/skybox_slot.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Never dereferenced: the slot only stores and hands back the pointers. */
static char g_model_a;
static char g_model_b;
#define MODEL_A ((struct ToriDraw_Model*)&g_model_a)
#define MODEL_B ((struct ToriDraw_Model*)&g_model_b)

static void
test_zeroed_wants_nothing(void)
{
    struct SkyboxSlot slot;

    memset(&slot, 0, sizeof(slot));
    TEST_ASSERT(!SkyboxSlot_NeedsBuild(&slot), "zeroed slot needs no build");
    TEST_ASSERT(!SkyboxSlot_NeedsRequest(&slot), "zeroed slot needs no request");
    TEST_ASSERT(SkyboxSlot_Model(&slot) == NULL, "zeroed slot draws nothing");
    TEST_ASSERT(SkyboxSlot_Want(&slot, -1) == NULL, "wanting none of nothing frees nothing");
}

static void
test_request_once_then_build(void)
{
    struct SkyboxSlot slot;

    memset(&slot, 0, sizeof(slot));
    TEST_ASSERT(SkyboxSlot_Want(&slot, 0) == NULL, "first want frees nothing");
    TEST_ASSERT(slot.id_plus1 == 1, "model 0 is stored as 1");
    TEST_ASSERT(SkyboxSlot_WantedId(&slot) == 0, "model 0 is wanted");
    TEST_ASSERT(SkyboxSlot_NeedsBuild(&slot), "a wanted model needs a build");
    TEST_ASSERT(SkyboxSlot_NeedsRequest(&slot), "and a request");
    SkyboxSlot_NoteRequested(&slot);
    TEST_ASSERT(SkyboxSlot_NeedsBuild(&slot), "asked for is not built");
    TEST_ASSERT(!SkyboxSlot_NeedsRequest(&slot), "asked for once, not every frame");

    /* The same id wanted again is asked for again: a want forgets the request. */
    SkyboxSlot_Want(&slot, 0);
    TEST_ASSERT(SkyboxSlot_NeedsRequest(&slot), "a new want asks again");
    SkyboxSlot_NoteRequested(&slot);

    TEST_ASSERT(SkyboxSlot_NoteBuilt(&slot, MODEL_A) == NULL, "first build replaces nothing");
    TEST_ASSERT(!SkyboxSlot_NeedsBuild(&slot), "built is done");
    TEST_ASSERT(!SkyboxSlot_NeedsRequest(&slot), "built needs no request");
    TEST_ASSERT(SkyboxSlot_Model(&slot) == MODEL_A, "the built model is drawn");
}

static void
test_change_keeps_old_until_built(void)
{
    struct SkyboxSlot slot;

    memset(&slot, 0, sizeof(slot));
    SkyboxSlot_Want(&slot, 41);
    SkyboxSlot_NoteBuilt(&slot, MODEL_A);
    TEST_ASSERT(SkyboxSlot_Want(&slot, 42) == NULL, "changing the skybox frees nothing yet");
    TEST_ASSERT(SkyboxSlot_Model(&slot) == MODEL_A, "the old one draws until the new is built");
    TEST_ASSERT(SkyboxSlot_NeedsBuild(&slot), "the new one needs a build");
    TEST_ASSERT(SkyboxSlot_NeedsRequest(&slot), "and a request");
    TEST_ASSERT(SkyboxSlot_WantedId(&slot) == 42, "the new id is wanted");
    TEST_ASSERT(SkyboxSlot_NoteBuilt(&slot, MODEL_B) == MODEL_A, "the old model is handed back");
    TEST_ASSERT(SkyboxSlot_Model(&slot) == MODEL_B, "the new one draws");
    TEST_ASSERT(slot.built_plus1 == 43, "built is the new id");
}

static void
test_want_none_drops_model(void)
{
    struct SkyboxSlot slot;

    memset(&slot, 0, sizeof(slot));
    SkyboxSlot_Want(&slot, 7);
    SkyboxSlot_NoteRequested(&slot);
    SkyboxSlot_NoteBuilt(&slot, MODEL_A);
    TEST_ASSERT(SkyboxSlot_Want(&slot, -1) == MODEL_A, "wanting none hands the model back");
    TEST_ASSERT(SkyboxSlot_Model(&slot) == NULL, "and draws nothing");
    TEST_ASSERT(slot.id_plus1 == 0, "none is stored as 0");
    TEST_ASSERT(slot.built_plus1 == 0, "nothing is built");
    TEST_ASSERT(slot.requested_plus1 == 0, "nothing is asked for");
    TEST_ASSERT(!SkyboxSlot_NeedsBuild(&slot), "none needs no build");
    TEST_ASSERT(SkyboxSlot_Want(&slot, -5) == NULL, "a second none hands back nothing");

    /* Wanted again after none: built from scratch. */
    SkyboxSlot_Want(&slot, 7);
    TEST_ASSERT(SkyboxSlot_NeedsBuild(&slot), "wanted again needs a build");
    TEST_ASSERT(SkyboxSlot_NeedsRequest(&slot), "and a request");
}

static void
test_want_none_before_built(void)
{
    struct SkyboxSlot slot;

    memset(&slot, 0, sizeof(slot));
    SkyboxSlot_Want(&slot, 9);
    SkyboxSlot_NoteRequested(&slot);
    TEST_ASSERT(SkyboxSlot_Want(&slot, -1) == NULL, "none before a build hands back nothing");
    TEST_ASSERT(slot.requested_plus1 == 0, "the request is forgotten");
    TEST_ASSERT(!SkyboxSlot_NeedsRequest(&slot), "and none is asked for");
}

int
main(void)
{
    test_zeroed_wants_nothing();
    test_request_once_then_build();
    test_change_keeps_old_until_built();
    test_want_none_drops_model();
    test_want_none_before_built();
    if( g_failures )
    {
        printf("skybox_slot_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("skybox_slot_test: ok\n");
    return 0;
}
