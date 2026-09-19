The XP Tracker's art, and where each piece came from.

  panel_icon.png
               The XP Tracker slot graphic from the cache's CS2 popout:
               sprite 3579 (`popout_icons,2`), selected through enum_4067 and
               struct_3742. It is the icon drawn in the plugin rail.

  overview_icon.png
               The distinct overall-row graphic the cache XP tracker names in
               xptracker_build_components_5363: sprite 222 (`staticons2,7`).

  skills.png   The 25 skill icons in one strip, 25x25 a cell, indexed BY SKILL
               ID (cell 3 is hitpoints). Cut from the cache's stats-tab icons
               the same way xp-drop-orbs' copy was -- see that plugin's notes
               for the sprite ids, which are NOT in skill order (197 attack,
               198 strength, 199 defence), and for sailing/summoning sitting
               apart at 228/229.

  text.png     The cache's own p12 face (fontmetrics_494 -- the face the CS2
  text.ini     XP tracker sets every one of its labels in), baked by
               tools/fontbake_atlas.py. One WHITE row: is_text multiplies the
               ink by a tint, so one bake serves the white values, the 0xcccccc
               keys and anything else, with the baked black shadow staying
               black.

Copied from script/plugins/assets/xp-drop-orbs/ rather than shared, because a
plugin's asset namespace is its own -- one plugin can neither read nor
overwrite another's, which is what stops a plugin from breaking when an
unrelated one re-bakes its art.

The layout and every colour these are drawn with are the cache's, read out of
the CS2 that builds interface 729 (`xptracker`); see the file comment in
src/plugin/plugins/xp_tracker.c for the decompile that states them.
