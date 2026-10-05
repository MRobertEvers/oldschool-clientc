use std::collections::{BTreeMap, HashSet};

use blert::Tick;

use super::db;
use crate::lifecycle::core::types::Stage;
use crate::proto::Coords;

const MAX_SPAWNS: usize = 9;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(super) struct LocalCoords(i16, i16);

#[derive(Debug)]
pub(super) struct SpawnKey {
    tick: Tick,
    vals: [i16; MAX_SPAWNS],
    len: usize,
}

impl SpawnKey {
    pub fn iter(&self) -> impl Iterator<Item = i16> + '_ {
        self.vals[..self.len].iter().copied()
    }

    pub fn tick(&self) -> Tick {
        self.tick
    }

    /// Encodes each wave spawn as `type << 10 | x << 5 | y` from its type in
    /// `arena` and its local coordinates, sorting the results.
    fn encode(arena: &Arena, spawns: &[(u32, LocalCoords)], tick: Tick) -> Self {
        let mut vals = [0; MAX_SPAWNS];
        for (val, &(npc, coords)) in vals[..spawns.len()].iter_mut().zip(spawns) {
            let npc_type = arena
                .npc_types
                .iter()
                .position(|&id| id == npc)
                .and_then(|index| i16::try_from(index).ok())
                .expect("spawn NPCs are arena types");
            *val = (npc_type << 10) | (coords.0 << 5) | coords.1;
        }
        vals[..spawns.len()].sort_unstable();

        Self {
            tick,
            vals,
            len: spawns.len(),
        }
    }
}

#[derive(Debug)]
pub(super) struct Arena {
    base: Coords,
    spawn_locations: HashSet<LocalCoords>,
    npc_types: &'static [u32],
}

impl Arena {
    /// Defines an arena that spawns NPCs in waves.
    /// `base` represents the global origin of the area, and `spawn_locations`
    /// are the global coordinates at which wave NPCs can spawn.
    /// `npc_types` defines the IDs of the NPCs that can spawn in waves. The
    /// index in the array is used as a type ID and must be stable.
    pub fn new(
        base: Coords,
        spawn_locations: impl IntoIterator<Item = Coords>,
        npc_types: &'static [u32],
    ) -> Self {
        let mut arena = Self {
            base,
            spawn_locations: HashSet::new(),
            npc_types,
        };
        arena.spawn_locations = spawn_locations
            .into_iter()
            .map(|coords| arena.to_local(coords))
            .collect();
        arena
    }

    /// Converts a world position to the arena's local coordinate space.
    pub fn to_local(&self, coords: Coords) -> LocalCoords {
        #![expect(clippy::cast_possible_truncation)]
        // y is inverted from the game because that's what popular spawn
        // analysis tools use and we want to be compatible.
        LocalCoords(
            (coords.x - self.base.x) as i16,
            (self.base.y - coords.y) as i16,
        )
    }
}

/// A `SpawnIndexer` tracks the wave spawn of NPCs in a wave-based challenge,
/// converting each spawn to an queryable and indexable format.
#[derive(Debug)]
pub(super) struct SpawnIndexer<'a> {
    arena: &'a Arena,
    expected_npcs: Vec<u32>,
    spawns_by_tick: BTreeMap<Tick, Vec<(u32, LocalCoords)>>,
}

impl<'a> SpawnIndexer<'a> {
    /// Creates an indexer for a specific wave in `arena`.
    /// `expected` is the list of NPCs that should spawn in the wave.
    pub fn new(arena: &'a Arena, expected: impl IntoIterator<Item = u32>) -> Self {
        let expected_npcs: Vec<u32> = expected.into_iter().collect();
        debug_assert!(expected_npcs.len() <= MAX_SPAWNS);

        Self {
            arena,
            expected_npcs,
            spawns_by_tick: BTreeMap::new(),
        }
    }

    pub fn track_spawn(&mut self, tick: Tick, npc: u32, coords: Coords) {
        let local = self.arena.to_local(coords);
        self.spawns_by_tick
            .entry(tick)
            .or_default()
            .push((npc, local));
    }

