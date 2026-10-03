package com.advancedraidtracker.rooms.tob;

import com.advancedraidtracker.AdvancedRaidTrackerConfig;

import com.advancedraidtracker.utility.RoomState;
import com.advancedraidtracker.constants.RaidRoom;
import com.advancedraidtracker.constants.TobIDs;

import com.advancedraidtracker.utility.RoomUtil;
import com.advancedraidtracker.utility.datautility.DataWriter;
import com.advancedraidtracker.utility.maidenbloodtracking.BloodDamageToBeApplied;
import com.advancedraidtracker.utility.maidenbloodtracking.BloodPositionWrapper;
import com.advancedraidtracker.utility.wrappers.NPCTimeInChunkShell;
import com.advancedraidtracker.utility.wrappers.PlayerHitsWrapper;
import com.google.common.collect.ImmutableMap;
import com.google.errorprone.annotations.Immutable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import com.advancedraidtracker.AdvancedRaidTrackerPlugin;

import java.util.*;
import java.util.stream.Collectors;

import static com.advancedraidtracker.constants.LogID.*;
import static com.advancedraidtracker.constants.TobIDs.*;
import static com.advancedraidtracker.utility.ItemReference.*;

import com.advancedraidtracker.utility.wrappers.MaidenCrab;
import net.runelite.api.kit.KitType;
import net.runelite.client.game.ItemManager;
import net.runelite.http.api.item.ItemEquipmentStats;
import net.runelite.http.api.item.ItemStats;

@Slf4j
public class MaidenHandler extends TOBRoomHandler
{
    public RoomState.MaidenRoomState roomState;

    int p70;
    int p50;
    int p30;
    int maidenDeathTick;
    NPC maidenNPC;
    int bloodHeals = 0;

    public int deferVarbitCheck = -1;
    ArrayList<MaidenCrab> maidenCrabs = new ArrayList<>();
    ArrayList<MaidenCrab> deferredCrabs = new ArrayList<>();

    ArrayList<PlayerHitsWrapper> hitsplatsPerPlayer;
    ArrayList<BloodPositionWrapper> thrownBloodLocations;
    ArrayList<WorldPoint> spawnedBloodLocations;
    ArrayList<Integer> maidenHeals;
    ArrayList<BloodDamageToBeApplied> queuedBloodDamage;

    ArrayList<Player> dinhsers;


    ArrayList<NPCTimeInChunkShell> npcs;


    AdvancedRaidTrackerConfig config;
    AdvancedRaidTrackerPlugin plugin;

    private final ItemManager itemManager;

    public MaidenHandler(Client client, DataWriter clog, AdvancedRaidTrackerConfig config, AdvancedRaidTrackerPlugin plugin, ItemManager itemManager)
    {
        super(client, clog, config, plugin);
        this.plugin = plugin;
        this.itemManager = itemManager;
        roomState = RoomState.MaidenRoomState.NOT_STARTED;
        p70 = -1;
        p50 = -1;
        p30 = -1;
        roomStartTick = -1;
        maidenDeathTick = -1;
        accurateEntry = true;
        hitsplatsPerPlayer = new ArrayList<>();
        thrownBloodLocations = new ArrayList<>();
        spawnedBloodLocations = new ArrayList<>();
        maidenHeals = new ArrayList<>();
        queuedBloodDamage = new ArrayList<>();
        npcs = new ArrayList<>();
        dinhsers = new ArrayList<>();
        this.config = config;
    }

    public boolean isActive()
    {
        return !(roomState == RoomState.MaidenRoomState.NOT_STARTED || roomState == RoomState.MaidenRoomState.FINISHED);
    }

    public String getName()
    {
        return "Maiden";
    }

    public void reset()
    {
        active = false;
        roomState = RoomState.MaidenRoomState.NOT_STARTED;
        accurateEntry = true;
        p70 = -1;
        p50 = -1;
        p30 = -1;
        roomStartTick = -1;
        maidenDeathTick = -1;
        hitsplatsPerPlayer.clear();
        thrownBloodLocations.clear();
        maidenHeals.clear();
        queuedBloodDamage.clear();
        deferredCrabs.clear();
        bloodHeals = 0;
        npcs.clear();
        dinhsers.clear();
        super.reset();
    }

    public void startMaiden()
    {
        active = true;
        roomStartTick = client.getTickCount();
        deferVarbitCheck = roomStartTick + 2;
    }

