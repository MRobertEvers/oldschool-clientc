#!/usr/bin/env python3
"""Deterministic source contract for the usable 2012 QBD reward chain."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content" / "osrs239-content"


def read(relative: str) -> str:
    return (CONTENT / relative).read_text(encoding="utf-8")


def block(text: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^\[{re.escape(name)}\][^\n]*\n(.*?)(?=^\[|\Z)", text
    )
    if match is None:
        raise AssertionError(f"missing config/script block [{name}]")
    return match.group(1)


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"{context}: missing {needle!r}")


def config_field(config: str, field: str, context: str) -> str:
    values = re.findall(rf"(?m)^{re.escape(field)}=(.+)$", config)
    if len(values) != 1:
        raise AssertionError(
            f"{context}: expected exactly one {field}, found {len(values)}"
        )
    return values[0]


def obj_ledger() -> dict[int, int]:
    path = CONTENT / "port/rs2012_qbd_td.map"
    rows: dict[int, int] = {}
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = raw.split("\t")
        if fields[0] != "obj":
            continue
        if len(fields) < 4:
            raise AssertionError(f"{path}:{line_number}: incomplete object row")
        source, destination = int(fields[1]), int(fields[3])
        if source in rows:
            raise AssertionError(f"{path}:{line_number}: duplicate object {source}")
        rows[source] = destination
    return rows


def main() -> None:
    reward = read(
        "server/scripts/minigames/minigame_rs2012_qbd/scripts/"
        "rs2012_qbd_reward_items.rs2"
    )
    coffer = read(
        "server/scripts/minigames/minigame_rs2012_qbd/scripts/"
        "rs2012_qbd_rewards.rs2"
    )
    qbd_selftest = read(
        "server/scripts/minigames/minigame_rs2012_qbd/scripts/"
        "rs2012_qbd_selftest.rs2"
    )
    qbd_obj = read(
        "server/scripts/minigames/minigame_rs2012_qbd/configs/rs2012_qbd.obj"
    )
    qbd_rows = read(
        "server/scripts/minigames/minigame_rs2012_qbd/configs/rs2012_qbd.dbrow"
    )
    imported = read("ported/rs2012_qbd_td/configs/rs2012.obj")
    # The Royal bindings moved out of `skill_crafting/.../leather.rs2` when this
    # lane became one a build may leave out: a base file may not name
    # `rs2012_obj_24374`. The panel and the batch loop they call are still base.
    leather = read(
        "server/scripts/minigames/minigame_rs2012_qbd/scripts/rs2012_royal_leather.rs2"
    )
    tanner = read("server/scripts/areas/alkharid/scripts/tanner.rs2")
    sbott = read("server/scripts/areas/area_canifis/scripts/sbott.rs2")
    disputed = read("server/scripts/skill_combat/configs/equipment_disputed.obj")
    level_rows = read("server/scripts/skill_combat/configs/levelrequire.dbrow")
    ranged = read("server/scripts/skill_combat/scripts/player/player_ranged.rs2")

    conversions = (
        ("magictraining_infinityhat", "rs2012_obj_24354"),
        ("magictraining_infinitytop", "rs2012_obj_24355"),
        ("magictraining_infinitybottom", "rs2012_obj_24356"),
        ("magictraining_infinitygloves", "rs2012_obj_24357"),
        ("magictraining_infinityboots", "rs2012_obj_24358"),
        ("brut_dragon_full_helm", "rs2012_obj_24359"),
        ("dragon_platebody", "rs2012_obj_24360"),
        ("hundred_gauntlets_level_9", "rs2012_obj_24361"),
        ("dragon_boots", "rs2012_obj_24362"),
        ("dragon_platelegs", "rs2012_obj_24363"),
        ("dragon_plateskirt", "rs2012_obj_24364"),
    )
    product = block(reward, "proc,rs2012_dragonbone_product")
    original = block(reward, "proc,rs2012_dragonbone_original")
    for source, output in conversions:
        require(product, f"case {source} : return({output});", "forward kit map")
        require(original, f"case {output} : return({source});", "reverse kit map")
        require(
            reward,
            f"[opheld3,{output}] ~rs2012_dragonbone_split({output}, last_slot);",
            "Split binding",
        )
    require(reward, "[opheldu,rs2012_obj_24352]", "upgrade-kit Use binding")
    require(reward, "[opheld1,rs2012_obj_24352]", "upgrade-kit Info binding")
    require(reward, "inv_freespace(inv) < 1", "safe two-item Split")

    recipes = {
        "rs2012_craft_royal_vambraces": (87, 1, "rs2012_obj_24376", 940),
        "rs2012_craft_royal_chaps": (89, 2, "rs2012_obj_24379", 1880),
        "rs2012_craft_royal_body": (93, 3, "rs2012_obj_24382", 2820),
    }
    for row, (level, leather_count, product_obj, xp) in recipes.items():
        data = block(qbd_rows, row)
        require(data, "table=craft_leather_table", row)
        require(data, f"data=levelrequired,{level}", row)
        require(data, f"data=leather,rs2012_obj_24374,{leather_count}", row)
        require(data, f"data=product,{product_obj}", row)
        require(data, f"data=productexp,{xp}", row)
    require(leather, "[opheld1,rs2012_obj_24374]", "Royal leather Craft option")
    require(leather, "[opheldu,rs2012_obj_24374]", "needle-on-Royal-leather")
    require(
        leather,
        'return(rs2012_obj_24382, rs2012_obj_24376, rs2012_obj_24379, "Royal");',
        "Royal leather product menu",
    )

    require(tanner, '"Royal.", 5', "tanner Royal-hide choice")
    require(
        tanner,
        "@tan_dragonhide(rs2012_obj_24372, rs2012_obj_24374, $count);",
        "Royal hide-to-leather conversion",
    )
    require(
        read("server/scripts/areas/alkharid/configs/tanner.constant"),
        "^tanner_dragonhide_cost = 20",
        "period 20-coin tanning cost",
    )
    require(
        sbott,
        "inv_total(inv, rs2012_obj_24372)",
        "Sbott recognises Royal dragonhide",
    )
    require(sbott, "@tan_leather_choices;", "Sbott shares Royal tanning menu")
    require(
        read("server/scripts/areas/alkharid/configs/tanner.constant"),
        "^werewolftanner_dragonhide_cost = 45",
        "Sbott 45-coin tanning cost",
    )

    journal_ids = tuple(range(24368, 24372))
    for index, item_id in enumerate(journal_ids, start=1):
        obj = f"rs2012_obj_{item_id}"
        require(reward, f"[opheld1,{obj}]", "journal Read binding")
        require(reward, f"[opheld5,{obj}]", "journal Destroy binding")
        require(
            block(reward, "proc,rs2012_dragonkin_journal_required_stage"),
            f"case {obj} : return({index});",
            "sequential journal unlock",
        )
    for bookcase in ("poh_bookcase1", "poh_bookcase2", "poh_bookcase3"):
        require(reward, f"[oploc1,{bookcase}]", "POH journal recovery")
    require(
        reward,
        "inv_total(rs2012_qbd_rewardinv, $journal) > 0",
        "unclaimed-coffer duplicate guard",
    )

    requirements = {
        "rs2012_obj_24336": (("ranged", 85),),
        "rs2012_obj_24338": (("ranged", 85),),
        **{
            f"rs2012_obj_{item_id}": (("defence", 25), ("magic", 50))
            for item_id in range(24354, 24359)
        },
        **{
            f"rs2012_obj_{item_id}": (("defence", 60),)
            for item_id in (24359, 24360, 24362, 24363, 24364, 24365)
        },
        "rs2012_obj_24376": (("ranged", 80),),
        "rs2012_obj_24379": (("ranged", 80),),
        "rs2012_obj_24382": (("defence", 40), ("ranged", 80)),
    }
    for obj, pairs in requirements.items():
        authored = block(disputed, obj)
        for stat, level in pairs:
            require(authored, f"param=levelrequire,{stat},{level}", f"{obj} gate")
            require(
                block(level_rows, f"levelrequire_{stat}_{level}"),
                f"data=obj,{obj}",
                f"{obj} generated gate index",
            )

    # Dragonbone gloves intentionally have no invented Defence gate: the
    # untradeable source gloves are the prerequisite and revision 727 supplies
    # no skill/level pair on object 24361.
    unsupported_glove_gate = re.search(
        r"(?ms)^\[rs2012_obj_24361\]\n.*?param=levelrequire",
        disputed,
    )
    if unsupported_glove_gate is not None:
        raise AssertionError("dragonbone gloves acquired an unsupported level gate")
    if "param=param_749" in block(imported, "rs2012_obj_24361"):
        raise AssertionError("source object 24361 unexpectedly acquired a skill pair")

    kite = block(imported, "rs2012_obj_24365")
    for param in (
        "wearpos=5",
        "param=stabdefence,int,52",
        "param=slashdefence,int,54",
        "param=crushdefence,int,53",
        "param=rangedefence,int,51",
        "param=magicdefence,int,-1",
    ):
        require(kite, param, "imported 2012 Dragon kiteshield")

    bolts = block(imported, "rs2012_obj_24336")
    require(bolts, "stackable=1", "Royal bolts stack")
    require(bolts, "wearpos=13", "Royal bolts ammo slot")
    require(block(qbd_obj, "rs2012_obj_24336"), "param=rangebonus_ammo,125", "Royal bolt strength")
    require(
        ranged,
        "$rhand = rs2012_obj_24338 & $ammo ! rs2012_obj_24336",
        "Royal crossbow rejects foreign ammo",
    )
    require(
        ranged,
        "$rhand ! rs2012_obj_24338 & $ammo = rs2012_obj_24336",
        "foreign crossbows reject Royal bolts",
    )

    # These source objects are adjacent unnoted/noted pairs.  Omitting the
    # note forms from the isolated import previously made cachepack resolve
    # their numeric source IDs against unrelated OSRS239 objects (including
    # placeholder knives and the white unicorn mask).
    note_pairs = {
        14472: (14473, 45007, 45063),
        14474: (14475, 45008, 45064),
        14476: (14477, 45009, 45065),
        14484: (14485, 45010, 45066),
        15272: (15273, 45011, 45067),
        20268: (20269, 45062, 45068),
    }
    ledger = obj_ledger()
    for unnoted, (noted, unnoted_dest, noted_dest) in note_pairs.items():
        unnoted_name = f"rs2012_obj_{unnoted}"
        noted_name = f"rs2012_obj_{noted}"
        unnoted_block = block(imported, unnoted_name)
        noted_block = block(imported, noted_name)
        if config_field(unnoted_block, "certlink", unnoted_name) != noted_name:
            raise AssertionError(f"{unnoted_name}: note link escaped RS2012 lane")
        if config_field(noted_block, "certlink", noted_name) != unnoted_name:
            raise AssertionError(f"{noted_name}: unnoted link escaped RS2012 lane")
        if config_field(noted_block, "certtemplate", noted_name) != "template_for_cert":
            raise AssertionError(f"{noted_name}: incorrect note template")
        if ledger.get(unnoted) != unnoted_dest:
            raise AssertionError(f"{unnoted_name}: published allocation changed")
        if ledger.get(noted) != noted_dest:
            raise AssertionError(f"{noted_name}: note allocation is not append-only")

    # The coffer is a compact display container: it may hold five unstackable
    # dragon bones in one cell. Backpack claims must expand that synthetic cell
    # into five one-item cells, whereas the bank and stackable objects retain a
    # full-count move. Pin the shared primitive and both UI claim paths so a
    # future direct inv_moveitem cannot silently recreate an illegal stack.
    transfer = block(coffer, "proc,rs2012_qbd_transfer_reward")
    require(
        transfer,
        "if ($destination = bank | oc_stackable($item) = true)",
        "coffer bank/stackable transfer branch",
    )
    require(
        transfer,
        "inv_moveitem(rs2012_qbd_rewardinv, $destination, $item, $amount);",
        "coffer full-count bank/stackable move",
    )
    require(
        transfer,
        "inv_del(rs2012_qbd_rewardinv, $item, $amount);",
        "coffer unstackable source removal",
    )
    require(
        transfer,
        "while ($given < $amount)",
        "coffer unstackable unit loop",
    )
    require(
        transfer,
        "inv_add($destination, $item, 1);",
        "coffer one-unit backpack insertion",
    )
    bulk_transfer = block(coffer, "proc,rs2012_qbd_transfer_rewards")
    slot_claim = block(coffer, "proc,rs2012_qbd_claim_slot")
    for claim_path, context in (
        (bulk_transfer, "bulk Take path"),
        (slot_claim, "per-slot Take path"),
    ):
        require(
            claim_path,
            "~rs2012_qbd_transfer_reward($destination, $item, $amount);",
            context,
        )
        if "inv_moveitem(rs2012_qbd_rewardinv" in claim_path:
            raise AssertionError(f"{context}: bypasses unit-expanding transfer")

    can_claim = block(coffer, "proc,rs2012_qbd_can_claim")
    require(
        can_claim,
        "$spaces = add($spaces, $amount);",
        "bulk backpack unstackable preflight",
    )
    require(
        slot_claim,
        "def_int $spaces = $amount;",
        "per-slot backpack unstackable preflight",
    )
    claim_all = block(coffer, "proc,rs2012_qbd_claim")
    if claim_all.index("~rs2012_qbd_can_claim(inv)") > claim_all.index(
        "~rs2012_qbd_transfer_rewards(inv)"
    ):
        raise AssertionError("bulk Take path transfers before capacity preflight")

    runtime = block(qbd_selftest, "proc,rs2012_qbd_test_claim_expansion")
    require(runtime, "inv_setslot(rs2012_qbd_rewardinv, $source_slot, dragon_bones, 5);", "runtime coffer fixture")
    require(runtime, "~rs2012_qbd_transfer_reward(inv, dragon_bones, 5);", "runtime production transfer")
    for slot in range(5):
        require(
            runtime,
            f"inv_getobj(inv, $slot{slot}) ! dragon_bones | inv_getnum(inv, $slot{slot}) ! 1",
            f"runtime backpack cell {slot}",
        )
        require(
            runtime,
            f"inv_delslot(inv, $slot{slot});",
            f"runtime backpack restoration {slot}",
        )
    require(
        runtime,
        "inv_getobj(rs2012_qbd_rewardinv, $source_slot) ! null",
        "runtime coffer source-removal assertion",
    )
    require(
        qbd_selftest,
        "[debugproc,rs2012qbdclaimtest]",
        "runtime claim debug entry",
    )

    print(
        "rs2012 QBD reward-item contract: 11 reversible kit maps, Royal "
        "tanning/crafting, 4 journals, wear gates, kite, bolts, and 6 note "
        "closures; coffer unit-expanding claims and runtime fixture OK"
    )


if __name__ == "__main__":
    main()
