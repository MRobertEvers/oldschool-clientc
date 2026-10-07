package com.advancedraidtracker.rooms.tob;

import com.advancedraidtracker.AdvancedRaidTrackerConfig;
import com.advancedraidtracker.AdvancedRaidTrackerPlugin;
import com.advancedraidtracker.constants.LogID;
import com.advancedraidtracker.constants.RaidRoom;
import com.advancedraidtracker.utility.Point;
import com.advancedraidtracker.utility.RoomUtil;
import com.advancedraidtracker.utility.datautility.DataWriter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import com.advancedraidtracker.utility.RoomState;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import static com.advancedraidtracker.constants.TobIDs.*;

@Slf4j
public class SotetsegHandler extends TOBRoomHandler
{
    public RoomState.SotetsegRoomState roomState = RoomState.SotetsegRoomState.NOT_STARTED;
    private int soteFirstMazeStart = -1;
    private int soteSecondMazeStart = -1;
    private int soteFirstMazeEnd = -1;
    private int soteSecondMazeEnd = -1;
    private int soteDeathTick = -1;
    private int deferTick = -1;
    private int lastRegion = -1;
    private final AdvancedRaidTrackerPlugin plugin;
    private boolean hasSteppedOnMaze = false;
    private String firstMazeChosen = "";
    private String secondMazeChosen = "";
    private String lastChosen = "";
    private final Map<String, ArrayList<Point>> playerTiles = new HashMap<>();

    private ArrayList<Point> excludedTiles = new ArrayList<>();
    private final AdvancedRaidTrackerConfig config;

    public SotetsegHandler(Client client, DataWriter clog, AdvancedRaidTrackerConfig config, AdvancedRaidTrackerPlugin plugin)
    {
        super(client, clog, config, plugin);
        this.plugin = plugin;
        this.config = config;
    }

    public boolean isActive()
    {
        return !(roomState == RoomState.SotetsegRoomState.NOT_STARTED || roomState == RoomState.SotetsegRoomState.FINISHED);
    }

    public String getName()
    {
        return "Sotetseg";
    }

    public void reset()
    {
        super.reset();
        playerTiles.clear();
        lastChosen = "";
        excludedTiles.clear();
        accurateTimer = true;
        roomStartTick = -1;
        roomState = RoomState.SotetsegRoomState.NOT_STARTED;
        soteFirstMazeStart = -1;
        soteSecondMazeStart = -1;
        soteFirstMazeEnd = -1;
        soteSecondMazeEnd = -1;
        soteDeathTick = -1;
        lastRegion = -1;
        hasSteppedOnMaze = false;
        firstMazeChosen = "";
        secondMazeChosen = "";
    }

    public void updateNpcSpawned(NpcSpawned event)
    {
        int id = event.getNpc().getId();
        if (id == SOTETSEG_ACTIVE || id == SOTETSEG_ACTIVE_HM || id == SOTETSEG_ACTIVE_SM)
        {
            if (lastRegion == SOTETSEG_UNDERWORLD)
            {
                if (roomState == RoomState.SotetsegRoomState.MAZE_1)
                {
                    endFirstMaze();
                } else if (roomState == RoomState.SotetsegRoomState.MAZE_2)
                {
                    endSecondMaze();
                }
            }
        }
    }

    private ArrayList<Point> currentMaze = new ArrayList<>();