    public void proc70()
    {
        p70 = super.client.getTickCount();
        roomState = RoomState.MaidenRoomState.PHASE_2;
        if (roomStartTick != -1)
            sendTimeMessage("Wave 'Maiden phase 1' complete! Duration: ", p70 - roomStartTick);
        clog.addLine(MAIDEN_70S, String.valueOf(p70 - roomStartTick));
        plugin.addDelayedLine(RaidRoom.MAIDEN, p70 - roomStartTick, "70s");

    }

    public void proc50()
    {
        p50 = super.client.getTickCount();
        roomState = RoomState.MaidenRoomState.PHASE_3;
        if (roomStartTick != -1)
            sendTimeMessage("Wave 'Maiden phase 2' complete! Duration: ", p50 - roomStartTick, p50 - p70);
        clog.addLine(MAIDEN_50S, String.valueOf(p50 - roomStartTick));
        plugin.addDelayedLine(RaidRoom.MAIDEN, p50 - roomStartTick, "50s");
    }

    public void proc30()
    {
        p30 = super.client.getTickCount();
        roomState = RoomState.MaidenRoomState.PHASE_4;
        if (roomStartTick != -1)
            sendTimeMessage("Wave 'Maiden phase 3' complete! Duration: ", p30 - roomStartTick, p30 - p50);
        clog.addLine(MAIDEN_30S, String.valueOf(p30 - roomStartTick));
        plugin.addDelayedLine(RaidRoom.MAIDEN, p30 - roomStartTick, "30s");
    }

    public void endMaiden()
    {
        active = false;
        roomState = RoomState.MaidenRoomState.FINISHED;
        maidenDeathTick = client.getTickCount() + MAIDEN_DEATH_ANIMATION_LENGTH;
        if (roomStartTick != -1)
            sendTimeMessage("Wave 'Maiden Skip' complete! Duration: ", maidenDeathTick - roomStartTick, maidenDeathTick - p30, false);
        clog.addLine(ACCURATE_MAIDEN_END);
        plugin.addDelayedLine(RaidRoom.MAIDEN, client.getTickCount() - roomStartTick, "Dead");
        plugin.liveFrame.setRoomFinished(getName(), maidenDeathTick - roomStartTick);
		plugin.lastSplits += "Maiden: " + RoomUtil.time(maidenDeathTick-roomStartTick) + "\n";
		plugin.currentDurationSum += (maidenDeathTick-roomStartTick);
    }

    private boolean didAuto = false;

    public void updateAnimationChanged(AnimationChanged event)
    {
        if (event.getActor().getAnimation() == MAIDEN_DEATH_ANIMATION)
        {
            endMaiden();
        } else if (event.getActor().getAnimation() == CHINCHOMPA_THROWN_ANIMATION)
        {
            if (event.getActor() instanceof Player)
            {
                Player player = (Player) event.getActor();
                Actor target = player.getInteracting();
                if (target == null)
                {
                    return;
                }
                int distance = target.getWorldArea().distanceTo(player.getWorldLocation());
                clog.addLine(MAIDEN_CHIN_THROWN, player.getName(), String.valueOf(distance));
                if (distance < 4 || distance > 6)
                {
                    if (config.showMistakesInChat())
                    {
                        client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", player.getName() + " chinned from " + distance + " tiles away.", null, false);
                    }
                }
            }
        }
		else if (event.getActor().getAnimation() == DINHS_BULWARK_ANIMATION)
        {
            dinhsers.add((Player) event.getActor());
        }
		else if (event.getActor().getAnimation() == MAIDEN_AUTO_ANIMATION)
        {
            didAuto = true;
        }
    }

