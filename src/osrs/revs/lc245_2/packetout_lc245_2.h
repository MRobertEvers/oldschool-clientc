#ifndef PACKETOUT_LC245_2_H
#define PACKETOUT_LC245_2_H

#include <stdint.h>

/* LC245_2 client→server logical opcode bytes (before Isaac): wire = (id + isaac_next()) & 0xff.
 * Enum values are the `id` field from `ClientGameProt(index, id, length)`; trailing comment is
 * `index`. */

enum PacketOutType_LC245_2
{
    PKTOUT_LC245_2_NO_TIMEOUT = 206, /* 6 - NXT naming */

    PKTOUT_LC245_2_IDLE_TIMER = 102,    /* 30 */
    PKTOUT_LC245_2_EVENT_TRACKING = 19, /* 34 */

    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC1 = 87,  /* 60 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC2 = 95,  /* 61 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC3 = 146, /* 62 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC4 = 186, /* 63 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC5 = 74,  /* 64 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC6 = 250, /* 65 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC7 = 119, /* 66 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC8 = 171, /* 67 */
    PKTOUT_LC245_2_ANTICHEAT_OPLOGIC9 = 233, /* 68 */

    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC1 = 136, /* 70 */
    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC2 = 223, /* 71 */
    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC3 = 181, /* 74 */
    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC4 = 94,  /* 72 */
    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC5 = 63,  /* 75 */
    PKTOUT_LC245_2_ANTICHEAT_CYCLELOGIC6 = 112, /* 73 */

    PKTOUT_LC245_2_OPOBJ1 = 113, /* 80 - NXT naming */
    PKTOUT_LC245_2_OPOBJ2 = 238, /* 81 - NXT naming */
    PKTOUT_LC245_2_OPOBJ3 = 55,  /* 82 - NXT naming */
    PKTOUT_LC245_2_OPOBJ4 = 17,  /* 83 - NXT naming */
    PKTOUT_LC245_2_OPOBJ5 = 247, /* 84 - NXT naming */
    PKTOUT_LC245_2_OPBJT = 122,  /* 88 - NXT naming */
    PKTOUT_LC245_2_OPOBJU = 143, /* 89 - NXT naming */

    PKTOUT_LC245_2_OPNPC1 = 180, /* 100 - NXT naming */
    PKTOUT_LC245_2_OPNPC2 = 252, /* 101 - NXT naming */
    PKTOUT_LC245_2_OPNPC3 = 196, /* 102 - NXT naming */
    PKTOUT_LC245_2_OPNPC4 = 107, /* 103 - NXT naming */
    PKTOUT_LC245_2_OPNPC5 = 43,  /* 104 - NXT naming */
    PKTOUT_LC245_2_OPNPCT = 141, /* 108 - NXT naming */
    PKTOUT_LC245_2_OPNPCU = 14,  /* 109 - NXT naming */

    PKTOUT_LC245_2_OPLOC1 = 1,   /* 120 - NXT naming */
    PKTOUT_LC245_2_OPLOC2 = 219, /* 121 - NXT naming */
    PKTOUT_LC245_2_OPLOC3 = 226, /* 122 - NXT naming */
    PKTOUT_LC245_2_OPLOC4 = 204, /* 123 - NXT naming */
    PKTOUT_LC245_2_OPLOC5 = 86,  /* 124 - NXT naming */
    PKTOUT_LC245_2_OPLOCT = 208, /* 128 - NXT naming */
    PKTOUT_LC245_2_OPLOCU = 147, /* 129 - NXT naming */

    PKTOUT_LC245_2_OPPLAYER1 = 135, /* 140 - NXT naming */
    PKTOUT_LC245_2_OPPLAYER2 = 165, /* 141 - NXT naming */
    PKTOUT_LC245_2_OPPLAYER3 = 172, /* 142 - NXT naming */
    PKTOUT_LC245_2_OPPLAYER4 = 54,  /* 143 - NXT naming */
    PKTOUT_LC245_2_OPPLAYERT = 52,  /* 148 - NXT naming */
    PKTOUT_LC245_2_OPPLAYERU = 210, /* 149 - NXT naming */

    PKTOUT_LC245_2_OPHELD1 = 104, /* 160 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELD2 = 193, /* 161 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELD3 = 115, /* 162 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELD4 = 194, /* 163 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELD5 = 9,   /* 164 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELDT = 188, /* 168 - name based on runescript trigger */
    PKTOUT_LC245_2_OPHELDU = 126, /* 169 - name based on runescript trigger */

    PKTOUT_LC245_2_INV_BUTTON1 = 13, /* 190 - NXT has "IF_BUTTON1" but for our interface system */
    PKTOUT_LC245_2_INV_BUTTON2 =
        58, /* 191 - NXT has "IF_BUTTON2" but for our interface system, this makes more sense */
    PKTOUT_LC245_2_INV_BUTTON3 =
        48, /* 192 - NXT has "IF_BUTTON3" but for our interface system, this makes more sense */
    PKTOUT_LC245_2_INV_BUTTON4 = 183, /* 193 - NXT has "IF_BUTTON4" but for our interface system */
    PKTOUT_LC245_2_INV_BUTTON5 = 242, /* 194 - NXT has "IF_BUTTON5" but for our interface system */

    PKTOUT_LC245_2_IF_BUTTON = 177,            /* 200 - NXT naming */
    PKTOUT_LC245_2_RESUME_PAUSEBUTTON = 239,   /* 201 - NXT naming */
    PKTOUT_LC245_2_CLOSE_MODAL = 245,          /* 202 - NXT naming */
    PKTOUT_LC245_2_RESUME_P_COUNTDIALOG = 241, /* 203 - NXT naming */
    PKTOUT_LC245_2_TUTORIAL_CLICKSIDE = 243,   /* 204 */

    PKTOUT_LC245_2_MOVE_OPCLICK =
        216, /* 242 - comes with OP packets, name based on other MOVE packets */
    PKTOUT_LC245_2_REPORT_ABUSE = 205,      /* 243 */
    PKTOUT_LC245_2_MOVE_MINIMAPCLICK = 198, /* 244 - NXT naming */
    PKTOUT_LC245_2_INV_BUTTOND =
        7, /* 245 - NXT has "IF_BUTTOND" but for our interface system, this makes more sense */
    PKTOUT_LC245_2_IGNORELIST_DEL = 4,    /* 246 - NXT naming */
    PKTOUT_LC245_2_IGNORELIST_ADD = 20,   /* 247 - NXT naming */
    PKTOUT_LC245_2_IF_PLAYERDESIGN = 150, /* 248 */
    PKTOUT_LC245_2_CHAT_SETMODE = 8,      /* 249 - NXT naming */
    PKTOUT_LC245_2_MESSAGE_PRIVATE = 99,  /* 250 - NXT naming */
    PKTOUT_LC245_2_FRIENDLIST_DEL = 61,   /* 251 - NXT naming */
    PKTOUT_LC245_2_FRIENDLIST_ADD = 116,  /* 252 - NXT naming */
    PKTOUT_LC245_2_CLIENT_CHEAT = 11,     /* 253 - NXT naming */
    PKTOUT_LC245_2_MESSAGE_PUBLIC = 78,   /* 254 - NXT naming */
    PKTOUT_LC245_2_MOVE_GAMECLICK = 182,  /* 255 - NXT naming */
};

#endif /* PACKETOUT_LC245_2_H */
