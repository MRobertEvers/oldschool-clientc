package com.advancedraidtracker.rooms.tob;

import com.advancedraidtracker.AdvancedRaidTrackerConfig;
import com.advancedraidtracker.AdvancedRaidTrackerPlugin;
import com.advancedraidtracker.constants.RaidRoom;
import com.advancedraidtracker.constants.TobIDs;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.events.*;
import com.advancedraidtracker.utility.Point;
import com.advancedraidtracker.utility.RoomUtil;
import com.advancedraidtracker.utility.datautility.DataWriter;
import com.advancedraidtracker.utility.RoomState;

import static com.advancedraidtracker.constants.LogID.*;
import static com.advancedraidtracker.constants.LogID.ACCURATE_XARP_END;
import static com.advancedraidtracker.constants.LogID.XARPUS_STARTED;
import static com.advancedraidtracker.constants.TobIDs.*;
import static com.advancedraidtracker.utility.RoomState.XarpusRoomState.FINISHED;

@Slf4j
public class XarpusHandler extends TOBRoomHandler
{
    public RoomState.XarpusRoomState roomState = RoomState.XarpusRoomState.NOT_STARTED;
    private final AdvancedRaidTrackerPlugin plugin;

    public XarpusHandler(Client client, DataWriter clog, AdvancedRaidTrackerConfig config, AdvancedRaidTrackerPlugin plugin)
    {
        super(client, clog, config, plugin);
        this.plugin = plugin;
    }

    private int xarpusEntryTick = -1;
    private int xarpusExhumedsEnd = -1;
    private int xarpusScreechTick = -1;
    private int xarpusEndTick = -1;

    public String getName()
    {
        return "Xarpus";
    }

    public boolean isActive()
    {
        return !(roomState == RoomState.XarpusRoomState.NOT_STARTED || roomState == FINISHED);
    }

    public void reset()
    {
        super.reset();
        xarpusEntryTick = -1;
        xarpusExhumedsEnd = -1;
        xarpusScreechTick = -1;
        xarpusEndTick = -1;
        roomState = RoomState.XarpusRoomState.NOT_STARTED;
    }

    public void updateAnimationChanged(AnimationChanged event)
    {
        if (event.getActor().getAnimation() == XARPUS_AWAKENS)
        {
            endExhumeds();
        }
        if (event.getActor().getAnimation() == XARPUS_DEATH_ANIMATION)
        {
            endXarpus();
        }
    }

    public void updateProjectileMoved(ProjectileMoved event)
    {
        if (event.getProjectile().getEndCycle() - event.getProjectile().getStartCycle() == event.getProjectile().getRemainingCycles())
        {
            if (event.getProjectile().getId() == XARPUS_EXHUMED_PROJECTILE)
            {
                clog.addLine(XARPUS_HEAL);
            }
        }
    }

    public void updateOverheadText(OverheadTextChanged event)
    {
        if (event.getActor() instanceof NPC)
        {
            NPC npc = (NPC) event.getActor();
            int id = npc.getId();
            if (id == XARPUS_P23 || id == XARPUS_P23_HM || id == XARPUS_P23_SM)
            {
                startScreech();
            }
        }
    }

    public void updateNpcDespawned(NpcDespawned event)
    {
        switch (event.getNpc().getId())
        {
            case XARPUS_INACTIVE:
            case XARPUS_P1:
            case XARPUS_P23:
            case XARPUS_DEAD:
            case XARPUS_INACTIVE_HM:
            case XARPUS_P1_HM:
            case XARPUS_P23_HM:
            case XARPUS_DEAD_HM:
            case XARPUS_INACTIVE_SM:
            case XARPUS_P1_SM:
            case XARPUS_P23_SM:
            case XARPUS_DEAD_SM:
                clog.addLine(XARPUS_DESPAWNED, String.valueOf(xarpusEndTick - xarpusEntryTick));
        }
    }

