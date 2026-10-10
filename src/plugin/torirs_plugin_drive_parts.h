/*
 * The quest driver's Lua parts, in load order: ONE list, read by the client's
 * plugin host (torirs_plugin_drive.c, concatenated into one chunk) and by the
 * server's in-process script runner (torirsserver/torirs_server_scriptrun.c),
 * so a part added for one is loaded by the other. Paths are relative to the
 * script root (script/).
 */
#ifndef TORIRS_PLUGIN_DRIVE_PARTS_H
#define TORIRS_PLUGIN_DRIVE_PARTS_H

static char const* const DRIVE_SCRIPT_PARTS[] = {
    "plugins/quest_driver/core.lua",
    "plugins/quest_driver/state.lua",
    "plugins/quest_driver/chat.lua",
    "plugins/quest_driver/read.lua",
    "plugins/quest_driver/pointer.lua",
    "plugins/quest_driver/world.lua",
    "plugins/quest_driver/ui.lua",
    "plugins/quest_driver/quest.lua",
    "plugins/quest_driver/combat.lua",
    "plugins/quest_driver/sail.lua",
    "plugins/quest_driver/session.lua",
    "plugins/quest_driver/spell.lua",
    "plugins/quest_driver/prayer.lua",
    "plugins/quest_driver/waves.lua",
    "plugins/quest_driver/ticklog.lua",
    "plugins/quest_driver/raid.lua",
    "plugins/quest_driver/raid_solve_verzik_p1.lua",
    "plugins/quest_driver/raid_solve_verzik_p2.lua",
    "plugins/quest_driver/raid_solve_verzik_p3.lua",
    "plugins/quest_driver/tob.lua",
    "plugins/quest_driver/tob_maiden_waves.lua",
    "plugins/quest_driver/tob_maiden.lua",
    "plugins/quest_driver/tob_bloat.lua",
    "plugins/quest_driver/tob_nylocas_waves.lua",
    "plugins/quest_driver/tob_nylocas.lua",
    "plugins/quest_driver/tob_sotetseg.lua",
    "plugins/quest_driver/tob_xarpus.lua",
    "plugins/quest_driver/tob_relay.lua",
    "plugins/quest_driver/cutscene.lua",
};

#define DRIVE_SCRIPT_PART_COUNT ((int)(sizeof(DRIVE_SCRIPT_PARTS) / sizeof(DRIVE_SCRIPT_PARTS[0])))

#endif
