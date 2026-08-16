#include "pg_gui.h"

#include "pg_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_GUI_ROW_HEIGHT 14.0f

static void
unpack(uint32_t colour, float out[4])
{
    out[0] = (float)((colour >> 24) & 0xFF) / 255.0f;
    out[1] = (float)((colour >> 16) & 0xFF) / 255.0f;
    out[2] = (float)((colour >> 8) & 0xFF) / 255.0f;
    out[3] = (float)(colour & 0xFF) / 255.0f;
}

int
pg_gui_init(struct PG_Gui* gui, struct PG_Renderer* renderer)
{
    memset(gui, 0, sizeof(*gui));
    gui->renderer = renderer;
    gui->hot = 0;
    gui->active = 0;
    gui->vertex_capacity = 4096;
    gui->vertices = malloc((size_t)gui->vertex_capacity * sizeof(*gui->vertices));
    if( !gui->vertices )
        return -1;

    pg_glGenVertexArrays(1, &gui->vao);
    pg_glBindVertexArray(gui->vao);
    pg_glGenBuffers(1, &gui->vbo);
    pg_glBindBuffer(GL_ARRAY_BUFFER, gui->vbo);
    pg_glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, (PG_GLsizei)sizeof(struct PG_GuiVertex), (void*)0);
    pg_glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, (PG_GLsizei)sizeof(struct PG_GuiVertex),
        (void*)(2 * sizeof(float)));
    pg_glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, (PG_GLsizei)sizeof(struct PG_GuiVertex),
        (void*)(4 * sizeof(float)));
    pg_glEnableVertexAttribArray(0);
    pg_glEnableVertexAttribArray(1);
    pg_glEnableVertexAttribArray(2);
    pg_glBindBuffer(GL_ARRAY_BUFFER, 0);
    pg_glBindVertexArray(0);
    return 0;
}

void
pg_gui_free(struct PG_Gui* gui)
{
    if( gui->vbo )
        pg_glDeleteBuffers(1, &gui->vbo);
    if( gui->vao )
        pg_glDeleteVertexArrays(1, &gui->vao);
    free(gui->vertices);
    memset(gui, 0, sizeof(*gui));
}

static void
flush(struct PG_Gui* gui)
{
    if( gui->vertex_count == 0 )
        return;
    pg_glUseProgram(gui->renderer->gui_shader.program);
    pg_glUniform2f(
        pg_glGetUniformLocation(gui->renderer->gui_shader.program, "screen"),
        gui->width,
        gui->height);
    pg_glUniform1i(pg_glGetUniformLocation(gui->renderer->gui_shader.program, "atlas"), 0);
    pg_glActiveTexture(GL_TEXTURE0);
    pg_glBindTexture(GL_TEXTURE_2D, pg_font_texture());

    pg_glBindVertexArray(gui->vao);
    pg_glBindBuffer(GL_ARRAY_BUFFER, gui->vbo);
    pg_glBufferData(
        GL_ARRAY_BUFFER,
        (PG_GLsizeiptr)((size_t)gui->vertex_count * sizeof(struct PG_GuiVertex)),
        gui->vertices,
        GL_DYNAMIC_DRAW);
    pg_glDrawArrays(GL_TRIANGLES, 0, gui->vertex_count);
    pg_glBindBuffer(GL_ARRAY_BUFFER, 0);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
    gui->vertex_count = 0;
}

static void
push_quad(
    struct PG_Gui* gui,
    float x,
    float y,
    float w,
    float h,
    float u0,
    float v0,
    float u1,
    float v1,
    uint32_t colour)
{
    float c[4];
    struct PG_GuiVertex* dst;
    const float xs[6] = { x, x + w, x + w, x, x + w, x };
    const float ys[6] = { y, y, y + h, y, y + h, y + h };
    const float us[6] = { u0, u1, u1, u0, u1, u0 };
    const float vs[6] = { v0, v0, v1, v0, v1, v1 };

    if( w <= 0.0f || h <= 0.0f )
        return;
    if( gui->vertex_count + 6 > gui->vertex_capacity )
    {
        int next = gui->vertex_capacity * 2;
        struct PG_GuiVertex* grown = realloc(gui->vertices, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            flush(gui);
            return;
        }
        gui->vertices = grown;
        gui->vertex_capacity = next;
    }
    unpack(colour, c);
    dst = &gui->vertices[gui->vertex_count];
    for( int i = 0; i < 6; i++ )
    {
        dst[i].x = xs[i];
        dst[i].y = ys[i];
        dst[i].u = us[i];
        dst[i].v = vs[i];
        dst[i].r = c[0];
        dst[i].g = c[1];
        dst[i].b = c[2];
        dst[i].a = c[3];
    }
    gui->vertex_count += 6;
}

