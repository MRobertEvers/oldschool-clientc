#ifndef SRC_PLATFORM_PLATFORM_MACOS_DIALOGS_H
#define SRC_PLATFORM_PLATFORM_MACOS_DIALOGS_H

/*
 * AppKit's own dialogs, for the executor seams that need one.
 *
 * Private to platform_sdl2.c, which installs them; the executor reaches them
 * through its hooks (platform_x_io.h PlatformX_IO_SetFolderPicker) and never
 * links Objective-C itself.
 */

/** NSOpenPanel choosing one directory, opened at `start` when it names one.
 *  1 and `out` filled when a folder was chosen, 0 on cancel. Modal: the
 *  frame waits, as every native folder dialog makes its application wait. */
int PlatformMacDialog_PickFolder(
    char const* start,
    char* out,
    int out_size);

#endif
