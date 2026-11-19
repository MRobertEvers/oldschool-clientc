#include "platform/device_caps.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
DeviceCaps_Reset(struct DeviceCaps* caps)
{
    assert(caps);
    memset(caps, 0, sizeof(*caps));
}

int
DeviceCaps_TouchUiRule(
    int clienttype,
    int on_mobile,
    int touch_platform,
    char const* override_text)
{
    int touch = (clienttype == 7 || on_mobile) ? 1 : 0;

    if( touch_platform )
        touch = 1;
    /* NULL is the sentinel for "no override stated", as an empty string is. */
    if( override_text && override_text[0] )
        touch = atoi(override_text) != 0;
    return touch;
}

void
DeviceCaps_SetTouchCamera(
    struct DeviceCaps* caps,
    int on)
{
    assert(caps);
    caps->touch_camera = on ? 1 : 0;
}

int
DeviceCaps_TouchCamera(struct DeviceCaps const* caps)
{
    assert(caps);
    return caps->touch_camera;
}

void
DeviceCaps_SetTouchUi(
    struct DeviceCaps* caps,
    int on)
{
    assert(caps);
    caps->touch_ui = on ? 1 : 0;
}

int
DeviceCaps_TouchUi(struct DeviceCaps const* caps)
{
    assert(caps);
    return caps->touch_ui;
}

void
DeviceCaps_SetScreenKeyboard(
    struct DeviceCaps* caps,
    int has)
{
    assert(caps);
    caps->screen_keyboard = has;
}

int
DeviceCaps_ScreenKeyboard(struct DeviceCaps const* caps)
{
    assert(caps);
    return caps->screen_keyboard;
}
