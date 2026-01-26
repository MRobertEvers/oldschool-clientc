export enum ServerProt {
    // interfaces
    IF_OPENCHAT = 7,
    IF_OPENMAIN_SIDE = 229,
    IF_CLOSE = 174,
    IF_SETTAB = 29,
    IF_OPENMAIN = 177,
    IF_OPENSIDE = 236,
    IF_SETTAB_ACTIVE = 8,

    // updating interfaces
    IF_SETCOLOUR = 135,
    IF_SETHIDE = 225,
    IF_SETOBJECT = 153,
    IF_SETMODEL = 60,
    IF_SETANIM = 69,
    IF_SETPLAYERHEAD = 83,
    IF_SETTEXT = 32,
    IF_SETNPCHEAD = 76,
    IF_SETPOSITION = 230,
    IF_SETSCROLLPOS = 226,

    // tutorial area
    TUT_FLASH = 132,
    TUT_OPEN = 152,

    // inventory
    UPDATE_INV_STOP_TRANSMIT = 143,
    UPDATE_INV_FULL = 156,
    UPDATE_INV_PARTIAL = 95,

    // camera control
    CAM_LOOKAT = 123,
    CAM_SHAKE = 103,
    CAM_MOVETO = 86,
    CAM_RESET = 134,

    // entity updates
    NPC_INFO = 105,
    PLAYER_INFO = 161,

    // input tracking
    FINISH_TRACKING = 165,
    ENABLE_TRACKING = 28,

    // social
    MESSAGE_GAME = 175,
    UPDATE_IGNORELIST = 181,
    CHAT_FILTER_SETTINGS = 2,
    MESSAGE_PRIVATE = 207,
    UPDATE_FRIENDLIST = 109,

    // misc
    UNSET_MAP_FLAG = 233,
    UPDATE_RUNWEIGHT = 70,
    HINT_ARROW = 243,
    UPDATE_REBOOT_TIMER = 26,
    UPDATE_STAT = 110,
    UPDATE_RUNENERGY = 208,
    RESET_ANIMS = 144,
    UPDATE_PID = 49,
    LAST_LOGIN_INFO = 238,
    LOGOUT = 36,
    P_COUNTDIALOG = 56,
    SET_MULTIWAY = 35,

    // maps
    REBUILD_NORMAL = 66,

    // vars
    VARP_SMALL = 192,
    VARP_LARGE = 75,
    RESET_CLIENT_VARCACHE = 25,

    // audio
    SYNTH_SOUND = 209,
    MIDI_SONG = 96,
    MIDI_JINGLE = 39,

    // zones
    UPDATE_ZONE_PARTIAL_FOLLOWS = 203,
    UPDATE_ZONE_FULL_FOLLOWS = 140,
    UPDATE_ZONE_PARTIAL_ENCLOSED = 15,

    // zone protocol
    LOC_MERGE = 188,
    LOC_ANIM = 71,
    OBJ_DEL = 13,
    OBJ_REVEAL = 190,
    LOC_ADD_CHANGE = 119,
    MAP_PROJANIM = 187,
    LOC_DEL = 198,
    OBJ_COUNT = 151,
    MAP_ANIM = 141,
    OBJ_ADD = 94
};

// prettier-ignore
export const ServerProtSizes = [
    0, 0, 3, 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 3, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 0, 0, 3, 0, 0, -2, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 4, 2, 4, 0, 0, 0, 6, 4, 0, 0, 0,
    0, 0, 0, 2, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 5, -2, 2, 0, 0, 0,
    0, 0, 0, 4, 0, -2, 0, 0, 0, 9, 6, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 6, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 0, 2, 6, 0, 2, 0, 0, 0, 0, 0, 0, 0, 7, 2,
    6, 0, 0, -2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 2, 0,
    0, 0, -2, 0, 0, 0, 0, 0, 15, 14, 0, 7, 0, 3, 0, 0, 0, 0, 0, 2, 0,
    0, 0, 0, 2, 0, 0, 0, -1, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    4, 0, 0, 4, 6, 0, 0, 0, 0, 0, 2, 0, 10, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0
];


for (const key of Object.keys(ServerProt)) {
    if (ServerProt[key] === undefined) {
        continue;
    }
    if (ServerProtSizes[ServerProt[key]] === undefined) {
        continue;
    }
    console.log(`
        [${key}] = { .length = ${ServerProtSizes[ServerProt[key]]} },
    `.trim());
}