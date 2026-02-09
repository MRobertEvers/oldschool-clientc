import { ServerProt } from '#/io/ServerProt.ts';

const lengths: number[] = [];
lengths[ServerProt.IF_OPENCHAT] = 2;
lengths[ServerProt.IF_OPENMAIN_SIDE] = 4;
lengths[ServerProt.IF_CLOSE] = 0;
lengths[ServerProt.IF_SETTAB] = 3;
lengths[ServerProt.IF_SETTAB_ACTIVE] = 1;
lengths[ServerProt.IF_OPENMAIN] = 2;
lengths[ServerProt.IF_OPENSIDE] = 2;

lengths[ServerProt.IF_SETCOLOUR] = 4;
lengths[ServerProt.IF_SETHIDE] = 3;
lengths[ServerProt.IF_SETOBJECT] = 6;
lengths[ServerProt.IF_SETMODEL] = 4;
lengths[ServerProt.IF_SETANIM] = 4;
lengths[ServerProt.IF_SETPLAYERHEAD] = 2;
lengths[ServerProt.IF_SETTEXT] = -2;
lengths[ServerProt.IF_SETNPCHEAD] = 4;
lengths[ServerProt.IF_SETPOSITION] = 6;
lengths[ServerProt.IF_SETSCROLLPOS] = 4;

lengths[ServerProt.TUT_FLASH] = 1;
lengths[ServerProt.TUT_OPEN] = 2;

lengths[ServerProt.UPDATE_INV_STOP_TRANSMIT] = 2;
lengths[ServerProt.UPDATE_INV_FULL] = -2;
lengths[ServerProt.UPDATE_INV_PARTIAL] = -2;

lengths[ServerProt.CAM_LOOKAT] = 6;
lengths[ServerProt.CAM_SHAKE] = 4;
lengths[ServerProt.CAM_MOVETO] = 6;
lengths[ServerProt.CAM_RESET] = 0;

lengths[ServerProt.NPC_INFO] = -2;
lengths[ServerProt.PLAYER_INFO] = -2;

lengths[ServerProt.FINISH_TRACKING] = 0;
lengths[ServerProt.ENABLE_TRACKING] = 0;

lengths[ServerProt.MESSAGE_GAME] = -1;
lengths[ServerProt.UPDATE_IGNORELIST] = -2;
lengths[ServerProt.CHAT_FILTER_SETTINGS] = 3;
lengths[ServerProt.MESSAGE_PRIVATE] = -1;
lengths[ServerProt.UPDATE_FRIENDLIST] = 9;

lengths[ServerProt.UNSET_MAP_FLAG] = 0;
lengths[ServerProt.UPDATE_RUNWEIGHT] = 2;
lengths[ServerProt.HINT_ARROW] = 6;
lengths[ServerProt.UPDATE_REBOOT_TIMER] = 2;
lengths[ServerProt.UPDATE_STAT] = 6;
lengths[ServerProt.UPDATE_RUNENERGY] = 1;
lengths[ServerProt.RESET_ANIMS] = 0;
lengths[ServerProt.UPDATE_PID] = 3;
lengths[ServerProt.LAST_LOGIN_INFO] = 10;
lengths[ServerProt.LOGOUT] = 0;
lengths[ServerProt.P_COUNTDIALOG] = 0;
lengths[ServerProt.SET_MULTIWAY] = 1;

lengths[ServerProt.REBUILD_NORMAL] = 4;

lengths[ServerProt.VARP_SMALL] = 3;
lengths[ServerProt.VARP_LARGE] = 6;
lengths[ServerProt.RESET_CLIENT_VARCACHE] = 0;

lengths[ServerProt.SYNTH_SOUND] = 5;
lengths[ServerProt.MIDI_SONG] = 2;
lengths[ServerProt.MIDI_JINGLE] = 4;

lengths[ServerProt.UPDATE_ZONE_PARTIAL_FOLLOWS] = 2;
lengths[ServerProt.UPDATE_ZONE_FULL_FOLLOWS] = 2;
lengths[ServerProt.UPDATE_ZONE_PARTIAL_ENCLOSED] = -2;

lengths[ServerProt.LOC_MERGE] = 14;
lengths[ServerProt.LOC_ANIM] = 4;
lengths[ServerProt.OBJ_DEL] = 3;
lengths[ServerProt.OBJ_REVEAL] = 7;
lengths[ServerProt.LOC_ADD_CHANGE] = 4;
lengths[ServerProt.MAP_PROJANIM] = 15;
lengths[ServerProt.LOC_DEL] = 2;
lengths[ServerProt.OBJ_COUNT] = 7;
lengths[ServerProt.MAP_ANIM] = 6;
lengths[ServerProt.OBJ_ADD] = 5;

const organized = [];
for (let i = 0; i < 255; i++) {
    if (typeof lengths[i] !== 'undefined') {
        organized[i] = lengths[i];
    } else {
        organized[i] = 0;
    }
}

console.log(organized.slice(0, 100));
console.log(organized.slice(100, 200));
console.log(organized.slice(200));
