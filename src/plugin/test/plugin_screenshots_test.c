/*
 * The pending frame captures (plugin/plugin_screenshots.h).
 *
 * A claim fills the first free slot and completes a name with no extension
 * with ".png"; a claimed slot is not waiting until it is told to wait; a full
 * table refuses; a file is waiting until it is taken; captures are handed back
 * in slot order, each once.
 *
 * Run: make -C src test-plugin-screenshots
 */

#include "plugin/plugin_screenshots.h"

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

/* The caller's part: resolve a destination into the slot, then wait it. */
static struct PluginScreenshot*
queue(
    struct PluginScreenshots* shots,
    char const* name,
    char const* path)
{
    struct PluginScreenshot* shot = PluginScreenshots_Claim(shots, "probe", "shots", name);
    if( !shot )
        return NULL;
    snprintf(shot->path, sizeof(shot->path), "%s", path);
    PluginScreenshots_Wait(shot);
    return shot;
}

static void
test_claim_fills_and_names(void)
{
    struct PluginScreenshots shots;
    struct PluginScreenshot* shot;

    memset(&shots, 0, sizeof(shots));
    shot = PluginScreenshots_Claim(&shots, "loot", NULL, "kill-42");
    TEST_ASSERT(shot == &shots.slots[0], "the first free slot");
    TEST_ASSERT(strcmp(shot->plugin, "loot") == 0, "the plugin is kept");
    TEST_ASSERT(strcmp(shot->dir, "") == 0, "no folder is the empty folder");
    TEST_ASSERT(strcmp(shot->name, "kill-42.png") == 0, "a bare name gets .png");
    TEST_ASSERT(!shot->in_use, "claimed is not yet waiting");
    TEST_ASSERT(PluginScreenshots_Waiting(&shots) == 0, "nothing waits");

    shot = PluginScreenshots_Claim(&shots, "loot", "boss", "kill-42.png");
    TEST_ASSERT(shot == &shots.slots[0], "an unwaited claim leaves the slot free");
    TEST_ASSERT(strcmp(shot->dir, "boss") == 0, "the folder is kept");
    TEST_ASSERT(strcmp(shot->name, "kill-42.png") == 0, "a name with an extension is left");

    shot = PluginScreenshots_Claim(&shots, "loot", NULL, "frame.bmp");
    TEST_ASSERT(strcmp(shot->name, "frame.bmp") == 0, "whatever the extension");
}

static void
test_long_name_is_not_overrun(void)
{
    struct PluginScreenshots shots;
    struct PluginScreenshot* shot;
    char name[PLUGIN_SCREENSHOT_NAME_CAP + 16];

    memset(&shots, 0, sizeof(shots));
    memset(name, 'a', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    shot = PluginScreenshots_Claim(&shots, "loot", NULL, name);
    TEST_ASSERT(
        strlen(shot->name) == PLUGIN_SCREENSHOT_NAME_CAP - 1,
        "a name past the slot is cut at its capacity");
    TEST_ASSERT(strchr(shot->name, '.') == NULL, "with no room, no extension is added");

    memset(name, 'b', PLUGIN_SCREENSHOT_NAME_CAP - 4);
    name[PLUGIN_SCREENSHOT_NAME_CAP - 4] = '\0';
    shot = PluginScreenshots_Claim(&shots, "loot", NULL, name);
    TEST_ASSERT(strchr(shot->name, '.') == NULL, "a name that leaves too little room is left");

    name[PLUGIN_SCREENSHOT_NAME_CAP - 5] = '\0';
    shot = PluginScreenshots_Claim(&shots, "loot", NULL, name);
    TEST_ASSERT(
        strcmp(shot->name + strlen(shot->name) - 4, ".png") == 0, "the longest that fits gets .png");
}

static void
test_wait_full_and_take(void)
{
    struct PluginScreenshots shots;
    char const* path;

    memset(&shots, 0, sizeof(shots));
    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        char file[32];
        snprintf(file, sizeof(file), "/tmp/shot-%d.png", i);
        TEST_ASSERT(queue(&shots, "shot", file) == &shots.slots[i], "requests fill in order");
    }
    TEST_ASSERT(PluginScreenshots_Waiting(&shots) == PLUGIN_SCREENSHOTS_MAX, "all of them wait");
    TEST_ASSERT(
        PluginScreenshots_Claim(&shots, "probe", NULL, "one-too-many") == NULL, "a full table refuses");
    TEST_ASSERT(PluginScreenshots_IsWaiting(&shots, "/tmp/shot-2.png"), "a queued file is waiting");
    TEST_ASSERT(!PluginScreenshots_IsWaiting(&shots, "/tmp/shot-9.png"), "another file is not");

    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        char file[32];
        snprintf(file, sizeof(file), "/tmp/shot-%d.png", i);
        path = PluginScreenshots_Take(&shots);
        TEST_ASSERT(path && strcmp(path, file) == 0, "taken in slot order");
        TEST_ASSERT(!PluginScreenshots_IsWaiting(&shots, file), "a taken file no longer waits");
    }
    TEST_ASSERT(PluginScreenshots_Take(&shots) == NULL, "each is taken once");
    TEST_ASSERT(PluginScreenshots_Waiting(&shots) == 0, "nothing waits");
}

static void
test_take_skips_free_slots(void)
{
    struct PluginScreenshots shots;
    char const* path;

    memset(&shots, 0, sizeof(shots));
    queue(&shots, "a", "/tmp/a.png");
    queue(&shots, "b", "/tmp/b.png");
    queue(&shots, "c", "/tmp/c.png");
    TEST_ASSERT(strcmp(PluginScreenshots_Take(&shots), "/tmp/a.png") == 0, "the first");
    /* A request made after a take reuses the freed slot, ahead of the rest. */
    queue(&shots, "d", "/tmp/d.png");
    TEST_ASSERT(shots.slots[0].in_use, "the freed slot is reused");
    path = PluginScreenshots_Take(&shots);
    TEST_ASSERT(strcmp(path, "/tmp/d.png") == 0, "slot order, not request order");
    TEST_ASSERT(PluginScreenshots_Waiting(&shots) == 2, "two still wait");
}

int
main(void)
{
    test_claim_fills_and_names();
    test_long_name_is_not_overrun();
    test_wait_full_and_take();
    test_take_skips_free_slots();
    if( g_failures )
    {
        printf("plugin_screenshots_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_screenshots_test: ok\n");
    return 0;
}