    public void updateNpcSpawned(NpcSpawned event)
    {
        boolean story = false;
        switch (event.getNpc().getId())
        {
            case TobIDs.XARPUS_INACTIVE_SM:
            case TobIDs.XARPUS_P1_SM:
            case TobIDs.XARPUS_P23_SM:
            case TobIDs.XARPUS_DEAD_SM:
                story = true;
                clog.addLine(IS_STORY_MODE);
            case TobIDs.XARPUS_INACTIVE_HM:
            case TobIDs.XARPUS_P1_HM:
            case TobIDs.XARPUS_P23_HM:
            case TobIDs.XARPUS_DEAD_HM:
                if (!story)
                    clog.addLine(IS_HARD_MODE);
            case TobIDs.XARPUS_INACTIVE:
            case TobIDs.XARPUS_P1:
            case TobIDs.XARPUS_P23:
            case TobIDs.XARPUS_DEAD:
                //clog.addLine(XARPUS_SPAWNED);
                break;
        }
    }

    public void updateGameTick(GameTick event)
    {
        if (roomState == RoomState.XarpusRoomState.NOT_STARTED)
        { //Xarpus timer starts when player crosses into region
            if (RoomUtil.crossedLine(XARPUS_REGION, new Point(25, 12), new Point(27, 12), false, client))
            {
                startXarpus();
            }
        }
        if (xarpusScreechTick != -1 && xarpusScreechTick != 0 && client.getTickCount() != xarpusScreechTick && (client.getTickCount() - xarpusScreechTick) % 8 == 0 && isActive())
        {
            plugin.addDelayedLine(RaidRoom.XARPUS, client.getTickCount() - xarpusEntryTick, "Turn");
        }
        super.updateGameTick(event);
    }

    private void startScreech()
    {
        clog.addLine(XARPUS_SCREECH, String.valueOf(client.getTickCount() - xarpusEntryTick));
        roomState = RoomState.XarpusRoomState.POSTSCREECH;
        xarpusScreechTick = client.getTickCount();
        String splitMessage = "Wave 'Xarpus phase 2' complete. Duration: " + timeColor() + RoomUtil.time(xarpusScreechTick - xarpusEntryTick) + " (" + RoomUtil.time(xarpusScreechTick - xarpusExhumedsEnd) + ")";
        this.client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", splitMessage, null, false);
        plugin.addDelayedLine(RaidRoom.XARPUS, xarpusScreechTick - xarpusEntryTick, "SCREECH");
    }

    private void startXarpus()
    {
        roomState = RoomState.XarpusRoomState.EXHUMEDS;
        xarpusEntryTick = client.getTickCount();
        roomStartTick = client.getTickCount();
        clog.addLine(XARPUS_STARTED);
        clog.addLine(ACCURATE_XARP_START);
        active = true;
    }

    private void endExhumeds()
    {
        roomState = RoomState.XarpusRoomState.PRESCREECH;
        xarpusExhumedsEnd = client.getTickCount();
        String splitMessage = "Wave 'Xarpus phase 1' complete. Duration: " + timeColor() + RoomUtil.time(xarpusExhumedsEnd - xarpusEntryTick);
        this.client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", splitMessage, null, false);
        plugin.addDelayedLine(RaidRoom.XARPUS, client.getTickCount() - xarpusEntryTick + 1, "Exhumeds End");
    }

    private void endXarpus()
    {

        roomState = FINISHED;
        xarpusEndTick = client.getTickCount() + XARPUS_DEATH_ANIMATION_LENGTH;
        clog.addLine(ACCURATE_XARP_END);
        plugin.liveFrame.setRoomFinished(getName(), xarpusEndTick - xarpusEntryTick);
        String splitMessage = "Wave 'Xarpus phase 3' complete. Duration: " + timeColor() + RoomUtil.time(xarpusEndTick - xarpusEntryTick) + " (" + RoomUtil.time(xarpusEndTick - xarpusScreechTick) + ")";
        this.client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", splitMessage, null, false);
        active = false;
		plugin.lastSplits += "Xarpus: " + RoomUtil.time(xarpusEndTick-roomStartTick) + " (" + RoomUtil.time(plugin.currentDurationSum) + ")\n";
		plugin.currentDurationSum += (xarpusEndTick-roomStartTick);
    }
}
