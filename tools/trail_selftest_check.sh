#!/bin/sh
# Report on the selftest assertions this lane owns.
#
# Written after a real miss: the strange-device check had been failing at 3 of 9
# for a whole slice because the ad-hoc grep looked for the word "device" in the
# FAIL text and that assertion's wording does not contain it. Grepping a log for
# keywords answers "did anything I recognise fail", which is not the question.
#
# `SELFTEST_CHECK` prints only on failure, so a passing assertion leaves no
# trace — which means "absent" is ambiguous between passed and never-ran. Both
# halves are therefore checked: the stanza HEADERS must be present (the stanza
# ran at all), and then no FAIL line may match any owned expectation.
#
#   tools/trail_selftest_check.sh <selftest log>
set -e
LOG=${1:?usage: trail_selftest_check.sh <selftest log>}
STATUS=0

for header in \
  "treasure trails read the cache's clue database" \
  "multicannon, Tears of Guthix, Shooting Stars, diaries"
do
  if grep -aqF "mock230 selftest: $header" "$LOG"; then
    printf 'ran     %s\n' "$header"
  else
    printf 'NOT RUN %s\n' "$header"
    STATUS=1
  fi
done

grep -aF '  FAIL ' "$LOG" | sort -u > "${TMPDIR:-/tmp}/trail_fails.$$" || true
while IFS= read -r want; do
  [ -z "$want" ] && continue
  case "$want" in \#*) continue ;; esac
  if grep -qF "$want" "${TMPDIR:-/tmp}/trail_fails.$$"; then
    printf 'FAIL    %s\n' "$want"
    STATUS=1
  fi
done <<'WANTS'
six tier markers resolve
obj -> trail_clue_row -> cluehelper row
the generated step pools are the sizes
a rolled trail length covers the wiki's range
a whole easy trail walks to its reward casket
an emote clue's emote index is the emote tab's own
the outfit check refuses naked and half-dressed
the five target kinds resolve
a dig solves a step only on the clue's own tile
the interaction claim says no without a clue
a reward casket is consumed and pays out
a challenge row yields its question AND its answer
656 npcs can drop a clue
the strange device reads the wiki's temperature bands
milestone counts and prizes are the wiki's
a key-target loc is not claimable without the key
every unbuilt STASH pairs with a different built one
all four parts are required, not just the base
steel and granite do not mix
Tears of Guthix needs BOTH seven days and something earned since
three blue and three green streams land on six different walls
the tears reward the genuine lowest skill
the nine star sizes map to nine distinct locs
every generated landing site resolves to a real coord
the stardust roll is neither never nor always
the generated diary registry agrees with the authored totals
Cerberus's souls and lava have different gates
the Guardians' phase gates, per-phase immunity
the Sire's stun ladder, the vent damage FLOOR
the Muspah swaps on DAMAGE not health
the Hydra's vent table, its 75% reduction
the three Dagannoth Kings' authored params PARSE
Zalcano's tephra formula excludes Mining
Araxxor's eggs and specials are two different counts
Vardorvis hits HARDER as he weakens
the Nightmare's shield scales from FIVE players up
Castle Wars pays FOUR outcomes
the selftest CAN build an arena
the Stronghold's portal skip is on combat level
a defender needs a re-entry before the next tier can drop
the retrieval container holds one death, not a running total
the Gauntlet's global best time is a minimum, and its unset value stays out of the comparison
Pest Control eligibility is a draining activity bar that cannot refill from zero
the duel's rule bits and worn bits share one varp and must not collide
two different clans are missing two different halves of the crystal set
the brimstone key's two branches meet at combat 100 and the Slayer boost scales the denominator
a skull helps the revenant table except where it hurts, and the amulet is not the cape of skulls
Barrows' 1000 cap is on the kill sum and the brothers sit above it
the party room's members threshold is the HIGHER one
the hot zone's two bands overlap at 30 and only one prize cell pays an item
corpbane is a per-weapon flag AND a stab-only rule, and Protect from Magic only reduces
four altars are 100% reduction without a demonbane weapon and 60% with one
Vorkath's two specials alternate and only the first is random
Porazdir's ball ignores prayer and only distance stops it
the Mark of Darkness doubles two things and is opt-in
a Rise of the Six death heals the survivors and resets the corpses
the rabbit foot narrows the nest range, it does not reweight the table
the gilded altar is 250/300/350, not a doubling
the Hespori's buds make it invulnerable and six clicks break the entangle
four of a kind and two doubles both total four and are different hands
a full inventory still makes a loot key; five held makes none
Bounty Hunter's skull boundary coin belongs to the lower tier
the seed vault refuses seedlings for their stage and quest seeds for their origin
eighteen chompy hats against twenty-two ranks
the nugget batch cap deletes and the upgraded sack is not double
Krystilia's base and extended amounts are separate columns
WANTS
rm -f "${TMPDIR:-/tmp}/trail_fails.$$"
[ "$STATUS" = 0 ] && printf 'all owned assertions pass\n'
exit $STATUS