    public int getDrainedStat(Player player) //Assumes berserker/ultor
    {
        if (player == null)
        {
            return NONE;
        }
        int stab = 0;
        int slash = 0;
        int crush = 0;
        int magic = 0;
        int range = 0;
        PlayerComposition pc = player.getPlayerComposition();
        int[] wornItems = {
                pc.getEquipmentId(KitType.HEAD),
                pc.getEquipmentId(KitType.CAPE),
                pc.getEquipmentId(KitType.AMULET),
                pc.getEquipmentId(KitType.WEAPON),
                pc.getEquipmentId(KitType.TORSO),
                pc.getEquipmentId(KitType.SHIELD),
                pc.getEquipmentId(KitType.LEGS),
                pc.getEquipmentId(KitType.HANDS),
                pc.getEquipmentId(KitType.BOOTS)
        };
        for (int item : wornItems)
        {
            ItemStats itemStats = itemManager.getItemStats(item, false);
            if (itemStats != null)
            {
                ItemEquipmentStats itemEquipmentStats = itemStats.getEquipment();
                stab += itemEquipmentStats.getAstab();
                slash += itemEquipmentStats.getAslash();
                crush += itemEquipmentStats.getAcrush();
                magic += itemEquipmentStats.getAmagic();
                range += itemEquipmentStats.getArange();
            }
        }
        if ((stab >= magic && stab >= range) || (slash >= magic && slash >= range) || (crush >= magic && crush >= range))
        {
            return MELEE;
        } else if (magic > range)
        {
            return MAGE;
        } else
        {
            return RANGE;
        }
    }

    public void updateNpcDespawned(NpcDespawned event)
    {
        NPC npc = event.getNpc();
        switch (npc.getId())
        {
            case TobIDs.MAIDEN_P0:
            case TobIDs.MAIDEN_P1:
            case TobIDs.MAIDEN_P2:
            case TobIDs.MAIDEN_P3:
            case TobIDs.MAIDEN_PRE_DEAD:
            case TobIDs.MAIDEN_DEAD:
            case TobIDs.MAIDEN_P0_HM:
            case TobIDs.MAIDEN_P1_HM:
            case TobIDs.MAIDEN_P2_HM:
            case TobIDs.MAIDEN_P3_HM:
            case TobIDs.MAIDEN_PRE_DEAD_HM:
            case TobIDs.MAIDEN_DEAD_HM:
            case TobIDs.MAIDEN_P0_SM:
            case TobIDs.MAIDEN_P1_SM:
            case TobIDs.MAIDEN_P2_SM:
            case TobIDs.MAIDEN_P3_SM:
            case TobIDs.MAIDEN_PRE_DEAD_SM:
            case TobIDs.MAIDEN_DEAD_SM:
                clog.addLine(MAIDEN_DESPAWNED, String.valueOf(client.getTickCount() - roomStartTick));
                break;
            case MAIDEN_MATOMENOS:
            case MAIDEN_MATOMENOS_HM:
            case MAIDEN_MATOMENOS_SM:
                maidenCrabs.removeIf(x -> x.crab.equals(event.getNpc()));
            default:
                break;
        }
    }

    public void updateNpcSpawned(NpcSpawned event)
    {
        NPC npc = event.getNpc();
        boolean story = false;
        switch (npc.getId())
        {
            case MAIDEN_P0_SM:
            case MAIDEN_P1_SM:
            case MAIDEN_P2_SM:
            case MAIDEN_P3_SM:
            case MAIDEN_PRE_DEAD_SM:
            case MAIDEN_DEAD_SM:
                story = true;
                clog.addLine(IS_STORY_MODE);
            case MAIDEN_P0_HM:
            case MAIDEN_P1_HM:
            case MAIDEN_P2_HM:
            case MAIDEN_P3_HM:
            case MAIDEN_PRE_DEAD_HM:
            case MAIDEN_DEAD_HM:
                if (!story)
                    clog.addLine(IS_HARD_MODE);
            case MAIDEN_P0:
            case MAIDEN_P1:
            case MAIDEN_P2:
            case MAIDEN_P3:
            case MAIDEN_DEAD:
                clog.addLine(MAIDEN_SPAWNED);
                maidenNPC = npc;
                startMaiden();
                break;
            case MAIDEN_MATOMENOS:
            case MAIDEN_MATOMENOS_HM:
            case MAIDEN_MATOMENOS_SM:
                String crabName = identifySpawn(npc);
                clog.addLine(ADD_NPC_MAPPING, String.valueOf(npc.getIndex()), crabName, getName());
                plugin.liveFrame.getPanel(getName()).addNPCMapping(npc.getIndex(), crabName);
                plugin.liveFrame.getPanel(getName()).addMaidenCrab(crabName);
                MaidenCrab crab = new MaidenCrab(npc, AdvancedRaidTrackerPlugin.scale, crabName);
                logCrabSpawn(crab.description);
                maidenCrabs.add(crab);
                break;
            case TobIDs.MAIDEN_BLOOD:
            case TobIDs.MAIDEN_BLOOD_HM:
            case TobIDs.MAIDEN_BLOOD_SM:
                clog.addLine(BLOOD_SPAWNED);
                break;
        }
    }

