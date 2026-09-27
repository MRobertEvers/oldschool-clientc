# Prying Times -- content brief (wiki-sourced)

Status: rebuilt 2026-09-26 (content parity pass parity1o). LostCity has no
Prying Times (a 2025 Sailing quest), so the port is constructed from the OSRS
wiki and Quest Helper's `pryingtimes/PryingTimes.java`. Every line an NPC or
the player speaks is the transcript's own.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Prying Times](https://oldschool.runescape.wiki/w/Prying_Times?oldid=15315235) | 15315235, 2026-08-21 | Requirements, walkthrough (hold loading, sealed crate from the vessel, the door behind the bar), rewards |
| [Transcript:Prying Times](https://oldschool.runescape.wiki/w/Transcript:Prying_Times?oldid=15073159) | 15073159, 2025-11-30 | Every Steve / Thurgo / port master line, every branch, the stout-on-land refusal |
| [Transcript:'Squawking' Steve Beanie](https://oldschool.runescape.wiki/w/Transcript:%27Squawking%27_Steve_Beanie?oldid=15074066) | 15074066, 2025-11-30 | Steve's standard five-row menu (shop, Old Grog, one rule, Nothing) |
| [Transcript:Captain Tobias](https://oldschool.runescape.wiki/w/Transcript:Captain_Tobias?oldid=15315412) | 15315412, 2026-08-21 | Post-Pandemonium travel menu (Musa Point / the Pandemonium, 30 coins) |
| [Transcript:Port master](https://oldschool.runescape.wiki/w/Transcript:Port_master?oldid=15131521) | 15131521, 2026-02-20 | Port master Talk-to, Claim-rewards, Cancel-task |
| [Port master](https://oldschool.runescape.wiki/w/Port_master?oldid=15323171) | 15323171, 2026-08-28 | Map pins: Port Sarim 3028,3194, the Pandemonium 3061,2985 (the ledger tables) |
| [Courier tasks](https://oldschool.runescape.wiki/w/Courier_tasks?oldid=15327263) | 15327263, 2026-09-01 | Task 600 row (level 12, Port Sarim -> the Pandemonium, 1 crate of looty), take / load / withdraw / deposit steps |
| [Crate of looty](https://oldschool.runescape.wiki/w/Crate_of_looty?oldid=15123025) | 15123025, 2026-02-07 | Equipable, slot 2h: a crate is carried in the hands |

Cache: `configs/all.dbrow [port_task_prying_times]` (task_id 600, cargo_port
Port Sarim, ending_port the Pandemonium, cargo `prying_times_cargo_crate` x1,
level 12, no starting_port -- no notice board offers it);
`[quest_pryingtimes]` (Sailing 12, Smithing 30, 800 Sailing XP).

## 2. Stages (`%quest_pry`, Quest Helper `steps.put`)

| Stage | Guide step | Content (file) |
| --- | --- | --- |
| 0 -> 5 | startQuest | Steve's "Got any work that needs doing here?" -> "... it is you!" -> "Start the Prying Times miniquest?" Yes writes 5; "give me your log" writes task 600 into a free port-task slot (no log / full log branches verbatim). `pryingtimes.rs2` |
| 5 | getDeliveryTask / deliverCargo (PortTaskStep 600) | Take-cargo at Port Sarim's ledger table, carry or load into the hold of the player's OWN boat, sail, withdraw, Deposit-cargo at the Pandemonium's ledger. Completion pays 180 Sailing XP and writes 10 (`sailing/scripts/port_tasks.rs2` -> `[proc,pry_looty_delivered]`). |
| 10 -> 15 | letSteveKnow | "I delivered that cargo for you." |
| 15 -> 20 | getKey | Thurgo: "I need some help with a 'special key'." -> his offer writes 20; "Make a crowbar with Thurgo?" consumes the steel bar and redberry pie (hammer kept). `pryingtimes_locs.rs2` |
| 20 -> 25 | giveKey | "I made that 'special key' you needed." |
| 25 | sailToCrate / testKey / drinkTheStout / killTheTroll | Sail the player's boat to the sealed crate at 3013,2998 (placed from stage 25; re-placed when the hull enters zone 0_47_46_0_48/0_56), Pry-open from the deck, drink the stout on a boat only (on land: the transcript's refusal), a drink troll appears (optional fight). |
| 25 -> 30 | goToSteve | "About that crate..." after the drink. |
| 30 -> 35 | openCrate | Through the bar's door (pandemonium_door_reverse) to the crate next to Steve; crate of crowbars, reward scroll, Steve's closing lines. |

## 3. OSRS-era choices recorded here

- Travel to the Pandemonium without a boat: Captain Tobias / Seaman Lorris /
  Seaman Thresnor after Pandemonium (Transcript:Captain Tobias). The quest's
  own courier and crate legs are sailed in the player's boat (Courier tasks:
  "Players are restricted to using their own boat for courier tasks").
- The "Yes, and don't ask again." stout option answers as Yes; this pack has
  no carrier for the remembered answer.
- Steve's "Let's see what you have." opens `pub_pandemonium`, which the cache
  declares without stock.
