#include "platform/platform_clipboard.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

int
PlatformClipboard_SetText(const char* text)
{
    HGLOBAL block;
    wchar_t* wide;
    int wide_count;
    int taken = 0;

    assert(text);

    wide_count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if( wide_count <= 0 )
        return 0;
    if( !OpenClipboard(NULL) )
        return 0;
    EmptyClipboard();
    block = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wide_count * sizeof(wchar_t));
    assert(block);
    wide = GlobalLock(block);
    assert(wide);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, wide_count);
    GlobalUnlock(block);
    /* The clipboard owns the block once it is set; only a refused set is
     * ours to free. */
    if( SetClipboardData(CF_UNICODETEXT, block) )
        taken = 1;
    else
        GlobalFree(block);
    CloseClipboard();
    return taken;
}

int
PlatformClipboard_GetText(
    char* out,
    size_t out_size)
{
    HANDLE block;
    const wchar_t* wide;
    int written = 0;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';

    if( !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(NULL) )
        return 0;
    block = GetClipboardData(CF_UNICODETEXT);
    wide = block ? GlobalLock(block) : NULL;
    if( wide )
    {
        /* A text longer than the buffer converts to nothing at all, so cut
         * the source to what fits before converting. */
        int wide_count = (int)wcslen(wide);
        while( wide_count > 0 )
        {
            written = WideCharToMultiByte(CP_UTF8, 0, wide, wide_count, out, (int)out_size - 1, NULL, NULL);
            if( written > 0 )
                break;
            wide_count--;
        }
        if( written < 0 )
            written = 0;
        out[written] = '\0';
        GlobalUnlock(block);
    }
    CloseClipboard();
    return written;
}

#elif defined(__EMSCRIPTEN__)

int
PlatformClipboard_SetText(const char* text)
{
    assert(text);
    return 0;
}

int
PlatformClipboard_GetText(
    char* out,
    size_t out_size)
{
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    return 0;
}

#else

/* The desktop's own tools, first one that runs: macOS, Wayland, X11. */
static const char* const k_copy_tools[] = {
    "pbcopy 2>/dev/null",
    "wl-copy 2>/dev/null",
    "xclip -selection clipboard 2>/dev/null",
};

static const char* const k_paste_tools[] = {
    "pbpaste 2>/dev/null",
    "wl-paste --no-newline 2>/dev/null",
    "xclip -selection clipboard -o 2>/dev/null",
};

int
PlatformClipboard_SetText(const char* text)
{
    size_t length;

    assert(text);
    length = strlen(text);

    for( size_t i = 0; i < sizeof(k_copy_tools) / sizeof(k_copy_tools[0]); i++ )
    {
        FILE* pipe = popen(k_copy_tools[i], "w");
        size_t wrote;
        if( !pipe )
            continue;
        wrote = fwrite(text, 1, length, pipe);
        /* A tool that is not installed exits 127 from the shell. */
        if( pclose(pipe) == 0 && wrote == length )
            return 1;
    }
    return 0;
}

int
PlatformClipboard_GetText(
    char* out,
    size_t out_size)
{
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';

    for( size_t i = 0; i < sizeof(k_paste_tools) / sizeof(k_paste_tools[0]); i++ )
    {
        FILE* pipe = popen(k_paste_tools[i], "r");
        size_t got;
        if( !pipe )
            continue;
        got = fread(out, 1, out_size - 1, pipe);
        out[got] = '\0';
        if( pclose(pipe) == 0 && got > 0 )
            return (int)got;
        out[0] = '\0';
    }
    return 0;
}

#endif
