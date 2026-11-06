#ifndef SRC_TORIRSMAPED_TORIRS_MAPEDCTL_ENCOUNTERS_H
#define SRC_TORIRSMAPED_TORIRS_MAPEDCTL_ENCOUNTERS_H

/*
 * torirsmapedctl's encounter verbs (workbench 6.7; editor/editor_encounters.h,
 * the ENCOUNTERS document editor/encounters.txt):
 *
 *   encounters
 *       every set, one line each, as the document states it
 *   encounter-set <name> [colour <0..7|->] [note <text|->] [rename <new>] [clear]
 *                 [spawn <npc|obj>:<name> <x> <z> <level> [<count>]]...
 *                 [unspawn <npc|obj>:<name> <x> <z> <level> [<count>]]...
 *       create the set or change it: `clear` drops its spawns first, `spawn`
 *       adds one, `unspawn` takes one out, `-` clears a colour or a note
 *   encounter-remove <name>
 *
 * A change is the document's new text planned from the old and sent as
 * TEXT_SPLICE commands in ONE stroke (one undo step), unsaved until `save`,
 * exactly what the encounter builder's panel sends. Split out of
 * torirs_mapedctl_main.c so a test drives the verbs over an embedded server
 * (Editor_HostOpenMapEdEmbedPeer): the bytes are the daemon's either way.
 * Each prints what it did on stdout, a refusal and its reason on stderr, and
 * returns the exit status (0 done, 1 refused).
 */

struct EditorHost;

int
ToriRSMapEdCtl_Encounters(struct EditorHost* host);

int
ToriRSMapEdCtl_EncounterSet(
    struct EditorHost* host,
    const char* const* args,
    int count);

int
ToriRSMapEdCtl_EncounterRemove(
    struct EditorHost* host,
    const char* name);

#endif
