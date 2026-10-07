package com.advancedraidtracker.rooms.tob;

import com.advancedraidtracker.AdvancedRaidTrackerConfig;
import com.advancedraidtracker.AdvancedRaidTrackerPlugin;
import com.advancedraidtracker.constants.RaidRoom;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import com.advancedraidtracker.utility.Point;
import com.advancedraidtracker.utility.RoomUtil;
import com.advancedraidtracker.utility.datautility.DataWriter;

import java.util.ArrayList;

import static com.advancedraidtracker.constants.LogID.*;
import static com.advancedraidtracker.constants.TobIDs.*;
import static com.advancedraidtracker.utility.RoomState.*;
import static com.advancedraidtracker.utility.RoomState.BloatRoomState.*;

@Slf4j
public class BloatHandler extends TOBRoomHandler
{
    public BloatRoomState roomState;

    private final ArrayList<Integer> walks = new ArrayList<>();
    private final ArrayList<Integer> downs = new ArrayList<>();
    private int bloatDeferTick = -1;
    private int bloatDeathTick = -1;
    private final AdvancedRaidTrackerPlugin plugin;

    public BloatHandler(Client client, DataWriter clog, AdvancedRaidTrackerConfig config, AdvancedRaidTrackerPlugin plugin)
    {
        super(client, clog, config, plugin);
        this.plugin = plugin;
        roomState = NOT_STARTED;
    }

    public boolean isActive()
    {
        return !(roomState == NOT_STARTED || roomState == FINISHED);
    }

    public String getName()
    {
        return "Bloat";
    }

    public void reset()
    {
        roomState = NOT_STARTED;
        bloatDeferTick = -1;
        accurateEntry = true;
        roomStartTick = -1;
        bloatDeathTick = -1;
        walks.clear();
        downs.clear();
        active = false;
        super.reset();
    }

    public void endBloat()
    {
        roomState = FINISHED;
        bloatDeathTick = client.getTickCount() + BLOAT_DEATH_ANIMATION_LENGTH;
        plugin.addDelayedLine(RaidRoom.BLOAT, client.getTickCount() - roomStartTick, "Dead");
        clog.addLine(ACCURATE_BLOAT_END);
        plugin.liveFrame.setRoomFinished(getName(), bloatDeathTick - roomStartTick);
        if (roomStartTick != -1)
        {
            sendTimeMessage("Wave 'Bloat last down' complete! Duration: ", splitLastDown(), " Room time: ", bloatDeathTick - roomStartTick, true);
        }
        active = false;
		plugin.lastSplits += "Bloat: " + RoomUtil.time(bloatDeathTick-roomStartTick) + " (" + RoomUtil.time(plugin.currentDurationSum) + ")\n";
		plugin.currentDurationSum += (bloatDeathTick-roomStartTick);
    }

    public int splitLastDown()
    {
        if (bloatDeathTick != -1)
        {
            if (walks.size() == downs.size() + 1)
            {
                return bloatDeathTick - walks.get(walks.size() - 1);
            } else if (walks.size() == downs.size())
            {
                return bloatDeathTick - downs.get(walks.size() - 1);
            } else
            {
                return -1;
            }
        } else
        {
            return -1;
        }
    }

    public void start()
    {
        roomStartTick = client.getTickCount();
        //clog.addLine(BLOAT_STARTED, client.getTickCount()); //todo why isnt this needed anymore?
        roomState = WALKING;
        active = true;
    }

    private int getLastWalk()
    {
        if (!downs.isEmpty() && !walks.isEmpty() && downs.size() == walks.size())
        {
            return downs.get(downs.size() - 1) - walks.get(walks.size() - 1);
        } else
        {
            return -1;
        }
    }

    private int getLastDownTime()
    {
        if (!downs.isEmpty())
        {
            return downs.get(downs.size() - 1) - roomStartTick;
        } else
        {
            return -1;
        }
    }

    private double deferHP = -1;

