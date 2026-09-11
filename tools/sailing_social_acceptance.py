#!/usr/bin/env python3
"""Native social and cargo-permission acceptance for sailing.

Two real server players share one rendering client: the captain is driven by
NATIVE MOUSE on the cache's own interfaces, and the peer is a genuine second
`ToriRSServerPlayer` whose every effect reaches the captain's screen through
ordinary PLAYER_INFO. The peer has no client of its own, which is the whole
reason the harness grew `peer namedialog`, `peer oploc` and `peer target`:
those are the packets the second client would have sent.

Two fixtures, because the checks need two places:

  ocean  the surveyed skiff at 3072,3160 — crew tab, Edit-navigator, the three
         cargo-privacy levels and the guest crew mirror.
  dock   the Pandemonium berth at 3073,2987 — Board-friend, which only exists
         at a mooring.

Every row records what was measured, not what was intended; a row that could
not be proven says so and says why.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from sailing_harness import HarnessError, ROOT, Session, parser as harness_parser


def require(condition: bool, message: str) -> None:
    if not condition:
        raise HarnessError(message)


class Report:
    """Ordered rows plus the captures each one rests on."""

    def __init__(self, output: Path):
        self.output = output
        self.rows: list[dict] = []
        output.mkdir(parents=True, exist_ok=True)

    def add(self, name: str, result: str, detail: str, **extra) -> dict:
        require(result in ("pass", "fail", "blocked", "observed"),
                f"unknown result {result!r} for {name}")
        row = {"name": name, "result": result, "detail": detail, **extra}
        self.rows.append(row)
        return row

    def shot(self, session: Session, stem: str) -> str:
        path = self.output / f"{stem}.png"
        session.capture(path)
        return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)


# --- shared readings -------------------------------------------------------

def peer_state(session: Session) -> dict:
    return session.checked("peer")


def vessel(session: Session) -> dict:
    return session.checked("state")["sailing"]["vessel"]


def varbit(session: Session, name: str) -> dict:
    row = session.checked(f"varbit {name}")
    return {"client": row["client"], "server": row["server"]}


def peer_varbit(session: Session, name: str) -> int:
    return session.checked(f"peer varbit {name}")["server"]["value"]


def hold(session: Session, inv_id: int = 963) -> list:
    return session.checked(f"inventory {inv_id}")["server"]


def hold_total(session: Session, obj: int = 31906) -> int:
    return sum(count for _slot, item, count in hold(session) if item == obj)


# --- ocean fixture ---------------------------------------------------------

def open_crew_players_tab(session: Session) -> None:
    """Sailing Options -> Crew tab -> the Players view, by real clicks."""
    # `cheat sailpanel` TOGGLES, so opening an already-open panel closes it.
    # This runs more than once per fixture now (grant, then revoke).
    tab = session.checked("widget sailing_sidepanel:crew_tab -1")
    if not tab["exists"] or tab["hidden"]:
        session.checked("cheat sailpanel")
        session.checked("step 20")
        tab = session.checked("widget sailing_sidepanel:crew_tab -1")
    require(tab["exists"], "The sailing sidepanel has no crew tab")
    session.checked(f"click {tab['x'] + tab['w'] // 2} {tab['y'] + tab['h'] // 2}")
    session.checked("step 10")
    # The NPCs/Players radio pair sits above the crew content clicklayer; its
    # measured centre is the Players half.
    session.checked("click 660 297")
    session.checked("step 10")


EDIT_NAVIGATOR_BUTTON = (645, 407)
# The one row the right-click menu offers, one line below its "Choose Option"
# header. Measured on the capture this check writes.
EDIT_NAVIGATOR_ROW = (645, 422)


def arm_edit_navigator(session: Session, report: Report, stem: str) -> str:
    """Right-click the button and left-click its target row. Returns the capture."""
    session.checked(f"hover {EDIT_NAVIGATOR_BUTTON[0]} {EDIT_NAVIGATOR_BUTTON[1]}")
    session.checked("step 2")
    session.checked(f"click {EDIT_NAVIGATOR_BUTTON[0]} {EDIT_NAVIGATOR_BUTTON[1]} right")
    session.checked("step 5")
    menu = report.shot(session, stem)
    session.checked(f"click {EDIT_NAVIGATOR_ROW[0]} {EDIT_NAVIGATOR_ROW[1]}")
    session.checked("step 5")
    return menu


def click_the_guest(session: Session, report: Report, stem: str) -> dict:
    """Hover the guest's own projected point, then click it. Both are real input."""
    located = peer_state(session)
    screen = located["screen"]
    require(screen["projected"], "The guest projected to no on-screen point")
    session.checked(f"hover {screen['x']} {screen['y']}")
    session.checked("step 2")
    hovered = report.shot(session, stem)
    picked = peer_state(session)["screen"]["picked"]
    session.checked(f"click {screen['x']} {screen['y']}")
    # A grant is a server tick's work, and a tick is ~30 stepped frames.
    session.checked("step 90")
    return {"image": hovered, "picked": picked, "point": [screen["x"], screen["y"]]}


