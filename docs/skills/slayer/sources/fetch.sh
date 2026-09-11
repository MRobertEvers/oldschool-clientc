#!/bin/sh
# Pinned OSRS Wiki sources for Konar quo Maten (slice C8).
set -e
cd "$(dirname "$0")"
fetch() {
  # A missing page is reported, not fatal: not every master has an assignments
  # subpage (Spria shares Turael's list) and `set -e` would otherwise stop the
  # whole pin at the first one.
  if curl -sf -A 'osrs239-content port (contact: repo owner)' \
      "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"; then
    echo "$2 $(wc -c < "$2") bytes"
  else
    echo "$2 MISSING ($1)"
    rm -f "$2"
  fi
}
fetch Konar_quo_Maten Konar_quo_Maten.wiki
fetch Krystilia Krystilia.wiki
fetch Duradel Duradel.wiki
fetch Nieve Nieve.wiki
fetch Chaeldar Chaeldar.wiki
fetch Vannaka Vannaka.wiki
fetch Mazchna Mazchna.wiki
fetch Spria Spria.wiki
fetch Turael Turael.wiki
fetch Brimstone_key Brimstone_key.wiki
fetch Brimstone_chest Brimstone_chest.wiki
fetch "Turael/Slayer_assignments" Turael_assignments.wiki
fetch "Spria/Slayer_assignments" Spria_assignments.wiki
fetch "Mazchna/Slayer_assignments" Mazchna_assignments.wiki
fetch "Vannaka/Slayer_assignments" Vannaka_assignments.wiki
fetch "Chaeldar/Slayer_assignments" Chaeldar_assignments.wiki
fetch "Nieve/Slayer_assignments" Nieve_assignments.wiki
fetch "Duradel/Slayer_assignments" Duradel_assignments.wiki
fetch "Konar_quo_Maten/Slayer_assignments" Konar_quo_Maten_assignments.wiki
fetch "Krystilia/Slayer_assignments" Krystilia_assignments.wiki
