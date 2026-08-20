/*
 * ServerScript split_init/split_get support, backed by the active cache's
 * actual font-metrics archives.
 *
 * Quest journal rows in the rev-239 cache are 415x20 single-line widgets.  The
 * official client therefore does not wrap their text; the server splits prose
 * before IF_SETTEXT, just as LostCity's StringOps SPLIT_INIT handler does.  A
 * font id is supplied by content through pack/13_fonts.pack -- no UI or font
 * ids live here.
 */

#include "mock230.h"

#include "serverscript/ssvm.h"

#include <rscache.h>
#include <datatypes/dat2_font_metrics.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SPLIT_STYLE_MAX = 96,
    SPLIT_FONT_CACHE_MAX = 16
};

struct SplitFont
{
    int loaded;
    int id;
    struct RSCache_Dat2FontMetrics metrics;
};

static struct SplitFont g_fonts[SPLIT_FONT_CACHE_MAX];

static struct RSCache_Dat2Disk*
open_cache(void)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    const char* cache_dir = mock230_world_cache_dir();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;
    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( disk )
        RSCache_Dat2DiskSetProfile(disk, &profile);
    return disk;
}

static const struct RSCache_Dat2FontMetrics*
font_metrics(int font_id)
{
    struct SplitFont* slot = NULL;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* files = NULL;
    int table;

    for( int i = 0; i < SPLIT_FONT_CACHE_MAX; i++ )
    {
        if( g_fonts[i].loaded && g_fonts[i].id == font_id )
            return &g_fonts[i].metrics;
        if( !slot && !g_fonts[i].loaded )
            slot = &g_fonts[i];
    }
    if( !slot )
        return NULL;

    disk = open_cache();
    if( !disk )
        return NULL;
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_FONTS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, font_id);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) ||
        archive->file_count <= 0 )
        goto done;
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files || files->file_count <= 0 || !files->files[0] )
        goto done;
    if( !RSCache_Dat2FontMetricsDecodeProfile(
            &disk->profile, (const uint8_t*)files->files[0], files->file_sizes[0],
            &slot->metrics) )
        goto done;
    slot->id = font_id;
    slot->loaded = 1;

done:
    RSCache_FileListFree(files);
    if( archive )
        RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return slot->loaded ? &slot->metrics : NULL;
}

static int
tag_end(const char* text, int at)
{
    const char* end;

    if( text[at] != '<' )
        return -1;
    end = strchr(text + at + 1, '>');
    return end ? (int)(end - text) : -1;
}

static int
glyph_advance(const struct RSCache_Dat2FontMetrics* metrics, unsigned char ch)
{
    /* The wire strings and cache font table share the game's CP-1252 byte
     * domain.  Quest content is ASCII; malformed/high UTF-8 bytes deliberately
     * fall back to '?' instead of indexing arbitrary metrics. */
    if( ch >= 128 )
        ch = '?';
    if( ch == 160 )
        ch = ' ';
    return metrics->advance[ch];
}

struct SplitPoint
{
    int output_end;
    int rest_start;
};

static struct SplitPoint
find_split(
    const char* text,
    const struct RSCache_Dat2FontMetrics* metrics,
    int max_width)
{
    struct SplitPoint last = { -1, -1 };
    int width = 0;

    for( int i = 0; text[i]; i++ )
    {
        if( text[i] == '|' )
            return (struct SplitPoint){ i, i + 1 };
        if( text[i] == '<' )
        {
            int end = tag_end(text, i);

            if( end >= 0 )
            {
                int length = end - i - 1;

                if( length == 2 && strncmp(text + i + 1, "br", 2) == 0 )
                    return (struct SplitPoint){ i, end + 1 };
                if( length == 2 && strncmp(text + i + 1, "lt", 2) == 0 )
                    width += glyph_advance(metrics, '<');
                else if( length == 2 && strncmp(text + i + 1, "gt", 2) == 0 )
                    width += glyph_advance(metrics, '>');
                i = end;
                if( max_width > 0 && width > max_width && last.output_end >= 0 )
                    return last;
                continue;
            }
        }
        /* Legacy journal markup is widthless too.  Keeping it here makes the
         * primitive faithful for any not-yet-modernised content caller. */
        if( text[i] == '@' && text[i + 1] && text[i + 2] && text[i + 3] &&
            text[i + 4] == '@' )
        {
            i += 4;
            continue;
        }

        width += glyph_advance(metrics, (unsigned char)text[i]);
        if( text[i] == ' ' )
            last = (struct SplitPoint){ i, i + 1 };
        else if( text[i] == '-' )
            last = (struct SplitPoint){ i + 1, i + 1 };
        if( max_width > 0 && width > max_width && last.output_end >= 0 )
            return last;
    }
    return (struct SplitPoint){ (int)strlen(text), (int)strlen(text) };
}

