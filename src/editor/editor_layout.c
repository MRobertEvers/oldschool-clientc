/*
 * The editor layout file's codec. editor_layout.h has the format and the
 * policy; this is only text <-> struct.
 */

#include "editor/editor_layout.h"

#include "3rd/ini/ini.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char*
EditorLayout_Path(void)
{
    const char* configured = getenv("TORIRS_EDITOR_LAYOUT");

    if( !configured )
        return EDITOR_LAYOUT_DEFAULT_PATH;
    /* Empty is "do not touch the filesystem", as TORIRS_PREFS="". */
    return *configured ? configured : NULL;
}

void
EditorLayout_Clear(struct EditorLayoutFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

const struct EditorLayoutPanel*
EditorLayout_Find(
    const struct EditorLayoutFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->panel_count; i++ )
        if( strcmp(file->panels[i].name, name) == 0 )
            return &file->panels[i];
    return NULL;
}

struct EditorLayoutPanel*
EditorLayout_Add(
    struct EditorLayoutFile* file,
    const char* name)
{
    struct EditorLayoutPanel* panel;

    assert(file);
    assert(name);
    assert(strlen(name) < EDITOR_LAYOUT_NAME_MAX);
    panel = (struct EditorLayoutPanel*)EditorLayout_Find(file, name);
    if( panel )
        return panel;
    assert(file->panel_count < EDITOR_LAYOUT_PANELS_MAX);
    panel = &file->panels[file->panel_count++];
    memset(panel, 0, sizeof(*panel));
    snprintf(panel->name, sizeof(panel->name), "%s", name);
    return panel;
}

static void
layout_trim(char* text)
{
    size_t len;
    char* start = text;

    while( *start && isspace((unsigned char)*start) )
        start++;
    len = strlen(start);
    while( len > 0 && isspace((unsigned char)start[len - 1]) )
        len--;
    memmove(text, start, len);
    text[len] = '\0';
}

/** A whole integer (optionally signed), or 0 when `text` is not one. */
static int
layout_int(
    const char* text,
    int* out)
{
    char* end = NULL;
    long v;

    if( !text[0] )
        return 0;
    v = strtol(text, &end, 10);
    /* A trailing comment is allowed: `x = 38 ; moved by hand`. */
    while( end && *end && isspace((unsigned char)*end) )
        end++;
    if( !end || (*end && *end != ';' && *end != '#') )
        return 0;
    if( v < -1000000 || v > 1000000 )
        return 0;
    *out = (int)v;
    return 1;
}

/* Section states. */
#define LAYOUT_SECTION_NONE (-2)
#define LAYOUT_SECTION_META (-1)

int
EditorLayout_Decode(
    struct EditorLayoutFile* out,
    const void* data,
    int size)
{
    struct INIReader reader;
    struct INIElement element;
    int section = LAYOUT_SECTION_NONE;
    int found = 0;

    assert(out);
    EditorLayout_Clear(out);
    if( size <= 0 )
        return 0;
    assert(data);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, (uint8_t*)(uintptr_t)data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            char* name = element._section.name;
            layout_trim(name);
            section = LAYOUT_SECTION_NONE;
            if( strcmp(name, "layout") == 0 )
            {
                section = LAYOUT_SECTION_META;
                found = 1;
            }
            else if( strncmp(name, "panel", 5) == 0 && isspace((unsigned char)name[5]) )
            {
                char* panel_name = name + 5;
                layout_trim(panel_name);
                if( panel_name[0] && strlen(panel_name) < EDITOR_LAYOUT_NAME_MAX
                    && (EditorLayout_Find(out, panel_name) || out->panel_count < EDITOR_LAYOUT_PANELS_MAX) )
                    section = (int)(EditorLayout_Add(out, panel_name) - out->panels);
            }
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL || section == LAYOUT_SECTION_NONE )
            continue;
        layout_trim(element._keyval.name);
        layout_trim(element._keyval.value);
        {
            const char* key = element._keyval.name;
            int v;
            if( !layout_int(element._keyval.value, &v) )
                continue;
            if( section == LAYOUT_SECTION_META )
            {
                if( strcmp(key, "version") == 0 )
                    out->version = v;
                else if( strcmp(key, "surface_w") == 0 && v > 0 )
                    out->surface_w = v;
                else if( strcmp(key, "surface_h") == 0 && v > 0 )
                    out->surface_h = v;
                else if( strcmp(key, "scale") == 0 && v > 0 )
                    out->scale = v;
                else if( strcmp(key, "snap") == 0 )
                {
                    out->has_snap = 1;
                    out->snap = v ? 1 : 0;
                }
                continue;
            }
            {
                struct EditorLayoutPanel* p = &out->panels[section];
                if( strcmp(key, "visible") == 0 )
                {
                    p->has_visible = 1;
                    p->visible = v ? 1 : 0;
                }
                else if( strcmp(key, "user_placed") == 0 )
                {
                    p->has_user_placed = 1;
                    p->user_placed = v ? 1 : 0;
                }
                else if( strcmp(key, "x") == 0 )
                {
                    p->has_x = 1;
                    p->x = v;
                }
                else if( strcmp(key, "y") == 0 )
                {
                    p->has_y = 1;
                    p->y = v;
                }
                else if( strcmp(key, "w") == 0 && v >= 0 )
                {
                    p->has_w = 1;
                    p->w = v;
                }
                else if( strcmp(key, "h") == 0 && v >= 0 )
                {
                    p->has_h = 1;
                    p->h = v;
                }
            }
        }
    }
    return found;
}

