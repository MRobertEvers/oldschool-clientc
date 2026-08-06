#ifndef SRC_NET_REV_PKTNAMES_H
#define SRC_NET_REV_PKTNAMES_H

/*
 * Canonical (revision-independent) packet names.
 *
 * Wire opcodes differ per server build (lc245_2 vs lc254) but the payload
 * layout of a given packet NAME is identical across the builds this client
 * targets. The parser, exec layer, and outbound builders all key on these
 * names; only the per-revision tables (net/rev/<rev>/packetin.h,
 * packetout.h) know wire numbers. GameProtoRevTable.packetin_code maps
 * wire -> name, .packetin_wire / .packetout_code map name -> wire.
 *
 * The inbound set is the authoritative LostCity_Server (rev 254)
 * ServerGameProt list plus its zone sub-packets; lc245_2 covers a subset
 * (its table simply has no row for names it lacks).
 */

enum GameProtoPktName
{
    PKT_NAME_NONE = 0,

    /* interfaces */
    PKT_NAME_IF_OPENCHAT,
    PKT_NAME_IF_OPENMAIN_SIDE,
    PKT_NAME_IF_CLOSE,
    PKT_NAME_IF_SETTAB,
    PKT_NAME_IF_SETTAB_ACTIVE,
    PKT_NAME_IF_OPENMAIN,
    PKT_NAME_IF_OPENSIDE,
    PKT_NAME_IF_OPENOVERLAY,

    /* modern gameframe interfaces (rev-230 openTop/openSub, targetComponent-uid
     * mount model): a root group is opened as the tree root and sub-interfaces
     * mount into a component slot addressed by a packed (parent<<16|child) uid. */
    PKT_NAME_IF_OPENTOP,
    PKT_NAME_IF_OPENSUB,
    PKT_NAME_IF_CLOSESUB,
    /** Move a mounted sub from one component slot to another (RSProt op 42). */
    PKT_NAME_IF_MOVESUB,
    /** Atomic revision-239 root + mounts + dual-word event-range snapshot. */
    PKT_NAME_IF_RESYNC_V2,
    /** Clear the item array embedded directly in an old-style component. */
    PKT_NAME_IF_CLEARINV,

    /* interface mutators */
    PKT_NAME_IF_SETCOLOUR,
    PKT_NAME_IF_SETHIDE,
    PKT_NAME_IF_SETEVENTS,
    PKT_NAME_IF_SETOBJECT,
    PKT_NAME_IF_SETMODEL,
    PKT_NAME_IF_SETANIM,
    PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_IF_SETTEXT,
    PKT_NAME_IF_SETNPCHEAD,
    PKT_NAME_IF_SETPOSITION,
    PKT_NAME_IF_SETSCROLLPOS,
    /** Rotate a model component automatically around its X/Y axes. */
    PKT_NAME_IF_SETROTATESPEED,
    /** Set a model component's camera angles and zoom. */
    PKT_NAME_IF_SETANGLE,
    /** Render one of the active NPC head slots in a model component. */
    PKT_NAME_IF_SETNPCHEAD_ACTIVE,
    /** Mutate one aspect of a component's player-composition model. */
    PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR,
    PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE,
    PKT_NAME_IF_SETPLAYERMODEL_OBJ,
    PKT_NAME_IF_SETPLAYERMODEL_SELF,

    /* tutorial */
    PKT_NAME_TUT_FLASH,
    PKT_NAME_TUT_OPEN,

    /* inventory */
    PKT_NAME_UPDATE_INV_STOP_TRANSMIT,
    PKT_NAME_UPDATE_INV_FULL,
    PKT_NAME_UPDATE_INV_PARTIAL,

    /* camera */
    PKT_NAME_CAM_LOOKAT,
    PKT_NAME_CAM_SHAKE,
    PKT_NAME_CAM_MOVETO,
    PKT_NAME_CAM_RESET,

    /* entity info command streams */
    PKT_NAME_NPC_INFO,
    PKT_NAME_PLAYER_INFO,