    public void handleNPCChanged(int id)
    {
        switch (id)
        {
            case MAIDEN_P1:
            case MAIDEN_P1_HM:
            case MAIDEN_P1_SM:
                proc70();
                break;
            case MAIDEN_P2:
            case MAIDEN_P2_HM:
            case MAIDEN_P2_SM:
                proc50();
                break;
            case MAIDEN_P3:
            case MAIDEN_P3_HM:
            case MAIDEN_P3_SM:
                proc30();
                break;
            case MAIDEN_DEAD:
            case MAIDEN_DEAD_HM:
            case MAIDEN_DEAD_SM:
                break;
        }
    }

    private void logCrabSpawn(String description)
    {
        clog.addLine(MATOMENOS_SPAWNED, description);
    }

    private void applyBlood()
    {
        for (BloodDamageToBeApplied p : queuedBloodDamage)
        {
            int bloodDamage = -1;
            Optional<PlayerHitsWrapper> hits = hitsplatsPerPlayer.stream().filter(playerHitsWrapper -> playerHitsWrapper.name.equals(p.playerName)).findAny();
            if (hits.isPresent())
            {
                bloodDamage = hits.get().hitsplats.get(hits.get().hitsplats.size() - 1); //Last hitsplat is blood
            }
            if (bloodDamage != -1)
            {
                if (p.bloodTicksAlive == -1)
                {
                    clog.addLine(PLAYER_STOOD_IN_SPAWNED_BLOOD, p.playerName, String.valueOf(bloodDamage)); //player, dmg
                } else
                {
                    clog.addLine(PLAYER_STOOD_IN_THROWN_BLOOD, p.playerName, String.valueOf(bloodDamage), String.valueOf(p.bloodTicksAlive)); //player, dmg, blood tick
                }
                bloodHeals++;
            }
        }
    }

    private void assessBloodForNextTick()
    {
        queuedBloodDamage.clear();
        for (Player p : client.getPlayers())
        {
            for (BloodPositionWrapper blood : thrownBloodLocations)
            {
                if (blood.initialTick <= client.getTickCount() && p.getWorldLocation().distanceTo(blood.location) == 0)
                {
                    queuedBloodDamage.add(new BloodDamageToBeApplied(p.getName(), 10 - (blood.finalTick - client.getTickCount())));
                }
            }
            for (WorldPoint blood : spawnedBloodLocations)
            {
                if (p.getWorldLocation().distanceTo(blood) == 0)
                {
                    queuedBloodDamage.add(new BloodDamageToBeApplied(p.getName(), -1));
                }
            }
        }

        thrownBloodLocations.removeIf(bloodPositionWrapper -> bloodPositionWrapper.finalTick <= client.getTickCount());
    }

    private void handleCrabHeals()
    {
        for (int i = 0; i < maidenHeals.size() - bloodHeals; i++)
        {
            clog.addLine(CRAB_HEALED_MAIDEN, String.valueOf(maidenHeals.get(i)));
        }
        bloodHeals = 0;
    }

    private int findChunk(int x, int y)
    {
        int chunkX = 3 - ((x - 8) / 8);
        int chunkY = 3 - ((y - 16) / 8);
        return (4 * chunkX) + chunkY;
    }

    private void trackNPCMovements()
    {
        ArrayList<NPCTimeInChunkShell> merge = new ArrayList<>();
        for (NPC npc : client.getNpcs())
        {
            if (npcs.stream().noneMatch(o -> o.npc.getIndex() == npc.getIndex()))
            {
                merge.add(new NPCTimeInChunkShell(npc, findChunk(npc.getWorldLocation().getRegionX(), npc.getWorldLocation().getRegionY()), 0));
            } else
            {
                int index = -1;
                for (int i = 0; i < npcs.size(); i++)
                {
                    if (npc.getIndex() == npcs.get(i).npc.getIndex())
                    {
                        index = i;
                    }
                }
                if (index != -1)
                {
                    npcs.get(index).marked = true;
                    if (npcs.get(index).chunk == findChunk(npc.getWorldLocation().getRegionX(), npc.getWorldLocation().getRegionY()))
                    {
                        npcs.get(index).timeInChunk++;
                    } else
                    {
                        npcs.get(index).chunk = findChunk(npc.getWorldLocation().getRegionX(), npc.getWorldLocation().getRegionY());
                        npcs.get(index).timeInChunk = 0;
                    }
                }

            }
        }
        npcs.removeIf(o -> !o.marked);
        for (NPCTimeInChunkShell npc : npcs)
        {
            npc.marked = false;
        }
        npcs.addAll(merge);
    }

