#ifndef SRC_EDITOR_ACCOUNTS_ACCOUNTS_PANEL_H
#define SRC_EDITOR_ACCOUNTS_ACCOUNTS_PANEL_H

/*
 * The Accounts panel (module "accounts", View > Accounts; docs/MAPED_STATUS.md
 * "Accounts"): the lane's accounts and, per account, everything a save holds
 * -- edited through the editor server, which applies each edit online (a
 * cheat on the attached lane) or offline (the save file) and says which
 * (torirsmaped/torirs_maped_accounts.h). Every edit applies at once; there is
 * no Apply / Revert.
 *
 *   (above tabs)   the dropdown of the lane's saves and one status line
 *                  ("online at x,z level n: edits are cheats..." / "offline:
 *                  edits write the save"). Nothing else repeats on the tabs.
 *   Account        the lane line; Refresh (online: a server checkpoint), Use
 *                  for RUN / Play, Duplicate, Delete; the position: x z level
 *                  + Move, To selection, Place player tool (arms
 *                  EDITOR_TOOL_PLACE_PLAYER: the next world click puts the
 *                  lane's PLAY account there, which stands on the hovered
 *                  tile in its own body and gear until then); Name +
 *                  Password (+ Fixture) + New account.
 *   Items          the account builder. Container: Equipment (the game's
 *                  worn layout: slot plates, the empty slots' silhouettes,
 *                  the links), Backpack (4 x 7), Bank and every
 *                  `[container.<inv>]` the save holds (paged 8 x 7 grids
 *                  sized from configs/all.inv), or any inv opened by name
 *                  (Inv + Open inv; the save gets it on the first Set). The
 *                  items draw as their own icons (EditorUiAppServices.obj_icon;
 *                  their ids without one). A click selects a slot and fills
 *                  Slot / Obj / Count; Obj takes an id, a pack name or From
 *                  Catalog pick; Set, Remove, Clear container. Online a Set
 *                  is ::item (worn: then ::wield); the bank, Remove and other
 *                  containers are offline only.
 *   Loadouts       the lane's named loadouts (worn items + backpack): the
 *                  list, the selected one's gear drawn, Name; Save current
 *                  gear (from the shown account), Apply to account, Delete
 *                  loadout.
 *   Stats          every skill with level and xp; Set level, Set XP, Max
 *                  all, Reset all.
 *   Vars           Find (a varp / varbit by name or id through the catalog;
 *                  settled typing), the matches, the save's varps with their
 *                  names, the selected var's detail (a varbit's base varp and
 *                  bits), Value + Set.
 *   Quests         the counts, Find + Show (all / not complete / complete /
 *                  unknown), every quest row with [x] / [ ] / [?] and its
 *                  progress, the selected row's var and value; Complete
 *                  (::complete, quest points paid), Mark complete
 *                  (::questmark), Reset (its var to 0), Complete shown /
 *                  Mark shown (every shown row not complete yet),
 *                  Mark all complete. Offline the completions are queued for
 *                  the account's next login; the queue is listed.
 *   Log            the server's answers (which path each edit took, the
 *                  refusals), and the Live log's lines while an online cheat
 *                  is answered.
 *
 * TORIRS_EDITOR_ACCOUNTS=show[,tab=<n>][,account=<name>][,container=<n>][,loadout=<name>]
 * (photographs): docks the panel over the dock column once the layout ran,
 * opens tab <n> (0 Account, 1 Items, 2 Loadouts, 3 Stats, 4 Vars, 5 Quests,
 * 6 Log), selects <account> and shows Items container <n> (0 Equipment,
 * 1 Backpack, 2 Bank, 3.. the save's others) and selects loadout <name>.
 *
 * The panel never reads a socket: it talks through an
 * Editor_AccountsTransport (accounts_client.h), bound to the editor host by
 * Editor_Accounts_Register; a test calls Editor_Accounts_RegisterWith.
 */

#include "editor/accounts/accounts_client.h"
#include "editor/editor_ui.h"

/** Register the panel over the window's editor host connection. Needs
 *  ui->editor with an open host; without one it registers nothing. */
void
Editor_Accounts_Register(struct EditorUi* ui);

/** Register over `transport` (copied). */
void
Editor_Accounts_RegisterWith(
    struct EditorUi* ui,
    const struct Editor_AccountsTransport* transport);

/** The panel's model (NULL when the panel is not registered). */
const struct Editor_AccountsModel*
Editor_AccountsPanelModel(struct EditorUi* ui);

/** The account the panel shows ("" none). */
const char*
Editor_Accounts_Current(struct EditorUi* ui);

/** Show `name` (asks the server for its view). 0 without the panel. */
int
Editor_Accounts_Show(
    struct EditorUi* ui,
    const char* name);

/**
 * The Place player tool's click: put the lane's PLAY account on absolute tile
 * (x, z, level) -- ACCOUNT_PLACE, which the server applies as ::tele when the
 * account is online and as the save's position when it is not (an account
 * that has never logged in gets its first save from the lane's fixture);
 * the answer is logged ("accounts: ... online: ::tele ..." / "offline:
 * wrote ..."). 1
 * when sent; 0 with a console line when this window has no Accounts panel,
 * the server has no lane, or the play account is not known yet.
 */
/**
 * The PLAY account's look (its body, colours and worn items) for Place
 * player's ghost: 1 with `*out` and the look's serial (it moves whenever the
 * look does) once known; 0 until then, having asked the server for the play
 * account's view (again after a while). 0 without the panel.
 */
int
Editor_Accounts_PlayLook(
    struct EditorUi* ui,
    struct Editor_AccountLook* out,
    int* out_serial);

/**
 * Where the PLAY account stands, absolute, from the server's account list: 1
 * with the tile once it is known and has ever had one. Asks for a fresh list
 * every few seconds while called, so an online account's walking shows. 0
 * without the panel.
 */
int
Editor_Accounts_PlayPosition(
    struct EditorUi* ui,
    int* out_x,
    int* out_z,
    int* out_level);

/** Bring the Accounts panel up on the play account. */
void
Editor_Accounts_RevealPlay(struct EditorUi* ui);

int
Editor_Accounts_PlacePlayer(
    struct EditorUi* ui,
    int x,
    int z,
    int level);

#endif
