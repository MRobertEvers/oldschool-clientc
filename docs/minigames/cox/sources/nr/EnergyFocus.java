package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.room.CrabPuzzleRoom;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.masks.Graphics;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.npc.NpcId;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.action.combat.magic.CombatSpell;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.game.world.region.CharacterLoop;

/**
 * @author Kris | 18. nov 2017 : 2:16.51
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class EnergyFocus extends NPC {

    private static final SoundEffect soundEffect = new SoundEffect(219, 10, 0);

    private static final Location[][] crystalTiles = new Location[][] {
            new Location[] {
                    new Location(3283, 5356, 2),
                    new Location(3279, 5366, 2),
                    new Location(3282, 5362, 2),
                    new Location(3287, 5359, 2)
            },
            new Location[] {
                    new Location(3311, 5356, 2),
                    new Location(3320, 5360, 2),
                    new Location(3306, 5361, 2),
                    new Location(3305, 5356, 2)
            },
            new Location[] {
                    new Location(3341, 5355, 2),
                    new Location(3337, 5354, 2),
                    new Location(3340, 5363, 2),
                    new Location(3340, 5367, 2)
            }
    };

    @Override
    public boolean isEntityClipped() {
        return false;
    }

    private static final int[][] directions = new int[][] { { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 } };

    public EnergyFocus(final CrabPuzzleRoom room, final Location tile) {
        super(7580, tile, true);
        this.room = room;
        direction = (room.getIndex() + room.getRotation()) & 3;
        for (int i = 0; i < 4; i++) {
            crystals[i] = room.getLocation(crystalTiles[room.getIndex()][i]);
        }
        lock(1);
    }

    private int direction;

    private final CrabPuzzleRoom room;

    private final Location[] crystals = new Location[4];

    private boolean finish;

    private String username;

    @Override
    public void processNPC() {
        if (isFinished() || isLocked()) {
            return;
        }
        if (finish) {
            int index = 0;
            for (final Location tile : crystals) {
                if (getX() == tile.getX() && getY() == tile.getY()) {
                    if (index == 0 && getId() == 7580 || index == 1 && getId() == 7583 || index == 2 && getId() == 7582 || index == 3 && getId() == 7581) {
                        finish();
                        room.transformCrystal(index);
                        room.sendFocus();
                        if (username != null) {
                            for (final Player player : room.getRaid().getPlayers()) {
                                if (player.getUsername().equals(username)) {
                                    Raid.incrementPoints(player, 1600);
                                    break;
                                }
                            }
                        }
                        final WorldObject object = World.getObjectWithType(tile, 10);
                        World.spawnObject(new WorldObject(29762, object.getType(), object.getRotation(), object));
                        final Graphics graphics = getGraphics(false);
                        World.sendGraphics(new Graphics(graphics.getId(), 0, graphics.getHeight()), new Location(getLocation()));
                        World.sendSoundEffect(new Location(getLocation()), soundEffect);
                    } else {
                        finish();
                        room.sendFocus();
                    }
                    return;
                }
                index++;
            }
            return;
        }
        if (checkPlayers(getLocation())) {
            return;
        }
        int tryCount = 0;
        int advancementCount = 0;
        loop: while (true) {
            if (++tryCount >= 10) {
                break;
            }
            final int[] dir = directions[direction];
            final int x = getX() + dir[0];
            final int y = getY() + dir[1];
            for (final JewelledCrab crab : room.getCrabs()) {
                if (crab.getX() == x && crab.getY() == y) {
                    if (crab.getId() != NpcId.JEWELLED_CRAB) {
                        setTransformation(7580 + (crab.getId() - 7576));
                    }
                    // If the focus tries to shift direction more than once in a single tick, it returns to start position.
                    if (++advancementCount >= 2) {
                        finish();
                        room.sendFocus();
                        final Graphics graphics = getGraphics(false);
                        World.sendSoundEffect(new Location(getLocation()), soundEffect);
                        World.sendGraphics(new Graphics(graphics.getId(), 0, graphics.getHeight()), new Location(getLocation()));
                        return;
                    }
                    advanceDirection();
                    final Entity target = crab.getCombat().getTarget();
                    username = target instanceof Player ? ((Player) target).getUsername() : null;
                    continue loop;
                }
            }
            if (atCrystal(x, y) && addWalkSteps(x, y, 1, false) || addWalkSteps(x, y, 1, true)) {
                if (atCrystal(x, y)) {
                    finish = true;
                }
                break;
            } else {
                finish();
                room.sendFocus();
                final Graphics graphics = getGraphics(false);
                World.sendSoundEffect(new Location(getLocation()), soundEffect);
                World.sendGraphics(new Graphics(graphics.getId(), 0, graphics.getHeight()), new Location(getLocation()));
                return;
            }
        }
        final Location nextTile = getNextPosition(1);
        if (!nextTile.matches(getLocation())) {
            checkPlayers(nextTile);
        }
    }

    private final boolean checkPlayers(final Location tile) {
        for (final Player player : room.getRaid().getPlayers()) {
            if (player.isDead() || player.isFinished()) {
                continue;
            }
            if (player.getLocation().getPositionHash() == tile.getPositionHash()) {
                finish();
                room.sendFocus();
                final Graphics graphics = getGraphics(true);
                World.sendGraphics(new Graphics(graphics.getId(), 0, graphics.getHeight()), new Location(player.getLocation()));
                World.sendSoundEffect(new Location(player.getLocation()), soundEffect);
                CharacterLoop.forEach(player.getLocation(), 1, Player.class, p -> p.applyHit(new Hit((int) (Math.ceil(Math.min(99, player.getHitpoints()) * 0.25F)), HitType.REGULAR)));
                return true;
            }
        }
        return false;
    }

    private Graphics getGraphics(final boolean large) {
        switch(id) {
            case 7580:
                return large ? CombatSpell.WIND_WAVE.getHitGfx() : CombatSpell.WIND_BOLT.getHitGfx();
            case 7581:
                return large ? CombatSpell.FIRE_WAVE.getHitGfx() : CombatSpell.FIRE_BOLT.getHitGfx();
            case 7582:
                return large ? CombatSpell.EARTH_WAVE.getHitGfx() : CombatSpell.WIND_BOLT.getHitGfx();
            default:
                return large ? CombatSpell.WATER_WAVE.getHitGfx() : CombatSpell.WATER_BOLT.getHitGfx();
        }
    }

    private void advanceDirection() {
        direction = (direction + 1) & 3;
    }

    private boolean atCrystal(final int x, final int y) {
        for (final Location tile : crystals) {
            if (x == tile.getX() && y == tile.getY()) {
                return true;
            }
        }
        return false;
    }
}
