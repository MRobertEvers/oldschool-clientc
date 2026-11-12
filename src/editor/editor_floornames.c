#include "editor/editor_floornames.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the common set ------------------------------------------------------------
 *
 * Counted on 2026-11-11 over the 2,949 squares of OSRS-Content/osrs-content/
 * maps (every ` u<id>` and ` o<id>` of every .jm2): the comment on a row is
 * tiles / squares. The colours are configs/all.underlay and all.overlay's;
 * the textures were named from their pictures in the Palette's Textures
 * well. A kind's rows are in the order a well shows them: most used first
 * within a family, the families grass, earth, sand, snow, rock.
 */
static const struct Editor_FloorCommon k_common[] = {
    /* Underlays. */
    { 0, 48, 0x35720A, -1, "grass", EDITOR_FLOOR_ROLE_NONE },             /* 988,458 / 597 */
    { 0, 50, 0x78680B, -1, "dry grass", EDITOR_FLOOR_ROLE_NONE },         /* 236,459 / 438 */
    { 0, 49, 0x58680B, -1, "meadow grass", EDITOR_FLOOR_ROLE_NONE },      /* 104,114 / 295 */
    { 0, 52, 0x819531, -1, "light grass", EDITOR_FLOOR_ROLE_NONE },       /*  61,066 / 167 */
    { 0, 93, 0x38562F, -1, "forest grass", EDITOR_FLOOR_ROLE_NONE },      /*  68,702 / 107 */
    { 0, 33, 0x29380F, -1, "dark grass", EDITOR_FLOOR_ROLE_NONE },        /*  47,527 /  59 */
    { 0, 54, 0x125841, -1, "swamp green", EDITOR_FLOOR_ROLE_NONE },       /*  58,672 /  91 */
    { 0, 63, 0x3D2B0B, -1, "dark earth (mud)", EDITOR_FLOOR_ROLE_NONE },  /* 504,001 / 623 */
    { 0, 64, 0x644E1E, -1, "earth (dirt)", EDITOR_FLOOR_ROLE_NONE },      /* 172,261 / 338 */
    { 0, 65, 0x654D0B, -1, "brown earth (dirt)", EDITOR_FLOOR_ROLE_NONE }, /* 128,803 / 227 */
    { 0, 67, 0x664411, -1, "red-brown earth (dirt)", EDITOR_FLOOR_ROLE_NONE }, /* 131,684 / 163 */
    { 0, 96, 0x4B3E14, -1, "muddy earth (dirt)", EDITOR_FLOOR_ROLE_NONE }, /*  58,022 / 122 */
    { 0, 68, 0xCBBA76, -1, "sand", EDITOR_FLOOR_ROLE_NONE },              /* 175,771 / 182 */
    { 0, 62, 0xD0C074, -1, "pale sand", EDITOR_FLOOR_ROLE_NONE },         /*  98,828 /  96 */
    { 0, 61, 0xB19A3D, -1, "desert sand", EDITOR_FLOOR_ROLE_NONE },       /* 109,063 / 112 */
    { 0, 58, 0xEEEEEE, -1, "snow", EDITOR_FLOOR_ROLE_NONE },              /* 200,477 / 175 */
    { 0, 59, 0xD1D6E7, -1, "blue snow (ice)", EDITOR_FLOOR_ROLE_NONE },   /*  40,315 /  53 */
    { 0, 55, 0x767676, -1, "light rock (stone)", EDITOR_FLOOR_ROLE_NONE }, /*  33,138 /  76 */
    { 0, 56, 0x4D4D4D, -1, "rock (stone)", EDITOR_FLOOR_ROLE_NONE },      /*  89,607 / 177 */
    { 0, 57, 0x2E2E2E, -1, "dark rock (stone)", EDITOR_FLOOR_ROLE_NONE }, /*  80,619 / 140 */
    { 0, 125, 0xA59E89, -1, "pale stone", EDITOR_FLOOR_ROLE_NONE },       /*  77,170 /  39 */
    { 0, 72, 0x030303, -1, "black (cave floor)", EDITOR_FLOOR_ROLE_NONE }, /* 542,706 / 405 */

    /* Overlays: the tracks, the floors, the textures. */
    { 1, 14, 0x3D2B0B, -1, "dirt track", EDITOR_FLOOR_ROLE_DIRT_TRACK }, /*  71,097 / 269 */
    { 1, 81, 0x504020, -1, "brown path (dirt)", EDITOR_FLOOR_ROLE_NONE }, /*  59,743 / 122 */
    { 1, 22, 0x6D5B2B, -1, "light path (dirt)", EDITOR_FLOOR_ROLE_NONE }, /*  36,499 / 128 */
    { 1, 80, 0x7D5B2B, -1, "sandy path (dirt)", EDITOR_FLOOR_ROLE_NONE }, /*  23,082 /  99 */
    { 1, 10, 0x505050, -1, "stone floor", EDITOR_FLOOR_ROLE_PAVING },     /*  24,120 /  99 */
    { 1, 3, 0x666666, -1, "light stone floor", EDITOR_FLOOR_ROLE_NONE },  /*  11,945 /  66 */
    { 1, 2, 0x444444, -1, "dark stone floor", EDITOR_FLOOR_ROLE_NONE },   /*  17,792 /  99 */
    { 1, 11, 0x222222, -1, "black stone floor", EDITOR_FLOOR_ROLE_NONE }, /*  76,162 / 210 */
    { 1, 171, 0x787060, -1, "warm stone floor", EDITOR_FLOOR_ROLE_NONE }, /*  15,081 /  37 */
    { 1, 5, 0, 3, "wood floor (planks)", EDITOR_FLOOR_ROLE_NONE },        /*  16,801 /  79 */
    { 1, 52, 0, 51, "polished wood floor", EDITOR_FLOOR_ROLE_NONE },      /*   6,466 /  17 */
    { 1, 174, 0, 43, "paving slabs (stone)", EDITOR_FLOOR_ROLE_NONE },    /*  10,284 /  11 */
    { 1, 12, 0, 46, "cobbles (stone)", EDITOR_FLOOR_ROLE_NONE },          /*   1,776 /  17 */
    { 1, 20, 0, 15, "marble (stone)", EDITOR_FLOOR_ROLE_NONE },           /*   1,803 /  14 */
    { 1, 84, 0, 35, "sandstone", EDITOR_FLOOR_ROLE_NONE },                /*   2,796 /   8 */
    { 1, 6, 0, 1, "water", EDITOR_FLOOR_ROLE_WATER },                     /* 217,653 / 254 */
    { 1, 7, 0, 25, "swamp water", EDITOR_FLOOR_ROLE_NONE },               /*  32,016 /  96 */
    { 1, 19, 0, 31, "lava", EDITOR_FLOOR_ROLE_NONE },                     /*  22,420 /  57 */
    { 1, 196, 0, 91, "ice", EDITOR_FLOOR_ROLE_NONE },                     /*     343 /   3 */
};
#define FLOOR_COMMON_COUNT ((int)(sizeof(k_common) / sizeof(k_common[0])))

