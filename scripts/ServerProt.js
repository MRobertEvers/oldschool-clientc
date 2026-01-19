"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ServerProtSizes = exports.ServerProt = void 0;
var ServerProt;
(function (ServerProt) {
    // interfaces
    ServerProt[ServerProt["IF_OPENCHAT"] = 141] = "IF_OPENCHAT";
    ServerProt[ServerProt["IF_OPENMAIN_SIDE"] = 249] = "IF_OPENMAIN_SIDE";
    ServerProt[ServerProt["IF_CLOSE"] = 174] = "IF_CLOSE";
    ServerProt[ServerProt["IF_SETTAB"] = 91] = "IF_SETTAB";
    ServerProt[ServerProt["IF_OPENMAIN"] = 197] = "IF_OPENMAIN";
    ServerProt[ServerProt["IF_OPENSIDE"] = 187] = "IF_OPENSIDE";
    ServerProt[ServerProt["IF_SETTAB_ACTIVE"] = 138] = "IF_SETTAB_ACTIVE";
    ServerProt[ServerProt["IF_OPENOVERLAY"] = 85] = "IF_OPENOVERLAY";
    // updating interfaces
    ServerProt[ServerProt["IF_SETCOLOUR"] = 38] = "IF_SETCOLOUR";
    ServerProt[ServerProt["IF_SETHIDE"] = 227] = "IF_SETHIDE";
    ServerProt[ServerProt["IF_SETOBJECT"] = 222] = "IF_SETOBJECT";
    ServerProt[ServerProt["IF_SETMODEL"] = 211] = "IF_SETMODEL";
    ServerProt[ServerProt["IF_SETANIM"] = 95] = "IF_SETANIM";
    ServerProt[ServerProt["IF_SETPLAYERHEAD"] = 161] = "IF_SETPLAYERHEAD";
    ServerProt[ServerProt["IF_SETTEXT"] = 41] = "IF_SETTEXT";
    ServerProt[ServerProt["IF_SETNPCHEAD"] = 3] = "IF_SETNPCHEAD";
    ServerProt[ServerProt["IF_SETPOSITION"] = 27] = "IF_SETPOSITION";
    ServerProt[ServerProt["IF_SETSCROLLPOS"] = 14] = "IF_SETSCROLLPOS";
    // tutorial area
    ServerProt[ServerProt["TUT_FLASH"] = 58] = "TUT_FLASH";
    ServerProt[ServerProt["TUT_OPEN"] = 239] = "TUT_OPEN";
    // inventory
    ServerProt[ServerProt["UPDATE_INV_STOP_TRANSMIT"] = 168] = "UPDATE_INV_STOP_TRANSMIT";
    ServerProt[ServerProt["UPDATE_INV_FULL"] = 28] = "UPDATE_INV_FULL";
    ServerProt[ServerProt["UPDATE_INV_PARTIAL"] = 170] = "UPDATE_INV_PARTIAL";
    // camera control
    ServerProt[ServerProt["CAM_LOOKAT"] = 0] = "CAM_LOOKAT";
    ServerProt[ServerProt["CAM_SHAKE"] = 225] = "CAM_SHAKE";
    ServerProt[ServerProt["CAM_MOVETO"] = 55] = "CAM_MOVETO";
    ServerProt[ServerProt["CAM_RESET"] = 167] = "CAM_RESET";
    // entity updates
    ServerProt[ServerProt["NPC_INFO"] = 123] = "NPC_INFO";
    ServerProt[ServerProt["PLAYER_INFO"] = 87] = "PLAYER_INFO";
    // input tracking
    ServerProt[ServerProt["FINISH_TRACKING"] = 29] = "FINISH_TRACKING";
    ServerProt[ServerProt["ENABLE_TRACKING"] = 251] = "ENABLE_TRACKING";
    // social
    ServerProt[ServerProt["MESSAGE_GAME"] = 73] = "MESSAGE_GAME";
    ServerProt[ServerProt["UPDATE_IGNORELIST"] = 63] = "UPDATE_IGNORELIST";
    ServerProt[ServerProt["CHAT_FILTER_SETTINGS"] = 24] = "CHAT_FILTER_SETTINGS";
    ServerProt[ServerProt["MESSAGE_PRIVATE"] = 60] = "MESSAGE_PRIVATE";
    ServerProt[ServerProt["UPDATE_FRIENDLIST"] = 111] = "UPDATE_FRIENDLIST";
    ServerProt[ServerProt["FRIENDLIST_LOADED"] = 255] = "FRIENDLIST_LOADED";
    // misc
    ServerProt[ServerProt["UNSET_MAP_FLAG"] = 108] = "UNSET_MAP_FLAG";
    ServerProt[ServerProt["UPDATE_RUNWEIGHT"] = 164] = "UPDATE_RUNWEIGHT";
    ServerProt[ServerProt["HINT_ARROW"] = 64] = "HINT_ARROW";
    ServerProt[ServerProt["UPDATE_REBOOT_TIMER"] = 143] = "UPDATE_REBOOT_TIMER";
    ServerProt[ServerProt["UPDATE_STAT"] = 136] = "UPDATE_STAT";
    ServerProt[ServerProt["UPDATE_RUNENERGY"] = 94] = "UPDATE_RUNENERGY";
    ServerProt[ServerProt["RESET_ANIMS"] = 203] = "RESET_ANIMS";
    ServerProt[ServerProt["UPDATE_PID"] = 213] = "UPDATE_PID";
    ServerProt[ServerProt["LAST_LOGIN_INFO"] = 146] = "LAST_LOGIN_INFO";
    ServerProt[ServerProt["LOGOUT"] = 21] = "LOGOUT";
    ServerProt[ServerProt["P_COUNTDIALOG"] = 5] = "P_COUNTDIALOG";
    ServerProt[ServerProt["SET_MULTIWAY"] = 75] = "SET_MULTIWAY";
    ServerProt[ServerProt["SET_PLAYER_OP"] = 204] = "SET_PLAYER_OP";
    // maps
    ServerProt[ServerProt["REBUILD_NORMAL"] = 209] = "REBUILD_NORMAL";
    // vars
    ServerProt[ServerProt["VARP_SMALL"] = 186] = "VARP_SMALL";
    ServerProt[ServerProt["VARP_LARGE"] = 196] = "VARP_LARGE";
    ServerProt[ServerProt["VARP_SYNC"] = 140] = "VARP_SYNC";
    // audio
    ServerProt[ServerProt["SYNTH_SOUND"] = 25] = "SYNTH_SOUND";
    ServerProt[ServerProt["MIDI_SONG"] = 163] = "MIDI_SONG";
    ServerProt[ServerProt["MIDI_JINGLE"] = 242] = "MIDI_JINGLE";
    // zones
    ServerProt[ServerProt["UPDATE_ZONE_PARTIAL_FOLLOWS"] = 173] = "UPDATE_ZONE_PARTIAL_FOLLOWS";
    ServerProt[ServerProt["UPDATE_ZONE_FULL_FOLLOWS"] = 159] = "UPDATE_ZONE_FULL_FOLLOWS";
    ServerProt[ServerProt["UPDATE_ZONE_PARTIAL_ENCLOSED"] = 61] = "UPDATE_ZONE_PARTIAL_ENCLOSED";
    // zone protocol
    ServerProt[ServerProt["P_LOCMERGE"] = 218] = "P_LOCMERGE";
    ServerProt[ServerProt["LOC_ANIM"] = 30] = "LOC_ANIM";
    ServerProt[ServerProt["OBJ_DEL"] = 115] = "OBJ_DEL";
    ServerProt[ServerProt["OBJ_REVEAL"] = 8] = "OBJ_REVEAL";
    ServerProt[ServerProt["LOC_ADD_CHANGE"] = 70] = "LOC_ADD_CHANGE";
    ServerProt[ServerProt["MAP_PROJANIM"] = 37] = "MAP_PROJANIM";
    ServerProt[ServerProt["LOC_DEL"] = 88] = "LOC_DEL";
    ServerProt[ServerProt["OBJ_COUNT"] = 98] = "OBJ_COUNT";
    ServerProt[ServerProt["MAP_ANIM"] = 114] = "MAP_ANIM";
    ServerProt[ServerProt["OBJ_ADD"] = 120] = "OBJ_ADD";
})(ServerProt || (exports.ServerProt = ServerProt = {}));
;
// prettier-ignore
exports.ServerProtSizes = [
    6, 0, 0, 4, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 4, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 0, 6, -2, 0, 4, 0,
    0, 0, 0, 0, 0, 15, 4, 0, 0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 1, 0, -1, -2, 0, -2,
    6, 0, 0, 0, 0, 0, 4, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 0, -2, 2, 0, 0, 3, 0, 0, 1, 4, 0, 0,
    7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 6, 3,
    0, 0, 0, 0, 5, 0, 0, -2, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0,
    0, 0, 6, 0, 1, 0, 0, 2, 0, 2, 0, 0, 10, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 2, 2, 0, 0, 0, 2, 0,
    -2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 6, 2, 0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 4, 0, 4, 0, 3, 0, 0, 0, 0, 14, 0, 0, 0, 6, 0,
    0, 4, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0,
    4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 1, 0
];
for (var _i = 0, _a = Object.keys(ServerProt); _i < _a.length; _i++) {
    var key = _a[_i];
    console.log("\n        [".concat(key, "] = { .length = ").concat(exports.ServerProtSizes[ServerProt[key]], " },\n    ").trim());
}