    /**
     * Dinhs spec targets are picked in order of chunk relative to the player where they use the spec at, this grid:
     * 7 4 1
     * 8 5 2
     * 9 6 3
     * is always centered around the player being in chunk 5, and the check starts in chunk 1, then moves to chunk 2, all the way to 9
     * if it will cross over 9 NPCs while checking a chunk, the NPCs are chosen on a last in first selected basis based on when
     * that NPC entered that chunk. The NPC must also be in a 11x11 area centered around the player to be targeted.
     * <p>
     * If multiple NPCs enter the chunk on the same tick, they are chosen by lowest NPC index first
     **/
    private void analyzeDinhs()
    {
        for (Player p : dinhsers)
        {
            ArrayList<NPC> targets = new ArrayList<>();
            if (p.getInteracting() instanceof NPC)
            {
                NPC primaryTarget = (NPC) p.getInteracting();
                int primaryIndex = primaryTarget.getIndex();
                int centerX = p.getWorldLocation().getRegionX();
                int centerY = p.getWorldLocation().getRegionY();
                int centerChunk = findChunk(centerX, centerY);
                int minChunk = centerChunk - 4;
                int maxChunk = centerChunk + 4;
                for (int i = minChunk; i <= maxChunk; i++)
                {
                    ArrayList<NPCTimeInChunkShell> potentialTargets = new ArrayList<>();
                    int maxToInclude = 9 - targets.size();
                    if (maxToInclude > 0)
                    {
                        for (NPCTimeInChunkShell npc : npcs)
                        {
                            if (npc.chunk == i)
                            { //For some reason Maiden is NEVER targeted by the additional dinhs hitsplat. Related to large non-moving NPCs
                                if (!Objects.requireNonNull(npc.npc.getName()).contains("Maiden") && !npc.npc.getName().contains("null") && npc.npc.getHealthRatio() != 0)
                                {
                                    if (npc.npc.getWorldLocation().getRegionX() <= centerX + 5 &&
                                            npc.npc.getWorldLocation().getRegionX() >= centerX - 5 &&
                                            npc.npc.getWorldLocation().getRegionY() <= centerY + 5 &&
                                            npc.npc.getWorldLocation().getRegionY() >= centerY - 5)
                                    {
                                        potentialTargets.add(npc);
                                    }
                                }
                            }
                        }
                        if (potentialTargets.size() > maxToInclude) //DO NOT REPLACE SORT METHODS WITH LIST.SORT OR WILL HAVE UNDEFINED RESULTS
                        {
                            Collections.sort(potentialTargets, Comparator.comparing(NPCTimeInChunkShell::getIndex));
                            Collections.sort(potentialTargets, Comparator.comparing(NPCTimeInChunkShell::getTimeInChunk));
                            for (int j = 0; j < maxToInclude; j++)
                            {
                                targets.add(potentialTargets.get(j).npc);
                            }

                        } else
                        {
                            for (NPCTimeInChunkShell npc : potentialTargets)
                            {
                                targets.add(npc.npc);
                            }
                        }
                    }
                }
                String whichCrab = "";
                int primaryHP = 0;
                boolean didDoubleHit = false;
                for (MaidenCrab crab : maidenCrabs)
                {
                    if (primaryTarget.getIndex() == crab.crab.getIndex())
                    {
                        whichCrab = crab.description;
                        primaryHP = crab.health;
                    }
                }
                String value3 = primaryTarget.getName() + "(" + whichCrab + ")" + ":" + primaryHP;
                StringBuilder value4 = new StringBuilder();
                if (targets.size() < 9)
                {
                    if (config.showMistakesInChat())
                    {
                        client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", p.getName() + " only targeted " + targets.size() + " additional NPCs with dinhs spec.", null, false);
                    }
                }
                ArrayList<Integer> healths = new ArrayList<>();
                for (NPC npc : targets)
                {
                    if (npc.getIndex() == primaryIndex)
                    {
                        didDoubleHit = true;
                    }
                    String additionalDescription = "^";
                    int hp = -1;
                    for (MaidenCrab crab : maidenCrabs)
                    {
                        if (crab.crab.getIndex() == npc.getIndex())
                        {
                            additionalDescription = crab.description;
                            hp = crab.health;
                            healths.add(hp);
                        }
                    }
                    value4.append(npc.getName()).append("~").append(additionalDescription).append("~").append(hp).append(":");
                }
                String value5 = getTargetsBelow27(healths, targets, didDoubleHit);
                clog.addLine(MAIDEN_DINHS_SPEC, p.getName(), value3, value4.toString(), value5);
            }
        }
        dinhsers.clear();
    }

