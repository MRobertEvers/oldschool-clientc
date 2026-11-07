/*
 * The integer scales, pinned: one panel holding every widget kind, built at
 * 1x, 2x and 3x, rendered through the same emit desc, frame translator and
 * Soft3D blit as every other scene in this file, and compared against goldens
 * written by the code as it stood BEFORE the chrome scale became a rational
 * (workbench step 6.0b).
 *
 * Two goldens per scale, because a failure wants to say where it is:
 *
 *   ui/test/golden/chrome_scale_<n>x.prims.txt   the display list, one prim a
 *                                                 line -- a geometry change
 *                                                 shows up as a readable diff;
 *   the last line of the same file                the FNV-1a 64 of every
 *                                                 rendered pixel -- the
 *                                                 byte-identical render.
 *
 * Regenerate only after an intentional change to how the integer scales LOOK:
 *   UPDATE_GOLDEN=1 make -C src test-debug-overlay-visual
 * The fractional scales are not pinned here; they have no "before".
 *
 * #included by uitree_debug_overlay_visual.c, for its world shims, its scene
 * and its VT_ASSERT.
 */

#define SG_BASE_W 500
#define SG_BASE_H 990

static int g_sg_pixels[SG_BASE_W * 3 * SG_BASE_H * 3];
static char g_sg_dump[1 << 20];
static int g_sg_dump_len;

static void
sg_appendf(char const* fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(
        g_sg_dump + g_sg_dump_len, sizeof(g_sg_dump) - (size_t)g_sg_dump_len, fmt, ap);
    va_end(ap);
    assert(n >= 0);
    assert(g_sg_dump_len + n < (int)sizeof(g_sg_dump));
    g_sg_dump_len += n;
}