/* The ground textures an overlay lays, by what the picture is. */
static const struct
{
    int first;
    int last;
    const char* name;
} k_textures[] = {
    { 1, 1, "water" },
    { 2, 2, "weathered boards (wood)" },
    { 3, 3, "wood planks" },
    { 11, 11, "rough rock (stone)" },
    { 15, 15, "marble (stone)" },
    { 17, 17, "starry void" },
    { 23, 23, "mossy boards (wood)" },
    { 25, 25, "swamp water" },
    { 31, 31, "lava" },
    { 35, 35, "sandstone" },
    { 43, 43, "paving slabs (stone)" },
    { 46, 46, "cobbles (stone)" },
    { 47, 47, "pale boards (wood)" },
    { 51, 51, "polished wood" },
    { 91, 91, "ice" },
    { 116, 119, "rocky ground (stone)" },
    { 120, 120, "olive brick" },
    { 121, 122, "crazy paving (stone)" },
    { 125, 126, "flagstones (stone)" },
    /* The tinted seas: one texture a colour of water. */
    { 130, 208, "tinted water" },
};

const struct Editor_FloorCommon*
Editor_FloorCommonList(int* out_count)
{
    assert(out_count);
    *out_count = FLOOR_COMMON_COUNT;
    return k_common;
}