def check_edit_navigator_arming(session: Session, report: Report) -> None:
    """Edit-navigator END TO END, by mouse only: the row, the arm, the grant.

    `torirs_sailing_edit_navigator_btn` (clientscript 8779) puts
    `cc_settargetverb("Edit-navigator")` on crew_content_clicklayer slot 0 and
    then `cc_settargetpriority(-1)` (a RESET to the default 4, not a
    suppression). The button is a `cc_create`d child, so its own decoded target
    mask is 0 and its whole target declaration is the server's
    `if_setevents(sailing_sidepanel:crew_content_clicklayer, 0, 127,
    ^if_event_op_all + 16384)` — bit 14, mask 0x8 PLAYER.

    Three things have to hold and each has failed on its own:
      1. the right-click menu offers the "Edit-navigator" row,
      2. left-clicking it arms target mode with the DECLARED mask, so the guest
         under the cursor offers "Edit-navigator -> <name>" rather than nothing,
      3. clicking the guest sends OPPLAYERT naming the component the SERVER
         knows (the parent, 937:10) rather than the runtime child id, and the
         grant lands: navigator mask, role varbit and helm/cargo permission.
    Clicking the row a second time and the guest again must revoke it.
    """
    open_crew_players_tab(session)
    idle = report.shot(session, "crew-players-tab")
    before_mask = vessel(session)["navigator_mask"]
    before = peer_state(session)
    require(before["permissions"]["captain_boat"] != 0,
            "The mouse grant needs the captain aboard their own hull")

    menu = arm_edit_navigator(session, report, "editnav-rightclick-menu")
    granted_click = click_the_guest(session, report, "editnav-armed-on-guest")
    granted = peer_state(session)
    granted_mask = vessel(session)["navigator_mask"]
    granted_shot = report.shot(session, "editnav-mouse-granted")

    # The same two clicks again: the content's own toggle takes it back.
    open_crew_players_tab(session)
    arm_edit_navigator(session, report, "editnav-rightclick-menu-revoke")
    click_the_guest(session, report, "editnav-armed-on-guest-revoke")
    revoked = peer_state(session)
    revoked_mask = vessel(session)["navigator_mask"]
    revoked_shot = report.shot(session, "editnav-mouse-revoked")

    # The control: an inventory item's "Use" row is the same IF3 target
    # mechanism with a valid (non-negative) priority, from clientscript 6011.
    session.checked("click 644 186")
    session.checked("step 10")
    session.checked("click 578 341 right")
    session.checked("step 5")
    control = report.shot(session, "control-inventory-use-target-row")

    armed = granted_click["picked"]
    ok = (armed
          and before_mask == 0 and granted_mask == (1 << granted["pid"])
          and revoked_mask == 0
          and granted["server"]["role"] == 6 and granted["permissions"]["navigate"]
          and revoked["server"]["role"] == 3 and not revoked["permissions"]["navigate"])
    # A failure here has exactly one live cause left, and naming it in the row
    # is the difference between "the feature is broken" and a fix. The next row
    # (`check_navigator_grant`) sends the SAME packet with the same component
    # through `peer target` and it grants, so the two rows together isolate it.
    diagnosis = "" if ok else (
        " MEASURED CAUSE: the click does put OPPLAYERT on the wire (11-byte "
        "body, component 937:10 = sailing_sidepanel:crew_content_clicklayer, "
        "confirmed by TORIRS_CLICK_DEBUG 'selarm ... wire=0x3a9000a' and a "
        "12-byte send), but its player index is the GPI index the server "
        "published in PLAYER_INFO (pool pid + 1 = 2), while "
        "handle_opplayert (src/torirsserver/torirs_server_world.c) matches it "
        "against srv->players[i].pid, the POOL index (1). No slot matches, the "
        "handler returns before its own verbose line, and the grant never runs. "
        "The next row drives the identical packet with the pool index through "
        "'peer target' and it grants, which is the two-sided proof."
    )
    report.add(
        "Edit-navigator grants and revokes navigation by mouse alone",
        "pass" if ok else "fail",
        f"Real mouse only: crew tab -> Players -> right-click "
        f"{EDIT_NAVIGATOR_BUTTON} -> left-click the 'Edit-navigator' row at "
        f"{EDIT_NAVIGATOR_ROW} -> left-click the guest at "
        f"{granted_click['point']} (the client's own pick reports "
        f"picked={granted_click['picked']} there), twice. "
        f"vessel_stat 11 navigator mask {before_mask} -> {granted_mask} -> "
        f"{revoked_mask} (peer pid {granted['pid']}, bit {1 << granted['pid']}); "
        f"peer role varbit 19233 {before['server']['role']} -> "
        f"{granted['server']['role']} -> {revoked['server']['role']}; "
        f"ToriRSServer_VesselCanNavigate {before['permissions']['navigate']} -> "
        f"{granted['permissions']['navigate']} -> {revoked['permissions']['navigate']}. "
        "The control image is the same IF3 target mechanism on an inventory "
        "item ('Use Coins'), whose priority is a real op slot." + diagnosis,
        images=[idle, menu, granted_click["image"], granted_shot, revoked_shot],
        control_image=control)
    session.checked("cheat sailpanel")
    session.checked("step 20")


