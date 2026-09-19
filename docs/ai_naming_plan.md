Work in:

/Users/matthewevers/Documents/git_repos/3draster

Your task is to audit and correct all OSRS rev-239 clientscript names that were first introduced today.

Do not merely report bad names. Perform the safe, high-confidence renames and maintain an audit log until every candidate has been reviewed.

Scope
=====

The last repository commit before today is:

a50d766bacf081c0ff5ee92097ea982076b0186b

Repository:

/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content

Content tree:

/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs239-content

Current name pack:

OSRS-Content/osrs239-content/pack/12_clientscripts.pack

Current sources:

OSRS-Content/osrs239-content/scripts

The trusted baseline is determined exclusively from the sources at commit
a50d766bacf081c0ff5ee92097ea982076b0186b.

A script has a trusted pre-today name only if its historical `.cs2` source has a header of either form:

[clientscript,<meaningful_name>]
[proc,<meaningful_name>]

The following are not meaningful baseline names:

[clientscript,script<ID>]
[clientscript,script_<ID>]
[proc,script<ID>]
[proc,script_<ID>]

Numeric or entity-bound event subjects are also not protected names, for example:

[worldmapelementmouserepeat,27]
[opworldmapelement1,13]

There should be 3,239 protected IDs and 6,486 candidate IDs in the current 9,725-script pack. Recompute these counts rather than hardcoding the actual ID list.

Do not use RuneStar’s `script-names.tsv` to expand the protected set. Even if a name appears there, it remains suspect if it was not present in this repository before today.

Baseline restoration
====================

Commit `a7872d33aa8a35d7c577e64c9453d7f13333f71e` mechanically converted the pre-today names into collision-safe pack/file names. It may add an `_ID` suffix where duplicate historical names require disambiguation.

For the 3,239 protected IDs:

- Use the historical header for the semantic name.
- Use the corresponding spelling from `a7872d33aa` as the collision-safe tree spelling.
- Do not semantically review or replace these names.
- If a protected name was overwritten later today, restore it mechanically.
- At the current starting point, approximately 170 protected IDs have drifted. Recompute this number.

Candidate audit
===============

Every ID outside the 3,239-ID protected set is suspect, including names that look plausible.

Create or update:

OSRS-Content/docs/CS2_SCRIPT_NAME_AUDIT.tsv

Use these columns:

id
old_name
historical_name
header_trigger
header_subject
decision
new_name
confidence
evidence
status

Valid decisions:

KEEP
RENAME
DEFER

Valid status values:

pending
applied
verified
deferred

Process candidates in batches of at most 25. Group related scripts by callers, callees, interface, component, or subsystem when possible.

For every candidate
===================

1. Do not assume the current pack name is correct.
2. Read the complete source body.
3. Identify its header trigger, parameters, returns, callers and callees.
4. Inspect literal strings, named variables, varbits, components, interfaces, enums, DB references and gamevals.
5. Inspect related scripts in the same interface or call-graph family.
6. Use protected pre-today script names as terminology anchors.
7. Decide whether the current name accurately describes the script.

The current name is not evidence for itself. Names from commits made today are not independent evidence.

A name should be marked RENAME when it:

- combines unrelated subsystem names;
- looks like token soup;
- copies an arbitrary sequence of callback or opcode names;
- contains a sentence fragment unrelated to the behavior;
- claims behavior contradicted by the body;
- is generic when a clear purpose is provable;
- appears to have been inferred from unrelated neighboring bytecode;
- uses an arbitrary script name or ID despite strong semantic evidence.

Names such as these are immediately suspicious:

filled_helper_settitle_transmit_gravestone_oninvtransmit_progress_mouseleave_iiiy_box_hover_tooltip

settings_warning_annakarl_tablet_iron_noloot_icon_disabled_ground_items_edit_mode_sortbutton_draw

Do not automatically reject concise structural names such as `world_map_element_27_mouse_repeat` if that is exactly what the trigger proves and no more specific purpose is recoverable.

