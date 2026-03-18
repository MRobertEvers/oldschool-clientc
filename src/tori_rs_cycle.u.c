#ifndef TORI_RS_CYCLE_U_C
#define TORI_RS_CYCLE_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/game_entity.h"
#include "osrs/gameproto_process.h"
#include "osrs/interface.h"
#include "osrs/isaac.h"
#include "osrs/packetout.h"
#include "osrs/painters.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/script_queue.h"
#include "osrs/wordpack.h"
#include "tori_rs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client.ts ClientProt.MOVE_GAMECLICK = 182 (index 255) */
#define MOVE_GAMECLICK_OPCODE 182

/* Client.ts ClientCode.CC_LOGOUT = 205 */
#define CC_LOGOUT 205

// static void
// load_model_animations_dati(
//     struct SceneElement* element,
//     int sequence_id,
//     struct BuildCacheDat* buildcachedat)
// {
//     // struct SceneAnimation* scene_animation = NULL;
//     // struct CacheDatSequence* sequence = NULL;
//     // struct CacheAnimframe* animframe = NULL;

//     // struct DashFrame* dash_frame = NULL;
//     // struct DashFramemap* dash_framemap = NULL;

//     // if( sequence_id == -1 || !buildcachedat->sequences_hmap )
//     //     return;

//     // scene_element_animation_free(element);
//     // scene_animation = scene_element_animation_new(element, 10);
//     // sequence = buildcachedat_get_sequence(buildcachedat, sequence_id);
//     // assert(sequence);

//     // for( int i = 0; i < sequence->frame_count; i++ )
//     // {
//     //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
//     //     //     first 2 bytes are the sequence ID,
//     //     //     the second 2 bytes are the frame file ID
//     //     int frame_id = sequence->frames[i];

//     //     animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//     //     assert(animframe);

//     //     if( !dash_framemap )
//     //     {
//     //         dash_framemap = dashframemap_new_from_animframe(animframe);
//     //     }

//     //     // From Client-TS 245.2
//     //     int length = sequence->delay[i];
//     //     if( length == 0 )
//     //         length = animframe->delay;

//     //     // scene_element_animation_push_frame(
//     //     //     element, dashframe_new_from_animframe(animframe), dash_framemap, length);
//     // }
//     // // if( scene_animation )
//     // //     scene_animation->_anim_sequence_id = sequence_id;
// }

// /* Load primary + secondary sequences for walkmerge. Client.ts maskAnimate. */
// static void
// load_model_animations_walkmerge(
//     struct SceneElement* element,
//     int primary_sequence_id,
//     int secondary_sequence_id,
//     struct BuildCacheDat* buildcachedat)
// {
//     struct SceneAnimation* scene_animation = NULL;
//     struct CacheDatSequence* primary_seq = NULL;
//     struct CacheDatSequence* secondary_seq = NULL;
//     struct CacheAnimframe* animframe = NULL;
//     struct DashFramemap* dash_framemap = NULL;

//     if( primary_sequence_id == -1 || secondary_sequence_id == -1 ||
//     !buildcachedat->sequences_hmap )
//         return;

//     primary_seq = buildcachedat_get_sequence(buildcachedat, primary_sequence_id);
//     secondary_seq = buildcachedat_get_sequence(buildcachedat, secondary_sequence_id);
//     if( !primary_seq || !secondary_seq || !primary_seq->walkmerge )
//         return;

//     scene_element_animation_free(element);
//     scene_animation = scene_element_animation_new(element, 10);

//     /* Load primary frames */
//     for( int i = 0; i < primary_seq->frame_count; i++ )
//     {
//         int frame_id = primary_seq->frames[i];
//         animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//         assert(animframe);
//         if( !dash_framemap )
//             dash_framemap = dashframemap_new_from_animframe(animframe);
//         int length = primary_seq->delay[i];
//         if( length == 0 )
//             length = animframe->delay;
//         scene_element_animation_push_frame(
//             element, dashframe_new_from_animframe(animframe), dash_framemap, length);
//     }

