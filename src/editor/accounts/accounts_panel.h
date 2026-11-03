#ifndef SRC_EDITOR_ACCOUNTS_ACCOUNTS_PANEL_H
#define SRC_EDITOR_ACCOUNTS_ACCOUNTS_PANEL_H

/*
 * The Accounts panel (module "accounts", View > Accounts; docs/MAPED_STATUS.md
 * "Accounts"): the lane's accounts and, per account, its position, stats,
 * vars, quests and items -- edited through the editor server, which applies
 * each edit online (a cheat on the attached lane) or offline (the save file)
 * and says which (torirsmaped/torirs_maped_accounts.h). Every edit applies
 * at once; there is no Apply / Revert.
 *
 *   Account        the dropdown of the lane's saves; the status row says
 *                  "online at x,z level n: edits are cheats..." or "offline:
 *                  edits write the save". Refresh (online: a server
 *                  checkpoint), Use for RUN / Play, Duplicate, Delete; Name +
 *                  Password (+ Fixture) + New account.
 *   Position       x z level + Move; To selection (the Select tool's tile or
 *                  loc); Place player tool (arms EDITOR_TOOL_PLACE_PLAYER:
 *                  the next world click puts the lane's PLAY account there).
 *   Stats          every skill with level and xp; Set level, Set XP, Max
 *                  all, Reset all.
 *   Vars           Find (a varp / varbit by name or id through the catalog;
 *                  settled typing), the matches, the save's varps with their
 *                  names, the selected var's detail (a varbit's base varp and
 *                  bits), Value + Set.
 *   Quests         every quest row with [x] complete / [ ] not / [?] unknown
 *                  (a proc arm); Complete (::complete, quest points paid),
 *                  Mark complete (::questmark), Mark all complete. Offline
 *                  these are queued for the account's next login; the queue
 *                  is listed.
 *   Items          Backpack / Worn / Bank slots; Slot + Obj (id or name, or
 *                  From Catalog pick) + Count; Set, Remove, Clear backpack.
 *                  Online a Set is ::item (first free backpack slot; worn is
 *                  then ::wield); the bank and Remove are offline only.
 *   Log            the server's answers (which path each edit took, the
 *                  refusals), and the Live log's lines while an online cheat
 *                  is answered.
 *
 * TORIRS_EDITOR_ACCOUNTS=show[,tab=<n>][,account=<name>] (photographs): docks
 * the panel over the column under Terrain once the layout ran, opens tab <n>
 * (0 Position .. 4 Items) and selects <account>.
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
 * account is online and as the save's position when it is not; the answer
 * is logged ("accounts: ... online: ::tele ..." / "offline: wrote ..."). 1
 * when sent; 0 with a console line when this window has no Accounts panel,
 * the server has no lane, or the play account is not known yet.
 */
int
Editor_Accounts_PlacePlayer(
    struct EditorUi* ui,
    int x,
    int z,
    int level);

#endif