void
pg_gui_begin(
    struct PG_Gui* gui,
    float width,
    float height,
    float dpi,
    const struct PG_GuiInput* input)
{
    gui->width = width;
    gui->height = height;
    gui->dpi = dpi > 0.0f ? dpi : 1.0f;
    gui->input = *input;
    gui->vertex_count = 0;
    gui->hot = 0;
    gui->mouse_captured = false;
    gui->clipping = false;

    pg_glDisable(GL_DEPTH_TEST);
    pg_glEnable(GL_BLEND);
    pg_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pg_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    pg_glDisable(GL_CULL_FACE);
}

void
pg_gui_end(struct PG_Gui* gui)
{
    flush(gui);
    pg_glDisable(GL_SCISSOR_TEST);
    /* A drag ends wherever the button comes up, not only over the widget. */
    if( !gui->input.down[PG_MOUSE_LEFT] )
        gui->active = 0;
}

void
pg_gui_push_clip(struct PG_Gui* gui, float x, float y, float w, float h)
{
    flush(gui);
    gui->clip[0] = (int)x;
    gui->clip[1] = (int)y;
    gui->clip[2] = (int)(w < 0 ? 0 : w);
    gui->clip[3] = (int)(h < 0 ? 0 : h);
    gui->clipping = true;
    pg_glEnable(GL_SCISSOR_TEST);
    /* Scissor is bottom-up and in device pixels; the UI is top-down and in points. */
    pg_glScissor(
        (int)((float)gui->clip[0] * gui->dpi),
        (int)((gui->height - (float)(gui->clip[1] + gui->clip[3])) * gui->dpi),
        (int)((float)gui->clip[2] * gui->dpi),
        (int)((float)gui->clip[3] * gui->dpi));
}

void
pg_gui_pop_clip(struct PG_Gui* gui)
{
    flush(gui);
    gui->clipping = false;
    pg_glDisable(GL_SCISSOR_TEST);
}

bool
pg_gui_hover(struct PG_Gui* gui, float x, float y, float w, float h)
{
    float mx = gui->input.mouse_x;
    float my = gui->input.mouse_y;
    if( gui->clipping )
    {
        if( mx < (float)gui->clip[0] || my < (float)gui->clip[1] ||
            mx >= (float)(gui->clip[0] + gui->clip[2]) ||
            my >= (float)(gui->clip[1] + gui->clip[3]) )
            return false;
    }
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

void
pg_gui_rect(struct PG_Gui* gui, float x, float y, float w, float h, uint32_t colour)
{
    push_quad(gui, x, y, w, h, -1.0f, -1.0f, -1.0f, -1.0f, colour);
}

void
pg_gui_border(struct PG_Gui* gui, float x, float y, float w, float h, uint32_t colour)
{
    pg_gui_rect(gui, x, y, w, 1, colour);
    pg_gui_rect(gui, x, y + h - 1, w, 1, colour);
    pg_gui_rect(gui, x, y, 1, h, colour);
    pg_gui_rect(gui, x + w - 1, y, 1, h, colour);
}

void
pg_gui_text(struct PG_Gui* gui, float x, float y, const char* text, uint32_t colour)
{
    float cursor = x;
    if( !text )
        return;
    for( const char* p = text; *p; p++ )
    {
        int cx, cy;
        pg_font_cell(*p, &cx, &cy);
        if( *p != ' ' )
        {
            push_quad(
                gui,
                cursor,
                y,
                (float)PG_FONT_GLYPH_W,
                (float)PG_FONT_GLYPH_H,
                (float)cx / (float)PG_FONT_ATLAS_W,
                (float)cy / (float)PG_FONT_ATLAS_H,
                (float)(cx + PG_FONT_GLYPH_W) / (float)PG_FONT_ATLAS_W,
                (float)(cy + PG_FONT_GLYPH_H) / (float)PG_FONT_ATLAS_H,
                colour);
        }
        cursor += (float)PG_FONT_CELL_W;
    }
}

void
pg_gui_text_clipped(
    struct PG_Gui* gui,
    float x,
    float y,
    float max_width,
    const char* text,
    uint32_t colour)
{
    char buffer[256];
    int max_chars;

    if( !text )
        return;
    max_chars = (int)(max_width / (float)PG_FONT_CELL_W);
    if( max_chars <= 0 )
        return;
    if( (int)strlen(text) <= max_chars )
    {
        pg_gui_text(gui, x, y, text, colour);
        return;
    }
    if( max_chars > (int)sizeof(buffer) - 1 )
        max_chars = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)max_chars);
    buffer[max_chars] = '\0';
    if( max_chars >= 2 )
    {
        buffer[max_chars - 1] = '.';
        buffer[max_chars - 2] = '.';
    }
    pg_gui_text(gui, x, y, buffer, colour);
}