    /* tracking */
    PKT_NAME_FINISH_TRACKING,
    PKT_NAME_ENABLE_TRACKING,

    /* social / chat */
    PKT_NAME_MESSAGE_GAME,
    PKT_NAME_UPDATE_IGNORELIST,
    PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_MESSAGE_PRIVATE,
    PKT_NAME_UPDATE_FRIENDLIST,
    PKT_NAME_FRIENDLIST_LOADED,

    /* misc */
    PKT_NAME_UNSET_MAP_FLAG,
    PKT_NAME_UPDATE_RUNWEIGHT,
    PKT_NAME_HINT_ARROW,
    PKT_NAME_UPDATE_REBOOT_TIMER,
    PKT_NAME_UPDATE_STAT,
    PKT_NAME_UPDATE_RUNENERGY,
    PKT_NAME_RESET_ANIMS,
    PKT_NAME_UPDATE_PID,
    PKT_NAME_LAST_LOGIN_INFO,
    PKT_NAME_LOGOUT,
    PKT_NAME_P_COUNTDIALOG,
    PKT_NAME_SET_MULTIWAY,
    PKT_NAME_SET_PLAYER_OP,
    /** Run the current root's CS2 onDialogAbort listeners before teardown. */
    PKT_NAME_TRIGGER_ONDIALOGABORT,
    /** End of a server tick's packet group. The client uses it to know a tick's
     *  worth of state has arrived whole; it carries no payload. Named here
     *  because the mock emits it and a canonical name is what lets the mock's
     *  wire adapter resolve it per revision (108 at 230, 83 at 239). */
    PKT_NAME_SERVER_TICK_END,
    /* RUNCLIENTSCRIPT: run a CS2 clientscript with server-supplied arguments.
     * The world map needs it (the server pushes the player's coord through
     * `worldmap_transmitdata` before opening the interface), and it is the
     * general channel for "server tells the UI something a varp cannot say". */
    PKT_NAME_RUNCLIENTSCRIPT,

    /* maps / vars / audio */
    PKT_NAME_REBUILD_NORMAL,
    /* REBUILD_REGION: the same scene rebuild, but built out of copied zones
     * instead of the map squares under it — the POH, Pest Control, a Barrows
     * tunnel. Its payload shares struct PktMapRebuild with REBUILD_NORMAL and
     * adds the per-zone descriptor grid. */
    PKT_NAME_REBUILD_REGION,
    PKT_NAME_VARP_SMALL,
    PKT_NAME_VARP_LARGE,
    PKT_NAME_VARP_SYNC,  /* restore client copy from server-authoritative set */
    PKT_NAME_VARP_RESET, /* zero both copies (login / cache clear) */
    PKT_NAME_SYNTH_SOUND,
    PKT_NAME_MIDI_SONG,
    PKT_NAME_MIDI_JINGLE,

    /* zones + zone sub-packets (sub-packet opcodes share the wire space) */
    PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
    PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS,
    PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
    PKT_NAME_LOC_MERGE, /* aka P_LOCMERGE */
    PKT_NAME_LOC_ANIM,
    PKT_NAME_OBJ_DEL,
    PKT_NAME_OBJ_REVEAL,
    PKT_NAME_LOC_ADD_CHANGE,
    PKT_NAME_MAP_PROJANIM,
    PKT_NAME_LOC_DEL,
    PKT_NAME_OBJ_COUNT,
    PKT_NAME_MAP_ANIM,
    PKT_NAME_OBJ_ADD,

    /*
     * The origin NPC_INFO's low-resolution deltas are relative to, as a
     * scene-local (build-area) tile pair. Its own packet since revision 222:
     * world entities mean the reference point is no longer always the local
     * player, so the server states it instead of the client assuming it.
     *
     * A revision that has it and is never sent it does not fail -- the client's
     * origin is simply 0,0, and every npc is placed at its raw delta, in the
     * south-west corner of the scene or off it entirely. They decode, they are
     * in the client's npc table with the right ids, and none of them is drawn.
     */
    PKT_NAME_SET_NPC_UPDATE_ORIGIN,