    public void updateGroundObjectSpawned(GroundObjectSpawned event)
    {
        if (!hasSteppedOnMaze)
        {
            if (event.getGroundObject().getId() == SOTETSEG_RED_TILE && lastRegion == SOTETSEG_OVERWORLD)
            {
                hasSteppedOnMaze = true;
                int ticksSinceLastProc = -1;
                if (roomState == RoomState.SotetsegRoomState.MAZE_1)
                {
                    ticksSinceLastProc = client.getTickCount() - soteFirstMazeStart;
                } else
                {
                    if (roomState == RoomState.SotetsegRoomState.MAZE_2)
                    {
                        ticksSinceLastProc = client.getTickCount() - soteSecondMazeStart;
                    }
                }
                if (ticksSinceLastProc != -1 && lastRegion == SOTETSEG_OVERWORLD)
                {
                    int distance = Math.abs(event.getGroundObject().getWorldLocation().getRegionX() - 15);
                    int stallDuration = 5;
                    int tickOffset = 1 + stallDuration + (int) ((distance - 0.5) / 2.0);
                    if (ticksSinceLastProc > tickOffset)
                    {
                        String mazeRunner = (roomState == RoomState.SotetsegRoomState.MAZE_1) ? firstMazeChosen : secondMazeChosen;
                        plugin.sendChatMessage(mazeRunner + " was late to first tile by " + (ticksSinceLastProc - tickOffset) + " ticks");
                    }
                }
            }
        }
        if (event.getGroundObject().getId() == SOTETSEG_RED_TILE && lastRegion == SOTETSEG_OVERWORLD)
        {
            int currentTileX = event.getGroundObject().getWorldLocation().getRegionX();
            int currentTileY = event.getGroundObject().getWorldLocation().getRegionY();
            currentMaze.add(new Point(currentTileX, currentTileY));
        }
    }

    private static ArrayList<Point> removeDuplicatePoints(ArrayList<Point> tiles)
    {
        ArrayList<Point> newTiles = new ArrayList<>();
        Point old = null;
        for (Point p : tiles)
        {
            if (old != null)
            {
                if (!(p.getX() == old.getX() && p.getY() == old.getY()))
                {
                    newTiles.add(p);
                }
            } else
            {
                newTiles.add(p);
            }
            old = p;
        }
        return newTiles;
    }

    private ArrayList<Point> addEveryTileBetween(ArrayList<Point> tiles)
    {
        ArrayList<Point> allTiles = new ArrayList<>();
        Point previous = null;
        for (Point p : tiles)
        {
            if (previous != null)
            {
                allTiles.addAll(everyTileBetween(previous, p));
            }
            previous = p;
        }
        return removeDuplicatePoints(allTiles);
    }

    public static ArrayList<Point> everyTileBetween(Point start, Point end)
    {
        if (end.getY() < start.getY())
        {
            return new ArrayList<>();
        }
        ArrayList<Point> crossTiles = new ArrayList<>();
        crossTiles.add(start);
        while (Math.abs(end.getX() - crossTiles.get(crossTiles.size() - 1).getX()) != end.getY() - crossTiles.get(crossTiles.size() - 1).getY())
        {
            if (crossTiles.size() > 1000) //something has gone terribly wrong and the data provided was not valid, client lag? bail out
            {
                return new ArrayList<>();
            }
            if (Math.abs(end.getX() - start.getX()) > end.getY() - start.getY())
            {
                int offset = (end.getX() - start.getX() > 0) ? 1 : -1;
                crossTiles.add(new Point(crossTiles.get(crossTiles.size() - 1).getX() + offset, crossTiles.get(crossTiles.size() - 1).getY()));
            } else if (end.getY() - start.getY() > Math.abs(end.getX() - start.getX()))
            {
                crossTiles.add(new Point(crossTiles.get(crossTiles.size() - 1).getX(), crossTiles.get(crossTiles.size() - 1).getY() + 1));
            }
        }
        while (end.getX() != crossTiles.get(crossTiles.size() - 1).getX() && end.getY() != crossTiles.get(crossTiles.size() - 1).getY())
        {
            int offset = (end.getX() - crossTiles.get(crossTiles.size() - 1).getX() > 0) ? 1 : -1;
            crossTiles.add(new Point(crossTiles.get(crossTiles.size() - 1).getX() + offset, crossTiles.get(crossTiles.size() - 1).getY() + 1));
        }
        return crossTiles;
    }

    public void updateAnimationChanged(AnimationChanged event)
    {
        if (event.getActor().getAnimation() == SOTETSEG_DEATH_ANIMATION)
        {
            endSotetseg();
        }
    }

