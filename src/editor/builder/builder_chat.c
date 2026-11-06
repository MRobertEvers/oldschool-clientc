#include "builder_chat.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- expansion ------------------------------------------------------------ */

/** `source` with `{player}` as `player`, into out->text; map[i] is the
 *  source offset byte i came from. */
static int
chat_expand(
    const char* source,
    const char* player,
    char* text,
    int* map,
    int max)
{
    static const char k_token[] = "{player}";
    size_t const token_length = sizeof(k_token) - 1;
    int length = 0;

    for( int at = 0; source[at]; )
    {
        if( strncmp(source + at, k_token, token_length) == 0 )
        {
            for( const char* p = player; *p && length < max - 1; p++ )
            {
                map[length] = at;
                text[length++] = *p;
            }
            at += (int)token_length;
            continue;
        }
        if( length >= max - 1 )
            break;
        map[length] = at;
        text[length++] = source[at++];
    }
    text[length] = '\0';
    map[length] = (int)strlen(source);
    return length;
}

/* ---- the splitter --------------------------------------------------------- */

static int
chat_tag_end(
    const char* text,
    int at)
{
    const char* end;

    if( text[at] != '<' )
        return -1;
    end = strchr(text + at + 1, '>');
    return end ? (int)(end - text) : -1;
}

static int
chat_advance(
    const struct Builder_ChatMetrics* metrics,
    unsigned char ch)
{
    /* torirs_server_split.c glyph_advance: high bytes measure as '?'. */
    if( ch >= 128 )
        ch = '?';
    return metrics->advance[ch];
}

struct chat_split
{
    /** The line ends here (exclusive); the next starts at `rest`. */
    int end;
    int rest;
    int width;
};

/** One line from `text + from`: torirs_server_split.c find_split, offsets
 *  absolute, the line's width kept. */
static struct chat_split
chat_find_split(
    const char* text,
    int from,
    const struct Builder_ChatMetrics* metrics)
{
    struct chat_split last = { -1, -1, 0 };
    int width = 0;
    int length = (int)strlen(text);

    for( int i = from; text[i]; i++ )
    {
        if( text[i] == '|' )
            return (struct chat_split){ i, i + 1, width };
        if( text[i] == '<' )
        {
            int end = chat_tag_end(text, i);
            if( end >= 0 )
            {
                int tag_length = end - i - 1;
                if( tag_length == 2 && strncmp(text + i + 1, "br", 2) == 0 )
                    return (struct chat_split){ i, end + 1, width };
                if( tag_length == 2 && strncmp(text + i + 1, "lt", 2) == 0 )
                    width += chat_advance(metrics, '<');
                else if( tag_length == 2 && strncmp(text + i + 1, "gt", 2) == 0 )
                    width += chat_advance(metrics, '>');
                i = end;
                if( metrics->width > 0 && width > metrics->width && last.end >= 0 )
                    return last;
                continue;
            }
        }
        width += chat_advance(metrics, (unsigned char)text[i]);
        if( text[i] == ' ' )
            last = (struct chat_split){ i, i + 1, width - chat_advance(metrics, ' ') };
        if( metrics->width > 0 && width > metrics->width && last.end >= 0 )
            return last;
        if( text[i] == '-' )
            last = (struct chat_split){ i + 1, i + 1, width };
    }
    return (struct chat_split){ length, length, width };
}

void
Builder_ChatLayoutText(
    const char* source,
    const struct Builder_ChatMetrics* metrics,
    struct Builder_ChatLayout* out)
{
    int map[BUILDER_CHAT_TEXT_MAX + 1];
    int length;
    int at = 0;

    assert(source);
    assert(metrics);
    assert(out);
    assert(metrics->page_lines > 0);

    memset(out, 0, sizeof(*out));
    length = chat_expand(source, metrics->player ? metrics->player : BUILDER_CHAT_PLAYER_STANDIN, out->text,
                         map, BUILDER_CHAT_TEXT_MAX);
    /* The reference returns one line for an empty string, as split_init
     * does: the do-while below runs once. */
    do
    {
        struct chat_split split = chat_find_split(out->text, at, metrics);
        struct Builder_ChatLine* line;

        if( out->line_count >= BUILDER_CHAT_LINES_MAX )
        {
            out->truncated = 1;
            break;
        }
        line = &out->lines[out->line_count];
        line->start = at;
        line->end = split.end;
        line->source_start = map[at];
        line->page = out->line_count / metrics->page_lines;
        line->width = split.width;
        out->line_count++;
        if( split.rest >= length )
            break;
        at = split.rest;
    } while( 1 );
    out->page_count = (out->line_count + metrics->page_lines - 1) / metrics->page_lines;
}

int
Builder_ChatPageBreaks(
    const struct Builder_ChatLayout* layout,
    int* out_offsets,
    int max)
{
    int count = 0;

    assert(layout);
    assert(out_offsets || max == 0);
    for( int i = 1; i < layout->line_count; i++ )
    {
        if( layout->lines[i].page == layout->lines[i - 1].page )
            continue;
        if( count < max )
            out_offsets[count] = layout->lines[i].source_start;
        count++;
    }
    return count;
}

void
Builder_ChatLineText(
    const struct Builder_ChatLayout* layout,
    int line,
    char* out,
    size_t out_size)
{
    int length;

    assert(layout);
    assert(out);
    assert(out_size > 0);
    assert(line >= 0);
    assert(line < layout->line_count);
    length = layout->lines[line].end - layout->lines[line].start;
    if( (size_t)length >= out_size )
        length = (int)out_size - 1;
    memcpy(out, layout->text + layout->lines[line].start, (size_t)length);
    out[length] = '\0';
}

const char*
Builder_ChatNodeText(const struct Builder_Node* node)
{
    assert(node);
    switch( node->kind )
    {
    case BUILDER_NODE_NPC:
    case BUILDER_NODE_PLAYER:
    case BUILDER_NODE_MESBOX:
    case BUILDER_NODE_OBJBOX:
    case BUILDER_NODE_CHOICE:
        return node->text;
    case BUILDER_NODE_QUEST:
        return node->quest == BUILDER_QUEST_COMPLETE ? node->text : NULL;
    default:
        return NULL;
    }
}
