#ifndef D3D9_AUTHENTIC_U_C
#define D3D9_AUTHENTIC_U_C

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


static void
d3d9_ev_tex_load(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_TEX_LOAD);
    int tex_id = command->u.tex_load.texture_id;
    struct ToriDraw_Texture* tex = command->u.tex_load.texture;
    assert(tex_id < 0 || tex_id >= 255 || !tex || !tex->texels);

    static uint8_t rgba_scratch[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    trspk_atlas_decode_texture_rgba(tex, TRSPK_ATLAS_TILE, rgba_scratch);

    if( tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE )
    {
        if( !d3d9_upload_standalone_texture(renderer, tex_id, rgba_scratch) )
        {
            fprintf(stderr, "D3D9: Upload standalone texture failed\n");
            goto fail;
        }
    }
    else
    {
        //
        if( !d3d9_ensure_atlas(renderer) )
        {
            fprintf(stderr, "D3D9: Ensure atlas failed\n");
            goto fail;
        }

        trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)(tex_id + 1),
            rgba_scratch,
            TRSPK_ATLAS_TILE * 4u,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            NULL);

    }

fail:
    return;
}

static void
d3d9_ev_begin_3d(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BEGIN_3D);
    IDirect3DDevice9* dev = renderer->device;
    const struct LibToriRS_RenderCommand_Begin3D* b3d = &command->u.begin_3d;
    renderer->cur_3d = *b3d;
    renderer->has_3d = true;
    renderer->in3d = true;

    compute_pass_matrices(
        renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);

    {
        const struct ToriDraw_ViewPort* vp = &renderer->cur_3d.view_port;
        DWORD vp_w = (DWORD)(vp->width > 0 ? vp->width : renderer->width);
        DWORD vp_h = (DWORD)(vp->height > 0 ? vp->height : renderer->height);
        D3DVIEWPORT9 d3d_vp = { 0, 0, vp_w, vp_h, 0.0f, 1.0f };
        IDirect3DDevice9_SetViewport(dev, &d3d_vp);
    }

    D3DMATRIX d3d_view, d3d_proj;
    float16_to_d3dmatrix(renderer->view, &d3d_view);
    float16_to_d3dmatrix(renderer->proj, &d3d_proj);
    IDirect3DDevice9_SetTransform(dev, D3DTS_VIEW, &d3d_view);
    IDirect3DDevice9_SetTransform(dev, D3DTS_PROJECTION, &d3d_proj);

    D3DMATRIX identity = { { { 1.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               1.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               1.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               0.0f,
                               1.0f } } };
    IDirect3DDevice9_SetTransform(dev, D3DTS_WORLD, &identity);

    /* Refresh atlas texture if needed and bind it. */
    if( trspk_atlas_is_dirty(&renderer->atlas) && trspk_atlas_is_initialized(&renderer->atlas) )
        d3d9_refresh_atlas_texture(renderer);

    if( renderer->atlas_tex )
        d3d9_bind_atlas_texture(dev, renderer->atlas_tex);
    else
        d3d9_set_no_texture_stages(dev);
}

static void
d3d9_ev_model_load(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_MODEL_LOAD);
    struct ToriDraw_Model* model = get_model(command->u.model_load.model);
    if( !model )
        return;
}

static void
d3d9_ev_model_draw(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_END_3D);
    renderer->has_3d = false;
    renderer->in3d = false;
}

#endif