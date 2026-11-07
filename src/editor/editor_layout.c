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

const struct EditorLayoutGroup*
EditorLayout_FindGroup(
    const struct EditorLayoutFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->group_count; i++ )
        if( strcmp(file->groups[i].name, name) == 0 )
            return &file->groups[i];
    return NULL;
}

struct EditorLayoutGroup*
EditorLayout_AddGroup(
    struct EditorLayoutFile* file,
    const char* name)
{
    struct EditorLayoutGroup* group;

    assert(file);
    assert(name);
    assert(strlen(name) < EDITOR_LAYOUT_NAME_MAX);
    group = (struct EditorLayoutGroup*)EditorLayout_FindGroup(file, name);
    if( group )
        return group;
    assert(file->group_count < EDITOR_LAYOUT_GROUPS_MAX);
    group = &file->groups[file->group_count++];
    memset(group, 0, sizeof(*group));
    snprintf(group->name, sizeof(group->name), "%s", name);
    return group;
}

const struct EditorLayoutWorkspace*
EditorLayout_FindWorkspace(
    const struct EditorLayoutFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->workspace_count; i++ )
        if( strcmp(file->workspaces[i].name, name) == 0 )
            return &file->workspaces[i];
    return NULL;
}

struct EditorLayoutWorkspace*
EditorLayout_AddWorkspace(
    struct EditorLayoutFile* file,
    const char* name)
{
    struct EditorLayoutWorkspace* ws;

    assert(file);
    assert(name);
    assert(strlen(name) < EDITOR_LAYOUT_NAME_MAX);
    ws = (struct EditorLayoutWorkspace*)EditorLayout_FindWorkspace(file, name);
    if( ws )
        return ws;
    assert(file->workspace_count < EDITOR_LAYOUT_WORKSPACES_MAX);
    ws = &file->workspaces[file->workspace_count++];
    memset(ws, 0, sizeof(*ws));
    snprintf(ws->name, sizeof(ws->name), "%s", name);
    return ws;
}

void
EditorLayout_WorkspaceFromTop(
    struct EditorLayoutWorkspace* ws,
    const struct EditorLayoutFile* file)
{
    assert(ws);
    assert(file);
    ws->surface_w = file->surface_w;
    ws->surface_h = file->surface_h;
    ws->scale = file->scale;
    memcpy(ws->panels, file->panels, sizeof(ws->panels));
    ws->panel_count = file->panel_count;
    /* The compact dock's collapsed wish is the window's (the top section's
     * [panel] keys), not a workspace's: its line has no field for it. */
    for( int i = 0; i < ws->panel_count; i++ )
    {
        ws->panels[i].has_collapsed = 0;
        ws->panels[i].collapsed = 0;
    }
    memcpy(ws->groups, file->groups, sizeof(ws->groups));
    ws->group_count = file->group_count;
}