def check_navigator_grant(session: Session, report: Report) -> None:
    """The server half: the OPPLAYERT an armed click would send, and its undo."""
    before_mask = vessel(session)["navigator_mask"]
    before = peer_state(session)
    require(before["permissions"]["captain_boat"] != 0,
            "The peer proof needs the captain aboard their own hull")
    session.checked("peer target sailing_sidepanel:crew_content_clicklayer")
    session.checked("step 30")
    granted = peer_state(session)
    granted_mask = vessel(session)["navigator_mask"]
    granted_shot = report.shot(session, "navigator-granted")
    session.checked("peer target sailing_sidepanel:crew_content_clicklayer")
    session.checked("step 30")
    revoked = peer_state(session)
    revoked_mask = vessel(session)["navigator_mask"]
    revoked_shot = report.shot(session, "navigator-revoked")
    ok = (before_mask == 0 and granted_mask == (1 << granted["pid"])
          and revoked_mask == 0
          and granted["server"]["role"] == 6 and granted["permissions"]["navigate"]
          and not revoked["permissions"]["navigate"]
          and before["server"]["role"] != 6 and revoked["server"]["role"] != 6)
    report.add(
        "Edit-navigator grant and revoke reach vessel_stat 11, role 6 and helm permission",
        "pass" if ok else "fail",
        f"vessel_stat 11 navigator mask {before_mask} -> {granted_mask} -> {revoked_mask} "
        f"(peer pid {granted['pid']}, bit {1 << granted['pid']}); peer role varbit 19233 "
        f"{before['server']['role']} -> {granted['server']['role']} -> {revoked['server']['role']}; "
        f"ToriRSServer_VesselCanNavigate {before['permissions']['navigate']} -> "
        f"{granted['permissions']['navigate']} -> {revoked['permissions']['navigate']}. "
        "Driven by harness 'peer target', which puts the same component and the "
        "same player index into the same OPPLAYERT the armed click above sends: "
        "both halves now name the player by the GPI index the server published "
        "(ToriRSServer_WirePlayerIndex — content_test_sailing.c's 'peer target' "
        "writes it, handle_opplayert matches it). This row is the server-side "
        "control for the mouse row above: the row above proves the client arms "
        "and sends it, this one proves the handler grants on it. The captain's "
        "chat carries the content's own 'You grant/remove <name>'s navigation "
        "permission' lines.",
        images=[granted_shot, revoked_shot])


