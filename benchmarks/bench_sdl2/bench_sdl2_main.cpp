#define SDL_MAIN_HANDLED

extern "C" {
#include "LibToriRSPlatformC.h"
#include "osrs/ginput.h"
#include "graphics/dash.h"
#include "graphics/raster_bench_runtime.h"
#include "osrs/game.h"
#include "osrs/world.h"
#include "platforms/common/sockstream.h"
#include "tori_rs.h"
}
extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
}
#include "platforms/common/torirs_nk_bench_panel.h"
#include "platforms/platform_impl2_sdl2.h"
#include "platforms/platform_impl2_sdl2_renderer_soft3d.h"

#include "nuklear/torirs_nuklear.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>
#endif

namespace
{
constexpr int kViewportInset = 4;
constexpr int kGameViewportWidth = 765;
constexpr int kGameViewportHeight = 503;
constexpr int kScreenWidth = kViewportInset + kGameViewportWidth + kViewportInset;
constexpr int kScreenHeight = kViewportInset + kGameViewportHeight + kViewportInset;

constexpr int kBenchSlotCount = 6;
constexpr int kBenchTotalSegments = 40; /* 1 default + 39 non-default variants */

struct VariantInfo
{
    const char* canonical;
    uint32_t value;
};

struct SlotInfo
{
    const char* slot_label;
    uint32_t shift;
    const VariantInfo* variants;
    size_t count;
    int default_variant_index;
};

/* Canonical names follow raster source filenames (without .u.c / .c). */

static const VariantInfo kVariantsFlat[] = {
    { "flat.deob", RASTER_BENCH_FLAT_DEOB },
    { "flat.screen.opaque.branching.s4", RASTER_BENCH_FLAT_OPAQUE_BRANCHING_S4 },
    { "flat.screen.opaque.sort.s4", RASTER_BENCH_FLAT_OPAQUE_SORT_S4 },
    { "flat.screen.alpha.branching.s4", RASTER_BENCH_FLAT_ALPHA_BRANCHING_S4 },
    { "flat.screen.alpha.sort.s4", RASTER_BENCH_FLAT_ALPHA_SORT_S4 },
};

static const VariantInfo kVariantsGouraud[] = {
    { "gouraud.deob", RASTER_BENCH_GOURAUD_DEOB },
    { "gouraud.screen.alpha.bary.branching.s1", RASTER_BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S1 },
    { "gouraud.screen.alpha.bary.branching.s4", RASTER_BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4 },
    { "gouraud.screen.alpha.bary.sort.s1", RASTER_BENCH_GOURAUD_ALPHA_BARY_SORT_S1 },
    { "gouraud.screen.alpha.bary.sort.s4", RASTER_BENCH_GOURAUD_ALPHA_BARY_SORT_S4 },
    { "gouraud.screen.alpha.edge.sort.s1", RASTER_BENCH_GOURAUD_ALPHA_EDGE_SORT_S1 },
    { "gouraud.screen.alpha.edge.sort.s4", RASTER_BENCH_GOURAUD_ALPHA_EDGE_SORT_S4 },
    { "gouraud.screen.opaque.bary.branching.s4", RASTER_BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4 },
    { "gouraud.screen.opaque.bary.sort.s1", RASTER_BENCH_GOURAUD_OPAQUE_BARY_SORT_S1 },
    { "gouraud.screen.opaque.bary.sort.s4", RASTER_BENCH_GOURAUD_OPAQUE_BARY_SORT_S4 },
    { "gouraud.screen.opaque.edge.sort.s1", RASTER_BENCH_GOURAUD_OPAQUE_EDGE_SORT_S1 },
    { "gouraud.screen.opaque.edge.sort.s4", RASTER_BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4 },
};

static const VariantInfo kVariantsTexOpaque[] = {
    { "texture.deob", RASTER_BENCH_TEXTURED_OPAQUE_DEOB },
    { "texture.deob2", RASTER_BENCH_TEXTURED_OPAQUE_DEOB2 },
    { "texshadeblend.persp.texopaque.branching.lerp8", RASTER_BENCH_TEXTURED_OPAQUE_PERSP_BRANCHING_LERP8 },
    { "texshadeblend.persp.texopaque.branching.lerp8_v3", RASTER_BENCH_TEXTURED_OPAQUE_PERSP_BRANCHING_LERP8_V3 },
    { "texshadeblend.persp.texopaque.sort.lerp8", RASTER_BENCH_TEXTURED_OPAQUE_PERSP_SORT_LERP8 },
    { "texshadeblend.persp.texopaque.sort.lerp8.scanline", RASTER_BENCH_TEXTURED_OPAQUE_PERSP_SORT_LERP8_SCANLINE },
    { "texshadeblend.affine.texopaque.branching.lerp8", RASTER_BENCH_TEXTURED_OPAQUE_AFFINE_BRANCHING_LERP8 },
    { "texshadeblend.affine.texopaque.branching.lerp8_v3", RASTER_BENCH_TEXTURED_OPAQUE_AFFINE_BRANCHING_LERP8_V3 },
};

static const VariantInfo kVariantsTexTrans[] = {
    { "texture.deob", RASTER_BENCH_TEXTURED_TRANS_DEOB },
    { "texture.deob2", RASTER_BENCH_TEXTURED_TRANS_DEOB2 },
    { "texshadeblend.persp.textrans.branching.lerp8", RASTER_BENCH_TEXTURED_TRANS_PERSP_BRANCHING_LERP8 },
    { "texshadeblend.persp.textrans.branching.lerp8_v3", RASTER_BENCH_TEXTURED_TRANS_PERSP_BRANCHING_LERP8_V3 },
    { "texshadeblend.persp.textrans.sort.lerp8", RASTER_BENCH_TEXTURED_TRANS_PERSP_SORT_LERP8 },
    { "texshadeblend.persp.textrans.sort.lerp8.scanline", RASTER_BENCH_TEXTURED_TRANS_PERSP_SORT_LERP8_SCANLINE },
    { "texshadeblend.affine.textrans.branching.lerp8", RASTER_BENCH_TEXTURED_TRANS_AFFINE_BRANCHING_LERP8 },
    { "texshadeblend.affine.textrans.branching.lerp8_v3", RASTER_BENCH_TEXTURED_TRANS_AFFINE_BRANCHING_LERP8_V3 },
};

static const VariantInfo kVariantsTexFlatOpaque[] = {
    { "texture.deob", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_DEOB },
    { "texture.deob2", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_DEOB2 },
    { "texshadeflat.persp.texopaque.branching.lerp8", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_BRANCHING_LERP8 },
    { "texshadeflat.persp.texopaque.sort.lerp8", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_SORT_LERP8 },
    { "texshadeflat.persp.texopaque.sort.lerp8.scanline", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_SORT_LERP8_SCANLINE },
    { "texshadeflat.persp.texopaque.ordered.lerp8.scanline", RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_ORDERED_LERP8_SCANLINE },
};

static const VariantInfo kVariantsTexFlatTrans[] = {
    { "texture.deob", RASTER_BENCH_TEXTURED_FLAT_TRANS_DEOB },
    { "texture.deob2", RASTER_BENCH_TEXTURED_FLAT_TRANS_DEOB2 },
    { "texshadeflat.persp.textrans.branching.lerp8", RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_BRANCHING_LERP8 },
    { "texshadeflat.persp.textrans.sort.lerp8", RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_SORT_LERP8 },
    { "texshadeflat.persp.textrans.sort.lerp8.scanline", RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_SORT_LERP8_SCANLINE },
    { "texshadeflat.persp.textrans.ordered.lerp8.scanline", RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_ORDERED_LERP8_SCANLINE },
};

/* Order matches RASTER_BENCH_PACK(g, f, tex_o, tex_t, texfo, tft). */
static const SlotInfo kSlots[kBenchSlotCount] = {
    { "Gouraud", RASTER_BENCH_SHIFT_GOURAUD, kVariantsGouraud, 12, 7 },
    { "Flat", RASTER_BENCH_SHIFT_FLAT, kVariantsFlat, 5, 1 },
    { "Tex opaque", RASTER_BENCH_SHIFT_TEXTURED_OPAQUE, kVariantsTexOpaque, 8, 3 },
    { "Tex transparent", RASTER_BENCH_SHIFT_TEXTURED_TRANS, kVariantsTexTrans, 8, 3 },
    { "Tex-flat opaque", RASTER_BENCH_SHIFT_TEXTURED_FLAT_OPAQUE, kVariantsTexFlatOpaque, 6, 2 },
    { "Tex-flat transparent", RASTER_BENCH_SHIFT_TEXTURED_FLAT_TRANS, kVariantsTexFlatTrans, 6, 2 },
};

struct BenchState
{
    int selected_variant[kBenchSlotCount];
    bool running_bench;
    int bench_phase_default; /* 1 while collecting the all-defaults sample */
    int bench_slot;
    int bench_variant_idx;
    FILE* report_fp;
    int next_report_index;
    int frames_in_segment;
    double segment_times_ms[1000];
    double ring_times_ms[1000];
    int ring_pos;
    int ring_count;
    char last_error[256];
};

static BenchState g_bench;

static uint32_t
pack_defaults(void)
{
    return RASTER_BENCH_PACK(
        kSlots[0].variants[kSlots[0].default_variant_index].value,
        kSlots[1].variants[kSlots[1].default_variant_index].value,
        kSlots[2].variants[kSlots[2].default_variant_index].value,
        kSlots[3].variants[kSlots[3].default_variant_index].value,
        kSlots[4].variants[kSlots[4].default_variant_index].value,
        kSlots[5].variants[kSlots[5].default_variant_index].value);
}

static uint32_t
pack_live(BenchState const& s)
{
    return RASTER_BENCH_PACK(
        kSlots[0].variants[s.selected_variant[0]].value,
        kSlots[1].variants[s.selected_variant[1]].value,
        kSlots[2].variants[s.selected_variant[2]].value,
        kSlots[3].variants[s.selected_variant[3]].value,
        kSlots[4].variants[s.selected_variant[4]].value,
        kSlots[5].variants[s.selected_variant[5]].value);
}

static uint32_t
pack_with_override(int slot, int variant_index)
{
    uint32_t const base = pack_defaults();
    uint32_t const shift = kSlots[slot].shift;
    uint32_t const val = kSlots[slot].variants[variant_index].value & RASTER_BENCH_MASK;
    return (base & ~(RASTER_BENCH_MASK << shift)) | (val << shift);
}

static uint32_t
current_bench_pack(BenchState const& s)
{
    if( !s.running_bench )
        return pack_live(s);
    if( s.bench_phase_default )
        return pack_defaults();
    return pack_with_override(s.bench_slot, s.bench_variant_idx);
}

static bool
bench_find_first_variant(int* out_slot, int* out_v)
{
    for( int si = 0; si < kBenchSlotCount; ++si )
    {
        for( size_t vi = 0; vi < kSlots[si].count; ++vi )
        {
            if( (int)vi == kSlots[si].default_variant_index )
                continue;
            *out_slot = si;
            *out_v = (int)vi;
            return true;
        }
    }
    return false;
}

static bool
bench_advance_variant(int* slot_io, int* v_io)
{
    int cs = *slot_io;
    int cv = *v_io + 1;
    for( ; cs < kBenchSlotCount; ++cs )
    {
        for( ; cv < (int)kSlots[cs].count; ++cv )
        {
            if( cv == kSlots[cs].default_variant_index )
                continue;
            *slot_io = cs;
            *v_io = cv;
            return true;
        }
        cv = 0;
    }
    return false;
}

static void
write_report_entry(
    FILE* fp,
    int section,
    char const* combination,
    double mean_frametime_ms,
    struct GGame* game)
{
    int wx_sw = 0, wz_sw = 0, wx_ne = 0, wz_ne = 0;
    if( game && game->world )
    {
        wx_sw = game->world->_chunk_sw_x;
        wz_sw = game->world->_chunk_sw_z;
        wx_ne = game->world->_chunk_ne_x;
        wz_ne = game->world->_chunk_ne_z;
    }