    private static String getTargetsBelow27(ArrayList<Integer> healths, ArrayList<NPC> targets, boolean didDoubleHit)
    {
        double total = 0;
        double count = healths.size();
        int belowThreshold = 0;
        for (Integer i : healths)
        {
            total += i;
            if (i < 27)
            {
                belowThreshold++;
            }
        }
        double average = total / healths.size();
        return ((int) average) + ":" + belowThreshold + ":" + ((int) count) + ":" + targets.size() + ":" + didDoubleHit;
    }

    public void updateGameTick(GameTick event)
    {
        trackNPCMovements();
        analyzeDinhs();

        applyBlood();
        handleCrabHeals();
        assessBloodForNextTick();
        hitsplatsPerPlayer.clear();
        maidenHeals.clear();
        if (didAuto)
        {
            Actor drained = maidenNPC.getInteracting();
            if (drained instanceof Player)
            {
                clog.addLine(MAIDEN_AUTO, drained.getName(), String.valueOf(client.getTickCount() - roomStartTick));
                int statDrained = getDrainedStat((Player) drained);
                if (statDrained == MELEE)
                {
                    if (config.showMistakesInChat())
                    {
                        plugin.sendChatMessage(drained.getName() + " was targeted by Maiden with melee as their highest bonus.");
                    }
                    clog.addLine(MAIDEN_PLAYER_DRAINED, drained.getName(), String.valueOf((client.getTickCount() - roomStartTick)));
                }
            }
            didAuto = false;
        }

        for (MaidenCrab crab : deferredCrabs)
        {
            clog.addLine(CRAB_LEAK, crab.description, String.valueOf(crab.health));
        }
        maidenCrabs.removeAll(deferredCrabs);
        deferredCrabs.clear();

        if (client.getTickCount() == deferVarbitCheck)
        {
            deferVarbitCheck = -1;
            if (client.getVarbitValue(HP_VARBIT) != FULL_HP)
            {
                accurateEntry = false;
            } else
            {
                accurateEntry = true;
                roomState = RoomState.MaidenRoomState.PHASE_1;
                clog.addLine(ACCURATE_MAIDEN_START);
            }
        }
        for (MaidenCrab crab : maidenCrabs)
        {
            int distance = crab.crab.getWorldArea().distanceTo2D(maidenNPC.getWorldArea());
            if (distance == 1 && crab.health > 0)
            {
                deferredCrabs.add(crab);
            }
        }
        super.updateGameTick(event);
    }

    /**
     * Tracks crab hps
     */
    public void updateHitsplatApplied(HitsplatApplied event)
    {
        if (event.getActor() instanceof NPC) //getHealthRatio doesn't give fine enough increments so we manually track the HP
        {
            if (maidenCrabs.stream().map(x -> x.crab).collect(Collectors.toList()).contains((NPC) event.getActor()))
            {
                MaidenCrab crab = maidenCrabs.stream().filter(x -> x.crab.equals(event.getActor())).collect(Collectors.toList()).get(0);
                crab.health -= event.getHitsplat().getAmount();
            }
        }
        if (event.getActor() instanceof Player) //Heal tracking
        {
            if (hitsplatsPerPlayer.stream().noneMatch(playerHPWrapper -> playerHPWrapper.name.equals(event.getActor().getName())))
            {
                hitsplatsPerPlayer.add(new PlayerHitsWrapper(event.getActor().getName(), event.getHitsplat().getAmount()));
            } else
            {
                for (PlayerHitsWrapper playerHitsWrapper : hitsplatsPerPlayer)
                {
                    if (playerHitsWrapper.name.equals(event.getActor().getName()))
                    {
                        playerHitsWrapper.hitsplats.add(event.getHitsplat().getAmount());
                    }
                }
            }
        } else if (event.getActor().getName() != null && event.getActor().getName().contains("Maiden"))
        {
            if (event.getHitsplat().getHitsplatType() == HitsplatID.HEAL)
            {
                maidenHeals.add(event.getHitsplat().getAmount());
            }
        }
        super.updateHitsplatApplied(event);
    }