    PKT_NAME_COUNT
};

/* Canonical client -> server packet names (authoritative rev-254 ClientProt
 * set; lc245_2 maps the subset it supports). */
enum GameProtoPktOutName
{
    PKTOUT_NAME_NONE = 0,

    PKTOUT_NAME_NO_TIMEOUT,
    PKTOUT_NAME_IDLE_TIMER,

    PKTOUT_NAME_EVENT_MOUSE_CLICK,
    PKTOUT_NAME_EVENT_MOUSE_MOVE,
    PKTOUT_NAME_EVENT_APPLET_FOCUS,
    PKTOUT_NAME_EVENT_TRACKING,
    PKTOUT_NAME_EVENT_CAMERA_POSITION,

    PKTOUT_NAME_ANTICHEAT_OPLOGIC1,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC2,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC3,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC4,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC5,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC6,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC7,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC8,
    PKTOUT_NAME_ANTICHEAT_OPLOGIC9,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC1,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC2,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC3,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC4,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC5,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC6,
    PKTOUT_NAME_ANTICHEAT_CYCLELOGIC7,

    PKTOUT_NAME_OPOBJ1,
    PKTOUT_NAME_OPOBJ2,
    PKTOUT_NAME_OPOBJ3,
    PKTOUT_NAME_OPOBJ4,
    PKTOUT_NAME_OPOBJ5,
    PKTOUT_NAME_OPOBJT,
    PKTOUT_NAME_OPOBJU,

    PKTOUT_NAME_OPNPC1,
    PKTOUT_NAME_OPNPC2,
    PKTOUT_NAME_OPNPC3,
    PKTOUT_NAME_OPNPC4,
    PKTOUT_NAME_OPNPC5,
    PKTOUT_NAME_OPNPCT,
    PKTOUT_NAME_OPNPCU,

    PKTOUT_NAME_OPLOC1,
    PKTOUT_NAME_OPLOC2,
    PKTOUT_NAME_OPLOC3,
    PKTOUT_NAME_OPLOC4,
    PKTOUT_NAME_OPLOC5,
    PKTOUT_NAME_OPLOCT,
    PKTOUT_NAME_OPLOCU,

    PKTOUT_NAME_OPPLAYER1,
    PKTOUT_NAME_OPPLAYER2,
    PKTOUT_NAME_OPPLAYER3,
    PKTOUT_NAME_OPPLAYER4,
    PKTOUT_NAME_OPPLAYER5,
    PKTOUT_NAME_OPPLAYERT,
    PKTOUT_NAME_OPPLAYERU,

    PKTOUT_NAME_OPHELD1,
    PKTOUT_NAME_OPHELD2,
    PKTOUT_NAME_OPHELD3,
    PKTOUT_NAME_OPHELD4,
    PKTOUT_NAME_OPHELD5,
    PKTOUT_NAME_OPHELDT,
    PKTOUT_NAME_OPHELDU,

    PKTOUT_NAME_INV_BUTTON1,
    PKTOUT_NAME_INV_BUTTON2,
    PKTOUT_NAME_INV_BUTTON3,
    PKTOUT_NAME_INV_BUTTON4,
    PKTOUT_NAME_INV_BUTTON5,
    PKTOUT_NAME_INV_BUTTOND,