static void
copy_tag(char* dst, size_t dst_size, const char* start, int length)
{
    if( length < 0 || (size_t)length >= dst_size )
        return;
    memcpy(dst, start, (size_t)length);
    dst[length] = '\0';
}

/*
 * Colour is the *only* style a continuation row inherits.
 *
 * The reference is FontType.split (LostCity src/cache/config/FontType.ts):
 * it keeps a single `savedCol` across the break and re-emits it, and the
 * strikethrough code `@str@` explicitly does *not* survive -- it even clears
 * `savedCol`.  Strike is a per-row style, because that is what journal content
 * is written against: `^journal_done` is a bare `<str>` opened at the start of
 * every completed step and never closed, and each step ends at a `|`.  Carry
 * the strike and the first completed step strikes out every row after it,
 * including the current objective (which is how this was found: the whole
 * Restless Ghost journal came up crossed out).
 *
 * The cost is that a *soft* wrap of a struck row loses the rule on the
 * continuation, exactly as the reference does.  Journal rows are hand-broken
 * with `|` to fit 415px, so that case does not arise in practice.
 */
static void
styles_at(
    const char* text,
    int length,
    char* colour)
{
    colour[0] = '\0';
    for( int i = 0; i < length && text[i]; i++ )
    {
        if( text[i] == '<' )
        {
            int end = tag_end(text, i);

            if( end < 0 || end >= length )
                continue;
            if( strncmp(text + i, "<col=", 5) == 0 )
                copy_tag(colour, SPLIT_STYLE_MAX, text + i, end - i + 1);
            else if( end - i == 5 && strncmp(text + i, "</col>", 6) == 0 )
                colour[0] = '\0';
            i = end;
        }
    }
}

int
mock230_split_init(
    struct SSVM_State* state,
    const char* text,
    int max_width,
    int lines_per_page,
    int font_id)
{
    const struct RSCache_Dat2FontMetrics* metrics = font_metrics(font_id);
    char* work;

    state->split_line_count = 0;
    state->split_lines_per_page = lines_per_page > 0 ? lines_per_page : 1;
    state->split_mesanim = -1;
    if( !metrics )
    {
        SSVM_Abort(state, "split_init could not load fontmetrics %d", font_id);
        return 0;
    }
    work = strdup(text ? text : "");
    if( !work )
    {
        SSVM_Abort(state, "split_init out of memory");
        return 0;
    }

    /* The reference returns one line for an empty string. */
    do
    {
        struct SplitPoint point = find_split(work, metrics, max_width);
        char colour[SPLIT_STYLE_MAX];
        char* line;
        size_t rest_length;
        size_t prefix_length;
        char* next;

        line = SSVM_StrPoolDupLen(&state->pool, work, (size_t)point.output_end);
        if( !line )
        {
            free(work);
            SSVM_Abort(state, "split_init out of memory");
            return 0;
        }
        state->split_lines[state->split_line_count++] = line;
        if( !work[point.rest_start] || state->split_line_count >= SSVM_SPLIT_MAX_LINES )
            break;

        styles_at(work, point.rest_start, colour);
        rest_length = strlen(work + point.rest_start);
        prefix_length = strlen(colour);
        next = (char*)malloc(prefix_length + rest_length + 1);
        if( !next )
        {
            free(work);
            SSVM_Abort(state, "split_init out of memory");
            return 0;
        }
        next[0] = '\0';
        strcat(next, colour);
        strcat(next, work + point.rest_start);
        free(work);
        work = next;
    } while( 1 );

    free(work);
    return 1;
}

const char*
mock230_split_get(struct SSVM_State* state, int page, int line)
{
    int index;

    if( page < 0 || line < 0 || line >= state->split_lines_per_page )
        return "";
    index = page * state->split_lines_per_page + line;
    if( index < 0 || index >= state->split_line_count )
        return "";
    return state->split_lines[index];
}

int
mock230_split_pagecount(struct SSVM_State* state)
{
    if( state->split_line_count <= 0 )
        return 0;
    return (state->split_line_count + state->split_lines_per_page - 1) /
           state->split_lines_per_page;
}

int
mock230_split_linecount(struct SSVM_State* state, int page)
{
    int remaining;

    if( page < 0 )
        return 0;
    remaining = state->split_line_count - page * state->split_lines_per_page;
    if( remaining <= 0 )
        return 0;
    return remaining < state->split_lines_per_page ? remaining : state->split_lines_per_page;
}