//     /* Load secondary frames */
//     scene_animation->dash_frames_secondary =
//         malloc((size_t)secondary_seq->frame_count * sizeof(struct DashFrame*));
//     scene_animation->frame_count_secondary = secondary_seq->frame_count;
//     for( int i = 0; i < secondary_seq->frame_count; i++ )
//     {
//         int frame_id = secondary_seq->frames[i];
//         animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//         assert(animframe);
//         if( !dash_framemap )
//             dash_framemap = dashframemap_new_from_animframe(animframe);
//         scene_animation->dash_frames_secondary[i] = dashframe_new_from_animframe(animframe);
//     }
//     scene_animation->dash_framemap = dash_framemap;
//     scene_animation->_anim_sequence_id = primary_sequence_id;
//     scene_animation->_anim_secondary_sequence_id = secondary_sequence_id;
//     scene_animation->walkmerge = primary_seq->walkmerge;
// }

/* View of common pathing/movement/animation state shared by NPCs and players. */
struct EntityAnimUpdateView
{
    struct EntitySceneElement* scene2_element;
    struct EntityPathing* pathing;
    struct EntityDrawPosition* draw_position;
    struct EntityOrientation* orientation;
    struct EntityAnimation* animation;
    int entity_id;
    int size_x;
    int size_z;
};

/* Client.ts entityFace: set dstYaw from faceEntity or faceSquare target. 325.949 ≈ 2048/(2π) for
 * rad->yaw. */
static void
entity_face(
    struct GGame* game,
    struct EntityAnimUpdateView* view,
    bool is_player)
{
    (void)is_player;
    int fe = -1;
    if( fe >= 0 )
    {
        int target_x = 0;
        int target_z = 0;
        int has_target = 0;

        if( fe < 32768 )
        {
            /* NPC target */
            if( fe < MAX_NPCS )
            {
                struct NPCEntity* target = &game->world->npcs[fe];
                if( target->alive )
                {
                    target_x = target->draw_position.x;
                    target_z = target->draw_position.z;
                    has_target = 1;
                }
            }
        }
        else
        {
            /* Player target: index = fe - 32768. Client.ts: if index === selfSlot, use LOCAL_PLAYER
             */
            int pid = fe - 32768;
            if( pid == ACTIVE_PLAYER_SLOT )
                pid = ACTIVE_PLAYER_SLOT;
            else if( pid < MAX_PLAYERS )
                pid = pid;
            else
                pid = -1;

            if( pid >= 0 && game->world->players[pid].alive )
            {
                struct PlayerEntity* target = &game->world->players[pid];
                target_x = target->draw_position.x;
                target_z = target->draw_position.z;
                has_target = 1;
            }
        }

        if( has_target )
        {
            int dst_x = view->draw_position->x - target_x;
            int dst_z = view->draw_position->z - target_z;
            if( dst_x != 0 || dst_z != 0 )
            {
                /* Client.ts: dstYaw = ((Math.atan2(dstX, dstZ) * 325.949) | 0) & 0x7ff */
                double angle = atan2((double)dst_x, (double)dst_z);
                int yaw = (int)(angle * 325.949) & 0x7ff;
                if( yaw < 0 )
                    yaw += 2048;
                view->orientation->dst_yaw = yaw;
            }
        }
    }

    /* Client.ts FACESQUARE: face tile; cleared after use */
    // if( view->orientation->face_square_x != 0 || view->orientation->face_square_z != 0 )
    // {
    //     /* Client: dstX = e.x - (faceSquareX - mapBuildBaseX) * 64; same for Z */
    //     int base_x = game->scene_base_tile_x;
    //     int base_z = game->scene_base_tile_z;
    //     int target_x = (view->orientation->face_square_x - base_x) * 128;
    //     int target_z = (view->orientation->face_square_z - base_z) * 128;
    //     int dst_x = view->position->x - target_x;
    //     int dst_z = view->position->z - target_z;
    //     if( dst_x != 0 || dst_z != 0 )
    //     {
    //         double angle = atan2((double)dst_x, (double)dst_z);
    //         int yaw = (int)(angle * 325.949) & 0x7ff;
    //         if( yaw < 0 )
    //             yaw += 2048;
    //         view->orientation->dst_yaw = yaw;
    //     }
    // view->orientation->face_square_x = 0;
    // view->orientation->face_square_z = 0;
    // }
}

