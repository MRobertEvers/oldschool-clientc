/*
 * AppKit dialogs for the executor seams.
 * @see platform_macos_dialogs.h
 */

#import <AppKit/AppKit.h>

#include "platform_macos_dialogs.h"

#include <assert.h>
#include <stdio.h>

int
PlatformMacDialog_PickFolder(
    char const* start,
    char* out,
    int out_size)
{
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    @autoreleasepool
    {
        NSWindow* const owner = [NSApp keyWindow];
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        NSModalResponse response;

        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.canCreateDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Choose";
        panel.message = @"Choose the folder screenshots are saved in";
        if( start && start[0] )
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start]
                                            isDirectory:YES];
        response = [panel runModal];
        /* runModal leaves the key window to whoever AppKit chooses; the game
         * window is the one the player was typing into. */
        [owner makeKeyAndOrderFront:nil];
        if( response != NSModalResponseOK || !panel.URL )
            return 0;
        snprintf(out, (size_t)out_size, "%s", panel.URL.fileSystemRepresentation);
    }
    return out[0] ? 1 : 0;
}
