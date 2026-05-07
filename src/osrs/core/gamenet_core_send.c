#include "gamenet_core_send.h"

#include "clientprot_send.h"
#include "osrs/game.h"
#include "osrs/rscache/rsbuf.h"
#include "tori_rs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Maximum outbound packet size. */
#define GAMENET_CORE_SCRATCH_SIZE 2048

/* Send a scratch buffer over the network. detail is optional extra info printed
 * in debug mode (pass NULL to omit). */
static void
gamenet_core_net_send(
    struct GGame* g,
    struct RSBuffer* b,
    const char* op_name,
    const char* detail)
{
    if( !g || b->position == 0 )
        return;

    static int dbg_enabled = -1;
    if( dbg_enabled < 0 )
    {
        const char* e = getenv("TORI_DEBUG_PKTOUT");
        dbg_enabled = (!e || !(e[0] == '0' && e[1] == '\0')) ? 1 : 0;
    }

    if( dbg_enabled )
    {
        if( detail )
            printf("[pkout] op=%s size=%d %s\n", op_name, (int)b->position, detail);
        else
            printf("[pkout] op=%s size=%d\n", op_name, (int)b->position);
    }

    LibToriRS_NetSend(g, (uint8_t*)b->data, (int)b->position);
}

/* Convert scene-local tile to wire (world) tile. */
static int
wire_x(
    struct GGame* g,
    int scene_x)
{
    return (g && g->world) ? scene_x + g->world->_base_tile_x : scene_x;
}
static int
wire_z(
    struct GGame* g,
    int scene_z)
{
    return (g && g->world) ? scene_z + g->world->_base_tile_z : scene_z;
}

/* ── Keepalives ──────────────────────────────────────────────────────────── */

void
gamenet_core_send_no_timeout_v1(
    struct GGame* g,
    int opcode)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_no_timeout_v1(&b, g->random_out, opcode);
    gamenet_core_net_send(g, &b, "NO_TIMEOUT", NULL);
}

void
gamenet_core_send_idle_timer_v1(
    struct GGame* g,
    int opcode)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_idle_timer_v1(&b, g->random_out, opcode);
    gamenet_core_net_send(g, &b, "IDLE_TIMER", NULL);
}

void
gamenet_core_send_event_tracking_v1(
    struct GGame* g,
    int opcode)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_event_tracking_v1(&b, g->random_out, opcode);
    gamenet_core_net_send(g, &b, "EVENT_TRACKING", NULL);
}

void
gamenet_core_send_close_modal_v1(
    struct GGame* g,
    int opcode)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_close_modal_v1(&b, g->random_out, opcode);
    gamenet_core_net_send(g, &b, "CLOSE_MODAL", NULL);
}

void
gamenet_core_send_resume_pausebutton_v1(
    struct GGame* g,
    int opcode,
    int comp_id)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    struct PktClientProt_ResumePauseButton_v1 w = { comp_id };
    clientprot_send_resume_pausebutton_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "RESUME_PAUSEBUTTON", NULL);
}

void
gamenet_core_send_resume_p_countdialog_v1(
    struct GGame* g,
    int opcode,
    int value)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    struct PktClientProt_ResumeCntDlg_v1 w = { value };
    clientprot_send_resume_p_countdialog_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "RESUME_P_COUNTDIALOG", NULL);
}

/* ── Movement ────────────────────────────────────────────────────────────── */

void
gamenet_core_send_move_gameclick_v1(
    struct GGame* g,
    int opcode,
    int run,
    const int* px,
    const int* pz,
    int n)
{
    if( !g || !g->random_out || n < 1 )
        return;
    struct PktClientProt_MoveGameClick_v1 w;
    w.run = run;
    w.path_len = n < GAMENET_CORE_SEND_MOVE_PATH_MAX ? n : GAMENET_CORE_SEND_MOVE_PATH_MAX;
    w.base_x = g->world ? g->world->_base_tile_x : 0;
    w.base_z = g->world ? g->world->_base_tile_z : 0;
    for( int i = 0; i < w.path_len; i++ )
    {
        w.path_x[i] = px[i];
        w.path_z[i] = pz[i];
    }
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_move_gameclick_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "MOVE_GAMECLICK", NULL);
}

