/*
 * Copyright (c) 2025 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.json;

import java.util.List;

/**
 * JSON representation of a game event sent to/from the server.
 */
public class Event {
    // General fields shared by all events
    public int type;
    public String challengeId;
    public Integer stage;
    public int tick;
    public int xCoord;
    public int yCoord;

    // Event-specific fields
    public Player player;
    public Attack playerAttack;
    public Npc npc;
    public NpcAttack npcAttack;
    public Spell playerSpell;

    // ToB event-specific fields
    public List<Coords> maidenBloodSplats;
    public BloatDown bloatDown;
    public List<Coords> bloatHands;
    public NyloWave nyloWave;
    public SoteMaze soteMaze;
    public Integer xarpusPhase;
    public XarpusExhumed xarpusExhumed;
    public XarpusSplat xarpusSplat;
    public Integer verzikPhase;
    public List<Coords> verzikYellows;
    public VerzikBounce verzikBounce;
    public VerzikHeal verzikHeal;
    public VerzikDawn verzikDawn;
    public VerzikDawnDrop verzikDawnDrop;

    // Colosseum event-specific fields
    public Integer handicap;
    public List<Integer> handicapOptions;
    public ColosseumTotemHeal colosseumTotemHeal;
    public ColosseumReentryPools colosseumReentryPools;
    public ColosseumSolDust colosseumSolDust;
    public ColosseumSolGrapple colosseumSolGrapple;
    public ColosseumSolPools colosseumSolPools;
    public ColosseumSolLasers colosseumSolLasers;

    // Mokhaiotl event-specific fields
    public AttackStyle mokhaiotlAttackStyle;
    public MokhaiotlOrb mokhaiotlOrb;
    public MokhaiotlObjects mokhaiotlObjects;
    public MokhaiotlLarvaLeak mokhaiotlLarvaLeak;
    public MokhaiotlShockwave mokhaiotlShockwave;

    // Inferno event-specific fields
    public InfernoWaveStart infernoWaveStart;

    public static class Player {
        public static final int DATA_SOURCE_PRIMARY = 0;
        public static final int DATA_SOURCE_SECONDARY = 1;

        public String name;
        public int offCooldownTick;
        public Integer hitpoints;
        public Integer prayer;
        public Integer attack;
        public Integer strength;
        public Integer defence;
        public Integer ranged;
        public Integer magic;
        public List<Long> equipmentDeltas;
        public Long activePrayers;
        public int dataSource;
        public Boolean snapshot;

        public static class EquippedItem {
            public int slot;
            public int id;
            public int quantity;
        }
    }

    public static class Npc {
        public int id;
        public long roomId;
        public Integer hitpoints;
        public Long activePrayers;

        public MaidenCrab maidenCrab;
        public Nylo nylo;
        public VerzikCrab verzikCrab;
    }

    public static class MaidenCrab {
        public int spawn;
        public int position;
        public boolean scuffed;
    }

    public static class Nylo {
        public int wave;
        public long parentRoomId;
        public boolean big;
        public int style;
        public int spawnType;
    }

    public static class VerzikCrab {
        public int phase;
        public int spawn;
    }

    public static class Attack {
        public int type;
        public Player.EquippedItem weapon;
        public Npc target;
        public int distanceToTarget;
    }

    public static class Spell {
        public int type;
        public String targetPlayer;
        public Npc targetNpc;
    }

    public static class NpcAttack {
        public int attack;
        public String target;
    }

    public static class BloatDown {
        public int downNumber;
        public int walkTime;
    }

    public static class NyloWave {
        public int wave;
        public int nylosAlive;
        public int roomCap;
    }

    public static class SoteMaze {
        public static final int MAZE_66 = 0;
        public static final int MAZE_33 = 1;

        public int maze;
        public List<Coords> overworldTiles;
        public List<Coords> overworldPivots;
        public List<Coords> underworldPivots;
        public String chosenPlayer;
    }

    public static class XarpusExhumed {
        public int spawnTick;
        public int healAmount;
        public List<Integer> healTicks;
    }

    public static class XarpusSplat {
        public int source;
        public Coords bounceFrom;
    }

    public static class AttackStyle {
        public int style;
        public int npcAttackTick;
    }

    public static class VerzikBounce {
        public int npcAttackTick;
        public int playersInRange;
        public int playersNotInRange;
        public String bouncedPlayer;
    }

    public static class VerzikHeal {
        public String player;
        public int healAmount;
    }

    public static class VerzikDawn {
        public int attackTick;
        public int damage;
        public String player;
    }

    public static class VerzikDawnDrop {
        public boolean dropped;
    }

    public static class MokhaiotlOrb {
        public int source;
        public Coords sourcePoint;
        public int style;
        public int startTick;
        public int endTick;
    }

    public static class MokhaiotlObjects {
        public List<Coords> rocksSpawned;
        public List<Coords> rocksDespawned;
        public List<Coords> splatsSpawned;
        public List<Coords> splatsDespawned;
    }

    public static class MokhaiotlLarvaLeak {
        public long roomId;
        public int healAmount;
    }

    public static class MokhaiotlShockwave {
        public List<Coords> tiles;
    }

    public static class ColosseumTotemHeal {
        public Npc source;
        public Npc target;
        public int startTick;
        public int healAmount;
    }

    public static class ColosseumReentryPools {
        public List<Coords> primarySpawned;
        public List<Coords> primaryDespawned;
        public List<Coords> secondarySpawned;
        public List<Coords> secondaryDespawned;
    }

    public static class ColosseumSolDust {
        public static final int PATTERN_TRIDENT_1 = 0;
        public static final int PATTERN_TRIDENT_2 = 1;
        public static final int PATTERN_SHIELD_1 = 2;
        public static final int PATTERN_SHIELD_2 = 3;
        public static final int DIRECTION_NORTH = 0;
        public static final int DIRECTION_EAST = 1;
        public static final int DIRECTION_SOUTH = 2;
        public static final int DIRECTION_WEST = 3;

        public int pattern;
        public Integer direction;
    }

    public static class ColosseumSolGrapple {
        public static final int OUTCOME_HIT = 0;
        public static final int OUTCOME_DEFEND = 1;
        public static final int OUTCOME_PARRY = 2;

        public int attackTick;
        public int target;
        public int outcome;
    }

    public static class ColosseumSolPools {
        public List<Coords> pools;
    }

    public static class ColosseumSolLasers {
        public static final int PHASE_SCAN = 0;
        public static final int PHASE_SHOT = 1;
        public int phase;
    }

    public static class InfernoWaveStart {
        public int wave;
        public int overallTicks;
    }
}