    public void startSotetseg()
    {
        roomStartTick = client.getTickCount();
        deferTick = roomStartTick + 2;
        roomState = RoomState.SotetsegRoomState.PHASE_1;
        clog.addLine(LogID.SOTETSEG_STARTED);
        active = true;
    }

    public void endSotetseg()
    {
        active = false;
        plugin.addDelayedLine(RaidRoom.SOTETSEG, client.getTickCount() - roomStartTick, "Dead");
        soteDeathTick = client.getTickCount() + SOTETSEG_DEATH_ANIMATION_LENGTH;
        roomState = RoomState.SotetsegRoomState.FINISHED;
        clog.addLine(LogID.ACCURATE_SOTE_END);
        clog.addLine(LogID.SOTETSEG_ENDED, String.valueOf(soteDeathTick - roomStartTick));
        plugin.liveFrame.setRoomFinished(getName(), soteDeathTick - roomStartTick);
        sendTimeMessage("Wave 'Sotetseg phase 3' complete. Duration: ", soteDeathTick - roomStartTick, soteDeathTick - soteSecondMazeEnd, false);
		plugin.lastSplits += "Sotetseg: " + RoomUtil.time(soteDeathTick-roomStartTick) + " (" + RoomUtil.time(plugin.currentDurationSum) + ")\n";
		plugin.currentDurationSum += (soteDeathTick-roomStartTick);
    }

    public void startFirstMaze()
    {
        startEitherMaze();
        lastChosen = "";
        excludedTiles.clear();
        currentMaze.clear();
        firstMazeChosen = "";
        hasSteppedOnMaze = false;
        soteFirstMazeStart = client.getTickCount();
        clog.addLine(LogID.SOTETSEG_FIRST_MAZE_STARTED, String.valueOf(soteFirstMazeStart - roomStartTick));
        roomState = RoomState.SotetsegRoomState.MAZE_1;
        sendTimeMessage("Wave 'Sotetseg phase 1' complete. Duration: ", soteFirstMazeStart - roomStartTick);
        plugin.addDelayedLine(RaidRoom.SOTETSEG, soteFirstMazeStart - roomStartTick, "Maze1 Start");
    }

    public void startEitherMaze()
    {
        playerTiles.clear();
        for (String s : plugin.currentPlayers)
        {
            playerTiles.put(s, new ArrayList<>());
        }
    }

    public void endFirstMaze()
    {
        endEitherMaze();
        soteFirstMazeEnd = client.getTickCount();
        clog.addLine(LogID.SOTETSEG_FIRST_MAZE_ENDED, String.valueOf(soteFirstMazeEnd - roomStartTick));
        roomState = RoomState.SotetsegRoomState.PHASE_2;
        sendTimeMessage("Wave 'Sotetseg maze 1' complete. Duration: ", soteFirstMazeEnd - roomStartTick, soteFirstMazeEnd - soteFirstMazeStart);
        plugin.addDelayedLine(RaidRoom.SOTETSEG, soteFirstMazeEnd - roomStartTick, "Maze1 End");
    }

    public void startSecondMaze()
    {
        startEitherMaze();
        lastChosen = "";
        excludedTiles.clear();
        currentMaze.clear();
        secondMazeChosen = "";
        hasSteppedOnMaze = false;
        soteSecondMazeStart = client.getTickCount();
        clog.addLine(LogID.SOTETSEG_SECOND_MAZE_STARTED, String.valueOf(soteSecondMazeStart - roomStartTick));
        roomState = RoomState.SotetsegRoomState.MAZE_2;
        sendTimeMessage("Wave 'Sotetseg phase 2' complete. Duration: ", soteSecondMazeStart - roomStartTick, soteSecondMazeStart - soteFirstMazeEnd);
        plugin.addDelayedLine(RaidRoom.SOTETSEG, soteSecondMazeStart - roomStartTick, "Maze2 Start");
    }

