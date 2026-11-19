/*
 * A scene element's allocation serial (ToriDraw_SceneElementSerial).
 *
 * Element ids are recycled: a freed id is the next one handed out. Anything
 * that names an element across a wait -- a sequence bind parked until its
 * sequence loads -- keys on (id, serial), and the serial must therefore tell
 * the new holder of an id from the old one, and read 0 once it is freed.
 *
 * Run: make -C src test-scene-element-serial
 */

#include "toridraw_scene.h"

#include <stdio.h>

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

int
main(void)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    int first;
    int second;
    uint32_t first_serial;

    TEST_ASSERT(scene != NULL, "a scene");
    first = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_ASSERT(first >= 0, "an element");
    first_serial = ToriDraw_SceneElementSerial(scene, first);
    TEST_ASSERT(first_serial != 0, "a live element has a serial");

    ToriDraw_SceneElementRemove(scene, first);
    TEST_ASSERT(ToriDraw_SceneElementSerial(scene, first) == 0, "a freed id reads 0");

    second = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_ASSERT(second == first, "the freed id is handed out again");
    TEST_ASSERT(ToriDraw_SceneElementSerial(scene, second) != first_serial,
        "and its new holder has a different serial");
    TEST_ASSERT(ToriDraw_SceneElementSerial(scene, second) != 0, "a live one");

    ToriDraw_SceneFree(scene);
    if( g_failures )
    {
        printf("scene_element_serial_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("scene_element_serial_test: all passed\n");
    return 0;
}
