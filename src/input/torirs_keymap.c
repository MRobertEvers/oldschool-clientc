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

/* Named VK codes the config spelling maps onto; everything else is derived
 * (letters, digits, F-keys are contiguous VK ranges). */
static struct
{
    char const* name;
    int vk;
} const s_named_vk[] = {
    { "escape", TORIRS_VK_ESCAPE },   { "esc", TORIRS_VK_ESCAPE },
    { "tab", TORIRS_VK_TAB },         { "space", TORIRS_VK_SPACE },
    { "enter", TORIRS_VK_ENTER },     { "return", TORIRS_VK_ENTER },
    { "backspace", TORIRS_VK_BACKSPACE }, { "delete", TORIRS_VK_DELETE },
    /* Modifiers: not bindable as revconfig hotkeys, but nameable so a headless
     * run can hold one — shift is the key the scripted inventory listens for
     * (shift-click drop). */
    { "shift", TORIRS_VK_SHIFT },     { "ctrl", TORIRS_VK_CTRL },
    { "alt", TORIRS_VK_ALT },
    { "pageup", 33 },                 { "pagedown", 34 },
    { "end", 35 },                    { "home", 36 },
    { "left", 37 },                   { "up", 38 },
    { "right", 39 },                  { "down", 40 },
};

static int
name_equals_ci(
    char const* name,
    char const* other)
{
    for( ;; name++, other++ )
    {
        int lhs = *name;
        int rhs = *other;
        if( lhs >= 'A' && lhs <= 'Z' )
            lhs += 'a' - 'A';
        if( lhs != rhs )
            return 0;
        if( lhs == '\0' )
            return 1;
    }
}

int
LibToriRS_OsrsKeyFromName(char const* name)
{
    if( !name || name[0] == '\0' )
        return -1;

    /* Single character: a letter or a digit. */
    if( name[1] == '\0' )
    {
        int ch = name[0];
        if( ch >= 'A' && ch <= 'Z' )
            ch += 'a' - 'A';
        if( ch >= 'a' && ch <= 'z' )
            return LibToriRS_OsrsKeyFromVk(65 + (ch - 'a')); /* VK_A .. VK_Z */
        if( ch >= '0' && ch <= '9' )
            return LibToriRS_OsrsKeyFromVk(48 + (ch - '0')); /* VK_0 .. VK_9 */
        return -1;
    }

    if( (name[0] == 'f' || name[0] == 'F') && name[1] >= '0' && name[1] <= '9' )
    {
        int number = name[1] - '0';
        int rest = 2;
        while( name[rest] >= '0' && name[rest] <= '9' )
            number = number * 10 + (name[rest++] - '0');
        if( name[rest] == '\0' && number >= 1 && number <= 12 )
            return LibToriRS_OsrsKeyFromVk(112 + (number - 1)); /* VK_F1 .. VK_F12 */
    }

    for( unsigned i = 0; i < sizeof(s_named_vk) / sizeof(s_named_vk[0]); i++ )
    {
        if( name_equals_ci(name, s_named_vk[i].name) )
            return LibToriRS_OsrsKeyFromVk(s_named_vk[i].vk);
    }
    return -1;
}