void
gamenet_core_send_move_opclick_v1(
    struct GGame* g,
    int opcode,
    int run,
    const int* px,
    const int* pz,
    int n)
{
    if( !g || !g->random_out || n < 1 )
        return;
    struct PktClientProt_MoveOpClick_v1 w;
    w.run = run;
    w.path_len = n < GAMENET_CORE_SEND_MOVE_PATH_MAX ? n : GAMENET_CORE_SEND_MOVE_PATH_MAX;
    w.base_x = g->world ? g->world->_base_tile_x : 0;
    w.base_z = g->world ? g->world->_base_tile_z : 0;
    for( int i = 0; i < w.path_len; i++ )
    {
        w.path_x[i] = px[i];
        w.path_z[i] = pz[i];
    }
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_move_opclick_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "MOVE_OPCLICK", NULL);
}

void
gamenet_core_send_move_minimapclick_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int run,
    const uint8_t* cam,
    int cam_n)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_MoveMinimapClick_v1 w = { x, z, run, cam, cam_n };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_move_minimapclick_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "MOVE_MINIMAPCLICK", NULL);
}

/* ── Chat ────────────────────────────────────────────────────────────────── */

void
gamenet_core_send_chat_setmode_v1(
    struct GGame* g,
    int opcode,
    int pub,
    int priv,
    int trade)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_ChatSetMode_v1 w = { pub, priv, trade };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_chat_setmode_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "CHAT_SETMODE", NULL);
}

void
gamenet_core_send_message_public_v1(
    struct GGame* g,
    int opcode,
    int color,
    int effect,
    const uint8_t* wp,
    int n)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_MessagePublic_v1 w = { color, effect, wp, n };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_message_public_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "MESSAGE_PUBLIC", NULL);
}

void
gamenet_core_send_message_private_v1(
    struct GGame* g,
    int opcode,
    const char* user,
    const uint8_t* wp,
    int n)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_MessagePrivate_v1 w = { user, wp, n };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_message_private_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "MESSAGE_PRIVATE", NULL);
}

void
gamenet_core_send_client_cheat_v1(
    struct GGame* g,
    int opcode,
    const char* line)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_ClientCheat_v1 w = { line };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_client_cheat_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "CLIENT_CHEAT", NULL);
}

/* ── Interface buttons ───────────────────────────────────────────────────── */

void
gamenet_core_send_inv_button_v1(
    struct GGame* g,
    int opcode,
    int comp_id,
    int slot,
    int obj_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_InvButton_v1 w = { comp_id, slot, obj_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_inv_button_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "INV_BUTTON", NULL);
}

void
gamenet_core_send_inv_button_d_v1(
    struct GGame* g,
    int opcode,
    int comp_id,
    int from_slot,
    int to_slot,
    int mode)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_InvButtonD_v1 w = { comp_id, from_slot, to_slot, mode };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_inv_button_d_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "INV_BUTTOND", NULL);
}

void
gamenet_core_send_if_button_v1(
    struct GGame* g,
    int opcode,
    int comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_IfButton_v1 w = { comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_if_button_v1(&b, g->random_out, opcode, &w);
    char if_button_detail[32];
    snprintf(if_button_detail, sizeof(if_button_detail), "comp_id=%d", comp_id);
    gamenet_core_net_send(g, &b, "IF_BUTTON", if_button_detail);
}

void
gamenet_core_send_if_playerdesign_v1(
    struct GGame* g,
    int opcode,
    const uint8_t* payload,
    int n)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_PlayerDesign_v1 w = { payload, n };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_if_playerdesign_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "IF_PLAYERDESIGN", NULL);
}