    fprintf(
        fp,
        "[bench%d]\n"
        "combination=%s\n"
        "frametime_ms=%.6f\n"
        "world_scene=%d, %d, %d, %d\n"
        "camera_position=%d, %d, %d\n"
        "camera_angle=%d, %d, %d\n"
        "\n",
        section,
        combination,
        mean_frametime_ms,
        wx_sw,
        wz_sw,
        wx_ne,
        wz_ne,
        game ? game->camera_world_x : 0,
        game ? game->camera_world_y : 0,
        game ? game->camera_world_z : 0,
        game ? game->camera_pitch : 0,
        game ? game->camera_yaw : 0,
        game ? game->camera_roll : 0);
    fflush(fp);
}

static double
mean_ms(double const* arr, int n)
{
    if( n <= 0 )
        return 0.0;
    double s = 0.0;
    for( int i = 0; i < n; ++i )
        s += arr[i];
    return s / (double)n;
}

static void
ring_push(BenchState* st, double ms)
{
    st->ring_times_ms[st->ring_pos] = ms;
    st->ring_pos = (st->ring_pos + 1) % 1000;
    if( st->ring_count < 1000 )
        st->ring_count++;
}

static double
rolling_mean_fps(BenchState const& st)
{
    if( st.ring_count == 0 )
        return 0.0;
    int const oldest = (st.ring_pos - st.ring_count + 1000) % 1000;
    double s = 0.0;
    for( int i = 0; i < st.ring_count; ++i )
        s += st.ring_times_ms[(oldest + i) % 1000];
    double const mean_ms_val = s / (double)st.ring_count;
    if( mean_ms_val <= 1e-12 )
        return 0.0;
    return 1000.0 / mean_ms_val;
}

static void
bench_panel_draw(struct nk_context* nk, struct GGame* game)
{
    (void)game;
    if( nk_begin(
            nk,
            "Raster bench",
            nk_rect(340, 10, 420, 560),
            NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE |
                NK_WINDOW_SCROLL_AUTO_HIDE) )
    {
        nk_layout_row_dynamic(nk, 22, 1);
        {
            double const roll_fps = rolling_mean_fps(g_bench);
            double const roll_ms = roll_fps > 1e-12 ? (1000.0 / roll_fps) : 0.0;
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "Rolling avg raster (FrameBegin..FrameEnd) (last %d frames): %.2f FPS (%.3f ms)",
                g_bench.ring_count,
                roll_fps,
                roll_ms);
        }

        for( int si = 0; si < kBenchSlotCount; ++si )
        {
            SlotInfo const& sl = kSlots[si];
            static const char* labels[32];
            int n = (int)sl.count;
            if( n > 32 )
                n = 32;
            for( int i = 0; i < n; ++i )
                labels[i] = sl.variants[i].canonical;

            nk_label(nk, sl.slot_label, NK_TEXT_LEFT);
            nk_combobox(
                nk,
                labels,
                n,
                &g_bench.selected_variant[si],
                22,
                nk_vec2(400, 200));
        }

        nk_layout_row_dynamic(nk, 28, 2);
        if( nk_button_label(nk, "Reset to defaults") )
        {
            for( int i = 0; i < kBenchSlotCount; ++i )
                g_bench.selected_variant[i] = kSlots[i].default_variant_index;
        }
        if( nk_button_label(nk, "Run bench") && !g_bench.running_bench )
        {
            if( g_bench.report_fp )
            {
                fclose(g_bench.report_fp);
                g_bench.report_fp = NULL;
            }
            g_bench.report_fp = fopen("report.ini", "w");
            if( !g_bench.report_fp )
            {
                snprintf(
                    g_bench.last_error,
                    sizeof(g_bench.last_error),
                    "Could not open report.ini for writing.");
            }
            else
            {
                g_bench.last_error[0] = '\0';
                g_bench.running_bench = true;
                g_bench.bench_phase_default = 1;
                g_bench.frames_in_segment = 0;
                g_bench.next_report_index = 1;
            }
        }

        nk_layout_row_dynamic(nk, 22, 1);
        if( g_bench.last_error[0] )
            nk_labelf(nk, NK_TEXT_LEFT, "%s", g_bench.last_error);

        if( g_bench.running_bench )
        {
            int done = g_bench.next_report_index - 1;
            char const* combo = "default";
            if( !g_bench.bench_phase_default )
                combo = kSlots[g_bench.bench_slot].variants[g_bench.bench_variant_idx].canonical;
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "Bench progress: %d / %d  (%s)",
                done,
                kBenchTotalSegments,
                combo);
        }
    }
    nk_end(nk);
}

