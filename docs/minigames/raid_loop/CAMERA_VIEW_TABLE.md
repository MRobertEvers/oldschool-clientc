# Camera view table: every writer and reader of the gathered view fields

Seam `world_view_gather` (pass matthew-mbp-m4-camera-b1-seam1), the input to the next seam (`runner_view_split`). Generated from the tree after the refactor: every code site that names a field of `struct App_WorldView` (all of them now reach it as `app->frame_view->...`, or `app.frame_view->...` in main.c), grouped by function, with the view each site will target once a second view exists, per the DESIGN's table (docs/minigames/raid_loop/CAMERA_TRIAGE_seam1.md).

Line numbers are src/-relative at the fixed tree. Kinds: W = assignment / ++ / --; R = read; A = address taken (`&view->field` handed to a callee that may write it); P = the minimenu pointer handed on (`view->minimenu`, or `*view->minimenu` copied / swapped).

Targets: **EVERY** = every attached view (scripted camera, rebase, load, init, viewport scale); **EACH** = each attached view in turn (the follow step); **PC** = PlayerClient (physical keys and mouse, plugin zoom, editors); **AR** = AutomationRunner (the driver's verbs, sim yaw, the headless harness, the capture re-render); **FRAME** = the view of the frame being built (the presented frame through PC, the offscreen frame through AR; the minimap and compass read the DRAWN yaw); **SOURCE** = the view of the input event's source (physical -> PC, driver -> AR; needs the DESIGN's per-event source); **UNCLASSIFIED** = not decided by the DESIGN, listed with a proposal, not guessed.

Totals: 558 code sites in 86 functions (W 168, R 307, A 39, P 44); 44 comment mentions renamed alongside (not listed).

Functions by target: AR 23, EACH 3, EVERY 9, EVERY / UNCLASSIFIED 1, FRAME 18, PC 12, SOURCE 13, UNCLASSIFIED 6, views[0] 1

## Unclassified (the next seam must decide these)

- `src/app/app_tick.c` `app_logic_tick` (lines 298-453): audio listener at the eye (:298), CS2 hover coord from the hover tile (:419-428), clientop menu_open (:453): not in the DESIGN's table; proposal: listener = PC (what is presented is heard), hover coord / menu_open = the hover owner
- `src/app/app_world_frame.c` `app_world_frame` (lines 212-220): CAM_FORCEANGLE writes (:217-220) -> every view; the CS2 angle mirror RS_CS2Host_SetCameraAngles (:211-212) reads ONE view's orbit: which view CAM_GETANGLE/CAM_GETYAW answers for is UNCLASSIFIED (proposal: PC while presented, AR headless = views[0])
- `src/game/rs_gameproto_exec.c` `RS_GameProto_Exec` (lines 1731-1732): CAM_MOVETO-family packet seeds the SHARED cam_script.move_lx/lz from ONE view's eye (:1731-1732); which view seeds a shared scripted camera is not in the DESIGN (proposal: views[0])
- `src/plugin/torirs_plugin_bridge.u.c` `app_plugin_click_node` (lines 4095-4104): scratch-menu swap; called by ordinary plugins AND by the driver (api_drive.if_click): needs the caller's view as a parameter (driver -> AR, plugin -> PC)
- `src/plugin/torirs_plugin_bridge.u.c` `app_plugin_world_op` (lines 4206-4209): scratch-menu swap; called by plugins AND by drive_pointer.c:1180: needs the caller's view (driver -> AR, plugin -> PC)
- `src/plugin/torirs_plugin_bridge.u.c` `app_plugin_inv_op` (lines 4333-4339): scratch-menu swap; called by plugins AND by drive_pointer.c:1208/1339: needs the caller's view (driver -> AR, plugin -> PC)
- `src/plugin/torirs_plugin_bridge.u.c` `app_plugin_widget_request` (lines 5234-5234): scratch-menu swap for a widget request; plugin-wide entry, caller's view not known here

## By file

### src/app.c

| function | target | sites | why |
|---|---|---|---|
| `App_Init` | EVERY | W world_camera 454,455,462,464; world_camera_pos 465; orbit 470,471; world_cam_zoom 474; world_hover_tile_x 475; world_hover_tile_z 476; world_hover_tile_level 477; world_hover_view 478 / R world_camera 456 | boot / login init: seeds views[0]; views[1] is made later as a copy of it |

### src/app.h

| function | target | sites | why |
|---|---|---|---|
| `App_WorldViewsInit` | views[0] | W minimenu 3215 | constructs the one view; the split seam adds views[1] (copy) and gives it its own minimenu storage |

### src/app/app_camera.c

| function | target | sites | why |
|---|---|---|---|
| `app_camera_move_forward` | PC | W world_camera_pos 69,70 / R world_camera 67,68 | free-cam keys (W/S fly) |
| `app_camera_move_left` | PC | W world_camera_pos 80,81 / R world_camera 78,79 | free-cam keys (A/D fly) |
| `app_world_camera_keys` | PC | W cam_key_left 125,156,166; cam_key_right 126,157,167; cam_key_up 127,158,168; cam_key_down 128,159,169; world_camera_pos 148,150; world_camera 174,177,180,183; camera_unlocked 194 / R camera_unlocked 171,194,195; cam_key_left 173; world_camera 175,178,181,184; cam_key_right 176; cam_key_up 179; cam_key_down 182 | arrow keys, U unlock, R/F free-cam |
| `app_debug_log_camera` | FRAME | R orbit 305,306; world_camera 305,306; world_cam_zoom 307; world_camera_pos 308,309,310 | debug log of the view it is called for |
| `app_world_camera_mouse` | PC | W cam_mmb_active 339,358,399,402; cam_mmb_x 359,368; cam_mmb_y 360,369; orbit 378,380,382,383; world_camera 387,389; world_cam_zoom 431 / R cam_mmb_active 355,363; cam_mmb_x 365; cam_mmb_y 366; orbit 379,381; world_camera 388,390; world_cam_zoom 433 / P minimenu 356,410 | middle-button orbit + wheel zoom; reads the PC menu's visibility |
| `app_cinema_angles` | EACH | R world_camera_pos 511,512,513 | cutscene look-at, evaluated for the view the cinema step is easing |
| `App_CinemaCameraSnapPosition` | EVERY | A world_camera_pos 533,534,535 | CAM_* packet snap (cutscene) |
| `App_CinemaCameraSnapAngle` | EVERY | A world_camera 542 | CAM_* packet snap (cutscene) |
| `app_world_camera_cinema` | EVERY | W world_camera_pos 591,593,595; world_camera 601,613,616,627 / R world_camera_pos 592,594,596,635,636,637; world_camera 602,606,614,617,621,638,639 | cutscene / CAM_* easing: scripted camera takes every view |
| `app_world_camera_limits` | EACH | R world_cam_zoom 712; world_camera 716 | limits of the view being stepped |
| `app_world_camera_follow` | EACH | W orbit 781,782,785,786; world_cam_zoom 794; world_camera 941,942 / R camera_unlocked 734; cam_key_left 861; cam_key_right 862; cam_key_up 863; cam_key_down 864; orbit 875,876,904,936,937,953,954,957,958; world_camera_pos 963,964,965 / A orbit 856,866,900,903; world_camera_pos 940 | the follow step runs once per attached view |

### src/app/app_debug_ui.c

| function | target | sites | why |
|---|---|---|---|
| `app_debug_log_position` | FRAME | R world_camera_pos 365,366,367; world_camera 368,369 | debug position line |

### src/app/app_frame.c

| function | target | sites | why |
|---|---|---|---|
| `App_RunOnce` | SOURCE | W pointer_absent 1170; world_mouse_in_viewport 1171; world_mouse_x 1174; world_mouse_y 1175; world_hover_tile_x 1178; world_hover_tile_z 1179; world_hover_view 1180 / R pointer_absent 1172; world_mouse_in_viewport 1176; world_pickset 1429 / A world_pickset 1181,1225,1450 / P minimenu 896,898,1214,1341,1377,1469,1478 | pointer latch from input->curr, hover/pick reset, left-click pick consume, minimenu afterimage and scratch-menu swaps: the view whose event it is (physical -> PC, driver -> AR); needs the per-event source of the DESIGN's INPUT paragraph |

### src/app/app_host_request.c

| function | target | sites | why |
|---|---|---|---|
| `app_host_request` | FRAME | R world_camera 119; world_camera_pos 131,133 / P minimenu 100 | UI/CS2 host reads: minimenu visible/state (the presented menu; the other view's drawn tinted, decision 3), compass yaw, minimap eye fallback |

### src/app/app_hotkeys.c

| function | target | sites | why |
|---|---|---|---|
| `app_ui_hotkeys` | PC | P minimenu 131 | keyboard closes the PC menu |
| `app_world_hotkeys` | PC | R world_hover_tile_x 291,297,303,314,321,328; world_hover_tile_z 291,298,304,315,322,329; world_hover_tile_level 299,305,316,323,330 | debug spawn hotkeys at the hovered tile |

### src/app/app_loc_editor.c

| function | target | sites | why |
|---|---|---|---|
| `app_loc_editor_tick` | PC | R world_hover_tile_x 591; world_hover_tile_z 592 | interactive loc editor, hovered tile |

### src/app/app_map_editor.c

| function | target | sites | why |
|---|---|---|---|
| `app_map_editor_world_click` | PC | R world_hover_tile_x 209,211,231,264,281; world_hover_tile_z 210,232,265,282; world_hover_tile_level 266 / P minimenu 187,192 | interactive map editor click |
| `app_map_editor_ghost_update` | PC | R world_hover_tile_x 376,391,407; world_hover_tile_z 392,408 / P minimenu 377 | map editor hover ghost |

### src/app/app_minimap.c

| function | target | sites | why |
|---|---|---|---|
| `app_minimap_push_dot` | FRAME | R world_camera 74 | minimap rotation by the DRAWN yaw |
| `app_minimap_push_hint` | FRAME | R world_camera 247 | minimap hint arrow by the drawn yaw |
| `App_MinimapBuildDots` | FRAME | R world_camera 354 | minimap dots by the drawn yaw |

### src/app/app_minimenu.c

| function | target | sites | why |
|---|---|---|---|
| `app_minimenu_entries_publish` | SOURCE | P minimenu 391 | publishes the open menu's entries (mouseover) for the view that opened it |
| `app_hover_text_update` | SOURCE | R pointer_absent 506 / A world_pickset 536 / P minimenu 506 | hover text follows whichever pointer moved last (DESIGN INPUT) |
| `app_minimenu_open` | SOURCE | R world_pickset 955,961 / A world_pickset 908,963 / P minimenu 917 | right-click opens the menu of the view that clicked, from its pickset |
| `app_run_default_ui_row` | SOURCE | P minimenu 1434,1469 | default-row click: the clicking view's scratch menu |
| `app_inv_drag_tick` | SOURCE | P minimenu 1853 | inventory drag closes the dragging view's menu |
| `app_minimenu_close_if_stale` | SOURCE | P minimenu 2073 | the open menu's view |
| `app_minimenu_run_option` | SOURCE | P minimenu 2094 | the menu row's view |
| `app_minimenu_use_option` | SOURCE | P minimenu 3260 | the menu row's view |

### src/app/app_overlay.c

| function | target | sites | why |
|---|---|---|---|
| `app_overlay_build_hover_footprint` | FRAME | R world_pickset 1343,1345 / A world_pickset 1347 | hover footprint overlay drawn from the frame view's pickset |

### src/app/app_plugin_api.c

| function | target | sites | why |
|---|---|---|---|
| `App_SetCameraPose` | AR | W orbit 305,306,308; world_camera 305,306; world_cam_zoom 307 | the driver's camera verb API (verbs-ui) |
| `App_MinimenuRowCenter` | SOURCE | P minimenu 405 | only caller main.c:3929, a TORIRS_SIM_* knob on the real event path: PC in a watched client, views[0] headless |

### src/app/app_plugin_assets.c

| function | target | sites | why |
|---|---|---|---|
| `app_capture_fallback_render` | AR | W world_mouse_in_viewport 343,345 / R world_mouse_in_viewport 342 | the capture re-render (pick disarmed, restored): the offscreen frame's view |

### src/app/app_render.c

| function | target | sites | why |
|---|---|---|---|
| `App_BuildFrame` | FRAME | R world_camera_pos 110,111,112 / A world_camera 109 | frame build reads the eye |
| `App_Render` | FRAME | R world_mouse_x 710; world_mouse_y 710 / A world_mouse_in_viewport 708 | arms the pick at the frame view's pointer |

### src/app/app_tick.c

| function | target | sites | why |
|---|---|---|---|
| `app_logic_tick` | UNCLASSIFIED | R world_camera_pos 298,299; world_hover_tile_z 420,423,427; world_hover_tile_x 422,426; world_hover_tile_level 428 / A world_hover_tile_x 419 / P minimenu 453 | audio listener at the eye (:298), CS2 hover coord from the hover tile (:419-428), clientop menu_open (:453): not in the DESIGN's table; proposal: listener = PC (what is presented is heard), hover coord / menu_open = the hover owner |

### src/app/app_ui_host.c

| function | target | sites | why |
|---|---|---|---|
| `app_ui_host_publish_inputs` | FRAME | R world_camera 161,163; world_camera_pos 165,167,169 / P minimenu 195 | UI retain signature's camera domain + menu state of the presented view |

### src/app/app_viewport.c

| function | target | sites | why |
|---|---|---|---|
| `app_apply_wedge_scale` | EVERY | W world_camera 115,134,135 / R world_camera 116,150,151,152 | viewport projection scale |

### src/app/app_wev.c

| function | target | sites | why |
|---|---|---|---|
| `app_sailing_heading_at` | SOURCE | A world_camera 143; world_camera_pos 144 | sailing steer heading under a pointer: the view whose pointer |
| `app_sailing_register_arrows` | SOURCE | R world_mouse_in_viewport 214; pointer_absent 214; world_mouse_x 218; world_mouse_y 218; world_camera 221,222,223 / P minimenu 215 | steer arrows at the hover owner's pointer (reads its camera yaw) |

### src/app/app_world_click.c

| function | target | sites | why |
|---|---|---|---|
| `app_world_pick_finish` | FRAME | W world_hover_tile_x 739,745; world_hover_tile_z 740,746; world_hover_tile_level 741; world_hover_view 750,756; world_hover_view_x 751; world_hover_view_z 752; world_hover_view_level 753 / R world_mouse_x 735,762; world_mouse_y 736,763; world_pickset 764,768,774,779,780,781,782,783 / A world_pickset 727,734 | fills the frame view's pickset and hover tiles after the draw |

### src/app/app_world_frame.c

| function | target | sites | why |
|---|---|---|---|
| `app_world_frame` | EVERY / UNCLASSIFIED | W orbit 217,218,219,220 / R orbit 212 | CAM_FORCEANGLE writes (:217-220) -> every view; the CS2 angle mirror RS_CS2Host_SetCameraAngles (:211-212) reads ONE view's orbit: which view CAM_GETANGLE/CAM_GETYAW answers for is UNCLASSIFIED (proposal: PC while presented, AR headless = views[0]) |

### src/app/app_world_load.c

| function | target | sites | why |
|---|---|---|---|
| `app_bind_configured_overlays` | EVERY | W minimenu 41,50 | minimenu font from the tree: every view's menu |
| `app_world_load_begin` | EVERY | R world_camera_pos 326,327,328; world_camera 329,330 / A cam_hold 323 | world load: camera hold capture |
| `App_WorldLoadFinish` | EVERY | W world_camera_pos 444,445,446,455,456,457; world_camera 447,448,458,459 / A cam_hold 431; world_camera_pos 435,436,437; world_camera 438,439 | world load: camera restore / reposition |

### src/app/app_world_paint.c

| function | target | sites | why |
|---|---|---|---|
| `app_world_roof_check` | FRAME | R world_camera_pos 58,59,61,62,75,76; world_camera 71 | per paint: roof mask from the frame view's eye |
| `app_update_painter_cull` | FRAME | R orbit 169,170,171,172; world_camera_pos 179,180,191; world_camera 193,240,241,242,253,259,260,306,307,317,318,319 | per paint: cull span / draw centre |
| `app_world_paint` | FRAME | W world_camera_pos 379,380,381,387,388,389,459,463,466,616,617,618; world_camera 382,383,390,391,470,474,619,620 / R world_camera_pos 399,400,441,442,443,460,467,508,509,510,542,543,544,575,576,577,592,593,594,635,636,637; world_camera 444,445,471,475,496,497,506,507,549,550,638,639 | per paint; CAM_SHAKE jitter applied to and restored on the frame view (the roll is shared once per frame per the DESIGN) |

### src/app/app_world_project.c

| function | target | sites | why |
|---|---|---|---|
| `app_world_project_at` | FRAME | A world_camera 46; world_camera_pos 47 | overlay projection |

### src/app/app_world_rebuild.c

| function | target | sites | why |
|---|---|---|---|
| `App_WorldRebuildShift` | EVERY | W world_camera_pos 461,462; orbit 463,464 / P minimenu 477 | scene rebase (orbit anchor + eye shift, menu hide) |

### src/app/app_worldmap.c

| function | target | sites | why |
|---|---|---|---|
| `app_worldmap_drag_tick` | SOURCE | P minimenu 881 | world map drag closes the dragging view's menu |

### src/editor/editor_panel.c

| function | target | sites | why |
|---|---|---|---|
| `panel_refresh` | PC | R world_hover_tile_x 1060; world_hover_tile_z 1061 | editor panel probes the hovered tile |
| `Editor_PanelEditLevel` | PC | R world_hover_tile_level 1627 | editor panel hovered level |

### src/game/content_test.c

| function | target | sites | why |
|---|---|---|---|
| `ContentTest_DrawRequested` | AR | R world_pickset 153 | headless content-test harness (views[0]) |
| `state_json` | AR | R orbit 218,219; world_cam_zoom 220; world_camera 277,278; world_camera_pos 279,280,281 | headless content-test state report |
| `wev_json` | AR | R world_pickset 482,484 | headless content-test sailing report |
| `ContentTest_Begin` | AR | W orbit 633,634,636; world_camera 633,634; world_cam_zoom 635 | headless content-test camera park |

### src/game/content_test_sailing.c

| function | target | sites | why |
|---|---|---|---|
| `peer_screen` | AR | A world_camera 452; world_camera_pos 453 | headless sailing harness projection |
| `peer_picked` | AR | R world_pickset 471,472,473 | headless sailing harness pick read |
| `ContentTestSailing_Save` | AR | R world_camera 659; world_camera_pos 660; orbit 663,664,665,666,667,668,669; world_cam_zoom 670; camera_unlocked 671 | headless sailing harness camera save |
| `ContentTestSailing_SettleRestore` | AR | W world_camera 942; world_camera_pos 943,944,945; orbit 946,947,948,949,950,951,952; world_cam_zoom 953; camera_unlocked 956 | headless sailing harness camera restore |

### src/game/rs_gameproto_exec.c

| function | target | sites | why |
|---|---|---|---|
| `RS_GameProto_Exec` | UNCLASSIFIED | R world_camera_pos 1731,1732 | CAM_MOVETO-family packet seeds the SHARED cam_script.move_lx/lz from ONE view's eye (:1731-1732); which view seeds a shared scripted camera is not in the DESIGN (proposal: views[0]) |

### src/main.c

| function | target | sites | why |
|---|---|---|---|
| `interactive_render_present` | FRAME | R world_mouse_in_viewport 762,833,914,986,1059,1135; world_mouse_x 765,836,917,989,1062,1138; world_mouse_y 765,836,917,989,1062,1138 | GPU lanes (D3D9, GLES2/3, WebGL1/2, GL3) arm the pick at the frame view's pointer |
| `frame_loop_step` | AR | W orbit 3724,3726 / R orbit 3744 | TORIRS_SIM_CAMERA_YAW (sim yaw) |
| `main` | AR | W orbit 7135,7137; world_camera 7138 / R orbit 7138; world_camera 7139 | sim_camera_yaw park (sim yaw) |

### src/plugin/torirs_plugin_bridge.u.c

| function | target | sites | why |
|---|---|---|---|
| `app_plugin_hover_tile` | FRAME | R world_hover_view 1864,1865,1868; world_hover_view_x 1874; world_hover_view_z 1875; world_hover_view_level 1876,1877; world_hover_tile_x 1881,1888; world_hover_tile_z 1881,1889; world_hover_tile_level 1890 | plugin hover tile (drawn for the presented view) |
| `app_plugin_hover_entity` | FRAME | R world_pickset 1920 / A world_pickset 1922 | plugin hover entity from the frame view's pickset |
| `app_plugin_feature_set` | PC | W world_cam_zoom 2747,2766 / R world_cam_zoom 2767 | plugin zoom feature |
| `app_plugin_click_node` | UNCLASSIFIED | P minimenu 4095,4104 | scratch-menu swap; called by ordinary plugins AND by the driver (api_drive.if_click): needs the caller's view as a parameter (driver -> AR, plugin -> PC) |
| `app_plugin_world_op` | UNCLASSIFIED | P minimenu 4206,4209 | scratch-menu swap; called by plugins AND by drive_pointer.c:1180: needs the caller's view (driver -> AR, plugin -> PC) |
| `app_plugin_inv_op` | UNCLASSIFIED | P minimenu 4333,4339 | scratch-menu swap; called by plugins AND by drive_pointer.c:1208/1339: needs the caller's view (driver -> AR, plugin -> PC) |
| `app_plugin_widget_request` | UNCLASSIFIED | P minimenu 5234 | scratch-menu swap for a widget request; plugin-wide entry, caller's view not known here |

### src/plugin/torirs_plugin_drive_pointer.c

| function | target | sites | why |
|---|---|---|---|
| `drive_pointer_project_point` | AR | A world_camera 174; world_camera_pos 175 | driver's analytic projection (DrivePointer_ScreenPosition) |
| `DrivePointer_PickHolds` | AR | R world_pickset 622,624 | driver pick_holds reads the runner's pickset |
| `DrivePointer_PickPoint` | AR | R world_pickset 643,644,645 | driver pick point |
| `drive_pointer_world_gate` | AR | P minimenu 735 | driver world gate (menu visible) |
| `DrivePointer_MenuRows` | AR | P minimenu 810 | driver menu rows |
| `DrivePointer_MenuRowFind` | AR | P minimenu 870 | driver menu_row_find |
| `drive_pointer_spell_arm` | AR | P minimenu 1460,1466 | driver spell arm scratch menu |
| `drive_pointer_inv_cast` | AR | P minimenu 1566,1573 | driver inventory cast scratch menu |
| `DrivePointer_Camera` | AR | W orbit 1630,1631,1633,1634; world_camera 1630,1631; world_cam_zoom 1632 | driver camera verb |
| `lua_drive_camera_state` | AR | R world_camera_pos 2229,2231; world_camera 2235,2237; world_cam_zoom 2239 | driver camera state read |

### src/plugin/torirs_plugin_drive_ui.c

| function | target | sites | why |
|---|---|---|---|
| `DriveUi_MenuRect` | AR | P minimenu 233 | driver menu rect |

## Not in the table

- Test fixtures that build a `struct App` by hand now call `App_WorldViewsInit` (src/app/test/app_mouseover_entry_test.c, src/world/test/wev_rebuild_test.c, src/game/test/minimap_dots_test.c); they read views[0].
- The minimenu's STORAGE stays `app->interact.minimenu` inside `struct UIInteraction` (src/ui/uitree_interact.c opens, steers and closes it through `interact->minimenu`); `view->minimenu` points at it. A second view needs its own `struct UIMinimenu` and the UI interaction step must be told which menu it is driving: that is src/ui work the next seam owns.
- Lanes: no platform renderer names a gathered field; the GPU lanes' pick arming is in main.c `interactive_render_present` (listed above). Built here after the rename: native macOS, questtest, scanmeter, gfmatrix, contenttest, torirsserver, maped(ctl), win64 + win32 (mingw, D3D9), web (emcc), android (NDK armv7). Not built here: linux, iOS (grep: no gathered name in ios/ or android/ Java/C).