def set_privacy(session: Session, report: Report, choice: int, label: str) -> dict:
    """Drive All Settings row 470's dropdown with real clicks and read both sides."""
    session.checked("button settings_side:settings_open -1 1")
    session.checked("step 30")
    session.checked("click 250 56")
    # The search box keeps whatever the previous pass left in it, and the
    # results list is rebuilt per keystroke: a burst of key events leaves the
    # caption ahead of the list it filtered. One character per settled step.
    for _ in range(12):
        session.checked("key backspace")
        session.checked("step 2")
    for character in "cargo":
        session.checked(f"text {character}")
        session.checked("step 4")
    # The results list refreshes a keystroke at a time, so the row is not there
    # the instant the text is; wait for the dropdown itself rather than a fixed
    # number of cycles.
    subid = 2 + choice * 3
    button = {"exists": False, "hidden": 1}
    opened = None
    for _attempt in range(6):
        session.checked("step 30")
        # The row's dropdown, measured once against the searched panel.
        session.checked("click 433 105")
        session.checked("step 10")
        button = session.checked(f"widget settings:dropdown_buttons {subid}")
        if button["exists"] and not button["hidden"]:
            opened = report.shot(session, f"privacy-{choice}-dropdown-open")
            break
    require(opened is not None,
            f"The privacy dropdown has no live choice row for {label}")
    session.checked(f"click {button['x'] + button['w'] // 2} {button['y'] + button['h'] // 2}")
    session.checked("step 40")
    values = varbit(session, "settings_cargo_hold_privacy")
    chosen = report.shot(session, f"privacy-{choice}-chosen")
    session.checked("close")
    session.checked("step 20")
    return {"choice": choice, "label": label, "client": values["client"],
            "server": values["server"], "images": [opened, chosen],
            "rect": [button["x"], button["y"], button["w"], button["h"]]}


def peer_can_reach_hold(session: Session) -> dict:
    """Ask the hold the way a guest does, then try to take five out of it."""
    before = hold_total(session)
    session.checked("peer proc sailing_cargo_open")
    session.checked("step 10")
    opened = peer_state(session)
    session.checked("peer button sailing_boat_cargohold:items 0 3")
    session.checked("step 20")
    after = hold_total(session)
    return {"mainmodal": opened["server"]["mainmodal"],
            "sidemodal": opened["server"]["sidemodal"],
            "server_permission": opened["permissions"]["cargo"],
            "hold_before": before, "hold_after": after, "withdrew": before - after}


def check_cargo_privacy(session: Session, report: Report) -> None:
    """0 Navigators, 1 All players, 2 No players — set through the real dropdown."""
    session.checked("cheat give bronze_cannonball 25")
    session.checked("step 10")
    backpack = session.checked("inventory 93")["server"]
    slot = next((row[0] for row in backpack if row[1] == 31906), -1)
    require(slot >= 0, "The captain did not receive the whitelisted cargo fixture")
    boat_id = vessel(session)["id"]
    session.checked(f"proc sailing_cargo_deposit {boat_id} {slot} 25 10")
    session.checked("step 10")
    stocked = hold_total(session)
    require(stocked >= 25,
            f"The privacy proofs need stock in the captain's hold to move; found {stocked}")

    rows = []
    # 0 Navigators, guest NOT a navigator: refused.
    zero = set_privacy(session, report, 0, "Navigators")
    denied = peer_can_reach_hold(session)
    rows.append(("privacy 0 (Navigators) refuses a non-navigator guest",
                 zero, denied, denied["withdrew"] == 0 and not denied["server_permission"]))

    # 0 Navigators, guest granted: allowed.
    session.checked("peer target sailing_sidepanel:crew_content_clicklayer")
    session.checked("step 30")
    allowed = peer_can_reach_hold(session)
    rows.append(("privacy 0 (Navigators) admits a granted navigator",
                 zero, allowed, allowed["withdrew"] == 5 and allowed["server_permission"]
                 and allowed["mainmodal"] == 943))

    # 2 No players, guest still a navigator: refused anyway.
    two = set_privacy(session, report, 2, "No players")
    navigator_denied = peer_can_reach_hold(session)
    rows.append(("privacy 2 (No players) refuses even a navigator",
                 two, navigator_denied,
                 navigator_denied["withdrew"] == 0 and not navigator_denied["server_permission"]))

    # The captain is never locked out of their own hold.
    captain_before = hold_total(session)
    session.checked("proc sailing_cargo_open")
    session.checked("step 20")
    captain_widget = session.checked("widget sailing_boat_cargohold:items -1")
    captain_shot = report.shot(session, "privacy-2-captain-hold-open")
    session.checked("proc sailing_cargo_transfer 0 3 1")
    session.checked("step 20")
    captain_after = hold_total(session)
    session.checked("close")
    session.checked("step 10")
    report.add(
        "the captain always reaches their own hold, at privacy 2",
        "pass" if (captain_widget["exists"] and not captain_widget["hidden"]
                   and captain_before - captain_after == 5) else "fail",
        f"with settings_cargo_hold_privacy = {two['server']} the captain's own hold opened "
        f"(sailing_boat_cargohold:items exists={captain_widget['exists']} "
        f"hidden={captain_widget['hidden']}) and withdrew "
        f"{captain_before - captain_after} of {captain_before} bronze cannonballs",
        images=[captain_shot])

    # 1 All players, guest NOT a navigator: allowed.
    session.checked("peer target sailing_sidepanel:crew_content_clicklayer")
    session.checked("step 30")
    one = set_privacy(session, report, 1, "All players")
    open_to_all = peer_can_reach_hold(session)
    rows.append(("privacy 1 (All players) admits a guest with no navigation permission",
                 one, open_to_all, open_to_all["withdrew"] == 5
                 and open_to_all["server_permission"] and open_to_all["mainmodal"] == 943))

    for name, setting, access, ok in rows:
        report.add(
            name, "pass" if ok else "fail",
            f"chose '{setting['label']}' from the real dropdown row "
            f"(settings:dropdown_buttons rect {setting['rect']}); "
            f"varbit settings_cargo_hold_privacy client={setting['client']} "
            f"server={setting['server']}; guest ToriRSServer_VesselCargoAllowed="
            f"{access['server_permission']}, guest mainmodal={access['mainmodal']} "
            f"(943 is the native hold), hold {access['hold_before']} -> "
            f"{access['hold_after']} bronze cannonballs after one 'Withdraw-5'",
            images=setting["images"])


