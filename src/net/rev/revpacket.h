#ifndef SRC_NET_REV_REVPACKET_H
#define SRC_NET_REV_REVPACKET_H

/*
 * Revision-independent parsed-packet payloads. Wire opcodes differ per
 * revision but every payload layout here is shared by the builds this
 * client targets (lc245_2, lc254); RevPacket.packet_type is the canonical
 * GameProtoPktName, never a wire opcode.
 */

#include "pktnames.h"

#include <stdint.h>

struct PktMapRebuild
{
    int zonex;
    int zonez;
    /* dat2 / osrs230 REBUILD_NORMAL carries per-map-square XTEA keys (dat1
     * maps are unencrypted, so these stay 0/NULL there). region_keys holds
     * region_count*4 ints; region_ids[i] = (mapSquareX<<8)|mapSquareZ. Both
     * heap-allocated, freed by gameproto_free. */
    int region_count;
    int* region_ids;
    int32_t* region_keys;

    /*
     * REBUILD_REGION only: the template-chunk descriptor grid, which is what
     * makes a scene instanced.
     *
     * `PKT_NAME_REBUILD_REGION` reuses this struct rather than getting its own
     * because everything above is identical between the two packets and the
     * world-load path wants one shape, not two. `zones` is NULL for
     * REBUILD_NORMAL and that is the test for "build from the cache the ordinary
     * way".
     *
     * PKT_MAP_REBUILD_ZONES ints, indexed
     * `[level * 13 * 13 + zone_x * 13 + zone_z]` with zone_x/zone_z relative to
     * the scene's south-west zone. 0 means the destination zone has no source
     * and stays void; otherwise it is the client's own packed form —
     * `rotation = v >> 1 & 0x3`, `srcZoneZ = v >> 3 & 0x7ff`,
     * `srcZoneX = v >> 14 & 0x3ff`, `srcLevel = v >> 24 & 0x3`. Heap-allocated,
     * freed by gameproto_free.
     */
    int32_t* zones;
};

/** Descriptors in a REBUILD_REGION grid: 4 levels x 13 x 13 zones. */
#define PKT_MAP_REBUILD_ZONES (4 * 13 * 13)

// Player info packet is more of a command stream
// than a fixed format packet
struct PktNpcInfo
{
    int length;
    uint8_t* data;
};

struct PktPlayerInfo
{
    int length;
    uint8_t* data;
};

struct PktUpdateInvFull
{
    int component_id;
    /* Container id the revision addresses separately from the bound component
     * (rev 230+ sends both: combinedId + inventoryId). -1 when the revision has
     * no such field and the component id *is* the container key. CS2 reads
     * containers by inventory id (93 backpack, 94 worn), so this wins when set. */
    int inv_id;
    int size;
    // These are 1-indexed, e.g. 841 is shortbow. Over the
    // network, it's sent as 842.
    int* obj_ids;
    int* obj_counts;
};

struct PktIfSetTab
{
    int component_id;
    int tab_id;
};

struct PktIfOpenChat
{
    int component_id; /* g2: chat interface component to show */
};

struct PktIfClose
{
    /* No payload - closes sidebar, chat, viewport */
    uint8_t _torirs_empty; /* C requires a member; keep 1-byte layout */
};

struct PktIfSetTabActive
{
    int tab_id;
};

struct PktVarpSmall
{
    int variable;
    int value; /* g1b signed byte */
};

struct PktVarpLarge
{
    int variable;
    int value; /* g4 */
};

struct PktUpdateStat
{
    int stat;   /* g1: 0-22 */
    int xp;     /* g4 */
    int level;  /* g1: effective level */
};

struct PktUpdateRunEnergy
{
    int run_energy; /* g1: 0-100 */
};

struct PktIfSetColour
{
    int component_id; /* g2 */
    int colour;       /* g2: 15-bit (r<<10|g<<5|b) */
};

struct PktIfSetHide
{
    int component_id; /* g2 */
    int hide;        /* g1: 1=hide */
};

/**
 * The server declaring which of a component's slots are interactive.
 *
 * `events` is a bitmask of what the client may send for slots `from`..`to`.
 * rev 230 has no clickable-by-default: a component the server never enabled
 * produces no menu row, which is why an interface that renders fine can still
 * swallow every click.
 */
struct PktIfSetEvents
{
    int component_id; /* p4 combined uid at rev 230 */
    int from;
    int to;
    int events;
    uint32_t events1;
    uint32_t events2;
};