    /// Determines the wave's spawn from the NPCs that spawned on its first tick.
    /// `extra` is an optional list of additional NPCs that can spawn as part of
    /// the wave, outside of the arena's spawn locations.
    pub fn check(self, extra: &[u32]) -> Option<SpawnKey> {
        debug_assert!(self.expected_npcs.len() + extra.len() <= MAX_SPAWNS);
        if self.expected_npcs.is_empty() {
            return None;
        }

        let (first_tick, mut spawns) = self.spawns_by_tick.into_iter().next()?;

        let mut matched = Vec::with_capacity(self.expected_npcs.len() + extra.len());
        for npc in self.expected_npcs {
            let index = spawns.iter().position(|&(id, coords)| {
                id == npc && self.arena.spawn_locations.contains(&coords)
            })?;
            matched.push(spawns.remove(index));
        }
        for &npc in extra {
            let index = spawns.iter().position(|&(id, _)| id == npc)?;
            matched.push(spawns.remove(index));
        }

        if spawns
            .iter()
            .any(|(id, _)| self.arena.npc_types.contains(id))
        {
            return None;
        }

        Some(SpawnKey::encode(self.arena, &matched, first_tick))
    }
}

/// Records the spawn of a challenge stage, with `None` indicating that the
/// spawn could not be determined. `modified` is set if the spawn differs from
/// the typical spawn of that stage.
pub(super) async fn save(
    txn: &db::Transaction,
    stage: Stage,
    spawn: Option<&SpawnKey>,
    player: Option<LocalCoords>,
    modified: bool,
) -> Result<(), db::Error> {
    let spawns: Option<Vec<i16>> = spawn.map(|key| key.iter().collect());
    let player = player.map(|coords| (coords.1 << 8) | coords.0);
    txn.execute(
        "INSERT INTO challenge_stage_spawns (challenge_id, stage, spawns, player, modified)
         VALUES ($1, $2, $3, $4, $5)
         ON CONFLICT (challenge_id, stage) DO NOTHING",
        &[
            &txn.challenge_id(),
            &(stage as i16),
            &spawns,
            &player,
            &modified,
        ],
    )
    .await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn colosseum_arena() -> Arena {
        Arena::new(
            Coords { x: 1808, y: 3123 },
            [
                (1811, 3109),
                (1817, 3106),
                (1825, 3114),
                (1821, 3109),
                (1827, 3109),
                (1836, 3109),
                (1832, 3107),
                (1811, 3104),
                (1836, 3104),
                (1821, 3103),
                (1827, 3103),
                (1824, 3099),
            ]
            .map(|(x, y)| Coords { x, y }),
            &[12811, 12817, 12818, 12819],
        )
    }

    fn inferno_arena() -> Arena {
        Arena::new(
            Coords { x: 2257, y: 5358 },
            [
                (2258, 5330),
                (2262, 5335),
                (2272, 5330),
                (2258, 5353),
                (2273, 5341),
                (2279, 5353),
                (2260, 5347),
                (2280, 5333),
                (2280, 5346),
            ]
            .map(|(x, y)| Coords { x, y }),
            &[7692, 7693, 7697, 7698, 7699],
        )
    }

    #[test]
    fn wave_npcs_determine_the_key() {
        // A wave 2 where the ranged fremmy spawns on the shaman.
        let arena = colosseum_arena();
        let mut indexer = SpawnIndexer::new(&arena, [12811, 12817]);
        indexer.track_spawn(Tick(0), 12816, Coords { x: 1822, y: 3110 });
        indexer.track_spawn(Tick(0), 12814, Coords { x: 1821, y: 3109 });
        indexer.track_spawn(Tick(0), 12815, Coords { x: 1823, y: 3109 });
        indexer.track_spawn(Tick(0), 12811, Coords { x: 1821, y: 3109 });
        indexer.track_spawn(Tick(0), 12817, Coords { x: 1836, y: 3104 });
        assert_eq!(
            indexer.check(&[]).map(|key| key.iter().collect::<Vec<_>>()),
            Some(vec![430, 1939]),
        );
    }

    #[test]
    fn later_spawns_are_ignored() {
        // A wave 42 where the meleer is resurrected.
        let arena = inferno_arena();
        let mut indexer = SpawnIndexer::new(&arena, [7697, 7699]);
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2257, y: 5349 });
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2267, y: 5335 });
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2274, y: 5351 });
        indexer.track_spawn(Tick(0), 7699, Coords { x: 2258, y: 5353 });
        indexer.track_spawn(Tick(0), 7697, Coords { x: 2279, y: 5353 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2267, y: 5347 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2266, y: 5347 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2266, y: 5346 });
        indexer.track_spawn(Tick(119), 7697, Coords { x: 2274, y: 5341 });
        assert_eq!(
            indexer.check(&[]).map(|key| key.iter().collect::<Vec<_>>()),
            Some(vec![2757, 4133]),
        );
    }

    #[test]
    fn partially_recorded_spawns_are_undetermined() {
        let arena = colosseum_arena();
        let mut indexer = SpawnIndexer::new(&arena, [12811, 12817]);
        indexer.track_spawn(Tick(0), 12816, Coords { x: 1822, y: 3110 });
        indexer.track_spawn(Tick(0), 12814, Coords { x: 1821, y: 3109 });
        indexer.track_spawn(Tick(0), 12815, Coords { x: 1823, y: 3109 });
        indexer.track_spawn(Tick(0), 12811, Coords { x: 1821, y: 3109 });
        assert!(indexer.check(&[]).is_none());
    }

    #[test]
    fn npcs_which_have_moved_off_their_spawn_tiles_fail() {
        let arena = colosseum_arena();
        let mut indexer = SpawnIndexer::new(&arena, [12811, 12817]);
        indexer.track_spawn(Tick(1), 12816, Coords { x: 1822, y: 3110 });
        indexer.track_spawn(Tick(1), 12814, Coords { x: 1821, y: 3109 });
        indexer.track_spawn(Tick(1), 12815, Coords { x: 1823, y: 3109 });
        indexer.track_spawn(Tick(1), 12811, Coords { x: 1822, y: 3108 });
        indexer.track_spawn(Tick(1), 12817, Coords { x: 1835, y: 3103 });
        assert!(indexer.check(&[]).is_none());
    }

    #[test]
    fn extra_wave_npcs_fail() {
        // Configured for wave 2, but given a recording of wave 3.
        let arena = colosseum_arena();
        let mut indexer = SpawnIndexer::new(&arena, [12811, 12817]);
        indexer.track_spawn(Tick(0), 12816, Coords { x: 1823, y: 3110 });
        indexer.track_spawn(Tick(0), 12814, Coords { x: 1822, y: 3109 });
        indexer.track_spawn(Tick(0), 12815, Coords { x: 1824, y: 3109 });
        indexer.track_spawn(Tick(0), 12811, Coords { x: 1821, y: 3109 });
        indexer.track_spawn(Tick(0), 12817, Coords { x: 1825, y: 3114 });
        indexer.track_spawn(Tick(0), 12817, Coords { x: 1824, y: 3099 });
        indexer.track_spawn(Tick(66), 12810, Coords { x: 1825, y: 3120 });
        assert!(indexer.check(&[]).is_none());
    }

    #[test]
    fn extra_npcs_can_spawn_anywhere() {
        // A wave 7 with Dynamic Duo whose extra colossus spawns on a new tile.
        let arena = colosseum_arena();
        let mut indexer = SpawnIndexer::new(&arena, [12817, 12818, 12819]);
        indexer.track_spawn(Tick(0), 12816, Coords { x: 1822, y: 3107 });
        indexer.track_spawn(Tick(0), 12814, Coords { x: 1821, y: 3106 });
        indexer.track_spawn(Tick(0), 12815, Coords { x: 1823, y: 3106 });
        indexer.track_spawn(Tick(0), 12818, Coords { x: 1836, y: 3104 });
        indexer.track_spawn(Tick(0), 12817, Coords { x: 1827, y: 3109 });
        indexer.track_spawn(Tick(0), 12819, Coords { x: 1824, y: 3099 });
        indexer.track_spawn(Tick(0), 12819, Coords { x: 1823, y: 3101 });
        indexer.track_spawn(Tick(66), 12812, Coords { x: 1825, y: 3120 });
        assert_eq!(
            indexer
                .check(&[12819])
                .map(|key| key.iter().collect::<Vec<_>>()),
            Some(vec![1646, 2963, 3574, 3608]),
        );
    }

    #[test]
    fn waves_without_expected_spawns_are_undetermined() {
        // A wave 3, which only has nibblers.
        let arena = inferno_arena();
        let mut indexer = SpawnIndexer::new(&arena, []);
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2257, y: 5349 });
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2267, y: 5335 });
        indexer.track_spawn(Tick(0), 7709, Coords { x: 2274, y: 5351 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2266, y: 5347 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2265, y: 5347 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2266, y: 5345 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2267, y: 5345 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2265, y: 5345 });
        indexer.track_spawn(Tick(0), 7691, Coords { x: 2266, y: 5346 });
        assert!(indexer.check(&[]).is_none());
    }
}