void
gamenet_core_send_tutorial_clickside_v1(
    struct GGame* g,
    int opcode,
    int tab)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_TutorialClick_v1 w = { tab };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_tutorial_clickside_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "TUTORIAL_CLICKSIDE", NULL);
}

/* ── World ops: held ─────────────────────────────────────────────────────── */

void
gamenet_core_send_opheld_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpHeld_v1 w = { obj_id, slot, comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opheld_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPHELD", NULL);
}

void
gamenet_core_send_opheldt_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id,
    int target_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpHeldT_v1 w = { obj_id, slot, comp_id, target_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opheldt_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPHELDT", NULL);
}

void
gamenet_core_send_opheldu_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id,
    int src_obj_id,
    int src_slot,
    int src_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpHeldU_v1 w = { obj_id, slot, comp_id, src_obj_id, src_slot, src_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opheldu_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPHELDU", NULL);
}

/* ── World ops: loc ──────────────────────────────────────────────────────── */

void
gamenet_core_send_oploc_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpLoc_v1 w = { wire_x(g, x), wire_z(g, z), loc_id, ctrl };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_oploc_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPLOC", NULL);
}

void
gamenet_core_send_oploct_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpLoc_v1 w = { wire_x(g, x), wire_z(g, z), loc_id, ctrl };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_oploct_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPLOCT", NULL);
}

void
gamenet_core_send_oplocu_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpLoc_v1 w = { wire_x(g, x), wire_z(g, z), loc_id, 0 };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_oplocu_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPLOCU", NULL);
}

void
gamenet_core_send_oploct_target_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl,
    int target_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpLoc_v1 w = { wire_x(g, x), wire_z(g, z), loc_id, ctrl };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_oploct_target_v1(
        &b, g->random_out, opcode, &w, target_comp_id);
    gamenet_core_net_send(g, &b, "OPLOCT", NULL);
}

void
gamenet_core_send_oplocu_use_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpLoc_v1 w = { wire_x(g, x), wire_z(g, z), loc_id, 0 };
    struct PktClientProt_OpHeldSourceInv_v1 src   = { obj_comp_id, slot, sel_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_oplocu_use_v1(&b, g->random_out, opcode, &w, &src);
    gamenet_core_net_send(g, &b, "OPLOCU", NULL);
}

/* ── World ops: npc ──────────────────────────────────────────────────────── */

