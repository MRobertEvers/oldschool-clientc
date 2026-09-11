#!/bin/sh
# Fetch the Chambers of Xeric wiki corpus as RAW WIKITEXT.
#
# Raw, not rendered: the tick-level numbers in CoX live inside {{CiteTwitter}} and
# {{CiteDiscord}} templates -- Mod Ash quoting the server's own constants -- and
# those quotes are collapsed into footnote markers by every HTML view. The raw
# form keeps author, date, archive URL and the full quote on one line.
#
# Quotes extracted from this corpus are recorded in docs/minigames/cox/COX_PLAN.md
# section 11. Re-run this before arguing with any number in that section.
#
#   sh tools/fetch_cox_wiki.sh [outdir]     # default outdir: /tmp/cox_wiki
#
# Then, to list every tick number Jagex has published about the raid:
#   grep -ho 'quote=[^|}]*' "$outdir"/*.wikitext | grep -i tick

set -eu

out="${1:-/tmp/cox_wiki}"
mkdir -p "$out"

# Redirects resolved already (Skeletal_Mystic, not Skeletal_mystic; Tekton#Enraged
# lives on Tekton). A page that 404s writes an empty file rather than aborting the
# run -- check sizes afterwards.
pages="
Chambers_of_Xeric
Chambers_of_Xeric/Strategies
Chambers_of_Xeric/Challenge_Mode
Chambers_of_Xeric/Challenge_Mode/Strategies
Great_Olm
Perfect_Olm_(Solo)
Tekton
Vasa_Nistirio
Glowing_crystal
Vespula
Abyssal_portal
Vespine_soldier
Vanguard
Muttadile
Ice_demon
Lizardman_shaman_(Chambers_of_Xeric)
Skeletal_Mystic
Guardian_(Chambers_of_Xeric)
Scavenger_beast
Deathly_ranger
Deathly_mage
Jewelled_Crab
Overload_(Chambers_of_Xeric)
Keystone_crystal
Cavern_grubs
Corrupted_scavenger
Ancient_chest
Talk:Chambers_of_Xeric
"

for p in $pages; do
    f=$(echo "$p" | tr '/:' '__')
    curl -sL -A "3draster-research/1.0 (mreverssci@gmail.com)" \
         "https://oldschool.runescape.wiki/w/$p?action=raw" \
         -o "$out/$f.wikitext"
done

# Attack speeds in ticks, per npc, machine-readable. The wiki infoboxes agree with
# this file everywhere both state a speed; it is the only source that states one
# for every CoX npc.
curl -sL "https://raw.githubusercontent.com/weirdgloop/osrs-dps-calc/main/cdn/json/monsters.json" \
     -o "$out/monsters.json"

wc -c "$out"/*.wikitext "$out"/monsters.json