Evidence standard
=================

Use at least two independent evidence signals before assigning a semantic custom name. Strong signals include:

- a protected caller or callee;
- a clearly identified interface/component;
- literal UI text or actions;
- named vars, varbits, enums or DB records;
- a specific event trigger;
- a family of adjacent scripts with consistent, independently supported behavior.

If the body is only a wrapper around another unnamed script, trace the callee. If the evidence remains insufficient, use DEFER rather than inventing a misleading name.

When an unknown opcode blocks understanding, inspect:

/Users/matthewevers/Documents/git_repos/Deobfuscator/src_osrs239_rl1_12_33

and:

/Users/matthewevers/Documents/git_repos/osclient_decompile

Use those trees to determine opcode behavior, not to invent script semantics.

Custom naming rules
===================

Every name invented during this audit must begin with:

torirs_

Use:

torirs_<subsystem>_<action>[_<object>][_<qualifier>]

Requirements:

- lowercase snake case;
- concise and readable;
- normally three to seven meaningful tokens after `torirs_`;
- terminology consistent with protected scripts;
- describes actual behavior;
- globally unique;
- no numeric suffix unless semantic disambiguation is genuinely impossible;
- do not use `torirs_script_<id>` as a substitute for understanding the script.

Examples:

torirs_target_world_update_countdown
torirs_bank_depositbox_refresh_slots
torirs_worldmap_marker_show_tooltip

If the current name makes sense, KEEP it unchanged. Do not add `torirs_` to retained names.

Applying a rename
=================

Preserve the numeric script ID and script behavior.

For every accepted RENAME:

1. Change the ID’s entry in `pack/12_clientscripts.pack`.
2. Rename the corresponding `.cs2` file.
3. Preserve its leading `// <id>` comment.
4. If the header is `[clientscript,...]` or `[proc,...]`, replace the symbolic subject with the new `torirs_` name.
5. Update all symbolic callsites using `~old_name`.
6. Also check for callsites using the old header alias or `script<ID>`.
7. Update textual callback references such as `"old_name(...)"`.
8. Preserve numeric/entity subjects in event-bound headers.
9. Search the entire OSRS-Content tree for stale references.
10. Reject any proposed name that collides with another pack name or symbolic header.

Do not perform a blind global header-to-filename rewrite. Many event-bound headers are correctly numeric and must remain so.

Do not run a blanket script unpack, because it can overwrite existing source edits.

Validation
==========

After every batch:

- verify that all numeric IDs are unchanged;
- verify that the pack still has exactly 9,725 entries;
- verify every entry has exactly one matching `.cs2` or `.cs2b`;
- verify pack names are unique;
- verify symbolic names are unambiguous;
- verify protected baseline names were not modified except for explicit restoration;
- verify there are no stale references to renamed aliases;
- compile every renamed script and its direct callers;
- record the validation result in the audit TSV.

Then run the script asset verification using the locally built cachepack executable:

CACHEPACK_CS2_NAMES=/Users/matthewevers/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2 \
3rd/rscache/tools/cachepack/cachepack verify \
    --cache cache.osrs239 \
    --rev osrs239 \
    --src OSRS-Content/osrs239-content \
    --assets=scripts

Require zero declined source records and byte-identical script output. If the executable path differs, locate the existing built cachepack binary; do not rebuild or unpack content unnecessarily.

Do not commit changes unless explicitly asked. Preserve unrelated working-tree changes.

Completion criteria
===================

Do not claim completion until:

- all 3,239 protected IDs are restored or confirmed;
- all 6,486 candidate IDs have KEEP, RENAME or DEFER decisions;
- every applied custom name begins with `torirs_`;
- every applied rename is recorded and verified;
- no name collisions or stale callsites remain;
- cachepack script verification passes;
- every DEFER row contains a specific explanation of what evidence is missing.

Continue batch by batch, updating the audit file after each batch so the work can resume safely if interrupted.