static uint64_t
sg_fnv(int const* px, int count)
{
    uint64_t h = 1469598103934665603ull;
    for( int i = 0; i < count; i++ )
    {
        uint32_t const v = (uint32_t)px[i] & 0xFFFFFFu;
        for( int b = 0; b < 3; b++ )
        {
            h ^= (v >> (b * 8)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

/** Every widget kind the chrome draws, in one window panel and a menu. */
static void
sg_build_every_kind(struct ToriRSChrome* ui)
{
    static char const* const options[] = { "Hidden", "Wall", "Roof" };
    static char const* const tabs[] = { "Locs", "Spawns" };
    int panel;
    int menu;
    int strip;
    int w;

    panel = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 4, 4, 280, "Every widget");
    ToriRSChrome_PanelSetFramed(ui, panel, 1);
    ToriRSChrome_PanelSetClosable(ui, panel, 1);
    ToriRSChrome_Heading(ui, panel, "Heading");
    ToriRSChrome_Label(ui, panel, "m50_50 (1,2) tile 12,40");
    ToriRSChrome_LabelColored(ui, panel, "draws    41", ui->theme.accent);
    ToriRSChrome_KeyValue(ui, panel, "Region", "12850");
    ToriRSChrome_Paragraph(
        ui, panel,
        "A paragraph long enough to wrap onto a second line inside the content column.");
    ToriRSChrome_Progress(ui, panel, "Loading", "3 of 7", 40);
    ToriRSChrome_Error(ui, panel, "Refused", "tile holds a wall");
    ToriRSChrome_Checkbox(ui, panel, "wireframe", 0);
    ToriRSChrome_Checkbox(ui, panel, "show hitboxes", 1);
    ToriRSChrome_ListRow(ui, panel, "tile_indicator", 1, 1);
    ToriRSChrome_ActionRow(ui, panel, "Hotkeys", "12 bound");
    ToriRSChrome_TextInput(ui, panel, "goto", "3222 3218");
    ToriRSChrome_TextArea(ui, panel, "notes", "first line\nsecond", 2);
    ToriRSChrome_Dropdown(ui, panel, "Layer", options, 3, 1);
    ToriRSChrome_ColorPick(ui, panel, "Colour", 0x1234);
    ToriRSChrome_Separator(ui, panel);
    w = ToriRSChrome_Button(ui, panel, "Save");
    ToriRSChrome_ButtonSetChord(ui, w, "Ctrl+S");
    ToriRSChrome_ModelView(ui, panel, 48, 32);
    ToriRSChrome_Custom(ui, panel, "custom", 20);
    ToriRSChrome_TreeRow(ui, panel, "Walls", 0, 2);
    ToriRSChrome_TreeRow(ui, panel, "Fence", 1, 0);
    w = ToriRSChrome_VList(ui, panel, "Catalog", 3, 5);
    ToriRSChrome_VListSetRow(ui, w, 0, "1 Oak tree");
    ToriRSChrome_VListSetRow(ui, w, 1, "2 Willow");
    ToriRSChrome_VListSetRow(ui, w, 2, "3 Maple");
    w = ToriRSChrome_IconButton(ui, panel, TORIRS_CHROME_SKIN_TOOL_SELECT, "Select");
    ToriRSChrome_IconButtonSetChord(ui, w, "S");
    ToriRSChrome_Slider(ui, panel, "Radius", 1, 9, 3, 1);
    ToriRSChrome_Radio(ui, panel, "Under", 1, 1);
    ToriRSChrome_Radio(ui, panel, "Over", 1, 0);
    w = ToriRSChrome_Code(ui, panel, "script", 2, 2);
    ToriRSChrome_CodeSetLine(ui, w, 0, "if( x > 0 )");
    ToriRSChrome_CodeSetLine(ui, w, 1, "    say(\"hi\");");
    w = ToriRSChrome_Graph(ui, panel, "graph", 40);
    ToriRSChrome_GraphBegin(ui, w);
    ToriRSChrome_GraphRect(ui, w, 4, 4, 30, 16, 0x406080, 1, 1);
    ToriRSChrome_GraphLine(ui, w, 34, 12, 70, 28, 0xFFFFFF);
    ToriRSChrome_GraphText(ui, w, 72, 30, 0xFFFF00, "node");
    ToriRSChrome_GraphEnd(ui, w);

    strip = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_WINDOW, 292, 4, 200, "Tabs");
    ToriRSChrome_Tabs(ui, strip, tabs, 2, 0);
    ToriRSChrome_PanelBeginTab(ui, strip, 0);
    ToriRSChrome_Label(ui, strip, "first tab");

    menu = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENU, 292, 120, 0, "Choose Option");
    ToriRSChrome_MenuItem(ui, menu, "Teleport here");
    ToriRSChrome_MenuItem(ui, menu, "Cancel");
}

static void
sg_dump_prims(struct ToriRSChromePrim const* prims, int count)
{
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSChromePrim const* p = &prims[i];
        sg_appendf(
            "%d k%d %d,%d %dx%d c%06X f%d l%d/%d font%d b%d s%d spr%d/%d t%d clip%d,%d,%dx%d",
            i, p->kind, p->x, p->y, p->w, p->h, (unsigned)p->color, p->filled,
            (int)p->line_direction, (int)p->line_width, p->font_slot, p->baseline, p->shadowed,
            p->sprite_slot, p->sprite_scene_id, p->trans, p->clip.x, p->clip.y, p->clip.w,
            p->clip.h);
        if( p->kind == TORIRS_CHROME_PRIM_TEXT )
            sg_appendf(" \"%s\"", p->text ? p->text : "");
        sg_appendf("\n");
    }
}

static void
sg_render(int scale, int width, int height)
{
    struct UITreeEmitDesc desc;
    struct ToriRS_Frame frame;
    struct ToriPlatform_Renderer_Soft3D* soft;
    char path[128];
    int count = 0;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_DEBUG_OVERLAY;
    desc.debug_prims = ToriRSChrome_Prims(&g_ui, &count);
    desc.debug_prim_count = count;
    desc.debug_font_id[TORIRS_CHROME_FONT_SMALL] = FONT_ID_AT(FONT_ID_SMALL, scale);
    desc.debug_font_id[TORIRS_CHROME_FONT_MENU] = FONT_ID_AT(FONT_ID_MENU, scale);
    desc.debug_font_id[TORIRS_CHROME_FONT_BODY] = FONT_ID_AT(FONT_ID_BODY, scale);
    desc.debug_skin_scene_id = SKIN_SCENE_ID;
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = i;
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = width;
    desc.clip.h = height;

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, g_scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmit(&frame, &desc, 1);

    soft = ToriPlatform_Renderer_Soft3D_New();
    ToriPlatform_Renderer_Soft3D_Init(soft, g_scene, g_sg_pixels, width, height);
    ToriPlatform_Renderer_Soft3D_RenderFrame(soft, &frame);
    ToriPlatform_Renderer_Soft3D_Free(soft);

    snprintf(path, sizeof(path), "build/debug_overlay_golden_scale%dx.bmp", scale);
    bmp_write_file(path, g_sg_pixels, width, height);
    printf("  wrote %s (%d prims)\n", path, count);
}