struct PktIfResyncMount
{
    int target_uid;
    int interface_id;
    int type;
};

struct PktIfResyncEvent
{
    int component_id;
    int from;
    int to;
    uint32_t events1;
    uint32_t events2;
};

struct PktIfResync
{
    int root_interface_id;
    int mount_count;
    struct PktIfResyncMount* mounts;
    int event_count;
    struct PktIfResyncEvent* events;
};

struct PktIfClearInv
{
    int component_id;
};

struct PktIfSetObject
{
    int component_id; /* g2 */
    int obj_id;      /* g2 */
    int zoom;        /* g2 */
};

struct PktIfSetModel
{
    int component_id; /* g2 */
    int model_id;     /* g2 */
};

struct PktIfSetAnim
{
    int component_id; /* g2 */
    int anim_id;      /* g2 */
};

struct PktIfSetPlayerHead
{
    int component_id; /* g2 */
};

struct PktIfSetText
{
    int component_id; /* g2 */
    char* text;       /* gjstr / newline-terminated */
};

struct PktIfSetNpcHead
{
    int component_id; /* g2 */
    int npc_id;       /* g2 */
};

struct PktIfSetPosition
{
    int component_id; /* g2 */
    int x;            /* g2b */
    int z;            /* g2b */
};

struct PktIfSetScrollPos
{
    int component_id; /* g2 */
    int pos;          /* g2 */
};

struct PktIfSetRotateSpeed
{
    int component_id;
    int x_speed;
    int y_speed;
};

struct PktIfSetAngle
{
    int component_id;
    int zoom;
    int angle_x;
    int angle_y;
};

struct PktIfSetNpcHeadActive
{
    int component_id;
    int index;
};

struct PktIfSetPlayerModel
{
    int component_id;
    int index;
    int value;
};

struct PktMessageGame
{
    char* text; /* gjstr / newline-terminated */
};

struct PktMessagePrivate
{
    int64_t from;       /* g8: base37 username */
    int message_id;    /* g4 */
    int staff_mod;     /* g1 */
    char* text;        /* WordPack.unpack(psize-13) */
};

struct PktChatFilterSettings
{
    int chat_public_mode;  /* g1 */
    int chat_private_mode; /* g1 */
    int chat_trade_mode;   /* g1 */
};

/* Zone packets: position = base_x + (pos>>4)&7, base_z + pos&7. Base and,
 * where the revision supplies it, plane are set by UPDATE_ZONE_*. level=-1
 * means this revision's header has no plane and the executor uses the player
 * plane for its classic wire semantics. */
struct PktUpdateZonePartialFollows
{
    int base_x; /* g1: zone base for subsequent zone packets */
    int base_z; /* g1 */
    int level;  /* header plane, or -1 when absent */
};

struct PktUpdateZoneFullFollows
{
    int base_x; /* g1 */
    int base_z; /* g1 */
    int level;  /* header plane, or -1 when absent */
};

struct PktLocAddChange
{
    int pos;   /* g1: x = base_x + (pos>>4)&7, z = base_z + pos&7 */
    int info;  /* g1: shape = info>>2, angle = info&3 */
    int loc_id; /* g2 */
};

struct PktLocDel
{
    int pos;  /* g1 */
    int info; /* g1: shape = info>>2, angle = info&3 */
};

struct PktLocAnim
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int seq_id; /* g2 */
};

struct PktObjAdd
{
    int pos;   /* g1 */
    int obj_id; /* g2 */
    int count;  /* g2 */
};

struct PktObjDel
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
};

struct PktObjReveal
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int count;     /* g2 */
    int receiver;  /* g2: player index, skip if != local */
};

struct PktObjCount
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int old_count; /* g2 */
    int new_count; /* g2 */
};

struct PktLocMerge
{
    int pos;   /* g1 */
    int info;  /* g1 */
    int loc_id; /* g2 */
    int start;  /* g2 */
    int end;    /* g2 */
    int pid;    /* g2 */
    int east;   /* g1b */
    int south;  /* g1b */
    int west;   /* g1b */
    int north;  /* g1b */
};

struct PktMapProjAnim
{
    int pos;         /* g1 */
    int dx_offset;   /* g1b: dx = x + dx_offset */
    int dz_offset;   /* g1b: dz = z + dz_offset */
    int target;      /* g2b */
    int spotanim;    /* g2 */
    int src_height;  /* g1 */
    int dst_height;  /* g1 */
    int start_delay; /* g2 */
    int end_delay;   /* g2 */
    int peak;        /* g1 */
    int arc;         /* g1 */