static void
osx_abort_startup(
    struct GGame* game,
    struct Platform2_SDL2* platform,
    struct ToriRSRenderCommandBuffer* rcb,
    struct ToriRSNetSharedBuffer* net)
{
    torirs_nk_bench_panel_unregister();
    if( game )
        LibToriRS_GameFree(game);
    if( platform )
        Platform2_SDL2_Free(platform);
    if( rcb )
        LibToriRS_RenderCommandBufferFree(rcb);
    if( net )
        LibToriRS_NetFreeBuffer(net);
}
#if defined(__APPLE__)
static void
osx_print_heap_at_exit(void)
{
    malloc_statistics_t mz = { 0 };
    malloc_zone_statistics(malloc_default_zone(), &mz);

    task_vm_info_data_t vm = { 0 };
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if( task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) != KERN_SUCCESS )
    {
        printf(
            "At exit: malloc default zone in use ~%zu bytes (%.2f MiB), allocated %zu bytes "
            "(%.2f MiB)\n",
            mz.size_in_use,
            mz.size_in_use / (1024.0 * 1024.0),
            mz.size_allocated,
            mz.size_allocated / (1024.0 * 1024.0));
        return;
    }

    printf(
        "At exit: malloc default zone in use ~%zu bytes (%.2f MiB), allocated %zu bytes (%.2f "
        "MiB); "
        "task phys_footprint ~%llu bytes (%.2f MiB)\n",
        mz.size_in_use,
        mz.size_in_use / (1024.0 * 1024.0),
        mz.size_allocated,
        mz.size_allocated / (1024.0 * 1024.0),
        (unsigned long long)vm.phys_footprint,
        vm.phys_footprint / (1024.0 * 1024.0));
}
#endif

} // namespace

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

    memset(&g_bench, 0, sizeof(g_bench));
    for( int i = 0; i < kBenchSlotCount; ++i )
        g_bench.selected_variant[i] = kSlots[i].default_variant_index;

    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net_shared, 513, 335);
    struct GInput input = { 0 };
    int const game_width = kGameViewportWidth;
    int const game_height = kGameViewportHeight;
    int const render_max_width = game_width;
    int const render_max_height = game_height;
    struct ToriRSRenderCommandBuffer* render_command_buffer = LibToriRS_RenderCommandBufferNew(1024);
    struct Platform2_SDL2* platform = Platform2_SDL2_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        osx_abort_startup(game, NULL, render_command_buffer, net_shared);
        return 1;
    }
    platform->current_game = game;

    if( !Platform2_SDL2_InitForSoft3D(platform, kScreenWidth, kScreenHeight) )
    {
        printf("Failed to initialize platform\n");
        osx_abort_startup(game, platform, render_command_buffer, net_shared);
        return 1;
    }

    struct Platform2_SDL2_Renderer_Soft3D* renderer_soft3d = PlatformImpl2_SDL2_Renderer_Soft3D_New(
        kScreenWidth, kScreenHeight, render_max_width, render_max_height);
    if( !renderer_soft3d )
    {
        printf("Failed to create Soft3D renderer\n");
        osx_abort_startup(game, platform, render_command_buffer, net_shared);
        return 1;
    }
    if( !PlatformImpl2_SDL2_Renderer_Soft3D_Init(renderer_soft3d, platform) )
    {
        printf("Failed to initialize Soft3D renderer\n");
        PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer_soft3d);
        osx_abort_startup(game, platform, render_command_buffer, net_shared);
        return 1;
    }

    torirs_nk_bench_panel_register(bench_panel_draw);

    game->iface_view_port->width = game_width;
    game->iface_view_port->height = game_height;
    game->iface_view_port->x_center = game_width / 2;
    game->iface_view_port->y_center = game_height / 2;
    game->iface_view_port->stride = game_width;
    game->iface_view_port->clip_left = 0;
    game->iface_view_port->clip_top = 0;
    game->iface_view_port->clip_right = game_width;
    game->iface_view_port->clip_bottom = game_height;

    game->viewport_offset_x = kViewportInset;
    game->viewport_offset_y = kViewportInset;
    PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(renderer_soft3d, kViewportInset, kViewportInset);

    renderer_soft3d->clicked_tile_x = -1;
    renderer_soft3d->clicked_tile_z = -1;

    struct SockStream* login_stream = NULL;
    sockstream_init();
    login_stream = sockstream_new();

    while( LibToriRS_GameIsRunning(game) )
    {
        uint32_t const packed = current_bench_pack(g_bench);
        g_raster_bench.packed = packed;
        g_raster_bench.active = 1;

        /* Rolling avg + report.ini frametime_ms: soft3d LibToriRS_FrameBegin..FrameEnd only
         * (see renderer->last_raster_ms in platform_impl2_sdl2_renderer_soft3d_shared.cpp). */

        Platform2_SDL2_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        uint64_t timestamp_ms = SDL_GetTicks64();

        Platform2_SDL2_PollEvents(platform, &input, 0);

        game->tick_ms = timestamp_ms;

        LibToriRS_GameStep(game, &input, render_command_buffer);

        PlatformImpl2_SDL2_Renderer_Soft3D_Render(renderer_soft3d, game, render_command_buffer);

        double const frame_ms = renderer_soft3d->last_raster_ms;

        ring_push(&g_bench, frame_ms);

        if( g_bench.running_bench && g_bench.report_fp )
        {
            g_bench.segment_times_ms[g_bench.frames_in_segment++] = frame_ms;
            if( g_bench.frames_in_segment >= 1000 )
            {
                double const m = mean_ms(g_bench.segment_times_ms, 1000);
                if( g_bench.bench_phase_default )
                {
                    write_report_entry(
                        g_bench.report_fp,
                        g_bench.next_report_index++,
                        "default",
                        m,
                        game);
                    g_bench.bench_phase_default = 0;
                    if( !bench_find_first_variant(&g_bench.bench_slot, &g_bench.bench_variant_idx) )
                    {
                        fclose(g_bench.report_fp);
                        g_bench.report_fp = NULL;
                        g_bench.running_bench = false;
                    }
                }
                else
                {
                    char const* combo
                        = kSlots[g_bench.bench_slot].variants[g_bench.bench_variant_idx].canonical;
                    write_report_entry(
                        g_bench.report_fp,
                        g_bench.next_report_index++,
                        combo,
                        m,
                        game);
                    if( !bench_advance_variant(&g_bench.bench_slot, &g_bench.bench_variant_idx) )
                    {
                        fclose(g_bench.report_fp);
                        g_bench.report_fp = NULL;
                        g_bench.running_bench = false;
                    }
                }
                g_bench.frames_in_segment = 0;
            }
        }
    }

    if( g_bench.report_fp )
    {
        fclose(g_bench.report_fp);
        g_bench.report_fp = NULL;
    }

    torirs_nk_bench_panel_unregister();

    if( login_stream )
    {
        sockstream_free(login_stream);
        login_stream = NULL;
    }

    sockstream_cleanup();

    PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer_soft3d);

    LibToriRS_GameFree(game);
    Platform2_SDL2_Free(platform);
    LibToriRS_RenderCommandBufferFree(render_command_buffer);
    LibToriRS_NetFreeBuffer(net_shared);
#if defined(__APPLE__)
    osx_print_heap_at_exit();
#endif
    return 0;
}
