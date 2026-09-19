#ifndef TORIRS_SAILING_SETTINGS_H
#define TORIRS_SAILING_SETTINGS_H

/* Revision-239 native All Settings row470 / struct6372 / enum244. */
#define SAILING_CARGO_PRIVACY_VARBIT 19614
#define SAILING_CARGO_PRIVACY_SCRIPT 8830
#define SAILING_CARGO_PRIVACY_SETTING 470
#define SAILING_CARGO_PRIVACY_NAVIGATORS 0
#define SAILING_CARGO_PRIVACY_ALL 1
#define SAILING_CARGO_PRIVACY_NONE 2

/*
 * The dropdown ENTRY script, which is the one the shipped cache actually runs
 * when a choice is clicked: `setting_dropdown_entry_op`, script3852.
 *
 * Its body is
 *
 *     if (cc_find($component3, $int1) = ^true) {
 *         cc_settext($text0);
 *         if ($int12 = 0) { ...~settings_set_dropdown / ~settings_set_keybind... }
 *     }
 *
 * so a row whose struct carries `param1085=1` -- "the SERVER applies this one"
 * -- gets its label rewritten and nothing else. Cargo privacy is such a row,
 * and no server in this revision arms it, so the label moved while the varbit
 * stayed where it was. The client has to finish this one row itself.
 *
 * The frame's integer locals, in declaration order, are the identification.
 * `$component3..$component8`, `$comsubid11` and `$struct13` are all int-typed
 * in the VM, so the indices below are simply the parameter positions:
 *
 *   [0]  $int0        entry kind:2 is a dropdown (anything else is a keybind)
 *   [2]  $int2        the chosen value -- what has to be applied
 *   [10] $int10       the All Settings row id (470 for cargo privacy)
 *   [12] $int12       non-zero = "server applies", the branch that skips3967
 *   [13] $struct13    the row's settings struct (6372 for cargo privacy)
 *
 * All five are checked, not just the row id: this must claim exactly the cargo
 * row of exactly this script and nothing else. `$int12 == 0` is deliberately
 * NOT claimed -- that is the branch which does call `~settings_set_dropdown`
 * (3967), whose apply hub the existing mirror path already handles.
 */
#define SAILING_CARGO_PRIVACY_DROPDOWN_SCRIPT 3852
#define SAILING_CARGO_PRIVACY_STRUCT 6372
#define SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT 14
#define SAILING_CARGO_PRIVACY_DROPDOWN_KIND 2
#define SAILING_CARGO_PRIVACY_LOCAL_KIND 0
#define SAILING_CARGO_PRIVACY_LOCAL_CHOICE 2
#define SAILING_CARGO_PRIVACY_LOCAL_SETTING 10
#define SAILING_CARGO_PRIVACY_LOCAL_SERVER_APPLIED 12
#define SAILING_CARGO_PRIVACY_LOCAL_STRUCT 13

static inline int
SailingCargoPrivacy_Valid(int value)
{
    return value >= SAILING_CARGO_PRIVACY_NAVIGATORS && value <= SAILING_CARGO_PRIVACY_NONE;
}

#endif