static char*
sg_read_whole(char const* path, int* out_len)
{
    FILE* f = fopen(path, "rb");
    long n;
    char* data;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    assert(data);
    if( fread(data, 1, (size_t)n, f) != (size_t)n )
        n = 0;
    fclose(f);
    data[n] = '\0';
    *out_len = (int)n;
    return data;
}

static void
visual_scale_golden(void)
{
    printf("VISUAL: every widget kind at 1x, 2x, 3x against the goldens\n");
    for( int scale = TORIRS_CHROME_SCALE_MIN; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
    {
        int const width = SG_BASE_W * scale;
        int const height = SG_BASE_H * scale;
        char path[128];
        int count = 0;
        struct ToriRSChromePrim const* prims;

        ToriRSChrome_Init(&g_ui);
        g_ui.theme = torirs_chrome_theme_osrs;
        for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
            ToriRSChrome_SetSkinAvailable(&g_ui, i, 1);
        g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
        g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;
        ToriRSChrome_SetScale(&g_ui, scale);
        ToriRSChrome_SetSurface(&g_ui, width, height);
        sg_build_every_kind(&g_ui);
        ToriRSChrome_Build(&g_ui);
        VT_ASSERT(g_ui.overflow == 0, "the every-kind panel fit the display list");

        memset(g_sg_pixels, 0, sizeof(g_sg_pixels));
        sg_render(scale, width, height);

        g_sg_dump_len = 0;
        g_sg_dump[0] = '\0';
        prims = ToriRSChrome_Prims(&g_ui, &count);
        sg_appendf("scale %d canvas %dx%d prims %d\n", scale, width, height, count);
        sg_dump_prims(prims, count);
        sg_appendf("pixels fnv1a64 %016llx\n",
                   (unsigned long long)sg_fnv(g_sg_pixels, width * height));

        snprintf(path, sizeof(path), "ui/test/golden/chrome_scale_%dx.prims.txt", scale);
        if( getenv("UPDATE_GOLDEN") )
        {
            FILE* g = fopen(path, "wb");
            VT_ASSERT(g != NULL, "open the golden for write (UPDATE_GOLDEN)");
            if( g )
            {
                fwrite(g_sg_dump, 1, (size_t)g_sg_dump_len, g);
                fclose(g);
                printf("  golden updated: %s (%d prims)\n", path, count);
            }
        }
        else
        {
            int glen = 0;
            char* golden = sg_read_whole(path, &glen);
            VT_ASSERT(golden != NULL, "the golden exists (UPDATE_GOLDEN=1 writes it)");
            if( golden )
            {
                int same = glen == g_sg_dump_len &&
                           memcmp(golden, g_sg_dump, (size_t)glen) == 0;
                if( !same )
                {
                    char actual[160];
                    FILE* a;
                    snprintf(actual, sizeof(actual), "build/chrome_scale_%dx.prims.txt", scale);
                    a = fopen(actual, "wb");
                    if( a )
                    {
                        fwrite(g_sg_dump, 1, (size_t)g_sg_dump_len, a);
                        fclose(a);
                    }
                    fprintf(stderr, "  diff %s %s\n", path, actual);
                }
                VT_ASSERT(same, "the integer-scale render matches its golden byte for byte");
                free(golden);
            }
        }
    }
    ToriRSChrome_Init(&g_ui);
}
