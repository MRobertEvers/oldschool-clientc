#include "torirs_keymap.h"

/*
 * Index = VK / DOM keyCode, value = OSRS internal code (-1 = unmapped).
 * Ported row-for-row from xrsps-typescript src/client/InputManager.ts
 * OSRS_KEY_MAP so the two stay diffable; do not reflow the rows.
 */
/* clang-format off */
static int const osrs_key_map[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, 85, 80, 84, -1, 91, 84, -1, -1, /* 0-15   (8=backspace->85, 9=tab->80, 10=enter->84, 13=enter->84) */
    81, 82, 86, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, /* 16-31  (16=shift->81, 17=ctrl->82, 18=alt->86, 27=escape->13) */
    83, 104, 105, 103, 102, 96, 98, 97, 99, -1, -1, -1, -1, -1, -1, -1, /* 32-47  (32=space->83, 33=pgup->104, 34=pgdn->105, 35=end->103, 36=home->102, 37=left->96, 38=up->98, 39=right->97, 40=down->99) */
    25, 16, 17, 18, 19, 20, 21, 22, 23, 24, -1, -1, -1, -1, -1, -1, /* 48-63  (48-57 = digits 0-9 -> 25, 16-24) */
    -1, 48, 68, 66, 50, 34, 51, 52, 53, 39, 54, 55, 56, 70, 69, 40, /* 64-79  (65-90 = A-Z letters) */
    41, 32, 35, 49, 36, 38, 67, 33, 65, 37, 64, -1, -1, -1, -1, -1, /* 80-95 */
    228, 231, 227, 233, 224, 219, 225, 230, 226, 232, 89, 87, -1, 88, 229, 90, /* 96-111 (numpad) */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, -1, -1, -1, 101,         /* 112-127 (F1-F12 -> 1-12, 127=delete->101) */
};
/* clang-format on */

int
LibToriRS_OsrsKeyFromVk(int vk)
{
    if( vk < 0 || vk >= (int)(sizeof(osrs_key_map) / sizeof(osrs_key_map[0])) )
        return -1;
    return osrs_key_map[vk];
}