    /*
     * Revision 239's MapProjAnimV2 states the destination as an ABSOLUTE
     * packed CoordGrid instead of the signed per-axis offsets above; the two
     * are the same field conceptually and nothing on the wire distinguishes
     * them. `dst_abs` says which pair is meaningful — the executor knows the
     * scene origin, and this layer does not — so a decoder that sets it leaves
     * dx_offset/dz_offset at zero and vice versa.
     */
    int dst_abs;
    int dst_abs_x;
    int dst_abs_z;
    int dst_abs_level;
};

struct PktMapAnim
{
    int pos;    /* g1 */
    int id;     /* g2 */
    int height; /* g1 */
    int delay;  /* g2 */
};

/* One decoded zone sub-packet from UPDATE_ZONE_PARTIAL_ENCLOSED. */
struct PktZoneSubPacket
{
    enum GameProtoPktName name;
    union
    {
        struct PktLocAddChange _loc_add_change;
        struct PktLocDel _loc_del;
        struct PktLocAnim _loc_anim;
        struct PktObjAdd _obj_add;
        struct PktObjDel _obj_del;
        struct PktObjReveal _obj_reveal;
        struct PktObjCount _obj_count;
        struct PktLocMerge _loc_merge;
        struct PktMapProjAnim _map_projanim;
        struct PktMapAnim _map_anim;
    };
};

struct PktUpdateZoneEnclosed
{
    int base_x; /* g1 */
    int base_z; /* g1 */
    int level;  /* header plane, or -1 when absent */
    int count;
    struct PktZoneSubPacket* entries; /* heap-allocated */
};

struct PktCamLookAt
{
    int local_x; /* g1 (classic) / g2 world tile (V2, absolute=1) */
    int local_z; /* g1 (classic) / g2 world tile (V2, absolute=1) */
    int height;  /* g2 */
    int rate;    /* g1 */
    int rate2;   /* g1 */
    /* V2 (rev 239) carries ABSOLUTE world tiles — "a specific coordinate in
     * the root world" — where the classic byte pair is scene-local 0..103.
     * The exec subtracts the live scene base when this is set; treating the
     * absolute pair as scene-local renders as a black viewport, not as
     * anything that looks like a camera bug. */
    int absolute;
};

struct PktCamMoveTo
{
    int local_x; /* g1 (classic) / g2 world tile (V2, absolute=1) */
    int local_z; /* g1 (classic) / g2 world tile (V2, absolute=1) */
    int height;  /* g2 */
    int rate;    /* g1 */
    int rate2;   /* g1 */
    /* See PktCamLookAt.absolute. */
    int absolute;
};

struct PktCamShake
{
    int axis;      /* g1 */
    int amplitude; /* g1 */
    int frequency; /* g1 */
    int speed;     /* g1 */
};

struct PktIfOpenMain
{
    int component_id; /* g2 */
};

struct PktIfOpenSide
{
    int component_id; /* g2 */
};

struct PktIfOpenOverlay
{
    int component_id; /* g2 */
};

struct PktIfOpenMainSide
{
    int main_component_id; /* g2 */
    int side_component_id; /* g2 */
};

/* rev-230 IF_OPENTOP: open a group as the gameframe root (TS setRootInterface).
 * Wire (RSProt IfOpenTopEncoder): interfaceId as p2Alt1 (little-endian short). */
struct PktIfOpenTop
{
    int interface_id;
};

/* rev-230 IF_OPENSUB: mount a sub-interface group into a component slot of an
 * already-open root (TS openSubInterface). Wire (RSProt IfOpenSubEncoder):
 * p1 type, p2Alt2 interfaceId, p4Alt3 destinationCombinedId. target_uid is the
 * packed (destInterface<<16 | destComponent) mount slot; type 0=modal, 1=overlay,
 * 3=tab/sidemodal. */
struct PktIfOpenSub
{
    int target_uid;    /* packed parent<<16 | child of the mount slot */
    int interface_id;  /* sub-interface group to mount */
    int type;          /* 0 modal, 1 overlay, 3 tab/sidemodal */
};

/* rev-230 IF_CLOSESUB: unmount whatever is mounted at a component slot. Wire
 * (RSProt IfCloseSubEncoder): combinedId as p4 (big-endian packed). */
struct PktIfCloseSub
{
    int target_uid; /* packed parent<<16 | child of the mount slot */
};