void
pg_gui_text_centred(
    struct PG_Gui* gui,
    float x,
    float y,
    float w,
    const char* text,
    uint32_t colour)
{
    float text_width = (float)pg_font_text_width(text);
    pg_gui_text(gui, x + (w - text_width) / 2.0f, y, text, colour);
}

/* ---- widgets ---------------------------------------------------------------- */

static bool
widget_click(struct PG_Gui* gui, int id, float x, float y, float w, float h)
{
    bool hovered = pg_gui_hover(gui, x, y, w, h);
    bool clicked = false;

    if( hovered )
    {
        gui->hot = id;
        gui->mouse_captured = true;
        if( gui->input.pressed[PG_MOUSE_LEFT] )
            gui->active = id;
        /* Fire on release over the widget, which is what lets a press be
         * cancelled by dragging off. */
        if( gui->input.released[PG_MOUSE_LEFT] && gui->active == id )
            clicked = true;
    }
    return clicked;
}

bool
pg_gui_button(struct PG_Gui* gui, int id, float x, float y, float w, float h, const char* label)
{
    bool clicked = widget_click(gui, id, x, y, w, h);
    uint32_t fill = gui->active == id && gui->hot == id
                        ? PG_COL_ACCENT
                        : (gui->hot == id ? PG_COL_HOVER : PG_COL_WIDGET);
    pg_gui_rect(gui, x, y, w, h, fill);
    pg_gui_text_centred(gui, x, y + (h - PG_FONT_GLYPH_H) / 2.0f, w, label, PG_COL_TEXT);
    return clicked;
}

bool
pg_gui_toggle(
    struct PG_Gui* gui,
    int id,
    float x,
    float y,
    float w,
    float h,
    const char* label,
    bool selected,
    bool enabled)
{
    bool clicked = enabled && widget_click(gui, id, x, y, w, h);
    uint32_t fill = selected ? PG_COL_ACCENT
                             : (enabled && gui->hot == id ? PG_COL_HOVER : PG_COL_WIDGET);
    pg_gui_rect(gui, x, y, w, h, fill);
    pg_gui_text_centred(
        gui,
        x,
        y + (h - PG_FONT_GLYPH_H) / 2.0f,
        w,
        label,
        enabled ? PG_COL_TEXT : PG_COL_TEXT_DIM);
    return clicked;
}

bool
pg_gui_slider(
    struct PG_Gui* gui,
    int id,
    float x,
    float y,
    float w,
    float h,
    int* value,
    int min_value,
    int max_value)
{
    char text[32];
    bool hovered = pg_gui_hover(gui, x, y, w, h);
    bool changed = false;
    float fill_fraction;

    if( hovered )
    {
        gui->hot = id;
        gui->mouse_captured = true;
        if( gui->input.pressed[PG_MOUSE_LEFT] )
        {
            gui->active = id;
            /* Anchor to where the drag began so the value tracks total movement
             * rather than accumulating rounding on each frame's delta. */
            gui->drag_origin = gui->input.mouse_x;
            gui->drag_value = (float)*value;
        }
    }
    if( gui->active == id && gui->input.down[PG_MOUSE_LEFT] )
    {
        /* A quarter of a unit per pixel: fine enough to land on an exact value
         * over a slider only 50px wide, coarse enough to cross the range. */
        float moved = (gui->input.mouse_x - gui->drag_origin) * 0.25f;
        int next = (int)(gui->drag_value + moved);
        next = pg_clampi(next, min_value, max_value);
        if( next != *value )
        {
            *value = next;
            changed = true;
        }
    }

    pg_gui_rect(gui, x, y, w, h, PG_COL_PANEL_DARK);
    fill_fraction = max_value > min_value
                        ? (float)(*value - min_value) / (float)(max_value - min_value)
                        : 0.0f;
    pg_gui_rect(
        gui,
        x,
        y,
        w * pg_clampf(fill_fraction, 0.0f, 1.0f),
        h,
        gui->active == id ? PG_COL_ACCENT : PG_COL_WIDGET);
    snprintf(text, sizeof(text), "%d", *value);
    pg_gui_text_centred(gui, x, y + (h - PG_FONT_GLYPH_H) / 2.0f, w, text, PG_COL_TEXT);
    return changed;
}

