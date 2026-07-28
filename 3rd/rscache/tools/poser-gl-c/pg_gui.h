#ifndef POSER_GL_C_PG_GUI_H
#define POSER_GL_C_PG_GUI_H

/*
 * A small immediate-mode UI, in place of LEGUI.
 *
 * poser-gl builds a retained component tree with a flexbox layout engine, a
 * theme system and NanoVG text. None of that exists in C here, and pulling in an
 * equivalent would dwarf the editor it serves. What the editor actually asks of
 * its UI is narrow — panels, buttons, three scrolling lists, numeric sliders, a
 * search box and a timeline — and every one of those is a rectangle plus a hit
 * test, which is what an immediate-mode layer is.
 *
 * The trade this makes: layout is explicit pixel arithmetic at each call site
 * rather than declarative, and the toolbar carries text labels where poser-gl
 * has PNG icons (there is no image decoder in this tool, and a label says what
 * an unlabelled glyph only implies).
 */

#include "pg_gl.h"
#include "pg_render.h"

#include <stdbool.h>
#include <stdint.h>

#define PG_RGBA(r, g, b, a)                                                                       \
    ((uint32_t)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) |            \
                (uint32_t)(a)))
#define PG_RGB(r, g, b) PG_RGBA(r, g, b, 255)

/* The theme, matching poser-gl's FlatColoredTheme values. */
#define PG_COL_BACKGROUND PG_RGB(33, 33, 33)
#define PG_COL_PANEL PG_RGB(66, 66, 66)
#define PG_COL_PANEL_DARK PG_RGB(71, 71, 71)
#define PG_COL_WIDGET PG_RGB(97, 97, 97)
#define PG_COL_HOVER PG_RGB(120, 120, 120)
#define PG_COL_ACCENT PG_RGB(2, 119, 189)
#define PG_COL_GREEN PG_RGB(27, 94, 32)
#define PG_COL_RED PG_RGB(183, 28, 28)
#define PG_COL_TEXT PG_RGB(225, 225, 225)
#define PG_COL_TEXT_DIM PG_RGB(150, 150, 150)
#define PG_COL_YELLOW PG_RGB(221, 215, 0)
#define PG_COL_PINK PG_RGB(221, 0, 215)

enum
{
    PG_MOUSE_LEFT = 0,
    PG_MOUSE_MIDDLE = 1,
    PG_MOUSE_RIGHT = 2,
    PG_MOUSE_COUNT = 3
};

struct PG_GuiInput
{
    float mouse_x;
    float mouse_y;
    float mouse_dx;
    float mouse_dy;
    bool down[PG_MOUSE_COUNT];
    bool pressed[PG_MOUSE_COUNT];
    bool released[PG_MOUSE_COUNT];
    float wheel;

    /** UTF-8 text typed this frame, for the search box. */
    char text[32];
    bool backspace;
    bool enter;
};

struct PG_GuiVertex
{
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct PG_Gui
{
    struct PG_Renderer* renderer;
    struct PG_GuiInput input;
    float width;
    float height;

    struct PG_GuiVertex* vertices;
    int vertex_count;
    int vertex_capacity;
    PG_GLuint vao;
    PG_GLuint vbo;

    /* Widget identity across frames. A widget is identified by an integer the
     * call site supplies; `active` survives between frames so a drag started on
     * a slider keeps going while the cursor leaves it. */
    int hot;
    int active;
    int focus;
    /** Value the active drag started from, so a drag is absolute, not cumulative. */
    float drag_origin;
    float drag_value;

    int clip[4];
    bool clipping;
    /** Set when a widget consumed the cursor, so the 3D view ignores the click. */
    bool mouse_captured;
};

int
pg_gui_init(struct PG_Gui* gui, struct PG_Renderer* renderer);
void
pg_gui_free(struct PG_Gui* gui);

void
pg_gui_begin(struct PG_Gui* gui, float width, float height, const struct PG_GuiInput* input);
void
pg_gui_end(struct PG_Gui* gui);

/** True when the cursor is inside this rectangle and not clipped away. */
bool
pg_gui_hover(struct PG_Gui* gui, float x, float y, float w, float h);

void
pg_gui_rect(struct PG_Gui* gui, float x, float y, float w, float h, uint32_t colour);
void
pg_gui_border(struct PG_Gui* gui, float x, float y, float w, float h, uint32_t colour);
void
pg_gui_text(struct PG_Gui* gui, float x, float y, const char* text, uint32_t colour);
/** Text truncated to `max_width` pixels, with a trailing ".." when it does not fit. */
void
pg_gui_text_clipped(
    struct PG_Gui* gui,
    float x,
    float y,
    float max_width,
    const char* text,
    uint32_t colour);
void
pg_gui_text_centred(
    struct PG_Gui* gui,
    float x,
    float y,
    float w,
    const char* text,
    uint32_t colour);

/** Restrict drawing and hit tests to this rectangle until pg_gui_pop_clip. */
void
pg_gui_push_clip(struct PG_Gui* gui, float x, float y, float w, float h);
void
pg_gui_pop_clip(struct PG_Gui* gui);

bool
pg_gui_button(struct PG_Gui* gui, int id, float x, float y, float w, float h, const char* label);
/** A button drawn as selected; returns true on click. */
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
    bool enabled);

/**
 * poser-gl's TextSlider: a number that also drags. Returns true on the frame the
 * value changed, having written the new value through `value`.
 */
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
    int max_value);

/** Returns true when the text changed. */
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
    const char* placeholder);

struct PG_GuiListRow
{
    const char* text;
    bool marked; /* drawn in the accent colour — a modified animation */
};

/**
 * A scrolling, selectable list. `scroll` is in rows and is updated in place.
 * Returns the row index clicked this frame, or -1.
 */
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
    int selected);

#endif
