package com.github.therealguru;

import lombok.Data;
import lombok.RequiredArgsConstructor;
import net.runelite.api.GameObject;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;

import static com.github.therealguru.Inferno2dMapPlugin.GRID_HEIGHT;

@RequiredArgsConstructor
@Data
public class Coordinate {

    private final int x,y;

    public Coordinate(NPC npc) {
        this.x = npc.getWorldLocation().getRegionX() - 17;
        this.y = npc.getWorldLocation().getRegionY() - 17;
    }

    public Coordinate(GameObject gameObject) {
        this.x = gameObject.getWorldLocation().getRegionX() - 18;
        this.y = gameObject.getWorldLocation().getRegionY() - 18;
    }

    public Coordinate toScoutToolPoint() {
        return new Coordinate(x, (GRID_HEIGHT - 1) - y);
    }

    public Coordinate toCleanPoint() {
        return new Coordinate(x - 17, y - 17);
    }

    public Coordinate transform(int x, int y) {
        return new Coordinate(this.x + x, this.y + y);
    }
}