void
EditorLayout_Encode(
    const struct EditorLayoutFile* file,
    char** out_data,
    int* out_size)
{
    /* Bounded: a header, and per panel one section line and six short
     * `key = int` lines. */
    int const cap = 1024 + EDITOR_LAYOUT_PANELS_MAX * (EDITOR_LAYOUT_NAME_MAX + 6 * 32 + 16);
    char* text;
    int len = 0;

    assert(file);
    assert(out_data);
    assert(out_size);
    assert(file->panel_count >= 0);
    assert(file->panel_count <= EDITOR_LAYOUT_PANELS_MAX);

    text = ToriMem_Alloc(ToriMem_Process(), (size_t)cap);
    assert(text);
    len += snprintf(text + len, (size_t)(cap - len),
        "; ToriRSEd dock layout. Written by the map editor (src/editor/editor_layout.c)\n"
        "; whenever a panel moves, resizes, opens or closes; safe to hand-edit.\n"
        "; Geometry is in chrome pixels at scale 1. On the same surface and scale every\n"
        "; panel gets its x/y/w/h back; on another surface only panels with\n"
        "; user_placed = 1 do, and the rest are placed by the default layout for it.\n"
        "\n[layout]\n"
        "version = %d\n"
        "surface_w = %d\n"
        "surface_h = %d\n"
        "scale = %d\n",
        EDITOR_LAYOUT_VERSION, file->surface_w, file->surface_h, file->scale);
    if( file->has_snap )
        len += snprintf(text + len, (size_t)(cap - len), "snap = %d\n", file->snap ? 1 : 0);
    for( int i = 0; i < file->panel_count; i++ )
    {
        const struct EditorLayoutPanel* p = &file->panels[i];
        len += snprintf(text + len, (size_t)(cap - len), "\n[panel %s]\n", p->name);
        if( p->has_visible )
            len += snprintf(text + len, (size_t)(cap - len), "visible = %d\n", p->visible ? 1 : 0);
        if( p->has_user_placed )
            len += snprintf(text + len, (size_t)(cap - len), "user_placed = %d\n", p->user_placed ? 1 : 0);
        if( p->has_x )
            len += snprintf(text + len, (size_t)(cap - len), "x = %d\n", p->x);
        if( p->has_y )
            len += snprintf(text + len, (size_t)(cap - len), "y = %d\n", p->y);
        if( p->has_w )
            len += snprintf(text + len, (size_t)(cap - len), "w = %d\n", p->w);
        if( p->has_h )
            len += snprintf(text + len, (size_t)(cap - len), "h = %d\n", p->h);
    }
    assert(len < cap);
    *out_data = text;
    *out_size = len;
}
