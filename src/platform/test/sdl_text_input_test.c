/*
 * What "text input off" means on a backend with no on-screen keyboard.
 *
 * The client asks for text input the way a phone needs it asked: on when
 * something is being typed into, off when nothing is. On Android and iOS that
 * raises and puts away a keyboard. On a desktop SDL backend the same switch
 * means something else entirely -- whether SDL_TEXTINPUT events are delivered
 * at all -- and there is no keyboard to put away, so an "off" honoured there
 * closes the only channel printable characters arrive on.
 *
 * That is not a hypothetical: it cost the whole client its typing. The login
 * form is the last thing to want text input, so the moment it stopped being the
 * focus the shell pushed an off, SDL stopped delivering SDL_TEXTINPUT, and
 * nothing ever asked for it back -- no chat line, no bank search, no chrome
 * field, on a machine whose keyboard never went anywhere. It was worst at a
 * cache revision, where the client owns no chat focus of its own and so never
 * asks for text input again after login.
 *
 * SDL answers "is there a keyboard to put away" itself, which is exactly the
 * question the backend is being asked.
 */

#include "platform/platform_window.h"

#include <SDL.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

int
main(void)
{
    struct PlatformWindow* platform;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    platform = PlatformWindow_New();
    CHECK(platform != NULL, "platform allocated");
    if( !platform )
        return 1;
    CHECK(PlatformWindow_Init(platform, 765, 503, "text-input-test"), "SDL window opened");

    PlatformWindow_SetTextInput(platform, 1);
    CHECK(SDL_IsTextInputActive(), "an explicit on starts text input");

    PlatformWindow_SetTextInput(platform, 0);
    if( SDL_HasScreenKeyboardSupport() )
        CHECK(!SDL_IsTextInputActive(),
            "a backend with a soft keyboard puts it away when asked");
    else
        CHECK(SDL_IsTextInputActive(),
            "a backend with no soft keyboard keeps SDL_TEXTINPUT delivered");

    PlatformWindow_Free(platform);
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("SDL text input: ok\n");
    return 0;
}