void
gamenet_core_send_opnpc_v1(
    struct GGame* g,
    int opcode,
    int npc_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpNpc_v1 w = { npc_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opnpc_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPNPC", NULL);
}

void
gamenet_core_send_opnpct_v1(
    struct GGame* g,
    int opcode,
    int npc_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpNpc_v1 w = { npc_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opnpct_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPNPCT", NULL);
}

void
gamenet_core_send_opnpcu_v1(
    struct GGame* g,
    int opcode,
    int npc_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpNpc_v1 w = { npc_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opnpcu_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPNPCU", NULL);
}

void
gamenet_core_send_opnpct_target_v1(
    struct GGame* g,
    int opcode,
    int npc_slot,
    int target_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpNpc_v1 w = { npc_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opnpct_target_v1(&b, g->random_out, opcode, &w, target_comp_id);
    gamenet_core_net_send(g, &b, "OPNPCT", NULL);
}

void
gamenet_core_send_opnpcu_use_v1(
    struct GGame* g,
    int opcode,
    int npc_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpNpc_v1 w = { npc_slot };
    struct PktClientProt_OpHeldSourceInv_v1 src   = { obj_comp_id, slot, sel_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opnpcu_use_v1(&b, g->random_out, opcode, &w, &src);
    gamenet_core_net_send(g, &b, "OPNPCU", NULL);
}

/* ── World ops: obj ──────────────────────────────────────────────────────── */

void
gamenet_core_send_opobj_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpObj_v1 w = { wire_x(g, x), wire_z(g, z), obj_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opobj_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPOBJ", NULL);
}

void
gamenet_core_send_opobjt_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpObj_v1 w = { wire_x(g, x), wire_z(g, z), obj_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opobjt_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPOBJT", NULL);
}

void
gamenet_core_send_opobju_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpObj_v1 w = { wire_x(g, x), wire_z(g, z), obj_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opobju_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPOBJU", NULL);
}

void
gamenet_core_send_opobjt_spell_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id,
    int target_comp_id)
{
    if( !g || !g->random_out )
        return;
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opobjt_spell_v1(
        &b,
        g->random_out,
        opcode,
        wire_x(g, x),
        wire_z(g, z),
        obj_id,
        target_comp_id);
    gamenet_core_net_send(g, &b, "OPOBJT", NULL);
}

void
gamenet_core_send_opobju_use_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpObj_v1 w = { wire_x(g, x), wire_z(g, z), obj_id };
    struct PktClientProt_OpHeldSourceInv_v1 src   = { obj_comp_id, slot, sel_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opobju_use_v1(&b, g->random_out, opcode, &w, &src);
    gamenet_core_net_send(g, &b, "OPOBJU", NULL);
}

/* ── World ops: player ───────────────────────────────────────────────────── */

void
gamenet_core_send_opplayer_v1(
    struct GGame* g,
    int opcode,
    int player_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpPlayer_v1 w = { player_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opplayer_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPPLAYER", NULL);
}

void
gamenet_core_send_opplayert_v1(
    struct GGame* g,
    int opcode,
    int player_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpPlayer_v1 w = { player_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opplayert_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPPLAYERT", NULL);
}

void
gamenet_core_send_opplayeru_v1(
    struct GGame* g,
    int opcode,
    int player_slot)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpPlayer_v1 w = { player_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opplayeru_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "OPPLAYERU", NULL);
}

void
gamenet_core_send_opplayert_target_v1(
    struct GGame* g,
    int opcode,
    int player_slot,
    int target_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpPlayer_v1 w = { player_slot };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opplayert_target_v1(
        &b, g->random_out, opcode, &w, target_comp_id);
    gamenet_core_net_send(g, &b, "OPPLAYERT", NULL);
}

void
gamenet_core_send_opplayeru_use_v1(
    struct GGame* g,
    int opcode,
    int player_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_OpPlayer_v1 w = { player_slot };
    struct PktClientProt_OpHeldSourceInv_v1 src   = { obj_comp_id, slot, sel_comp_id };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_opplayeru_use_v1(&b, g->random_out, opcode, &w, &src);
    gamenet_core_net_send(g, &b, "OPPLAYERU", NULL);
}

/* ── Social ──────────────────────────────────────────────────────────────── */

void
gamenet_core_send_friend_add_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_FriendIgnore_v1 w = { userhash };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_friend_add_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "FRIEND_ADD", NULL);
}

void
gamenet_core_send_friend_del_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_FriendIgnore_v1 w = { userhash };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_friend_del_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "FRIEND_DEL", NULL);
}

void
gamenet_core_send_ignore_add_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_FriendIgnore_v1 w = { userhash };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_ignore_add_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "IGNORE_ADD", NULL);
}

void
gamenet_core_send_ignore_del_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_FriendIgnore_v1 w = { userhash };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_ignore_del_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "IGNORE_DEL", NULL);
}

void
gamenet_core_send_report_abuse_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash,
    int type)
{
    if( !g || !g->random_out )
        return;
    struct PktClientProt_ReportAbuse_v1 w = { userhash, type };
    uint8_t scratch[GAMENET_CORE_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, sizeof(scratch));
    clientprot_send_report_abuse_v1(&b, g->random_out, opcode, &w);
    gamenet_core_net_send(g, &b, "REPORT_ABUSE", NULL);
}