    public void down()
    {
        clog.addLine(BLOAT_DOWN, String.valueOf(client.getTickCount() - roomStartTick));
        if (downs.isEmpty())
        {
            int currentBloatHP = client.getVarbitValue(HP_VARBIT);
            clog.addLine(BLOAT_HP_1ST_DOWN, String.valueOf(currentBloatHP));
        }
        downs.add(client.getTickCount());
        roomState = DOWN;
        if (roomStartTick != -1)
        {
            deferHP = client.getVarbitValue(HP_VARBIT) / 10.0;
            bloatDeferTick = client.getTickCount() + 5; //delay so that the chat message can't be used to know immediately know when bloat has gone down
        }
        plugin.addDelayedLine(RaidRoom.BLOAT, client.getTickCount() - roomStartTick, "Down");
    }

    public void walk()
    {
        walks.add(client.getTickCount());
        plugin.addDelayedLine(RaidRoom.BLOAT, client.getTickCount() - roomStartTick, "Moving");
        roomState = WALKING;
    }

    public void updateGameTick(GameTick event)
    {
        if (bloatDeferTick != -1 && bloatDeferTick == client.getTickCount())
        {
            sendTimeMessage("Wave 'Bloat walk' complete! Duration: ", getLastWalk(), getLastDownTime(), true, ", HP: " + deferHP + "%");
            bloatDeferTick = -1;
        }
        if (roomStartTick == -1)
        { //room time starts when player enters either gate in the bloat region
            if (RoomUtil.crossedLine(BLOAT_REGION, new Point(39, 30), new Point(39, 33), true, client)
                    || RoomUtil.crossedLine(BLOAT_REGION, new Point(24, 30), new Point(24, 33), true, client))
            {
                start();
                walk();
            }
        }

        if (NyloHandler.instanceStart == -1)
        { //Nylo instance timer is started when the first player crosses the unreachable line by the bloat chest
            if (RoomUtil.crossedLine(BLOAT_REGION, new Point(4, 31), new Point(4, 32), true, client))
            {
                NyloHandler.instanceStart = client.getTickCount();
            }
        }
        super.updateGameTick(event);
    }

    public void updateAnimationChanged(AnimationChanged event)
    {
        if (event.getActor().getAnimation() == BLOAT_DOWN_ANIMATION)
        {
            down();
        } else if (event.getActor().getName() != null && event.getActor().getAnimation() == -1 && event.getActor().getName().contains("Bloat"))
        {
            walk();
        }
        if (event.getActor().getAnimation() == BLOAT_DEATH_ANIMATION)
        {
            endBloat();
        }
        if (event.getActor().getAnimation() == SCYTHE_ANIMATION)
        {
            if (event.getActor() instanceof Player)
            {
                Player p = (Player) event.getActor();
                clog.addLine(BLOAT_SCYTHE_1ST_WALK, p.getName(), String.valueOf(client.getTickCount() - roomStartTick));
            }
        }
    }

    public void updateNpcSpawned(NpcSpawned event)
    {
        boolean story = false;
        switch (event.getNpc().getId())
        {
            case BLOAT_SM:
                story = true;
                clog.addLine(IS_STORY_MODE);
            case BLOAT_HM:
                if (!story)
                    clog.addLine(IS_HARD_MODE);
            case BLOAT:
                clog.addLine(BLOAT_SPAWNED);
                if (client.getVarbitValue(ROOM_ACTIVE_VARBIT) != 0)
                {
                    accurateEntry = false;
                } else
                {
                    clog.addLine(ACCURATE_BLOAT_START);
                }
                break;
        }
    }

    public void updateNpcDespawned(NpcDespawned event)
    {
        int id = event.getNpc().getId();
        if (id == BLOAT || id == BLOAT_HM || id == BLOAT_SM)
        {
            clog.addLine(BLOAT_DESPAWN, String.valueOf(client.getTickCount() - roomStartTick));
        }
    }

}