void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    /* Chat input (Client.ts handleInputKey when chatInterfaceId === -1).
     * chat_input_focused: 0=unfocused (keys go to movement); 1=focused (typing).
     * Enter when unfocused: focus. Enter when focused+empty: unfocus. Enter when focused+text:
     * send. */
    if( game->chat_interface_id == -1 && GAME_NET_STATE_GAME == game->net_state )
    {
        if( input->chat_key_return )
        {
            if( !game->chat_input_focused )
                game->chat_input_focused = 1;
            else if( !game->chat_typed[0] )
                game->chat_input_focused = 0;
        }
        if( game->chat_input_focused )
        {
            if( input->chat_key_backspace )
            {
                size_t len = strlen(game->chat_typed);
                if( len > 0 )
                    game->chat_typed[len - 1] = '\0';
            }
            if( input->chat_key_char &&
                strlen(game->chat_typed) < (size_t)(GAME_CHAT_TYPED_LEN - 1) )
            {
                char c = (char)input->chat_key_char;
                if( (c >= 32 && c <= 122) || (c >= 48 && c <= 57) )
                {
                    size_t len = strlen(game->chat_typed);
                    game->chat_typed[len] = c;
                    game->chat_typed[len + 1] = '\0';
                }
            }
            if( input->chat_key_return && game->chat_typed[0] )
            {
                int color = 0;
                int effect = 0;
                uint8_t temp_buf[256];
                struct RSBuffer buf;
                rsbuf_init(&buf, (int8_t*)temp_buf, sizeof(temp_buf));
                wordpack_pack(&buf, game->chat_typed);
                int wordpack_len = buf.position;
                int payload_len = 2 + wordpack_len;
                if( 2 + payload_len <= 268 && GAME_NET_STATE_GAME == game->net_state )
                {
                    uint8_t pkt[268];
                    uint32_t op =
                        (PKTOUT_LC245_2_MESSAGE_PUBLIC + isaac_next(game->random_out)) & 0xff;
                    int sz = 0;
                    pkt[sz++] = (uint8_t)op;
                    pkt[sz++] = (uint8_t)payload_len;
                    pkt[sz++] = (uint8_t)color;
                    pkt[sz++] = (uint8_t)effect;
                    memcpy(pkt + sz, temp_buf, (size_t)wordpack_len);
                    sz += wordpack_len;
                    LibToriRS_NetSend(game, pkt, sz);
                    game_add_message(game, 2, game->chat_typed, "Player");
                    game->chat_typed[0] = '\0';
                    game->chat_input_focused = 0;
                }
            }
        }
    }

    gameproto_process(game, game->io);

    LibToriRS_GameProcessInput(game, input);

    if( game->world )
        world_cycle(game->world, game->cycles_elapsed);

    /* Scrollbar arrow hold: while mouse is down and over up/down arrow, scroll at rate *
     * game_cycles */
    if( game->mouse_button_down && game->mouse_x >= 553 && game->mouse_x < 763 &&
        game->mouse_y >= 205 && game->mouse_y < 498 )
    {
        struct CacheDatConfigComponent* hold_component = NULL;
        int hold_root_x = 553, hold_root_y = 205;
        if( game->sidebar_interface_id != -1 )
        {
            hold_component =
                buildcachedat_get_component(game->buildcachedat, game->sidebar_interface_id);
        }
        else if(
            game->selected_tab >= 0 && game->selected_tab < 14 &&
            game->tab_interface_id[game->selected_tab] != -1 )
        {
            hold_component = buildcachedat_get_component(
                game->buildcachedat, game->tab_interface_id[game->selected_tab]);
        }
        if( hold_component )
        {
            int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
            int scrollbar_hit = interface_find_scrollbar_at(
                game,
                hold_component,
                hold_root_x,
                hold_root_y,
                game->mouse_x,
                game->mouse_y,
                &sb_y,
                &sb_height,
                &sb_scroll_height);
            if( scrollbar_hit >= 0 )
            {
                int local_y = game->mouse_y - sb_y;
                int max_scroll = sb_scroll_height - sb_height;
                if( max_scroll < 0 )
                    max_scroll = 0;
                if( local_y < 16 )
                {
                    int delta = game->cycles_elapsed - game->scroll_arrow_hold_cycles_last;
                    int step = 4 * (delta > 0 ? delta : 1);
                    interface_handle_scrollbar_arrow_step(game, scrollbar_hit, max_scroll, 1, step);
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
                }
                else if( local_y >= sb_height - 16 )
                {
                    int delta = game->cycles_elapsed - game->scroll_arrow_hold_cycles_last;
                    int step = 4 * (delta > 0 ? delta : 1);
                    interface_handle_scrollbar_arrow_step(game, scrollbar_hit, max_scroll, 0, step);
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
                }
                else
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
            }
            else
                game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
        }
        else
            game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
    }
    else
        game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;

    // Handle interface clicks (tab bar, then inventory items, etc.)
    game->interface_consumed_click = 0;
    if( game->mouse_clicked )
    {
        int mouse_x = game->mouse_clicked_x;
        int mouse_y = game->mouse_clicked_y;
        int panel_h = 50;
        int panel_top = (game->iface_view_port && game->iface_view_port->height > panel_h)
                            ? (game->iface_view_port->height - panel_h)
                            : 453;

        // Tab bar click (Client.ts handleTabInput 3018-3072): same bounds, only if tab has
        // interface
        int tab_clicked = -1;
        if( mouse_x >= 539 && mouse_x <= 573 && mouse_y >= 169 && mouse_y < 205 &&
            game->tab_interface_id[0] != -1 )
            tab_clicked = 0;
        else if(
            mouse_x >= 569 && mouse_x <= 599 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[1] != -1 )
            tab_clicked = 1;
        else if(
            mouse_x >= 597 && mouse_x <= 627 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[2] != -1 )
            tab_clicked = 2;
        else if(
            mouse_x >= 625 && mouse_x <= 669 && mouse_y >= 168 && mouse_y < 203 &&
            game->tab_interface_id[3] != -1 )
            tab_clicked = 3;
        else if(
            mouse_x >= 666 && mouse_x <= 696 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[4] != -1 )
            tab_clicked = 4;
        else if(
            mouse_x >= 694 && mouse_x <= 724 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[5] != -1 )
            tab_clicked = 5;
        else if(
            mouse_x >= 722 && mouse_x <= 756 && mouse_y >= 169 && mouse_y < 205 &&
            game->tab_interface_id[6] != -1 )
            tab_clicked = 6;
        else if(
            mouse_x >= 540 && mouse_x <= 574 && mouse_y >= 466 && mouse_y < 502 &&
            game->tab_interface_id[7] != -1 )
            tab_clicked = 7;
        else if(
            mouse_x >= 572 && mouse_x <= 602 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[8] != -1 )
            tab_clicked = 8;
        else if(
            mouse_x >= 599 && mouse_x <= 629 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[9] != -1 )
            tab_clicked = 9;
        else if(
            mouse_x >= 627 && mouse_x <= 671 && mouse_y >= 467 && mouse_y < 502 &&
            game->tab_interface_id[10] != -1 )
            tab_clicked = 10;
        else if(
            mouse_x >= 669 && mouse_x <= 699 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[11] != -1 )
            tab_clicked = 11;
        else if(
            mouse_x >= 696 && mouse_x <= 726 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[12] != -1 )
            tab_clicked = 12;
        else if(
            mouse_x >= 724 && mouse_x <= 758 && mouse_y >= 466 && mouse_y < 502 &&
            game->tab_interface_id[13] != -1 )
            tab_clicked = 13;
        if( tab_clicked >= 0 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            game->selected_tab = tab_clicked;
            /* Client.ts does not send a packet for tab change; server sets tab via IF_SETTAB_ACTIVE
             */
        }
        /* Chat area click: focus chat input when no chat interface (Client.ts chat at 17,357 size
         * ~536x96). Skip when menu visible - frame handles menu clicks. */
        else if(
            !game->menu_visible && game->chat_interface_id == -1 && mouse_x >= 17 &&
            mouse_x < 553 && mouse_y >= 357 && mouse_y < 453 )
        {
            game->interface_consumed_click = 1;
            game->chat_input_focused = 1;
        }
        /* Chat interface buttons: when chat_interface_id is set, hit-test chat component for
         * clicks. Skip when menu visible. */
        else if( !game->menu_visible && game->chat_interface_id != -1 )
        {
            int chat_y = (game->view_port && game->view_port->height > 0)
                             ? (game->view_port->height + 19)
                             : 357;
            int chat_height = 96;
            if( mouse_x >= 17 && mouse_x < 553 && mouse_y >= chat_y &&
                mouse_y < chat_y + chat_height && mouse_y < panel_top )
            {
                game->interface_consumed_click = 1;
                game->mouse_cycle = -1;
                struct CacheDatConfigComponent* chat_component =
                    buildcachedat_get_component(game->buildcachedat, game->chat_interface_id);
                if( chat_component )
                {
                    int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                    int scrollbar_hit = interface_find_scrollbar_at(
                        game,
                        chat_component,
                        17,
                        chat_y,
                        mouse_x,
                        mouse_y,
                        &sb_y,
                        &sb_height,
                        &sb_scroll_height);
                    if( scrollbar_hit >= 0 )
                    {
                        int local_y = mouse_y - sb_y;
                        int max_scroll = sb_scroll_height - sb_height;
                        if( max_scroll < 0 )
                            max_scroll = 0;
                        if( local_y < 16 )
                            interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                        else if( local_y >= sb_height - 16 )
                            interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                        else
                            interface_handle_scrollbar_click(
                                game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                    }
                    else
                    {
                        int hit_component_id = -1;
                        int hit_client_code = 0;
                        int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                        int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                        if( interface_find_button_click_at(
                                game,
                                chat_component,
                                17,
                                chat_y,
                                mouse_x,
                                mouse_y,
                                &hit_component_id,
                                &hit_client_code,
                                &button_action,
                                &menu_param_a,
                                &menu_param_b,
                                &menu_param_c) )
                        {
                            if( GAME_NET_STATE_GAME == game->net_state )
                            {
                                uint8_t pkt[8];
                                int pkt_sz;
                                if( hit_client_code == CC_LOGOUT )
                                {
                                    uint32_t op =
                                        (PKTOUT_LC245_2_LOGOUT + isaac_next(game->random_out)) &
                                        0xff;
                                    uint16_t c = menu_param_c;
                                    pkt[0] = (uint8_t)op;
                                    pkt[1] = (c >> 8) & 0xFF;
                                    pkt[2] = c & 0xFF;
                                    pkt_sz = 3;
                                    LibToriRS_NetSend(game, pkt, pkt_sz);
                                }
                                else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                                {
                                    uint32_t op = (PKTOUT_LC245_2_CLOSE_MODAL +
                                                   isaac_next(game->random_out)) &
                                                  0xff;
                                    pkt[0] = (uint8_t)op;
                                    pkt_sz = 1;
                                    LibToriRS_NetSend(game, pkt, pkt_sz);
                                }
                                else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                                {
                                    uint32_t op = (PKTOUT_LC245_2_RESUME_PAUSEBUTTON +
                                                   isaac_next(game->random_out)) &
                                                  0xff;
                                    uint16_t c = menu_param_c;
                                    pkt[0] = (uint8_t)op;
                                    pkt[1] = (c >> 8) & 0xFF;
                                    pkt[2] = c & 0xFF;
                                    pkt_sz = 3;
                                    LibToriRS_NetSend(game, pkt, pkt_sz);
                                }
                                else
                                {
                                    uint32_t op =
                                        (PKTOUT_LC245_2_IF_BUTTON + isaac_next(game->random_out)) &
                                        0xff;
                                    uint16_t c = menu_param_c;
                                    pkt[0] = (uint8_t)op;
                                    pkt[1] = (c >> 8) & 0xFF;
                                    pkt[2] = c & 0xFF;
                                    pkt_sz = 3;
                                    LibToriRS_NetSend(game, pkt, pkt_sz);
                                    interface_apply_button_click_varp_optimistic(
                                        game, menu_param_c);
                                }
                            }
                        }
                    }
                }
            }
        }
        /* Client.ts handleChatModeInput: four buttons in bottom strip (panel matches platform
         * privacy_panel_y = bottom 50px when height < 503, else y 453). */
        else if( mouse_y >= panel_top && mouse_y < panel_top + panel_h )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            if( mouse_x >= 6 && mouse_x <= 106 )
            {
                game->chat_public_mode = (game->chat_public_mode + 1) % 4;
            }
            else if( mouse_x >= 135 && mouse_x <= 235 )
            {
                game->chat_private_mode = (game->chat_private_mode + 1) % 3;
            }
            else if( mouse_x >= 273 && mouse_x <= 373 )
            {
                game->chat_trade_mode = (game->chat_trade_mode + 1) % 3;
            }
            else if( mouse_x >= 412 && mouse_x <= 512 )
            {
                /* TODO: open report abuse interface (clientCode 600) */
            }
        }
        /* Viewport overlay (e.g. bank, inventory): hit-test buttons and inventory, else block 3D.
         * Sidebar starts at x 553. Skip when menu visible - frame handles menu clicks. */
        else if( !game->menu_visible && game->viewport_interface_id != -1 && mouse_x < 553 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            struct CacheDatConfigComponent* viewport_component =
                buildcachedat_get_component(game->buildcachedat, game->viewport_interface_id);
            if( viewport_component )
            {
                int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                int scrollbar_hit = interface_find_scrollbar_at(
                    game,
                    viewport_component,
                    0,
                    0,
                    mouse_x,
                    mouse_y,
                    &sb_y,
                    &sb_height,
                    &sb_scroll_height);
                if( scrollbar_hit >= 0 )
                {
                    int local_y = mouse_y - sb_y;
                    int max_scroll = sb_scroll_height - sb_height;
                    if( max_scroll < 0 )
                        max_scroll = 0;
                    if( local_y < 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                    else if( local_y >= sb_height - 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                    else
                        interface_handle_scrollbar_click(
                            game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                }
                else
                {
                    int hit_component_id = -1;
                    int hit_client_code = 0;
                    int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                    int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                    if( interface_find_button_click_at(
                            game,
                            viewport_component,
                            0,
                            0,
                            mouse_x,
                            mouse_y,
                            &hit_component_id,
                            &hit_client_code,
                            &button_action,
                            &menu_param_a,
                            &menu_param_b,
                            &menu_param_c) )
                    {
                        if( GAME_NET_STATE_GAME == game->net_state )
                        {
                            uint8_t pkt[8];
                            int pkt_sz;
                            if( hit_client_code == CC_LOGOUT )
                            {
                                uint32_t op =
                                    (PKTOUT_LC245_2_LOGOUT + isaac_next(game->random_out)) & 0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                            {
                                uint32_t op =
                                    (PKTOUT_LC245_2_CLOSE_MODAL + isaac_next(game->random_out)) &
                                    0xff;
                                pkt[0] = (uint8_t)op;
                                pkt_sz = 1;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                            {
                                uint32_t op = (PKTOUT_LC245_2_RESUME_PAUSEBUTTON +
                                               isaac_next(game->random_out)) &
                                              0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else
                            {
                                uint32_t op =
                                    (PKTOUT_LC245_2_IF_BUTTON + isaac_next(game->random_out)) &
                                    0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                                /* Client.ts doAction: optimistic varp update for TOGGLE/SELECT */
                                interface_apply_button_click_varp_optimistic(game, menu_param_c);
                            }
                        }
                    }
                    else if(
                        viewport_component->type == COMPONENT_TYPE_LAYER &&
                        viewport_component->children )
                    {
                        for( int i = 0; i < viewport_component->children_count; i++ )
                        {
                            if( !viewport_component->childX || !viewport_component->childY )
                                continue;
                            int child_id = viewport_component->children[i];
                            int childX = viewport_component->childX[i];
                            int childY = viewport_component->childY[i];
                            struct CacheDatConfigComponent* child =
                                buildcachedat_get_component(game->buildcachedat, child_id);
                            if( !child )
                                continue;
                            childX += child->x;
                            childY += child->y;
                            if( child->type == COMPONENT_TYPE_INV )
                            {
                                int slot = interface_check_inv_click(
                                    game, child, childX, childY, mouse_x, mouse_y);
                                if( slot != -1 )
                                {
                                    int obj_id = child->invSlotObjId[slot] - 1;
                                    int action =
                                        interface_get_inv_default_action(game, child, obj_id, slot);
                                    interface_handle_inv_button(
                                        game, action, obj_id, slot, child_id);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if( mouse_x >= 553 && mouse_x < 763 && mouse_y >= 205 && mouse_y < 498 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */

            // Determine which interface to check
            int component_id = -1;
            struct CacheDatConfigComponent* component = NULL;

            if( game->sidebar_interface_id != -1 )
            {
                component_id = game->sidebar_interface_id;
                component = buildcachedat_get_component(game->buildcachedat, component_id);
            }
            else if(
                game->selected_tab >= 0 && game->selected_tab < 14 &&
                game->tab_interface_id[game->selected_tab] != -1 )
            {
                component_id = game->tab_interface_id[game->selected_tab];
                component = buildcachedat_get_component(game->buildcachedat, component_id);
            }
            if( component )
            {
                int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                int scrollbar_hit = interface_find_scrollbar_at(
                    game,
                    component,
                    553,
                    205,
                    mouse_x,
                    mouse_y,
                    &sb_y,
                    &sb_height,
                    &sb_scroll_height);
                if( scrollbar_hit >= 0 )
                {
                    /* Client.ts: top 16px = up arrow, bottom 16px = down arrow, else track */
                    int local_y = mouse_y - sb_y;
                    int max_scroll = sb_scroll_height - sb_height;
                    if( max_scroll < 0 )
                        max_scroll = 0;
                    if( local_y < 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                    else if( local_y >= sb_height - 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                    else
                        interface_handle_scrollbar_click(
                            game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                }
                else
                {
                    // Client.ts: when button is clicked, send packet per addComponentOptions +
                    // doAction. IF_BUTTON/TOGGLE/SELECT send IF_BUTTON p2(c). CLOSE sends
                    // CLOSE_MODAL. PAUSE sends RESUME_PAUSEBUTTON p2(c). LOGOUT sends LOGOUT p2(c).
                    int hit_component_id = -1;
                    int hit_client_code = 0;
                    int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                    int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                    if( interface_find_button_click_at(
                            game,
                            component,
                            553,
                            205,
                            mouse_x,
                            mouse_y,
                            &hit_component_id,
                            &hit_client_code,
                            &button_action,
                            &menu_param_a,
                            &menu_param_b,
                            &menu_param_c) )
                    {
                        if( GAME_NET_STATE_GAME == game->net_state )
                        {
                            uint8_t pkt[8];
                            int pkt_sz;
                            if( hit_client_code == CC_LOGOUT )
                            {
                                uint32_t op =
                                    (PKTOUT_LC245_2_LOGOUT + isaac_next(game->random_out)) & 0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                            {
                                uint32_t op =
                                    (PKTOUT_LC245_2_CLOSE_MODAL + isaac_next(game->random_out)) &
                                    0xff;
                                pkt[0] = (uint8_t)op;
                                pkt_sz = 1;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                            {
                                uint32_t op = (PKTOUT_LC245_2_RESUME_PAUSEBUTTON +
                                               isaac_next(game->random_out)) &
                                              0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                            }
                            else
                            {
                                /* IF_BUTTON, TOGGLE_BUTTON, SELECT_BUTTON: all send IF_BUTTON p2(c)
                                 */
                                uint32_t op =
                                    (PKTOUT_LC245_2_IF_BUTTON + isaac_next(game->random_out)) &
                                    0xff;
                                uint16_t c = menu_param_c;
                                pkt[0] = (uint8_t)op;
                                pkt[1] = (c >> 8) & 0xFF;
                                pkt[2] = c & 0xFF;
                                pkt_sz = 3;
                                LibToriRS_NetSend(game, pkt, pkt_sz);
                                /* Client.ts doAction: optimistic varp update for TOGGLE/SELECT */
                                interface_apply_button_click_varp_optimistic(game, menu_param_c);
                            }
                        }
                    }
                    else
                    {
                        // If it's a layer, check children for inventory components
                        if( component->type == COMPONENT_TYPE_LAYER && component->children )
                        {
                            for( int i = 0; i < component->children_count; i++ )
                            {
                                if( !component->childX || !component->childY )
                                    continue;

                                int child_id = component->children[i];
                                int childX = 553 + component->childX[i];
                                int childY = 205 + component->childY[i];

                                struct CacheDatConfigComponent* child =
                                    buildcachedat_get_component(game->buildcachedat, child_id);

                                if( !child )
                                    continue;

                                childX += child->x;
                                childY += child->y;

                                if( child->type == COMPONENT_TYPE_INV )
                                {
                                    int slot = interface_check_inv_click(
                                        game, child, childX, childY, mouse_x, mouse_y);

                                    if( slot != -1 )
                                    {
                                        int obj_id = child->invSlotObjId[slot] - 1;
                                        int action = interface_get_inv_default_action(
                                            game, child, obj_id, slot);

                                        interface_handle_inv_button(
                                            game, action, obj_id, slot, child_id);
                                        break;
                                    }
                                }
                            }
                        }
                        else if( component->type == COMPONENT_TYPE_INV )
                        {
                            // Direct inventory component (not in a layer)
                            int slot = interface_check_inv_click(
                                game, component, 553, 205, mouse_x, mouse_y);

                            if( slot != -1 )
                            {
                                int obj_id = component->invSlotObjId[slot] - 1;
                                int action =
                                    interface_get_inv_default_action(game, component, obj_id, slot);

                                interface_handle_inv_button(
                                    game, action, obj_id, slot, component_id);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Terrain tile click: send MOVE_GAMECLICK. Client.ts tryMove(routeTileX[0], routeTileZ[0],
     * x, z, 0, ..., true). Payload: p1(size), p1(run), p2(startX+sceneBase), p2(startZ+sceneBase),
     * then for i=1..bufferSize-1: p1(bfsStepX-startX), p1(bfsStepZ-startZ) (signed byte offset
     * from start). bufferSize = min(length, 25); start = first path point (route head). */

    dash_animate_textures(game->sys_dash, game->cycles_elapsed);
    if( game->cycle >= game->next_notimeout_cycle && GAME_NET_STATE_GAME == game->net_state )
    {
        game->next_notimeout_cycle = game->cycle + 50;
        int opcode = 206;
        uint8_t op_byte = (uint8_t)((opcode + isaac_next(game->random_out)) & 0xff);
        LibToriRS_NetSend(game, &op_byte, 1);
    }

    /* Scene dynamic elements (players/NPCs) are updated earlier in GameStep so they
     * are always present for rendering even when a script is yielded. */

    // for( int i = 0; i < game->cycles_elapsed; i++ )
    // {
    //     game->cycle++;

    //     for( int i = 0; i < game->scene->scenery->elements_length; i++ )
    //     {
    //         struct SceneElement* element = scene_element_at(game->scene->scenery, i);
    //         if( element->animation )
    //         {
    //             advance_animation(element->animation, 1);
    //         }
    //     }
    // }
    game->cycles_elapsed = 0;
}

#endif