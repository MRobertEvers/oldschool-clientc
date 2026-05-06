#include "serverprot_core_parse.h"

#include "osrs/rscache/rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ── Map / zone ───────────────────────────────────────────────────────────── */

int
serverprot_core_parse_maprebuild_v1(
    uint8_t* data,
    int      n,
    int*     out_zonex,
    int*     out_zonez)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    *out_zonex = g2(&b);
    *out_zonez = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_update_zone_v1(
    uint8_t* data, int n, struct WireIn_UpdateZone_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->base_x = g1(&b);
    out->base_z = g1(&b);
    return 1;
}

int
serverprot_core_parse_loc_add_change_v1(
    uint8_t* data, int n, struct WireIn_LocAddChange_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->info   = g1(&b);
    out->loc_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_loc_del_v1(
    uint8_t* data, int n, struct WireIn_LocDel_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos  = g1(&b);
    out->info = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_loc_anim_v1(
    uint8_t* data, int n, struct WireIn_LocAnim_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->info   = g1(&b);
    out->seq_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_loc_merge_v1(
    uint8_t* data, int n, struct WireIn_LocMerge_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->info   = g1(&b);
    out->loc_id = g2(&b);
    out->start  = g2(&b);
    out->end    = g2(&b);
    out->pid    = g2(&b);
    out->east   = g1b(&b);
    out->south  = g1b(&b);
    out->west   = g1b(&b);
    out->north  = g1b(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_obj_add_v1(
    uint8_t* data, int n, struct WireIn_ObjAdd_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->obj_id = g2(&b);
    out->count  = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_obj_del_v1(
    uint8_t* data, int n, struct WireIn_ObjDel_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->obj_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_obj_reveal_v1(
    uint8_t* data, int n, struct WireIn_ObjReveal_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos      = g1(&b);
    out->obj_id   = g2(&b);
    out->count    = g2(&b);
    out->receiver = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_obj_count_v1(
    uint8_t* data, int n, struct WireIn_ObjCount_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos       = g1(&b);
    out->obj_id    = g2(&b);
    out->old_count = g2(&b);
    out->new_count = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_map_projanim_v1(
    uint8_t* data, int n, struct WireIn_MapProjAnim_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos         = g1(&b);
    out->dx_offset   = g1b(&b);
    out->dz_offset   = g1b(&b);
    out->target      = g2b(&b);
    out->spotanim    = g2(&b);
    out->src_height  = g1(&b);
    out->dst_height  = g1(&b);
    out->start_delay = g2(&b);
    out->end_delay   = g2(&b);
    out->peak        = g1(&b);
    out->arc         = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_map_anim_v1(
    uint8_t* data, int n, struct WireIn_MapAnim_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->pos    = g1(&b);
    out->id     = g2(&b);
    out->height = g1(&b);
    out->delay  = g2(&b);
    assert(b.position == n);
    return 1;
}

/* ── Interface ────────────────────────────────────────────────────────────── */

int
serverprot_core_parse_if_settab_v1(
    uint8_t* data, int n, struct WireIn_IfSettab_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->tab_id       = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setcolour_v1(
    uint8_t* data, int n, struct WireIn_IfSetcolour_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->colour       = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_sethide_v1(
    uint8_t* data, int n, struct WireIn_IfSethide_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->hide         = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setobject_v1(
    uint8_t* data, int n, struct WireIn_IfSetobject_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->obj_id       = g2(&b);
    out->zoom         = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setmodel_v1(
    uint8_t* data, int n, struct WireIn_IfSetmodel_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->model_id     = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setanim_v1(
    uint8_t* data, int n, struct WireIn_IfSetanim_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->anim_id      = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setplayerhead_v1(
    uint8_t* data, int n, struct WireIn_IfSetplayerhead_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_settext_v1(
    uint8_t* data, int n, struct WireIn_IfSettext_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->text         = gstringnewline(&b);
    return 1;
}

int
serverprot_core_parse_if_setnpchead_v1(
    uint8_t* data, int n, struct WireIn_IfSetnpchead_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->npc_id       = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setposition_v1(
    uint8_t* data, int n, struct WireIn_IfSetposition_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->x            = g2b(&b);
    out->z            = g2b(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_setscrollpos_v1(
    uint8_t* data, int n, struct WireIn_IfSetscrollpos_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    out->pos          = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_open_v1(
    uint8_t* data, int n, struct WireIn_IfOpen_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->component_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_open_main_side_v1(
    uint8_t* data, int n, struct WireIn_IfOpenMainSide_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->main_component_id = g2(&b);
    out->side_component_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_if_close_v1(uint8_t* data, int n)
{
    (void)data;
    (void)n;
    return 1;
}

/* ── Inventory + stats ────────────────────────────────────────────────────── */

int
serverprot_core_parse_update_stat_v1(
    uint8_t* data, int n, struct WireIn_UpdateStat_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->stat  = g1(&b);
    out->xp    = g4(&b);
    out->level = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_update_runenergy_v1(
    uint8_t* data, int n, struct WireIn_UpdateRunenergy_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->run_energy = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_update_runweight_v1(
    uint8_t* data, int n, struct WireIn_UpdateRunweight_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->run_weight = g2b(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_varp_small_v1(
    uint8_t* data, int n, struct WireIn_VarpSmall_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->variable = g2(&b);
    out->value    = g1b(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_varp_large_v1(
    uint8_t* data, int n, struct WireIn_VarpLarge_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->variable = g2(&b);
    out->value    = g4(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_reset_anims_v1(uint8_t* data, int n)
{
    (void)data;
    (void)n;
    return 1;
}

int
serverprot_core_parse_update_pid_v1(
    uint8_t* data, int n, struct WireIn_UpdatePid_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->local_player_index = g2(&b);
    out->unused             = g1(&b);
    assert(b.position == n);
    return 1;
}

/* ── Camera + misc ────────────────────────────────────────────────────────── */

int
serverprot_core_parse_cam_lookat_v1(
    uint8_t* data, int n, struct WireIn_CamLookAt_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->local_x = g2(&b);
    out->local_z = g2(&b);
    out->height  = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_cam_moveto_v1(
    uint8_t* data, int n, struct WireIn_CamMoveTo_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->local_x = g2(&b);
    out->local_z = g2(&b);
    out->height  = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_cam_shake_v1(
    uint8_t* data, int n, struct WireIn_CamShake_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->axis      = g1(&b);
    out->amplitude = g1(&b);
    out->frequency = g1(&b);
    out->speed     = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_cam_reset_v1(uint8_t* data, int n)
{
    (void)data;
    (void)n;
    return 1;
}

int
serverprot_core_parse_unset_map_flag_v1(uint8_t* data, int n)
{
    (void)data;
    (void)n;
    return 1;
}

int
serverprot_core_parse_hint_arrow_v1(
    uint8_t* data, int n, struct WireIn_HintArrow_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->type   = g1(&b);
    out->id     = g2(&b);
    out->z      = 0;
    out->height = 0;
    if( out->type >= 3 )
    {
        out->z      = g2(&b);
        out->height = g1(&b);
    }
    return 1;
}

int
serverprot_core_parse_set_multiway_v1(
    uint8_t* data, int n, struct WireIn_SetMultiway_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->multiway = g1(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_set_player_op_v1(
    uint8_t* data, int n, struct WireIn_SetPlayerOp_v1* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->op_index  = g1(&b);
    out->priority  = g1(&b);
    out->op_text   = gstringnewline(&b);
    return 1;
}