/* rev-230 IF_MOVESUB: move a mounted sub from source slot to destination slot.
 * Wire (RSProt IfMoveSubEncoder): destinationCombinedId then sourceCombinedId,
 * each as p4Alt1. */
struct PktIfMoveSub
{
    int dest_uid;   /* packed dest parent<<16 | child */
    int source_uid; /* packed source parent<<16 | child */
};

/* Always 6 bytes on the wire: p1 type + p2 + p2 + p1. Field meaning varies:
 * type 1 = npc slot in `id`; type 2-6 = tile target (id=x, z, height=y);
 * type 10 = player slot in `id`; type 255/-1 = clear. */
struct PktHintArrow
{
    int type;   /* g1 */
    int id;     /* g2: npc/player slot, or tile x */
    int z;      /* g2: tile z (types 2-6) */
    int height; /* g1: height offset (types 2-6) */
};

struct PktUpdatePid
{
    int local_player_index; /* g2 */
    int unused;             /* g1 */
};

struct PktUpdateRunWeight
{
    int run_weight; /* g2s: weight in grams */
};

struct PktUpdateInvStopTransmit
{
    int component_id; /* g2 */
    int inv_id;       /* see PktUpdateInvFull.inv_id; -1 when absent */
};

struct PktUpdateInvPartialEntry
{
    int slot;   /* g1 or g2 (varies) */
    int obj_id; /* g2 */
    int count;  /* g1 or g4 */
};

struct PktUpdateInvPartial
{
    int component_id;
    int inv_id; /* see PktUpdateInvFull.inv_id; -1 when absent */
    int count;
    struct PktUpdateInvPartialEntry* entries; /* heap-allocated, count entries */
};

struct PktSetMultiway
{
    int multiway; /* g1: 1=in multicombat zone */
};

struct PktUpdateIgnoreList
{
    int count;
    int64_t* names37; /* heap-allocated, count entries */
};

struct PktUpdateFriendList
{
    int64_t name37; /* g8 */
    int world;      /* g1: 0=offline, >0 world/node id */
    int present;    /* 239 permits an empty update to complete initial sync */
};

struct PktSetNpcUpdateOrigin
{
    int x;
    int z;
};

struct PktFriendListLoaded
{
    int status; /* g1: 2 = connected */
};

struct PktUpdateRebootTimer
{
    int ticks; /* g2 */
};

struct PktLastLoginInfo
{
    int last_ip;             /* g4 */
    int days_since_login;    /* g2 */
    int days_since_recovery; /* g1 */
    int unread_messages;     /* g2 */
    int member_warning;      /* g1 (trailing byte) */
};

struct PktTutFlash
{
    int tab; /* g1 */
};

struct PktTutOpen
{
    int component_id; /* g2 */
};

struct PktSynthSound
{
    int id;    /* g2 */
    int loops; /* g1 */
    int delay; /* g2 */
};

struct PktMidiSong
{
    int id; /* g2 */
    /*
     * Fade envelope, in milliseconds, from MIDI_SONG_V2.
     *
     * The wire carries four numbers in client cycles -- a delay and a speed for
     * each direction. Only the speeds are a fade *length*; the delays are a
     * pre-roll the player does not need, because a switch here already waits
     * for the load. Zero on the pre-V2 form, which reads correctly as "switch
     * immediately".
     */
    int fade_out_ms;
    int fade_in_ms;
};

struct PktMidiJingle
{
    int id;    /* g2 */
    int delay; /* g2 */
};

/**
 * A song plus a pre-queued tonal variant of itself.
 *
 * `secondary_id` is not the next track -- it is the same arrangement with a
 * different mood (an upbeat versus a somber mix of one piece). MIDI_SWAP later
 * crossfades to it *without restarting*, so the phrase keeps running and only
 * the instrumentation changes. That is why the two ids arrive together: the
 * client has to have the variant's soundfont ready before the swap arrives.
 */
struct PktMidiSongWithSecondary
{
    int primary_id;
    int secondary_id;
    int fade_out_ms;
    int fade_in_ms;
};

/**
 * Crossfade to the variant pre-queued by MIDI_SONG_WITHSECONDARY.
 *
 * Carries no id: the target is whatever the last WITHSECONDARY named. With no
 * pending secondary this is a no-op, which is what the reference does too.
 */
struct PktMidiSwap
{
    int fade_out_ms;
    int fade_in_ms;
};

struct PktMidiSongStop
{
    /** Fade length in milliseconds (the wire carries client cycles). */
    int fade_out_ms;
};