const struct Editor_FloorCommon*
Editor_FloorCommonFind(
    int is_overlay,
    int id,
    uint32_t rgb,
    int texture)
{
    for( int i = 0; i < FLOOR_COMMON_COUNT; i++ )
    {
        const struct Editor_FloorCommon* common = &k_common[i];

        if( common->id != id || common->is_overlay != (is_overlay ? 1 : 0) )
            continue;
        if( common->texture >= 0 )
            /* A textured entry: the texture, or trust until it is known. */
            return texture == common->texture || texture == -2 ? common : NULL;
        if( texture >= 0 )
            return NULL;
        return (rgb & 0xffffff) == common->rgb ? common : NULL;
    }
    return NULL;
}

const char*
Editor_FloorTextureName(int texture)
{
    for( size_t i = 0; i < sizeof(k_textures) / sizeof(k_textures[0]); i++ )
        if( texture >= k_textures[i].first && texture <= k_textures[i].last )
            return k_textures[i].name;
    return NULL;
}

/* ---- a colour's words ------------------------------------------------------------ */

/** Hue in degrees, saturation and lightness in thousandths (plain HSL: the
 *  words are a person's, not the client's packing). */
static void
floor_hsl(
    uint32_t rgb,
    int* out_hue,
    int* out_sat,
    int* out_light)
{
    int const r = (int)((rgb >> 16) & 0xff);
    int const g = (int)((rgb >> 8) & 0xff);
    int const b = (int)(rgb & 0xff);
    int const max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int const min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int const sum = max + min;
    int const span = max - min;
    int hue = 0;

    *out_light = sum * 1000 / 510;
    if( span == 0 )
    {
        *out_hue = 0;
        *out_sat = 0;
        return;
    }
    *out_sat = span * 1000 / (sum <= 255 ? sum : 510 - sum);
    if( max == r )
        hue = (g - b) * 60 / span + (g < b ? 360 : 0);
    else if( max == g )
        hue = (b - r) * 60 / span + 120;
    else
        hue = (r - g) * 60 / span + 240;
    *out_hue = (hue % 360 + 360) % 360;
}

void
Editor_FloorColourWords(
    uint32_t rgb,
    char* out,
    size_t size)
{
    const char* tone;
    const char* colour;
    const char* ground = NULL;
    int hue;
    int sat;
    int light;

    assert(out);
    assert(size >= EDITOR_FLOOR_NAME_MAX);
    rgb &= 0xffffff;
    /* The client's key for "no colour": such an overlay draws nothing. */
    if( rgb == 0xff00ff )
    {
        snprintf(out, size, "magenta (drawn as no overlay)");
        return;
    }
    floor_hsl(rgb, &hue, &sat, &light);
    tone = light < 130 ? "very dark " : light < 260 ? "dark " : light < 520 ? "" : light < 740 ? "light " : "pale ";
    if( sat < 130 )
    {
        /* Greys: what rock, a stone floor and snow are. */
        if( light < 60 )
        {
            snprintf(out, size, "black");
            return;
        }
        if( light > 880 )
        {
            snprintf(out, size, "white (snow)");
            return;
        }
        colour = "grey";
        ground = "stone";
    }
    else if( hue < 15 || hue >= 345 )
        colour = "red";
    else if( hue < 48 )
    {
        /* Orange darkened is the brown of earth; lightened and greyed it is
         * sand. */
        if( light < 480 )
        {
            colour = "brown";
            ground = "dirt";
        }
        else if( sat < 650 )
        {
            colour = "tan";
            ground = "sand";
        }
        else
            colour = "orange";
    }
    else if( hue < 68 )
    {
        if( light >= 420 )
        {
            colour = sat < 650 ? "tan" : "yellow";
            ground = "sand";
        }
        else
        {
            colour = "olive";
            ground = "dry grass";
        }
    }
    else if( hue < 150 )
    {
        colour = "green";
        ground = "grass";
    }
    else if( hue < 190 )
        colour = "teal";
    else if( hue < 262 )
    {
        colour = "blue";
        ground = light > 780 ? "ice" : "water";
    }
    else
        colour = "purple";
    if( ground )
        snprintf(out, size, "%s%s (%s)", tone, colour, ground);
    else
        snprintf(out, size, "%s%s", tone, colour);
}

