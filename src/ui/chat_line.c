#include "ui/chat_line.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
UIChatLine_Reset(struct UIChatLine* line)
{
    assert(line);
    memset(line, 0, sizeof(*line));
}

int
UIChatLine_InputActive(struct UIChatLine const* line)
{
    assert(line);
    return line->input_active;
}

void
UIChatLine_SetInputActive(
    struct UIChatLine* line,
    int active)
{
    assert(line);
    line->input_active = active ? 1 : 0;
}

int
UIChatLine_Open(struct UIChatLine const* line)
{
    assert(line);
    return line->open;
}

char const*
UIChatLine_CoveredText(
    struct UIChatLine const* line,
    char const* shown)
{
    assert(line);
    assert(shown);
    if( line->saved[0] && strcmp(shown, line->prompt) == 0 )
        return line->saved;
    return NULL;
}

void
UIChatLine_SetOpen(
    struct UIChatLine* line,
    int open)
{
    assert(line);
    line->saved[0] = '\0';
    line->prompt[0] = '\0';
    line->open = open ? 1 : 0;
}

char const*
UIChatLine_Cover(
    struct UIChatLine* line,
    char const* shown)
{
    char const* colon;
    char const* colour;
    char const* colour_end;

    assert(line);
    assert(shown);
    if( strcmp(shown, line->prompt) == 0 )
        return NULL;
    /* `<icon><name>:<nbsp><col=…><typed></col>*` -- the space after the colon
     * is cp1252 0xA0, not 0x20. The "set a name" line has no name to keep and
     * nothing typed to cover. */
    colon = strchr(shown, ':');
    if( !colon )
        return NULL;
    colour = strstr(colon, "<col=");
    if( !colour )
        return NULL;
    colour_end = strchr(colour, '>');
    if( !colour_end )
        return NULL;
    snprintf(line->saved, sizeof(line->saved), "%s", shown);
    snprintf(
        line->prompt,
        sizeof(line->prompt),
        "%.*sClick here or press Enter to chat...</col>",
        (int)(colour_end + 1 - shown),
        shown);
    return line->prompt;
}