/**
 * Server-driven background ambience.
 *
 * `id` is a sound-effect id played on a loop until AMBIENTSOUND_STOP. It has no
 * position: this is the bed for a region, not a thing standing somewhere, which
 * is why it does not go through the positional area-sound path.
 */
struct PktAmbientSoundStart
{
    int id;
    int fade_ms;
};

struct PktAmbientSoundStop
{
    int fade_ms;
};

/* SET_MAP_FLAG / UNSET_MAP_FLAG. Scene-local (x, z); 255,255 clears. lc254
 * sends a 0-byte UNSET which is treated as clear. */
struct PktSetMapFlag
{
    int x;
    int z;
    int clear; /* 1 = clear the flag */
    /* Revision 239's SetMapFlagV2 carries an absolute packed coord where the
     * classic packet carries two scene-local tile bytes. Set by the decoder
     * that read the absolute form; the executor subtracts the scene origin. */
    int absolute;
};

struct PktSetPlayerOp
{
    int slot;    /* g1: op slot 1-5 */
    int primary; /* g1: 1 = left-click priority op */
    char* text;  /* gjstr, heap */
};

/** RUNCLIENTSCRIPT: a clientscript id plus its arguments. Strings are heap and
 *  owned by the packet; `str_mask` bit i marks argument i as the string in
 *  `strv[i]` rather than the int in `intv[i]`, the same convention the CS2
 *  hook-argument plumbing already uses.
 *
 *  28 matches `ge_pricechecker_prices` (script 785) and the compiler / mock
 *  host caps. `str_mask` is uint32_t so the hard ceiling is 32. */
#define PKT_RUNCLIENTSCRIPT_ARG_MAX 28
/*
 * 128 was not enough, and the way it was not enough is worth recording: a
 * RUNCLIENTSCRIPT string argument is not a label, it is a *payload*.
 *
 * rev 230's multi-choice dialogue takes its whole option list as one string
 * with the rows joined by `|` (`chatbox_multi_init`, which splits them itself).
 * Hans's three-way choice is 132 characters, so at 128 the client silently kept
 * 127 of them — which cut the third option off entirely and left the second one
 * mid-word. The dialogue then rendered *correctly* with two options, because
 * the clientscript sizes and positions its rows by how many it was given.
 *
 * A five-option list is comfortably past 300. 512 is sized for that with room,
 * and the parser still truncates rather than overruns if something longer
 * arrives.
 */
#define PKT_RUNCLIENTSCRIPT_STR_LEN 512

struct PktRunClientScript
{
    int script_id;
    int argc;
    uint32_t str_mask;
    int intv[PKT_RUNCLIENTSCRIPT_ARG_MAX];
    char strv[PKT_RUNCLIENTSCRIPT_ARG_MAX][PKT_RUNCLIENTSCRIPT_STR_LEN];
};

/**
 * Rewrite the sparse `strv` into the COMPACTED list the CS2 dispatch wants,
 * and return how many entries it wrote.
 *
 * Two conventions meet at this call and they are not the same one. Here, an
 * argument's string is at `strv[i]` for the argument's own index `i` — the
 * index `str_mask` bit `i` names. `RS_CS2_RunScript` takes the CS2 hook path's
 * convention instead: string 0 first, because `task_cs2_run.c` walks the
 * arguments with a separate running string counter and reads `str_args[str_i]`.
 *
 * Passing the sparse array bound string N of the *packet* to string local N of
 * the *clientscript*, so a payload whose only string is the last argument bound
 * string local 0 to `strv[0]`, which is "". It could not have shown up earlier:
 * an all-int payload has nothing to place and an all-string one is already
 * compact, and those were the only two shapes content sent.
 *
 * Inline and here, beside the struct, so the conversion is testable without
 * the app: `src/net/test/runclientscript_test.c` asserts it against the same
 * packets `osrs230_parse` produced.
 */
static inline int
pkt_runclientscript_compact_strings(
    struct PktRunClientScript const* pkt,
    char const** out,
    int out_max)
{
    int count = 0;

    for( int i = 0; i < pkt->argc && i < PKT_RUNCLIENTSCRIPT_ARG_MAX; i++ )
    {
        if( !(pkt->str_mask & (1u << i)) )
            continue;
        if( count >= out_max )
            break;
        out[count++] = pkt->strv[i];
    }
    return count;
}

struct RevPacket
{
    enum GameProtoPktName packet_type;