void
EditorLayout_TopFromWorkspace(
    struct EditorLayoutFile* out,
    const struct EditorLayoutWorkspace* ws)
{
    assert(out);
    assert(ws);
    EditorLayout_Clear(out);
    out->version = EDITOR_LAYOUT_VERSION;
    out->surface_w = ws->surface_w;
    out->surface_h = ws->surface_h;
    out->scale = ws->scale;
    memcpy(out->panels, ws->panels, sizeof(out->panels));
    out->panel_count = ws->panel_count;
    memcpy(out->groups, ws->groups, sizeof(out->groups));
    out->group_count = ws->group_count;
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

/* Section states: a panel's index, or one of these; a group is
 * LAYOUT_SECTION_GROUP minus its index (down to LAYOUT_SECTION_GROUP_END), a
 * workspace LAYOUT_SECTION_WORKSPACE minus its. */
#define LAYOUT_SECTION_NONE (-2)
#define LAYOUT_SECTION_META (-1)
#define LAYOUT_SECTION_GROUP (-3)
#define LAYOUT_SECTION_WORKSPACE (-1000)

/** A name value: a trailing comment cut, trimmed, one word that fits. */
static int
layout_name_value(
    char* value,
    char* out)
{
    char* cut = strpbrk(value, ";#");
    if( cut )
        *cut = '\0';
    layout_trim(value);
    if( !value[0] || strlen(value) >= EDITOR_LAYOUT_NAME_MAX || strpbrk(value, " \t") )
        return 0;
    memset(out, 0, EDITOR_LAYOUT_NAME_MAX);
    memcpy(out, value, strlen(value));
    return 1;
}

/** The next blank-separated token of `*at`, NUL-terminated in place, or NULL. */
static char*
layout_token(char** at)
{
    char* p = *at;
    char* start;
    while( *p && isspace((unsigned char)*p) )
        p++;
    if( !*p )
        return NULL;
    start = p;
    while( *p && !isspace((unsigned char)*p) )
        p++;
    if( *p )
        *p++ = '\0';
    *at = p;
    return start;
}

/** One workspace row's fields: visible user_placed x y w h maximised, each
 *  an int or "-" (unstated), into has[] / v[]. Returns the tokens read (a
 *  bad token stops it: the fields before it stand). */
static int
layout_row_fields(
    char** at,
    int* has,
    int* v)
{
    int n = 0;
    for( ; n < 7; n++ )
    {
        char* tok = layout_token(at);
        if( !tok )
            break;
        has[n] = 0;
        if( strcmp(tok, "-") == 0 )
            continue;
        if( !layout_int(tok, &v[n]) )
            break;
        has[n] = 1;
    }
    return n;
}

/** `surface = W H S`, `panel.<name> = ...` or `group.<name> = ...` in
 *  workspace `ws`. Anything else is skipped. */
static void
layout_workspace_row(
    struct EditorLayoutWorkspace* ws,
    const char* key,
    char* value)
{
    int has[7] = { 0 };
    int v[7] = { 0 };
    char* cut = strpbrk(value, ";#");
    char* at = value;

    if( cut )
        *cut = '\0';
    if( strcmp(key, "surface") == 0 )
    {
        int n = 0;
        int got[3] = { 0 };
        char* tok;
        while( n < 3 && (tok = layout_token(&at)) != NULL && layout_int(tok, &got[n]) )
            n++;
        if( n == 3 && got[0] > 0 && got[1] > 0 && got[2] > 0 )
        {
            ws->surface_w = got[0];
            ws->surface_h = got[1];
            ws->scale = got[2];
        }
        return;
    }
    if( strncmp(key, "panel.", 6) == 0 && key[6] && strlen(key + 6) < EDITOR_LAYOUT_NAME_MAX )
    {
        struct EditorLayoutPanel* p = NULL;
        for( int i = 0; i < ws->panel_count; i++ )
            if( strcmp(ws->panels[i].name, key + 6) == 0 )
                p = &ws->panels[i];
        if( !p )
        {
            if( ws->panel_count >= EDITOR_LAYOUT_PANELS_MAX )
                return;
            p = &ws->panels[ws->panel_count++];
            memset(p, 0, sizeof(*p));
            snprintf(p->name, sizeof(p->name), "%s", key + 6);
        }
        (void)layout_row_fields(&at, has, v);
        p->has_visible = has[0];
        p->visible = v[0] ? 1 : 0;
        p->has_user_placed = has[1];
        p->user_placed = v[1] ? 1 : 0;
        p->has_x = has[2];
        p->x = v[2];
        p->has_y = has[3];
        p->y = v[3];
        p->has_w = has[4] && v[4] >= 0;
        p->w = v[4];
        p->has_h = has[5] && v[5] >= 0;
        p->h = v[5];
        p->has_maximised = has[6];
        p->maximised = v[6] ? 1 : 0;
        return;
    }
    if( strncmp(key, "group.", 6) == 0 && key[6] && strlen(key + 6) < EDITOR_LAYOUT_NAME_MAX )
    {
        struct EditorLayoutGroup* g = NULL;
        char* shown;
        for( int i = 0; i < ws->group_count; i++ )
            if( strcmp(ws->groups[i].name, key + 6) == 0 )
                g = &ws->groups[i];
        if( !g )
        {
            if( ws->group_count >= EDITOR_LAYOUT_GROUPS_MAX )
                return;
            g = &ws->groups[ws->group_count++];
            memset(g, 0, sizeof(*g));
            snprintf(g->name, sizeof(g->name), "%s", key + 6);
        }
        if( layout_row_fields(&at, has, v) == 7 && (shown = layout_token(&at)) != NULL && strcmp(shown, "-") != 0
            && strlen(shown) < EDITOR_LAYOUT_NAME_MAX )
        {
            g->has_shown = 1;
            memset(g->shown, 0, sizeof(g->shown));
            memcpy(g->shown, shown, strlen(shown));
        }
        g->has_visible = has[0];
        g->visible = v[0] ? 1 : 0;
        g->has_user_placed = has[1];
        g->user_placed = v[1] ? 1 : 0;
        g->has_x = has[2];
        g->x = v[2];
        g->has_y = has[3];
        g->y = v[3];
        g->has_w = has[4] && v[4] >= 0;
        g->w = v[4];
        g->has_h = has[5] && v[5] >= 0;
        g->h = v[5];
        g->has_maximised = has[6];
        g->maximised = v[6] ? 1 : 0;
    }
}

/** `[<kind> <name>]`: the name after the kind word, trimmed, or NULL when the
 *  section is not of that kind. */
static char*
layout_section_named(
    char* section,
    const char* kind)
{
    size_t const n = strlen(kind);
    char* name;

    if( strncmp(section, kind, n) != 0 || !isspace((unsigned char)section[n]) )
        return NULL;
    /* Past the separating blanks without moving them, so the section text
     * still reads `<kind> <name>`. */
    name = section + n;
    while( *name && isspace((unsigned char)*name) )
        name++;
    layout_trim(name);
    return name;
}

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
            char* panel_name;
            char* group_name;
            char* workspace_name;
            layout_trim(name);
            section = LAYOUT_SECTION_NONE;
            panel_name = layout_section_named(name, "panel");
            group_name = layout_section_named(name, "group");
            workspace_name = layout_section_named(name, "workspace");
            if( strcmp(name, "layout") == 0 )
            {
                section = LAYOUT_SECTION_META;
                found = 1;
            }
            else if( panel_name )
            {
                if( panel_name[0] && strlen(panel_name) < EDITOR_LAYOUT_NAME_MAX
                    && (EditorLayout_Find(out, panel_name) || out->panel_count < EDITOR_LAYOUT_PANELS_MAX) )
                    section = (int)(EditorLayout_Add(out, panel_name) - out->panels);
            }
            else if( group_name )
            {
                if( group_name[0] && strlen(group_name) < EDITOR_LAYOUT_NAME_MAX
                    && (EditorLayout_FindGroup(out, group_name) || out->group_count < EDITOR_LAYOUT_GROUPS_MAX) )
                    section = LAYOUT_SECTION_GROUP - (int)(EditorLayout_AddGroup(out, group_name) - out->groups);
            }
            else if( workspace_name )
            {
                if( workspace_name[0] && strlen(workspace_name) < EDITOR_LAYOUT_NAME_MAX
                    && !strpbrk(workspace_name, " \t")
                    && (EditorLayout_FindWorkspace(out, workspace_name)
                        || out->workspace_count < EDITOR_LAYOUT_WORKSPACES_MAX) )
                    section = LAYOUT_SECTION_WORKSPACE
                              - (int)(EditorLayout_AddWorkspace(out, workspace_name) - out->workspaces);
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
            /* Before the group test below: a workspace's state is under it. */
            if( section <= LAYOUT_SECTION_WORKSPACE )
            {
                layout_workspace_row(
                    &out->workspaces[LAYOUT_SECTION_WORKSPACE - section], key, element._keyval.value);
                continue;
            }
            /* [layout]'s one name: the current workspace. */
            if( section == LAYOUT_SECTION_META && strcmp(key, "workspace") == 0 )
            {
                if( layout_name_value(element._keyval.value, out->workspace) )
                    out->has_workspace = 1;
                continue;
            }
            /* The one key whose value is a name: the group's front tab. A
             * trailing comment is cut off, as layout_int allows one. */
            if( section <= LAYOUT_SECTION_GROUP && strcmp(key, "shown") == 0 )
            {
                struct EditorLayoutGroup* g = &out->groups[LAYOUT_SECTION_GROUP - section];
                char* value = element._keyval.value;
                char* cut = strpbrk(value, ";#");
                if( cut )
                    *cut = '\0';
                layout_trim(value);
                if( value[0] && strlen(value) < EDITOR_LAYOUT_NAME_MAX && !strpbrk(value, " \t") )
                {
                    g->has_shown = 1;
                    memset(g->shown, 0, sizeof(g->shown));
                    memcpy(g->shown, value, strlen(value));
                }
                continue;
            }
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
                else if( strcmp(key, "compact") == 0 && v >= -1 && v <= 1 )
                {
                    out->has_compact = 1;
                    out->compact = v;
                }
                else if( strcmp(key, "window_x") == 0 )
                    out->window_x = v;
                else if( strcmp(key, "window_y") == 0 )
                    out->window_y = v;
                else if( strcmp(key, "window_w") == 0 && v > 0 )
                {
                    out->window_w = v;
                    out->has_window = out->window_h > 0;
                }
                else if( strcmp(key, "window_h") == 0 && v > 0 )
                {
                    out->window_h = v;
                    out->has_window = out->window_w > 0;
                }
                continue;
            }
            if( section <= LAYOUT_SECTION_GROUP )
            {
                struct EditorLayoutGroup* g = &out->groups[LAYOUT_SECTION_GROUP - section];
                if( strcmp(key, "visible") == 0 )
                {
                    g->has_visible = 1;
                    g->visible = v ? 1 : 0;
                }
                else if( strcmp(key, "user_placed") == 0 )
                {
                    g->has_user_placed = 1;
                    g->user_placed = v ? 1 : 0;
                }
                else if( strcmp(key, "x") == 0 )
                {
                    g->has_x = 1;
                    g->x = v;
                }
                else if( strcmp(key, "y") == 0 )
                {
                    g->has_y = 1;
                    g->y = v;
                }
                else if( strcmp(key, "w") == 0 && v >= 0 )
                {
                    g->has_w = 1;
                    g->w = v;
                }
                else if( strcmp(key, "h") == 0 && v >= 0 )
                {
                    g->has_h = 1;
                    g->h = v;
                }
                else if( strcmp(key, "maximised") == 0 )
                {
                    g->has_maximised = 1;
                    g->maximised = v ? 1 : 0;
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
                else if( strcmp(key, "maximised") == 0 )
                {
                    p->has_maximised = 1;
                    p->maximised = v ? 1 : 0;
                }
                else if( strcmp(key, "collapsed") == 0 )
                {
                    p->has_collapsed = 1;
                    p->collapsed = v ? 1 : 0;
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
    /* Bounded: a header, per panel one section line and eight short
     * `key = int` lines, and per group the same plus its `shown` name. */
    int const cap = 1024 + EDITOR_LAYOUT_PANELS_MAX * (EDITOR_LAYOUT_NAME_MAX + 8 * 32 + 16)
                    + EDITOR_LAYOUT_GROUPS_MAX * (2 * EDITOR_LAYOUT_NAME_MAX + 8 * 32 + 16)
                    + EDITOR_LAYOUT_WORKSPACES_MAX
                          * (256 + EDITOR_LAYOUT_PANELS_MAX * (EDITOR_LAYOUT_NAME_MAX + 7 * 12 + 16)
                              + EDITOR_LAYOUT_GROUPS_MAX * (2 * EDITOR_LAYOUT_NAME_MAX + 7 * 12 + 16));
    char* text;
    int len = 0;

    assert(file);
    assert(out_data);
    assert(out_size);
    assert(file->panel_count >= 0);
    assert(file->panel_count <= EDITOR_LAYOUT_PANELS_MAX);
    assert(file->group_count >= 0);
    assert(file->group_count <= EDITOR_LAYOUT_GROUPS_MAX);

    text = ToriMem_Alloc(ToriMem_Process(), (size_t)cap);
    assert(text);
    len += snprintf(text + len, (size_t)(cap - len),
        "; ToriRSEd dock layout. Written by the map editor (src/editor/editor_layout.c)\n"
        "; whenever a panel moves, resizes, opens or closes; safe to hand-edit.\n"
        "; Geometry is in chrome pixels at scale 1. On the same surface and scale every\n"
        "; panel gets its x/y/w/h back; on another surface only panels with\n"
        "; user_placed = 1 do, and the rest are placed by the default layout for it.\n"
        "; maximised = 1: drawn over the dock, x/y/w/h being the box it restores to.\n"
        "; compact: -1 automatic (small surfaces), 0 never, 1 always; collapsed = 1: a\n"
        "; compact-dock panel shown as its title bar. window_*: the OS window's box in\n"
        "; window points, where the next run opens it.\n"
        "; A [group] is panels behind one tab strip: its box is the whole group's,\n"
        "; shown its front tab; a member's [panel] says only whether it is in the strip.\n"
        "; A [workspace] is one workspace's layout (Ctrl+1..4), a line per panel and\n"
        "; group; [layout] workspace names the current one, whose layout is above.\n"
        "\n[layout]\n"
        "version = %d\n"
        "surface_w = %d\n"
        "surface_h = %d\n"
        "scale = %d\n",
        EDITOR_LAYOUT_VERSION, file->surface_w, file->surface_h, file->scale);
    if( file->has_snap )
        len += snprintf(text + len, (size_t)(cap - len), "snap = %d\n", file->snap ? 1 : 0);
    if( file->has_compact )
        len += snprintf(text + len, (size_t)(cap - len), "compact = %d\n", file->compact);
    if( file->has_window )
        len += snprintf(text + len, (size_t)(cap - len), "window_x = %d\nwindow_y = %d\nwindow_w = %d\nwindow_h = %d\n",
            file->window_x, file->window_y, file->window_w, file->window_h);
    if( file->has_workspace )
        len += snprintf(text + len, (size_t)(cap - len), "workspace = %s\n", file->workspace);
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
        if( p->has_maximised )
            len += snprintf(text + len, (size_t)(cap - len), "maximised = %d\n", p->maximised ? 1 : 0);
        if( p->has_collapsed )
            len += snprintf(text + len, (size_t)(cap - len), "collapsed = %d\n", p->collapsed ? 1 : 0);
    }
    for( int i = 0; i < file->group_count; i++ )
    {
        const struct EditorLayoutGroup* g = &file->groups[i];
        len += snprintf(text + len, (size_t)(cap - len), "\n[group %s]\n", g->name);
        if( g->has_visible )
            len += snprintf(text + len, (size_t)(cap - len), "visible = %d\n", g->visible ? 1 : 0);
        if( g->has_user_placed )
            len += snprintf(text + len, (size_t)(cap - len), "user_placed = %d\n", g->user_placed ? 1 : 0);
        if( g->has_x )
            len += snprintf(text + len, (size_t)(cap - len), "x = %d\n", g->x);
        if( g->has_y )
            len += snprintf(text + len, (size_t)(cap - len), "y = %d\n", g->y);
        if( g->has_w )
            len += snprintf(text + len, (size_t)(cap - len), "w = %d\n", g->w);
        if( g->has_h )
            len += snprintf(text + len, (size_t)(cap - len), "h = %d\n", g->h);
        if( g->has_maximised )
            len += snprintf(text + len, (size_t)(cap - len), "maximised = %d\n", g->maximised ? 1 : 0);
        if( g->has_shown )
            len += snprintf(text + len, (size_t)(cap - len), "shown = %s\n", g->shown);
    }
    for( int i = 0; i < file->workspace_count; i++ )
    {
        const struct EditorLayoutWorkspace* ws = &file->workspaces[i];
        assert(ws->panel_count >= 0);
        assert(ws->panel_count <= EDITOR_LAYOUT_PANELS_MAX);
        assert(ws->group_count >= 0);
        assert(ws->group_count <= EDITOR_LAYOUT_GROUPS_MAX);
        len += snprintf(text + len, (size_t)(cap - len), "\n[workspace %s]\n", ws->name);
        if( ws->surface_w > 0 && ws->surface_h > 0 && ws->scale > 0 )
            len += snprintf(
                text + len, (size_t)(cap - len), "surface = %d %d %d\n", ws->surface_w, ws->surface_h, ws->scale);
        if( ws->panel_count > 0 )
            len += snprintf(
                text + len, (size_t)(cap - len), "; panel.<module> = visible user_placed x y w h maximised\n");
        for( int k = 0; k < ws->panel_count; k++ )
        {
            const struct EditorLayoutPanel* p = &ws->panels[k];
            int const has[7] = { p->has_visible, p->has_user_placed, p->has_x,        p->has_y,
                                 p->has_w,       p->has_h,           p->has_maximised };
            int const v[7] = { p->visible ? 1 : 0, p->user_placed ? 1 : 0, p->x, p->y, p->w, p->h,
                               p->maximised ? 1 : 0 };
            len += snprintf(text + len, (size_t)(cap - len), "panel.%s =", p->name);
            for( int f = 0; f < 7; f++ )
                len += has[f] ? snprintf(text + len, (size_t)(cap - len), " %d", v[f])
                              : snprintf(text + len, (size_t)(cap - len), " -");
            len += snprintf(text + len, (size_t)(cap - len), "\n");
        }
        if( ws->group_count > 0 )
            len += snprintf(
                text + len, (size_t)(cap - len), "; group.<name> = visible user_placed x y w h maximised shown\n");
        for( int k = 0; k < ws->group_count; k++ )
        {
            const struct EditorLayoutGroup* g = &ws->groups[k];
            int const has[7] = { g->has_visible, g->has_user_placed, g->has_x,        g->has_y,
                                 g->has_w,       g->has_h,           g->has_maximised };
            int const v[7] = { g->visible ? 1 : 0, g->user_placed ? 1 : 0, g->x, g->y, g->w, g->h,
                               g->maximised ? 1 : 0 };
            len += snprintf(text + len, (size_t)(cap - len), "group.%s =", g->name);
            for( int f = 0; f < 7; f++ )
                len += has[f] ? snprintf(text + len, (size_t)(cap - len), " %d", v[f])
                              : snprintf(text + len, (size_t)(cap - len), " -");
            len += snprintf(text + len, (size_t)(cap - len), " %s\n", g->has_shown ? g->shown : "-");
        }
    }
    assert(len < cap);
    *out_data = text;
    *out_size = len;
}
