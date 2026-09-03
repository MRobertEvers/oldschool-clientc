#ifndef SRC_INPUT_TORIRS_KEYMAP_H
#define SRC_INPUT_TORIRS_KEYMAP_H

/*
 * OSRS internal key codes, and the Java KeyEvent.VK_* / DOM keyCode values they
 * are indexed by.
 *
 * CS2 scripts speak OSRS internal codes: an onKey handler receiving 84 means
 * Enter, 85 means Backspace, and op-key bindings (CC/IF_SETOPKEY) are expressed
 * the same way. That is a script ABI, deliberately kept separate from
 * enum LibToriRS_KeyCode, which is the platform-neutral edge/held boolean API.
 *
 * The translation is two-step -- native keysym -> VK -> OSRS -- because the
 * reference table is indexed by DOM keyCode, which matches Java's VK_* values.
 * Keeping that indexing lets the table be diffed line-for-line against
 * xrsps-typescript src/client/InputManager.ts OSRS_KEY_MAP.
 */

/* Java KeyEvent.VK_* / DOM keyCode values named by callers. */
#define TORIRS_VK_BACKSPACE 8
#define TORIRS_VK_TAB 9
#define TORIRS_VK_ENTER 13
#define TORIRS_VK_SHIFT 16
#define TORIRS_VK_CTRL 17
#define TORIRS_VK_ALT 18
#define TORIRS_VK_ESCAPE 27
#define TORIRS_VK_SPACE 32
#define TORIRS_VK_DELETE 127

/* OSRS internal key codes named by callers (see the table for the rest). */
#define TORIRS_OSRSKEY_ESCAPE 13
#define TORIRS_OSRSKEY_TAB 80
#define TORIRS_OSRSKEY_SHIFT 81
#define TORIRS_OSRSKEY_CTRL 82
#define TORIRS_OSRSKEY_SPACE 83
#define TORIRS_OSRSKEY_ENTER 84
#define TORIRS_OSRSKEY_BACKSPACE 85
#define TORIRS_OSRSKEY_ALT 86
#define TORIRS_OSRSKEY_DELETE 101
#define TORIRS_OSRSKEY_LEFT 96
#define TORIRS_OSRSKEY_RIGHT 97

/** Number of distinct OSRS key codes; sizes held/pressed state arrays. */
#define TORIRS_OSRSKEY_COUNT 256

/** OSRS internal key code for a VK code, or -1 when unmapped. */
int
LibToriRS_OsrsKeyFromVk(int vk);

/**
 * OSRS internal key code for a configuration key NAME, or -1 when unknown.
 *
 * The spelling revconfig uses in a [hotkey:<name>] section header: "f1".."f12",
 * "0".."9", "a".."z", and "escape" / "tab" / "space" / "enter" / "backspace" /
 * "delete" / "left" / "right" / "up" / "down" / "pageup" / "pagedown" /
 * "home" / "end". Case-insensitive.
 *
 * Resolves name -> VK -> OSRS through the one table above rather than carrying
 * a second mapping. OSRS codes are also what LibToriRS_Input.osrs_key_pressed
 * is indexed by, and that array (unlike enum LibToriRS_KeyCode) already covers
 * the F-keys -- see the note on sdl_keycode_to_vk about not growing
 * TORIRSK_COUNT.
 */
int
LibToriRS_OsrsKeyFromName(char const* name);

#endif