    private static ArrayList<Point> filterMaze(ArrayList<Point> tiles)
    {
        ArrayList<Point> filteredTiles = new ArrayList<>();
        for (Point p : tiles)
        {
            if (p.getY() % 2 == 0)
            {
                filteredTiles.add(p);
            }
        }
        return filteredTiles;
    }

    public ArrayList<Point> findOverlappingTiles(ArrayList<Point> tiles1, ArrayList<Point> tiles2)
    {
        ArrayList<Point> overlap = new ArrayList<>();
        for (Point p1 : tiles1)
        {
            for (Point p2 : tiles2)
            {
                if (p1.getX() == p2.getX() && p1.getY() == p2.getY())
                {
                    overlap.add(p1);
                }
            }
        }
        return overlap;
    }

    public ArrayList<Point> findNonOverlappingTiles(ArrayList<Point> tiles1, ArrayList<Point> tiles2)
    {
        ArrayList<Point> nonOverlappedTiles = new ArrayList<>();
        for (Point p1 : tiles1)
        {
            boolean found = false;
            for (Point p2 : tiles2)
            {
                if (p2.getX() == p1.getX() && p2.getY() == p1.getY())
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                nonOverlappedTiles.add(p1);
            }
        }
        return nonOverlappedTiles;
    }

    public void endEitherMaze()
    {
        if (!currentMaze.isEmpty())
        {
            if (currentMaze.get(currentMaze.size() - 1).getY() == 35)
            {
                currentMaze.add(new Point(currentMaze.get(currentMaze.size() - 1).getX(), 37));
            }
        }
        currentMaze = removeDuplicatePoints(currentMaze);
        currentMaze = addEveryTileBetween(currentMaze);
        ArrayList<Point> overlap = removeDuplicatePoints(findOverlappingTiles(currentMaze, excludedTiles));
        ArrayList<Point> nonoverlap = removeDuplicatePoints(findNonOverlappingTiles(excludedTiles, currentMaze));
        if (!overlap.isEmpty() && config.showMistakesInChat())
        {
            plugin.sendChatMessage(lastChosen + " ragged " + overlap.size() + " while running the maze");
        }
        if (!nonoverlap.isEmpty() && config.showMistakesInChat())
        {
            plugin.sendChatMessage("Players following the maze ragged " + nonoverlap.size() + " tiles");
        }
        currentMaze = filterMaze(currentMaze);
        excludedTiles = removeDuplicatePoints(excludedTiles);
        playerTiles.replaceAll((s, v) -> addEveryTileBetween(removeDuplicatePoints(v)));
        for (String s : playerTiles.keySet())
        {
            int ragged = 0;
            for (Point tile : playerTiles.get(s))
            {
                for (Point raggedTile : nonoverlap)
                {
                    if (tile.getX() == raggedTile.getX() && tile.getY() == raggedTile.getY())
                    {
                        ragged++;
                    }
                }
            }
            if (ragged > 0)
            {
                plugin.sendChatMessage(s + " ragged " + ragged + " tiles");
            }
        }
    }

    public void endSecondMaze()
    {
        endEitherMaze();
        soteSecondMazeEnd = client.getTickCount();
        clog.addLine(LogID.SOTETSEG_SECOND_MAZE_ENDED, String.valueOf(soteSecondMazeEnd - roomStartTick));
        roomState = RoomState.SotetsegRoomState.PHASE_3;
        sendTimeMessage("Wave 'Sotetseg maze 2' complete. Duration: ", soteSecondMazeEnd - roomStartTick, soteSecondMazeEnd - soteSecondMazeStart);
        plugin.addDelayedLine(RaidRoom.SOTETSEG, soteSecondMazeEnd - roomStartTick, "Maze2 End");
    }

    public String getAboveWorldChosen()
    {
        for (String playerName : plugin.currentPlayers)
        {
            boolean found = false;
            for (Player player : client.getPlayers())
            {
                if (playerName.equals(player.getName()))
                {
                    found = true;
                }
            }
            if (!found)
            {
                return playerName;
            }
        }
        return "";
    }