void
Editor_FloorName(
    int is_overlay,
    int id,
    uint32_t rgb,
    int texture,
    char* out,
    size_t size)
{
    const struct Editor_FloorCommon* common;
    const char* texture_name;

    assert(out);
    assert(size >= EDITOR_FLOOR_NAME_MAX);
    common = Editor_FloorCommonFind(is_overlay, id, rgb, texture);
    if( common )
    {
        snprintf(out, size, "%s", common->name);
        return;
    }
    if( texture >= 0 )
    {
        texture_name = Editor_FloorTextureName(texture);
        if( texture_name )
            snprintf(out, size, "%s", texture_name);
        else
            snprintf(out, size, "texture %d", texture);
        return;
    }
    Editor_FloorColourWords(rgb, out, size);
}

/* ---- the search ------------------------------------------------------------------ */

/* A word a person types for a word the names use. */
static const char* const k_means[][2] = {
    { "road", "track" },    { "road", "path" },    { "path", "track" },   { "track", "path" },
    { "mud", "dirt" },      { "earth", "dirt" },   { "soil", "dirt" },    { "dirt", "earth" },
    { "rock", "stone" },    { "stone", "rock" },   { "paved", "paving" }, { "pavement", "paving" },
    { "pavement", "stone floor" }, { "planks", "wood" }, { "boards", "wood" }, { "timber", "wood" },
    { "pond", "water" },    { "river", "water" },  { "lake", "water" },   { "sea", "water" },
    { "lawn", "grass" },    { "desert", "sand" },  { "beach", "sand" },   { "floor", "paving" },
};

static char
floor_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/** Is `word` (length `n`, any case) part of `text`, case aside? */
static int
floor_has(
    const char* text,
    const char* word,
    size_t n)
{
    if( n == 0 )
        return 1;
    for( ; *text; text++ )
    {
        size_t i = 0;
        while( i < n && text[i] && floor_lower(text[i]) == floor_lower(word[i]) )
            i++;
        if( i == n )
            return 1;
    }
    return 0;
}

/** One word of a query against one record. */
static int
floor_word_matches(
    const char* word,
    size_t n,
    int id,
    const char* name,
    uint32_t rgb)
{
    char hex[8];
    int digits = 1;

    if( word[0] == '#' )
    {
        /* "#3d2b" is the start of the colour. */
        snprintf(hex, sizeof(hex), "#%06x", (unsigned)(rgb & 0xffffff));
        if( n > 7 )
            return 0;
        for( size_t i = 0; i < n; i++ )
            if( floor_lower(word[i]) != hex[i] )
                return 0;
        return 1;
    }
    for( size_t i = 0; i < n; i++ )
        if( word[i] < '0' || word[i] > '9' )
            digits = 0;
    if( digits )
    {
        /* A number is the id, whole. */
        char number[16];
        snprintf(number, sizeof(number), "%d", id);
        return strlen(number) == n && memcmp(number, word, n) == 0;
    }
    if( floor_has(name, word, n) )
        return 1;
    for( size_t i = 0; i < sizeof(k_means) / sizeof(k_means[0]); i++ )
        if( strlen(k_means[i][0]) == n && floor_has(k_means[i][0], word, n)
            && floor_has(name, k_means[i][1], strlen(k_means[i][1])) )
            return 1;
    return 0;
}

int
Editor_FloorMatches(
    const char* query,
    int id,
    const char* name,
    uint32_t rgb)
{
    assert(query);
    assert(name);
    while( *query )
    {
        size_t n = 0;

        while( *query == ' ' )
            query++;
        while( query[n] && query[n] != ' ' )
            n++;
        if( n > 0 && !floor_word_matches(query, n, id, name, rgb) )
            return 0;
        query += n;
    }
    return 1;
}
