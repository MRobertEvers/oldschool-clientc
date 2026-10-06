package com.advancedraidtracker.rooms.tob;

import com.advancedraidtracker.AdvancedRaidTrackerConfig;
import com.advancedraidtracker.AdvancedRaidTrackerPlugin;
import com.advancedraidtracker.constants.RaidRoom;
import com.advancedraidtracker.utility.RoomUtil;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import com.advancedraidtracker.utility.datautility.DataWriter;
import com.advancedraidtracker.utility.RoomState;
import com.advancedraidtracker.utility.nyloutility.NylocasData;
import com.advancedraidtracker.utility.nyloutility.NylocasShell;
import com.advancedraidtracker.utility.nyloutility.NylocasWave;
import com.advancedraidtracker.utility.nyloutility.NylocasWaveMatcher;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

import static com.advancedraidtracker.constants.LogID.*;
import static com.advancedraidtracker.constants.TobIDs.*;
import static com.advancedraidtracker.utility.RoomState.NyloRoomState.*;

@Slf4j
public class NyloHandler extends TOBRoomHandler
{
    public RoomState.NyloRoomState roomState;
    private final ArrayList<NylocasShell> buildWave;

    public NyloHandler(Client client, DataWriter clog, AdvancedRaidTrackerConfig config, AdvancedRaidTrackerPlugin plugin)
    {
        super(client, clog, config, plugin);
        this.plugin = plugin;
        buildWave = new ArrayList<>();
        nylosAlive = new ArrayList<>();
        roomState = NOT_STARTED;
    }

    public static int instanceStart = -1;
    private int instanceReference = -1;
    private int bossSpawn = -1;
    private int lastDead = -1;
    private int entryTickOffset = -1;
    private int wave31 = -1;
    private final ArrayList<NPC> nylosAlive;
    int currentWave = 0;
    boolean hard = false;
    boolean story = false;
    private final AdvancedRaidTrackerPlugin plugin;
    ArrayList<NPC> bigsDeadThisTick = new ArrayList<>();
    Map<Integer, String> bigDescription = new HashMap<>();

    public void reset()
    {
        currentWave = 0;
        roomState = NOT_STARTED;
        roomStartTick = -1;
        instanceReference = -1;
        instanceStart = -1;
        bossSpawn = -1;
        lastDead = -1;
        accurateEntry = true;
        entryTickOffset = -1;
        hard = false;
        story = false;
        wave31 = -1;
        bigsDeadThisTick.clear();
        bigDescription.clear();
        super.reset();
    }

    public boolean isActive()
    {
        return !(roomState == NOT_STARTED || roomState == FINISHED);
    }

    public String getName()
    {
        return "Nylocas";
    }

    private int expectedWaveTick;

