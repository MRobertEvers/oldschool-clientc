# Vendored: Skretzo/shortest-path transport data

Source: https://github.com/Skretzo/shortest-path

- Upstream commit: `8551e6016d053aa5930bb16485069a6997718da3` (branch `master`)
- Fetched: 2026-08-12
- Files: `transports/*.tsv`, copied verbatim from
  `src/main/resources/transports/` at the commit above. No content was edited;
  `tools/maplink_import.py` is the only consumer and treats every row as an
  unverified claim to be checked against `OSRS-Content/osrs239-content` before
  it can land in generated content.

Re-vendor with:

```sh
for f in transports agility_shortcuts boats canoes charter_ships fairy_rings \
         gnome_gliders hot_air_balloons magic_carpets magic_mushtrees \
         minecarts quetzals quetzal_whistle seasonal_transports ships \
         spirit_trees teleportation_boxes teleportation_items \
         teleportation_levers teleportation_minigames teleportation_portals \
         teleportation_portals_poh teleportation_spells \
         teleportation_spells_home wilderness_obelisks; do
  curl -sL -o "tools/data/shortest_path/transports/$f.tsv" \
    "https://raw.githubusercontent.com/Skretzo/shortest-path/master/src/main/resources/transports/$f.tsv"
done
```

then update the commit hash above and re-run
`python3 tools/maplink_import.py --check`.