    PKTOUT_NAME_IF_BUTTON,
    /* IF_BUTTON1..10 (RSProt If3Button): op N of an IF3 component, carrying the
     * packed component uid plus the sub id (a grid cell index, or -1 for a
     * plain widget). IF_BUTTON above is the op-less plain click; these are the
     * numbered verbs `if_setop` installs — the world map orb's "World Map" is
     * op 2 on component 160:53. */
    PKTOUT_NAME_IF_BUTTON1,
    PKTOUT_NAME_IF_BUTTON2,
    PKTOUT_NAME_IF_BUTTON3,
    PKTOUT_NAME_IF_BUTTON4,
    PKTOUT_NAME_IF_BUTTON5,
    PKTOUT_NAME_IF_BUTTON6,
    PKTOUT_NAME_IF_BUTTON7,
    PKTOUT_NAME_IF_BUTTON8,
    PKTOUT_NAME_IF_BUTTON9,
    PKTOUT_NAME_IF_BUTTON10,
    /*
     * IF_BUTTONX / IF_SUBOP — the one packet the whole family above collapses
     * into from revision ~237.
     *
     * `p4 combinedId, p2 sub, p2 obj, p1 op` (IF_SUBOP adds `p1 subop`), and
     * the OP NUMBER IS A FIELD rather than the opcode. That is why the newer
     * table has no OPHELD1..5, no INV_BUTTON1..5 and no IF_BUTTON1..10: twenty-
     * two opcodes became two, and `obj` (0xffff for "not an item") is what
     * separates an inventory verb from a plain widget click.
     *
     * The server already reads them (`mock239_inbound.c` fans them back out to
     * the canonical names); these exist so the *client* can name them, because
     * `net_out_opcode` refuses a canonical name the revision's table does not
     * carry — and refusing is silent. With no row, every equip, every use-on
     * and every tab click at 239 was built, encrypted, and dropped before it
     * reached the socket.
     */
    PKTOUT_NAME_IF_BUTTONX,
    PKTOUT_NAME_IF_SUBOP,
    /* IF_TRIGGEROPLOCAL (CS2 2929). Unlike IF_BUTTONX, its typed tail has no
     * on-wire type tags: crc selects the server-side signature. */
    PKTOUT_NAME_IF_SCRIPT_TRIGGER,
    /* IF_BUTTONT: the use-on, component to component — 230's OPHELDU and
     * OPHELDT in one packet, with the target and the selected item
     * interleaved rather than written as two triples. */
    PKTOUT_NAME_IF_BUTTONT,
    /* CLICK_WORLD_MAP: a click on the open world map surface, as the absolute
     * tile it landed on (packed level<<28 | x<<14 | z). */
    PKTOUT_NAME_CLICK_WORLD_MAP,
    PKTOUT_NAME_RESUME_PAUSEBUTTON,
    PKTOUT_NAME_CLOSE_MODAL,
    PKTOUT_NAME_RESUME_P_COUNTDIALOG,
    PKTOUT_NAME_RESUME_P_NAMEDIALOG,
    PKTOUT_NAME_RESUME_P_STRINGDIALOG,
    PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG,
    PKTOUT_NAME_RESUME_P_OBJDIALOG,
    PKTOUT_NAME_TUT_CLICKSIDE,

    PKTOUT_NAME_MAP_BUILD_COMPLETE,
    PKTOUT_NAME_MOVE_OPCLICK,
    PKTOUT_NAME_REPORT_ABUSE,
    PKTOUT_NAME_MOVE_MINIMAPCLICK,
    PKTOUT_NAME_MOVE_GAMECLICK,

    PKTOUT_NAME_IGNORELIST_DEL,
    PKTOUT_NAME_IGNORELIST_ADD,
    PKTOUT_NAME_FRIENDLIST_DEL,
    PKTOUT_NAME_FRIENDLIST_ADD,
    PKTOUT_NAME_IDK_SAVEDESIGN,
    PKTOUT_NAME_CHAT_SETMODE,
    PKTOUT_NAME_MESSAGE_PRIVATE,
    PKTOUT_NAME_MESSAGE_PUBLIC,
    PKTOUT_NAME_CLIENT_CHEAT,

    /** Client reports Display layout mode + canvas size (mock-local opcode;
     *  RSProt WINDOW_STATUS is op 10, but that collides with OPNPC2 here).
     *  mode is OpenRune clientMode 0/1/2, not just fixed/resizable. */
    PKTOUT_NAME_WINDOW_STATUS,

    PKTOUT_NAME_COUNT
};

#endif