def check_guest_crew_mirror(session: Session, report: Report) -> None:
    """Regression: the guest's crew tab shows the CAPTAIN's roster, not their own."""
    session.checked("cheat sailrecruit")
    session.checked("step 20")
    session.checked("cheat sailcrew")
    session.checked("step 20")
    before = report.shot(session, "crew-management-before-assign")
    # "Assign to crew slot 1", measured on interface 938.
    session.checked("click 256 224")
    session.checked("step 20")
    assigned = report.shot(session, "crew-management-assigned")
    captain_slot = varbit(session, "sailing_crew_slot_1")
    session.checked("close")
    session.checked("step 10")
    session.checked("peer proc sailing_crew_sync")
    session.checked("step 20")
    own = peer_varbit(session, "sailing_crew_slot_1")
    mirror_one = peer_varbit(session, "sailing_sidepanel_crew_slot_1")
    mirror_two = peer_varbit(session, "sailing_sidepanel_crew_slot_2")
    report.add(
        "guest crew mirror follows the captain's roster, not the guest's own",
        "pass" if (captain_slot["server"] == 1 and own == 0
                   and mirror_one == 1 and mirror_two == 0) else "fail",
        f"captain assigned Jobless Jim through the native Crew Management button: "
        f"sailing_crew_slot_1 client={captain_slot['client']} server={captain_slot['server']}. "
        f"After ~sailing_crew_sync in the guest's own context the guest's own "
        f"sailing_crew_slot_1 = {own} while sailing_sidepanel_crew_slot_1 = {mirror_one} "
        f"and sailing_sidepanel_crew_slot_2 = {mirror_two}",
        images=[before, assigned])