    public void updateGameObjectSpawned(GameObjectSpawned event)
    {
        if (event.getGameObject().getId() == BLOOD_ON_GROUND)
        {
            spawnedBloodLocations.add(event.getGameObject().getWorldLocation());
        }
    }

    public void updateGameObjectDespawned(GameObjectDespawned event)
    {
        if (event.getGameObject().getId() == BLOOD_ON_GROUND)
        {
            spawnedBloodLocations.removeIf(worldPoint -> worldPoint.getRegionX() == event.getGameObject().getWorldLocation().getRegionX() &&
                    worldPoint.getRegionY() == event.getGameObject().getWorldLocation().getRegionY());

        }
    }

    public void updateGraphicsObjectCreated(GraphicsObjectCreated event)
    {
        if (event.getGraphicsObject().getId() == MAIDEN_THROWN_BLOOD_GRAPHIC_OBJECT)
        {
            thrownBloodLocations.add(new BloodPositionWrapper(WorldPoint.fromLocal(client, event.getGraphicsObject().getLocation()), ((((event.getGraphicsObject().getStartCycle() - client.getGameCycle() + 1) / 30)) + client.getTickCount() - 1)));
        }
    }

    private static final Map<Point, String> maidenCrabSpawns = ImmutableMap.<Point, String>builder()
            .put(new Point(21, 40), "N1")
            .put(new Point(22, 41), "sN1")
            .put(new Point(25, 40), "N2")
            .put(new Point(26, 41), "sN2")
            .put(new Point(29, 40), "N3")
            .put(new Point(30, 41), "sN3")
            .put(new Point(33, 40), "N4 (1)")
            .put(new Point(34, 41), "sN4 (1)")
            .put(new Point(33, 38), "N4 (2)")
            .put(new Point(34, 39), "sN4 (2)")
            .put(new Point(21, 20), "S1")
            .put(new Point(22, 19), "sS1")
            .put(new Point(25, 20), "S2")
            .put(new Point(26, 19), "sS2")
            .put(new Point(29, 20), "S3")
            .put(new Point(30, 19), "sS3")
            .put(new Point(33, 20), "S4 (1)")
            .put(new Point(34, 19), "sS4 (1)")
            .put(new Point(33, 22), "S4 (2)")
            .put(new Point(34, 21), "sS4 (2)")
            .build();

    /**
     * Returns a string describing the spawn position of a maiden crab
     */
    private String identifySpawn(NPC npc)
    {
        int x = npc.getWorldLocation().getRegionX();
        int y = npc.getWorldLocation().getRegionY();
        Point p = new Point(x, y);
        String proc = getProc();
        if (maidenCrabSpawns.containsKey(p))
        {
            String crabSpawn = maidenCrabSpawns.get(p);
            if (crabSpawn.startsWith("s"))
            {
                clog.addLine(MAIDEN_SCUFFED, crabSpawn);
            }
            return maidenCrabSpawns.get(p) + proc;
        }
        return "Unknown";
    }

    private String getProc()
    {
        String proc = "";
        if (maidenNPC.getId() == MAIDEN_P1 || maidenNPC.getId() == MAIDEN_P1_HM || maidenNPC.getId() == MAIDEN_P1_SM)
        {
            proc = " 70s";
        } else if (maidenNPC.getId() == MAIDEN_P2 || maidenNPC.getId() == MAIDEN_P2_HM || maidenNPC.getId() == MAIDEN_P2_SM)
        {
            proc = " 50s";
        } else if (maidenNPC.getId() == MAIDEN_P3 || maidenNPC.getId() == MAIDEN_P3_HM || maidenNPC.getId() == MAIDEN_P3_SM)
        {
            proc = " 30s";
        }
        return proc;
    }

}