    public void updateGameTick(GameTick event)
    {
        bigsDeadThisTick.clear();
        if (!buildWave.isEmpty())
        {
            if (NylocasWaveMatcher.isWave(buildWave))
            {
                NylocasWave wave = NylocasWaveMatcher.getWave();
                if (wave.getWave() == 1)
                {
                    wave1Spawn();
                }
                if (wave.getWave() == 31)
                {
                    wave31Spawn();
                } else
                {
                    expectedWaveTick = client.getTickCount() + ((hard && wave.getWave() % 10 == 0) ? 16 : NylocasWave.waves[wave.getWave()].getDelay());
                    if (hard && (wave.getWave() % 10 == 0)) //in hard mode each wave a prinkipas comes out on (10, 20, 30) has been changed to a 4 cycle natural delay
                    {
                        expectedWaveTick = client.getTickCount() + 16;
                    }
                }
                currentWave = wave.getWave();
                plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick, "W" + currentWave);
                clog.addLine(NYLO_WAVE, String.valueOf(currentWave), String.valueOf(client.getTickCount() - roomStartTick));
            }
            buildWave.clear();
        }
        if (client.getTickCount() == expectedWaveTick && currentWave != 31)
        {
            clog.addLine(NYLO_STALL, String.valueOf(currentWave), String.valueOf(client.getTickCount() - roomStartTick), String.valueOf(nylosAlive.size()));
            plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick, "Stall");
            expectedWaveTick += 4;
        }
        super.updateGameTick(event);
    }

    public void updateNpcSpawned(NpcSpawned event)
    {
        switch (event.getNpc().getId())
        {
            case NYLO_MELEE_SMALL_HM:
            case NYLO_MELEE_SMALL_AGRO_HM:
            case NYLO_MELEE_BIG_HM:
            case NYLO_MELEE_BIG_AGRO_HM:
            case NYLO_RANGE_SMALL_HM:
            case NYLO_RANGE_SMALL_AGRO_HM:
            case NYLO_RANGE_BIG_HM:
            case NYLO_RANGE_BIG_AGRO_HM:
            case NYLO_MAGE_SMALL_HM:
            case NYLO_MAGE_SMALL_AGRO_HM:
            case NYLO_MAGE_BIG_HM:
            case NYLO_MAGE_BIG_AGRO_HM:
                if (!hard)
                {
                    clog.addLine(IS_HARD_MODE);
                }
                hard = true;
            case NYLO_MELEE_SMALL_SM:
            case NYLO_MELEE_SMALL_AGRO_SM:
            case NYLO_MELEE_BIG_SM:
            case NYLO_MELEE_BIG_AGRO_SM:
            case NYLO_RANGE_SMALL_SM:
            case NYLO_RANGE_SMALL_AGRO_SM:
            case NYLO_RANGE_BIG_SM:
            case NYLO_RANGE_BIG_AGRO_SM:
            case NYLO_MAGE_SMALL_SM:
            case NYLO_MAGE_SMALL_AGRO_SM:
            case NYLO_MAGE_BIG_SM:
            case NYLO_MAGE_BIG_AGRO_SM:
                if (!hard)
                {
                    if (!story)
                    {
                        clog.addLine(IS_STORY_MODE);
                    }
                }
                story = true;
            case NYLO_MELEE_BIG:
            case NYLO_MELEE_BIG_AGRO:
            case NYLO_RANGE_BIG:
            case NYLO_RANGE_BIG_AGRO:
            case NYLO_MAGE_BIG:
            case NYLO_MAGE_BIG_AGRO:
            case NYLO_MELEE_SMALL:
            case NYLO_MELEE_SMALL_AGRO:
            case NYLO_RANGE_SMALL:
            case NYLO_RANGE_SMALL_AGRO:
            case NYLO_MAGE_SMALL:
            case NYLO_MAGE_SMALL_AGRO:
                if (roomStartTick == -1)
                {
                    startNylo();
                }
                NylocasShell cShell = new NylocasShell(event.getNpc().getId(), event.getNpc().getWorldLocation().getRegionX(), event.getNpc().getWorldLocation().getRegionY());
                if (cShell.isBig())
                {
                    bigDescription.put(event.getNpc().getIndex(), "W" + (currentWave + 1) + " " + cShell.getDescription());
                }
                if (cShell.position != NylocasData.NyloPosition.ROOM)
                {
                    buildWave.add(cShell);
                    plugin.liveFrame.getPanel(getName()).addNPCMapping(event.getNpc().getIndex(), "W" + (currentWave + 1) + " " + cShell.getDescription());
                    clog.addLine(ADD_NPC_MAPPING, String.valueOf(event.getNpc().getIndex()), "W" + (currentWave + 1) + " " + cShell.getDescription(), getName());
                } else
                {
                    int matches = 0;
                    String lastMatchedDescription = "";
                    for (NPC npc : bigsDeadThisTick)
                    {
                        int bigX = npc.getWorldLocation().getRegionX();
                        int bigY = npc.getWorldLocation().getRegionY();
                        int littleX = event.getNpc().getWorldLocation().getRegionX();
                        int littleY = event.getNpc().getWorldLocation().getRegionY();
                        if ((bigX == littleX && bigY == littleY) || (littleX - 1 == bigX || littleY - 1 == bigY))
                        {
                            matches++;
                            lastMatchedDescription = bigDescription.get(npc.getIndex());
                        }
                    }
                    if (matches == 1)
                    {
                        clog.addLine(ADD_NPC_MAPPING, String.valueOf(event.getNpc().getIndex()), NylocasShell.getTypeName(event.getNpc().getId()) + " split from " + lastMatchedDescription + "(on w" + currentWave + ")", getName());
                        plugin.liveFrame.getPanel(getName()).addNPCMapping(event.getNpc().getIndex(), NylocasShell.getTypeName(event.getNpc().getId()) + " split from " + lastMatchedDescription + "(on w" + currentWave + ")");
                    }
                    switch (event.getNpc().getId())
                    {
                        case NYLO_MELEE_SMALL:
                        case NYLO_MELEE_SMALL_AGRO:
                        case NYLO_MELEE_SMALL_HM:
                        case NYLO_MELEE_SMALL_AGRO_HM:
                        case NYLO_MELEE_SMALL_SM:
                        case NYLO_MELEE_SMALL_AGRO_SM:
                            clog.addLine(MELEE_SPLIT, String.valueOf(currentWave), String.valueOf(client.getTickCount() - roomStartTick));
                            break;
                        case NYLO_RANGE_SMALL:
                        case NYLO_RANGE_SMALL_AGRO:
                        case NYLO_RANGE_SMALL_HM:
                        case NYLO_RANGE_SMALL_AGRO_HM:
                        case NYLO_RANGE_SMALL_SM:
                        case NYLO_RANGE_SMALL_AGRO_SM:
                            clog.addLine(RANGE_SPLIT, String.valueOf(currentWave), String.valueOf(client.getTickCount() - roomStartTick));
                            break;
                        case NYLO_MAGE_SMALL:
                        case NYLO_MAGE_SMALL_AGRO:
                        case NYLO_MAGE_SMALL_HM:
                        case NYLO_MAGE_SMALL_AGRO_HM:
                        case NYLO_MAGE_SMALL_SM:
                        case NYLO_MAGE_SMALL_AGRO_SM:
                            clog.addLine(MAGE_SPLIT, String.valueOf(currentWave), String.valueOf(client.getTickCount() - roomStartTick));
                            break;
                    }
                }
                nylosAlive.add(event.getNpc());
                break;
            case NYLO_BOSS_MELEE:
            case NYLO_BOSS_RANGE:
            case NYLO_BOSS_MAGE:
            case NYLO_BOSS_MELEE_HM:
            case NYLO_BOSS_RANGE_HM:
            case NYLO_BOSS_MAGE_HM:
            case NYLO_BOSS_MELEE_SM:
            case NYLO_BOSS_RANGE_SM:
            case NYLO_BOSS_MAGE_SM:
                bossSpawned();
                if (roomStartTick == -1)
                {
                    startNylo();
                }
                break;
            case NYLO_PILLAR:
            case NYLO_PILLAR_HM:
            case NYLO_PILLAR_SM:
                if (roomStartTick == -1)
                {
                    startNylo();
                }
                break;
            case NYLO_PRINKIPAS_DROPPING:
                break;
            case NYLO_PRINKIPAS_MELEE:
            case NYLO_PRINKIPAS_MAGIC:
            case NYLO_PRINKIPAS_RANGE:
                nylosAlive.add(event.getNpc());
                break;

        }
    }

    public void updateNpcDespawned(NpcDespawned event)
    {
        switch (event.getNpc().getId())
        {
            case NYLO_PILLAR:
            case NYLO_PILLAR_HM:
            case NYLO_PILLAR_SM:
                //clog.addLine(NYLO_PILLAR_DESPAWNED, String.valueOf(client.getTickCount() - roomStartTick));
                break;
            case NYLO_BOSS_MELEE:
            case NYLO_BOSS_RANGE:
            case NYLO_BOSS_MAGE:
            case NYLO_BOSS_MELEE_HM:
            case NYLO_BOSS_RANGE_HM:
            case NYLO_BOSS_MAGE_HM:
            case NYLO_BOSS_MELEE_SM:
            case NYLO_BOSS_RANGE_SM:
            case NYLO_BOSS_MAGE_SM:
            {
                bossKilled();
            }
            break;
            case NYLO_MELEE_BIG:
            case NYLO_MELEE_BIG_AGRO:
            case NYLO_MAGE_BIG:
            case NYLO_MAGE_BIG_AGRO:
            case NYLO_RANGE_BIG:
            case NYLO_RANGE_BIG_AGRO:
            case NYLO_MELEE_BIG_HM:
            case NYLO_MELEE_BIG_AGRO_HM:
            case NYLO_RANGE_BIG_HM:
            case NYLO_RANGE_BIG_AGRO_HM:
            case NYLO_MAGE_BIG_HM:
            case NYLO_MAGE_BIG_AGRO_HM:
            case NYLO_MELEE_BIG_SM:
            case NYLO_MELEE_BIG_AGRO_SM:
            case NYLO_RANGE_BIG_SM:
            case NYLO_RANGE_BIG_AGRO_SM:
            case NYLO_MAGE_BIG_SM:
            case NYLO_MAGE_BIG_AGRO_SM:
                bigsDeadThisTick.add(event.getNpc());
            case NYLO_MELEE_SMALL_HM:
            case NYLO_MELEE_SMALL_AGRO_HM:
            case NYLO_RANGE_SMALL_HM:
            case NYLO_RANGE_SMALL_AGRO_HM:
            case NYLO_MAGE_SMALL_HM:
            case NYLO_MAGE_SMALL_AGRO_HM:
            case NYLO_MELEE_SMALL_SM:
            case NYLO_MELEE_SMALL_AGRO_SM:
            case NYLO_RANGE_SMALL_SM:
            case NYLO_RANGE_SMALL_AGRO_SM:
            case NYLO_MAGE_SMALL_SM:
            case NYLO_MAGE_SMALL_AGRO_SM:
            case NYLO_MELEE_SMALL:
            case NYLO_MELEE_SMALL_AGRO:
            case NYLO_RANGE_SMALL:
            case NYLO_RANGE_SMALL_AGRO:
            case NYLO_MAGE_SMALL:
            case NYLO_MAGE_SMALL_AGRO:
            case NYLO_PRINKIPAS_DROPPING:
            case NYLO_PRINKIPAS_MELEE:
            case NYLO_PRINKIPAS_MAGIC:
            case NYLO_PRINKIPAS_RANGE:
                nylosAlive.remove(event.getNpc());
                if (nylosAlive.isEmpty() && NylocasWaveMatcher.getWave().getWave() == 31)
                {
                    cleanupOver();
                }
                break;
            default:
                break;
        }
    }

    /**
     * Handles saving data about boss phases.
     *
     * @param id ID of new form
     */
    public void handleNPCChanged(int id)
    {
        switch (id)
        {
            case NYLO_BOSS_MELEE:
            case NYLO_BOSS_MELEE_HM:
            case NYLO_BOSS_MELEE_SM:
                clog.addLine(MELEE_PHASE, String.valueOf(client.getTickCount() - roomStartTick));
                plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick+1, "Phase");
                break;
            case NYLO_BOSS_MAGE:
            case NYLO_BOSS_MAGE_HM:
            case NYLO_BOSS_MAGE_SM:
                clog.addLine(MAGE_PHASE, String.valueOf(client.getTickCount() - roomStartTick));
                plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick+1, "Phase");
                break;
            case NYLO_BOSS_RANGE:
            case NYLO_BOSS_RANGE_HM:
            case NYLO_BOSS_RANGE_SM:
                clog.addLine(RANGE_PHASE, String.valueOf(client.getTickCount() - roomStartTick));
                plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick+1, "Phase");
                break;
        }
    }

    private void startNylo()
    {
        if (client.getNpcs().stream().anyMatch(p -> Objects.requireNonNull(p.getName()).toLowerCase().contains("nylo")))
        {
            accurateEntry = false;
        } else
        {
            clog.addLine(ACCURATE_NYLO_START);
        }
        clog.addLine(NYLO_PILLAR_SPAWN);
        roomState = WAVES;
        roomStartTick = client.getTickCount();
        active = true;
    }

    private void wave1Spawn()
    {
        instanceReference = client.getTickCount();
        entryTickOffset = (instanceReference - roomStartTick - 4);
    }

    private void wave31Spawn()
    {
        clog.addLine(LAST_WAVE, String.valueOf(client.getTickCount() - roomStartTick));
        roomState = CLEANUP;
        int stalls = (client.getTickCount() - roomStartTick - entryTickOffset - 236) / 4;
        wave31 = client.getTickCount();
        sendTimeMessage("Wave 'Nylocas last wave' complete! Duration: ", wave31 - roomStartTick, " Stalls: ", stalls);
    }

    private void cleanupOver()
    {
        roomState = WAITING_FOR_BOSS;
        lastDead = client.getTickCount();
        clog.addLine(ACCURATE_NYLO_END);
        clog.addLine(LAST_DEAD, String.valueOf(lastDead - roomStartTick));
        sendTimeMessage("Wave 'Nylocas waves and cleanup' complete! Duration: ", lastDead - roomStartTick, lastDead - wave31);
    }

    private void bossSpawned()
    {
        clog.addLine(BOSS_SPAWN, String.valueOf(client.getTickCount() - roomStartTick));
        roomState = BOSS;
        bossSpawn = client.getTickCount() - 2;
        plugin.addDelayedLine(RaidRoom.NYLOCAS, client.getTickCount() - roomStartTick + 2, "Boss Attackable");
        sendTimeMessage("Wave 'Nylocas boss spawn' complete! Duration: ", bossSpawn - roomStartTick, bossSpawn - lastDead);
    }

    private void bossKilled()
    {
        roomState = FINISHED;
        int deathTick = client.getTickCount();
        int offset1 = 4 - ((deathTick - instanceReference) % 4);
        if ((4 - ((deathTick - instanceReference) % 4) == 4))
        {
            offset1 = 0;
        }
        plugin.liveFrame.setRoomFinished(getName(), deathTick - roomStartTick + offset1);
        sendTimeMessage("Wave 'Nylocas boss' complete! Duration: ", deathTick - roomStartTick + offset1, deathTick + offset1 - bossSpawn, false);
        clog.addLine(NYLO_DESPAWNED, String.valueOf(deathTick - roomStartTick + offset1));
        active = false;
		plugin.lastSplits += "Nylocas: " + RoomUtil.time(deathTick+offset1-roomStartTick) + " (" + RoomUtil.time(plugin.currentDurationSum) + ")\n";
		plugin.currentDurationSum += (deathTick+offset1-roomStartTick);
    }
}