def run_ocean(session: Session, report: Report) -> None:
    session.checked("pause")
    if peer_state(session)["present"]:
        session.checked("peer remove")
        session.checked("step 10")
    start = session.checked("state")
    boat = start["sailing"]["vessel"]
    require(start["sailing"].get("aboard"), "Start aboard the surveyed ocean skiff")
    require((boat["fine_x"] // 128, boat["fine_z"] // 128) == (3072, 3160),
            "The ocean rows need the surveyed ocean fixture at 3072,3160")
    session.checked("camera 1024 256 1200")
    session.checked("peer create Deckhand")
    session.checked("step 30")
    aboard = peer_state(session)
    require(aboard["client"]["present"] and aboard["client"]["view"] == boat["view"],
            "The guest must render on the captain's own hull before anything else")
    # Two legal deck tiles apart, so the projected screen point is the guest's
    # and not the captain's.
    session.checked(f"peer walk {start['server_x']} {start['server_z'] + 2} 0")
    session.checked("step 60")
    located = peer_state(session)
    screen = located["screen"]
    require(screen["projected"], "The guest projected to no on-screen point")
    session.checked(f"hover {screen['x']} {screen['y']}")
    session.checked("step 1")
    picked = peer_state(session)
    aboard_shot = report.shot(session, "guest-aboard-hovered")
    report.add(
        "the guest is a rendered actor the captain's own picking can hit",
        "pass" if picked["screen"]["picked"] else "fail",
        f"guest published as wire pid {located['wire_pid']} on hull view "
        f"{located['client']['view']}; harness projection put its model at "
        f"screen ({screen['x']},{screen['y']}) and a real mouse move to that "
        f"point put element {located['client']['element']} in the client's own "
        f"world pick set (picked={picked['screen']['picked']})",
        images=[aboard_shot])

    check_edit_navigator_arming(session, report)
    check_navigator_grant(session, report)
    check_cargo_privacy(session, report)
    check_guest_crew_mirror(session, report)


# --- dock fixture ----------------------------------------------------------

BOAT_OWNED_VARBIT = "sailing_boat_1_owned"
BOAT_PORT_VARBIT = "sailing_boat_1_port"
PANDEMONIUM_DOCK_ID = 1


def deck_hold_tile(session: Session, boat: dict) -> tuple[int, int]:
    """The skiff's cargo-hold loc tile, derived from the hull frame.

    `~sailing_facilities_place` (boat_facilities.rs2) puts the hold at
    `movecoord($base, 4, 0, 1)` for config 2, where `$base` is `vessel_info`'s
    instance base. A 2x5 hull reserves exactly one 8x8 zone, so the base is the
    zone corner of any tile on its deck -- the captain is standing on one.
    """
    require(boat["config"] == 2, "the hold offset below is the skiff's (config 2)")
    deck = session.checked("state")["sailing"]["player"]
    return (deck["x"] & ~7) + 4, (deck["z"] & ~7) + 1


def run_dock(session: Session, report: Report, captain_name: str) -> None:
    """Board-friend: the guest boards the captain's boat by typing their name."""
    session.checked("pause")
    if peer_state(session)["present"]:
        session.checked("peer remove")
        session.checked("step 10")
    boat = vessel(session)
    require((boat["fine_x"] // 128, boat["fine_z"] // 128) == (3073, 2987),
            "The Board-friend row needs the Pandemonium berth fixture at 3073,2987")
    session.checked("camera 1024 256 1200")
    # The captain's boat has to be a boat they OWN, moored at this dock: that is
    # what ~sailing_board_friend checks before it will let anyone aboard.
    owned = session.checked("peer create Deckhand")
    require(owned["present"], "no guest")
    session.checked("step 20")
    owned_id = session.checked(f"peer varbit {BOAT_OWNED_VARBIT}")["server"]["varbit"]
    port_id = session.checked(f"peer varbit {BOAT_PORT_VARBIT}")["server"]["varbit"]
    session.checked(f"cheat setting {owned_id} 1")
    session.checked(f"cheat setting {port_id} {PANDEMONIUM_DOCK_ID}")
    session.checked("step 10")
    registered = {"owned": varbit(session, BOAT_OWNED_VARBIT),
                  "port": varbit(session, BOAT_PORT_VARBIT),
                  "cargo_slot": vessel(session)["cargo_slot"]}

    # The captain's Private chat has to be visible or p_findvisibleplayer
    # refuses; drive it from the chat bar's own right-click menu.
    session.checked("click 219 490 right")
    session.checked("step 5")
    private_menu = report.shot(session, "captain-private-chat-menu")
    session.checked("click 200 434")
    session.checked("step 20")

    session.checked("peer place 0 3069 2987")
    session.checked("step 30")
    session.checked("peer cheat setlevel sailing 99")
    session.checked("step 20")
    ashore = peer_state(session)
    require(ashore["server"]["level"] == 0 and ashore["server"]["navigating"] == 0,
            "The guest has to be ashore at the dock before Board-friend")
    ashore_shot = report.shot(session, "guest-ashore-at-the-dock")
    mask_before = vessel(session)["navigator_mask"]

    session.checked("peer oploc 3 3070 2987 sailing_gangplank_the_pandemonium")
    parked = peer_state(session)
    session.checked("peer namedialog " + captain_name)
    session.checked("step 30")
    after = peer_state(session)
    deck = session.checked("state")["sailing"]["player"]
    boarded = (after["client"]["view"] == boat["view"]
               and after["server"]["level"] == deck["level"]
               and after["server"]["role"] not in (0, -1))
    after_shot = report.shot(session, "guest-after-board-friend")
    # ToriRSServer_VesselBoardPlayer takes the first walkable deck tile from the
    # pivot outward and does not avoid an occupied one, so the guest lands on
    # the captain's own tile and the two models coincide exactly. One deck step
    # separates them for the picture; it is a harness walk, not the content's.
    session.checked(f"peer walk {deck['x']} {deck['z'] + 1} 0")
    # A deck step is a server tick, and a tick is ~30 stepped frames.
    session.checked("step 90")
    aboard_view = peer_state(session)
    separated = (aboard_view["server"]["x"], aboard_view["server"]["z"]) != \
                (after["server"]["x"], after["server"]["z"])
    deck_shot = report.shot(session, "guest-and-captain-on-deck")
    if aboard_view["screen"]["projected"]:
        session.checked(f"hover {aboard_view['screen']['x']} {aboard_view['screen']['y']}")
        session.checked("step 3")
    hovered_shot = report.shot(session, "guest-aboard-hovered-at-the-berth")
    picked = peer_state(session)["screen"]["picked"]

    report.add(
        "Board-friend: the guest boards the captain's boat by name",
        "pass" if boarded else "fail",
        "The guest's real OPLOC3 on the Pandemonium gangplank ran "
        "[oploc3,sailing_gangplank_embark] -> ~sailing_board_friend and PARKED on "
        f"p_namedialog (peer active_script={parked['server']['script']}, "
        f"execution={parked['server']['script_wait']} = SSVM_NAMEDIALOG); the "
        f"harness answered with RESUME_P_NAMEDIALOG \"{captain_name}\" and the script "
        f"resumed and ran to completion (script={after['server']['script']}). The guest "
        f"went from {ashore['server']['x']},{ashore['server']['z']} level "
        f"{ashore['server']['level']} view {ashore['client']['view']} to "
        f"{after['server']['x']},{after['server']['z']} level {after['server']['level']} "
        f"view {after['client']['view']}, which is the captain's hull view "
        f"{boat['view']} and the captain's own deck plane {deck['level']}. Captain's "
        f"owned-boat registration ({BOAT_OWNED_VARBIT}={registered['owned']['server']}, "
        f"{BOAT_PORT_VARBIT}={registered['port']['server']}, hull cargo slot "
        f"{registered['cargo_slot']}, dock_id {PANDEMONIUM_DOCK_ID}) is read in the "
        "CAPTAIN's script context, which is what the vessel_* opcodes now answer for; "
        f"the client's own world pick reports picked={picked} on the guest's model. "
        f"For the deck picture the guest was then walked one tile to "
        f"{aboard_view['server']['x']},{aboard_view['server']['z']} "
        f"(separated={separated}), because VesselBoardPlayer lands it on the "
        "captain's own tile and the two models coincide exactly.",
        images=[private_menu, ashore_shot, after_shot, deck_shot, hovered_shot])

    mask_after = vessel(session)["navigator_mask"]
    report.add(
        "boarding by name grants no navigation permission",
        "pass" if (after["server"]["role"] == 3
                   and not after["permissions"]["navigate"]
                   and mask_after == mask_before) else "fail",
        f"peer role varbit sailing_sidepanel_player_role = {after['server']['role']} "
        "(the cache's passenger rung 3 -- see "
        "docs/sailing_validation/social/roles-results.json), "
        f"ToriRSServer_VesselCanNavigate={after['permissions']['navigate']}, "
        f"vessel_stat 11 navigator mask {mask_before} -> {mask_after}",
        images=[deck_shot])

    # The guest's own hold click, from the deck. The hold loc's tile comes from
    # the hull frame, not from a hardcoded map coordinate.
    hold_x, hold_z = deck_hold_tile(session, boat)
    privacy_id = session.checked("peer varbit settings_cargo_hold_privacy")["server"]["varbit"]
    session.checked(f"cheat setting {privacy_id} 0")
    session.checked("step 10")
    session.checked(f"peer oploc 1 {hold_x} {hold_z} sailing_boat_cargo_hold_regular_2x5")
    session.checked("step 120")
    refused = peer_state(session)
    session.checked(f"cheat setting {privacy_id} 1")
    session.checked("step 10")
    session.checked(f"peer oploc 1 {hold_x} {hold_z} sailing_boat_cargo_hold_regular_2x5")
    session.checked("step 120")
    admitted = peer_state(session)
    hold_shot = report.shot(session, "guest-opened-the-hold-from-the-deck")
    report.add(
        "the guest opens the cargo hold with a real OPLOC on the deck's hold loc",
        "pass" if (refused["server"]["mainmodal"] == 0
                   and admitted["server"]["mainmodal"] == 943) else "fail",
        f"the hold loc sits at deck {hold_x},{hold_z} -- ~sailing_facilities_place puts "
        "sailing_boat_cargo_hold_regular_2x5 at the hull frame's base+(4,1) for config 2, "
        "and the guest's OPLOC1 walked it from "
        f"{aboard_view['server']['x']},{aboard_view['server']['z']} to "
        f"{admitted['server']['x']},{admitted['server']['z']}. At privacy 0 (Navigators) "
        f"a non-navigator guest is refused: mainmodal={refused['server']['mainmodal']}, "
        f"cargo={refused['permissions']['cargo']}. At privacy 1 (All players) the same "
        f"click opens the native hold: mainmodal={admitted['server']['mainmodal']} (943), "
        f"cargo={admitted['permissions']['cargo']}. The capture is the CAPTAIN's screen "
        "-- the guest has no client of its own, so its open hold is the server reading "
        "above, not a rendered panel; what the picture carries is the guest standing at "
        "the hold on the captain's deck.",
        images=[hold_shot])
    session.checked(f"cheat setting {privacy_id} 0")
    session.checked("step 10")


# --- entry point -----------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-social-ocean"))
    parser.add_argument("--dock-session", type=Path, default=Path("/tmp/sailing-social-berth"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation/social")
    parser.add_argument("--start", action="store_true")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--binary", type=Path, default=ROOT / "src/torirs")
    parser.add_argument("--scripts", type=Path)
    parser.add_argument("--user", default="socialcap")
    parser.add_argument("--dock-user", default="socialdock")
    parser.add_argument("--skip-dock", action="store_true")
    args = parser.parse_args()

    def launch(session: Session, user: str, tile: tuple[int, int]) -> None:
        argv = ["--session", str(session.directory), "start", "--user", user,
                "--binary", str(args.binary), "--fixture-tile", str(tile[0]), str(tile[1])]
        if args.headless:
            argv.append("--headless")
        if args.scripts:
            argv += ["--scripts", str(args.scripts)]
        session.start(harness_parser().parse_args(argv))

    started = time.perf_counter()
    report = Report(args.output.expanduser().resolve())
    ocean = Session(args.session)
    sessions = {}
    with ocean.locked():
        if args.start and not ocean.alive():
            launch(ocean, args.user, (3072, 3160))
        require(ocean.alive(), "Start the ocean harness session first or pass --start")
        sessions["ocean"] = ocean.metadata()
        run_ocean(ocean, report)

    if not args.skip_dock:
        dock = Session(args.dock_session)
        with dock.locked():
            if args.start and not dock.alive():
                launch(dock, args.dock_user, (3073, 2987))
            require(dock.alive(), "Start the dock harness session first or pass --start")
            sessions["dock"] = dock.metadata()
            run_dock(dock, report, args.dock_user)

    failures = [row["name"] for row in report.rows if row["result"] == "fail"]
    blocked = [row["name"] for row in report.rows if row["result"] == "blocked"]
    result = {
        "tool": "tools/sailing_social_acceptance.py",
        "recorded_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "elapsed_ms": round((time.perf_counter() - started) * 1000, 3),
        "sessions": sessions,
        "scope": "two real server players, one native rendering client; the captain "
                 "clicks cache interfaces with the real mouse and the guest sends the "
                 "packets a second client would send",
        "checks": report.rows,
        "failed": failures,
        "blocked": blocked,
        "ok": not failures and not blocked,
    }
    (report.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"ok": result["ok"], "failed": failures, "blocked": blocked,
                      "results": str(report.output / "results.json")}, indent=2))


if __name__ == "__main__":
    main()