    public void updateGraphicsObjectCreated(GraphicsObjectCreated event)
    {
        if (event.getGraphicsObject().getId() == SOTETSEG_RAGGED_TILE)
        {
            WorldPoint wp = WorldPoint.fromLocal(client, event.getGraphicsObject().getLocation());
            excludedTiles.add(new Point(wp.getRegionX(), wp.getRegionY()));
        }
    }

    public void updateGameTick(GameTick event)
    {
        lastRegion = client.isInInstancedRegion() ? WorldPoint.fromLocalInstance(client, client.getLocalPlayer().getLocalLocation()).getRegionID() : client.getLocalPlayer().getWorldLocation().getRegionID();
        if (roomState == RoomState.SotetsegRoomState.MAZE_1 || roomState == RoomState.SotetsegRoomState.MAZE_2)
        {
            int ticksSinceLastProc;
            if (roomState == RoomState.SotetsegRoomState.MAZE_1)
            {
                ticksSinceLastProc = client.getTickCount() - soteFirstMazeStart;
            } else
            {
                ticksSinceLastProc = client.getTickCount() - soteSecondMazeStart;
            }
            if (ticksSinceLastProc > 5)
            {
                for (Player player : client.getPlayers())
                {
                    WorldPoint location = player.getWorldLocation();
                    if (location.getRegionX() <= 22 && location.getRegionX() >= 9)
                    {
                        if (location.getRegionY() >= 21 && location.getRegionY() <= 37)
                        {
                            if (playerTiles.containsKey(player.getName()))
                            {
                                playerTiles.get(player.getName()).add(new Point(location.getRegionX(), location.getRegionY()));
                            }
                        }
                    }
                }
            }
        }
        if (client.getTickCount() == deferTick)
        {
            deferTick = -1;
            if (client.getVarbitValue(HP_VARBIT) == FULL_HP)
            {
                clog.addLine(LogID.ACCURATE_SOTE_START);
            }
        }
        if (client.getTickCount() == soteFirstMazeStart + 5)
        {
            if (lastRegion == SOTETSEG_UNDERWORLD)
            {
                firstMazeChosen = client.getLocalPlayer().getName();
            } else
            {
                firstMazeChosen = getAboveWorldChosen();
            }
            lastChosen = firstMazeChosen;
        } else if (client.getTickCount() == soteSecondMazeStart + 5)
        {
            if (lastRegion == SOTETSEG_UNDERWORLD)
            {
                secondMazeChosen = client.getLocalPlayer().getName();
            } else
            {
                secondMazeChosen = getAboveWorldChosen();
            }
            lastChosen = secondMazeChosen;
        }
        super.updateGameTick(event);
    }

    public void handleNPCChanged(int id)
    {
        if (id == SOTETSEG_ACTIVE || id == SOTETSEG_ACTIVE_HM || id == SOTETSEG_ACTIVE_SM)
        {
            if (roomState == RoomState.SotetsegRoomState.NOT_STARTED)
            {
                if (id == SOTETSEG_ACTIVE_HM)
                {
                    clog.addLine(LogID.IS_HARD_MODE);
                } else if (id == SOTETSEG_ACTIVE_SM)
                {
                    clog.addLine(LogID.IS_STORY_MODE);
                }
                startSotetseg();
            } else if (roomState == RoomState.SotetsegRoomState.MAZE_1)
            {
                endFirstMaze();
            } else if (roomState == RoomState.SotetsegRoomState.MAZE_2)
            {
                endSecondMaze();
            }
        } else if (id == SOTETSEG_INACTIVE || id == SOTETSEG_INACTIVE_HM || id == SOTETSEG_INACTIVE_SM)
        {
            if (roomState == RoomState.SotetsegRoomState.PHASE_1)
            {
                startFirstMaze();
            } else if (roomState == RoomState.SotetsegRoomState.PHASE_2)
            {
                startSecondMaze();
            }
        }
    }
}