bool
pg_gui_text_input(
    struct PG_Gui* gui,
    int id,
    float x,
    float y,
    float w,
    float h,
    char* buffer,
    int capacity,
    const char* placeholder)
{
    bool hovered = pg_gui_hover(gui, x, y, w, h);
    bool changed = false;
    int length = (int)strlen(buffer);

    if( hovered )
    {
        gui->hot = id;
        gui->mouse_captured = true;
        if( gui->input.pressed[PG_MOUSE_LEFT] )
            gui->focus = id;
    }
    else if( gui->input.pressed[PG_MOUSE_LEFT] && gui->focus == id )
    {
        gui->focus = 0;
    }

    if( gui->focus == id )
    {
        if( gui->input.backspace && length > 0 )
        {
            buffer[--length] = '\0';
            changed = true;
        }
        for( const char* p = gui->input.text; *p; p++ )
        {
            if( length >= capacity - 1 )
                break;
            buffer[length++] = *p;
            buffer[length] = '\0';
            changed = true;
        }
    }

    pg_gui_rect(gui, x, y, w, h, PG_COL_PANEL_DARK);
    if( gui->focus == id )
        pg_gui_border(gui, x, y, w, h, PG_COL_ACCENT);
    if( length > 0 )
    {
        pg_gui_text_clipped(
            gui, x + 3, y + (h - PG_FONT_GLYPH_H) / 2.0f, w - 6, buffer, PG_COL_TEXT);
        if( gui->focus == id )
            pg_gui_rect(
                gui, x + 3 + (float)pg_font_text_width(buffer), y + 2, 1, h - 4, PG_COL_TEXT);
    }
    else
    {
        pg_gui_text_clipped(
            gui, x + 3, y + (h - PG_FONT_GLYPH_H) / 2.0f, w - 6, placeholder, PG_COL_TEXT_DIM);
    }
    return changed;
}

int
pg_gui_list(
    struct PG_Gui* gui,
    int id,
    float x,
    float y,
    float w,
    float h,
    int count,
    void (*row)(void* ctx, int index, struct PG_GuiListRow* out),
    void* ctx,
    float* scroll,
    int selected)
{
    const float row_h = PG_GUI_ROW_HEIGHT;
    float visible = h / row_h;
    float max_scroll = (float)count - visible;
    int clicked = -1;
    int first;
    int last;
    bool hovered = pg_gui_hover(gui, x, y, w, h);
    float bar_w = 6.0f;

    if( max_scroll < 0.0f )
        max_scroll = 0.0f;
    if( hovered )
    {
        gui->mouse_captured = true;
        if( gui->input.wheel != 0.0f )
            *scroll -= gui->input.wheel * 3.0f;
    }
    *scroll = pg_clampf(*scroll, 0.0f, max_scroll);

    pg_gui_rect(gui, x, y, w, h, PG_COL_PANEL_DARK);
    pg_gui_push_clip(gui, x, y, w, h);

    first = (int)(*scroll);
    last = first + (int)visible + 1;
    if( last > count )
        last = count;

    for( int i = first; i < last; i++ )
    {
        struct PG_GuiListRow info = { NULL, false };
        float ry = y + ((float)i - *scroll) * row_h;
        bool row_hover = pg_gui_hover(gui, x, ry, w - bar_w, row_h);
        uint32_t colour;

        row(ctx, i, &info);
        if( i == selected )
            pg_gui_rect(gui, x, ry, w - bar_w, row_h, PG_COL_ACCENT);
        else if( row_hover )
            pg_gui_rect(gui, x, ry, w - bar_w, row_h, PG_COL_WIDGET);

        colour = info.marked ? PG_COL_YELLOW : PG_COL_TEXT;
        pg_gui_text_clipped(gui, x + 3, ry + 3, w - bar_w - 6, info.text, colour);

        if( row_hover && gui->input.released[PG_MOUSE_LEFT] && gui->active == id )
            clicked = i;
    }
    if( hovered && gui->input.pressed[PG_MOUSE_LEFT] )
        gui->active = id;

    pg_gui_pop_clip(gui);

    if( max_scroll > 0.0f )
    {
        float track = h;
        float thumb = track * (visible / (float)count);
        float thumb_y;
        if( thumb < 12.0f )
            thumb = 12.0f;
        thumb_y = y + (track - thumb) * (*scroll / max_scroll);
        pg_gui_rect(gui, x + w - bar_w, y, bar_w, h, PG_COL_BACKGROUND);
        pg_gui_rect(gui, x + w - bar_w, thumb_y, bar_w, thumb, PG_COL_WIDGET);
    }
    return clicked;
}