    union
    {
        struct PktMapRebuild _map_rebuild;
        struct PktNpcInfo _npc_info;
        struct PktPlayerInfo _player_info;
        struct PktUpdateInvFull _update_inv_full;
        struct PktIfSetTab _if_settab;
        struct PktIfOpenChat _if_openchat;
        struct PktIfClose _if_close;
        struct PktIfSetTabActive _if_settab_active;
        struct PktVarpSmall _varp_small;
        struct PktVarpLarge _varp_large;
        struct PktUpdateStat _update_stat;
        struct PktUpdateRunEnergy _update_run_energy;
        struct PktIfSetColour _if_setcolour;
        struct PktIfSetHide _if_sethide;
        struct PktIfSetEvents _if_setevents;
        struct PktIfResync _if_resync;
        struct PktIfClearInv _if_clearinv;
        struct PktIfSetObject _if_setobject;
        struct PktIfSetModel _if_setmodel;
        struct PktIfSetAnim _if_setanim;
        struct PktIfSetPlayerHead _if_setplayerhead;
        struct PktIfSetText _if_settext;
        struct PktIfSetNpcHead _if_setnpchead;
        struct PktIfSetPosition _if_setposition;
        struct PktIfSetScrollPos _if_setscrollpos;
        struct PktIfSetRotateSpeed _if_setrotatespeed;
        struct PktIfSetAngle _if_setangle;
        struct PktIfSetNpcHeadActive _if_setnpchead_active;
        struct PktIfSetPlayerModel _if_setplayermodel;
        struct PktMessageGame _message_game;
        struct PktMessagePrivate _message_private;
        struct PktChatFilterSettings _chat_filter_settings;
        struct PktUpdateZonePartialFollows _update_zone_partial_follows;
        struct PktUpdateZoneFullFollows _update_zone_full_follows;
        struct PktUpdateZoneEnclosed _update_zone_enclosed;
        struct PktLocAddChange _loc_add_change;
        struct PktLocDel _loc_del;
        struct PktLocAnim _loc_anim;
        struct PktObjAdd _obj_add;
        struct PktObjDel _obj_del;
        struct PktObjReveal _obj_reveal;
        struct PktObjCount _obj_count;
        struct PktLocMerge _loc_merge;
        struct PktMapProjAnim _map_projanim;
        struct PktMapAnim _map_anim;
        struct PktCamLookAt _cam_lookat;
        struct PktCamMoveTo _cam_moveto;
        struct PktCamShake _cam_shake;
        struct PktIfOpenMain _if_openmain;
        struct PktIfOpenSide _if_openside;
        struct PktIfOpenOverlay _if_openoverlay;
        struct PktIfOpenMainSide _if_openmain_side;
        struct PktIfOpenTop _if_opentop;
        struct PktIfOpenSub _if_opensub;
        struct PktIfCloseSub _if_closesub;
        struct PktIfMoveSub _if_movesub;
        struct PktHintArrow _hint_arrow;
        struct PktUpdatePid _update_pid;
        struct PktUpdateRunWeight _update_runweight;
        struct PktUpdateInvStopTransmit _update_inv_stop_transmit;
        struct PktUpdateInvPartial _update_inv_partial;
        struct PktSetMultiway _set_multiway;
        struct PktUpdateIgnoreList _update_ignorelist;
        struct PktUpdateFriendList _update_friendlist;
        struct PktFriendListLoaded _friendlist_loaded;
        struct PktUpdateRebootTimer _update_reboot_timer;
        struct PktLastLoginInfo _last_login_info;
        struct PktTutFlash _tut_flash;
        struct PktTutOpen _tut_open;
        struct PktSynthSound _synth_sound;
        struct PktMidiSong _midi_song;
        struct PktMidiJingle _midi_jingle;
        struct PktMidiSongStop _midi_song_stop;
        struct PktMidiSongWithSecondary _midi_song_with_secondary;
        struct PktMidiSwap _midi_swap;
        struct PktAmbientSoundStart _ambientsound_start;
        struct PktAmbientSoundStop _ambientsound_stop;
        struct PktSetMapFlag _set_map_flag;
        struct PktSetPlayerOp _set_player_op;
        struct PktRunClientScript _runclientscript;
        struct PktSetNpcUpdateOrigin _set_npc_update_origin;
    };
};

/** FIFO node for parsed packets awaiting exec (v0 packets list). */
struct RevPacketItem
{
    struct RevPacket packet;
    struct RevPacketItem* next_nullable;
};

#endif
