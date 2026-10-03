//! Inferno challenge processing.

use std::collections::BTreeSet;
use std::sync::LazyLock;

use async_trait::async_trait;
use blert::npc::id::{JAL_AK, JAL_IMKOT, JAL_MEJRAH, JAL_XIL, JAL_ZEK};
use blert::{Tick, Ticks, npc};
use serde::{Deserialize, Serialize};

use super::StoredState;
use super::challenge_processor::{
    ChallengeContext, ChallengeProcessor, ChallengeTicks, EventCursor, RoomNpc, StageContext,
};
use super::db;
use super::persist;
use super::spawn_index::{self, Arena, SpawnIndexer};
use super::split::SplitType;
use crate::lifecycle::core::types::{ChallengeInfo, ChallengeStatus, ProcessingError, Stage};
use crate::merging::MergedEvents;
use crate::metrics;
use crate::price::PriceResolver;
use crate::proto::{ChallengeData, Coords, NpcAttack, challenge_data, event};

/// Ticks between the end of one wave and the start of the next.
const WAVE_INTERVAL_TICKS: Ticks = Ticks(6);

static ARENA: LazyLock<Arena> = LazyLock::new(|| {
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
        &[JAL_MEJRAH, JAL_AK, JAL_IMKOT, JAL_XIL, JAL_ZEK],
    )
});

const WAVES: [&[u32]; 69] = [
    &[JAL_MEJRAH],
    &[JAL_MEJRAH, JAL_MEJRAH],
    &[],
    &[JAL_AK],
    &[JAL_AK, JAL_MEJRAH],
    &[JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_AK, JAL_AK],
    &[],
    &[JAL_IMKOT],
    &[JAL_IMKOT, JAL_MEJRAH],
    &[JAL_IMKOT, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_IMKOT, JAL_AK],
    &[JAL_IMKOT, JAL_AK, JAL_MEJRAH],
    &[JAL_IMKOT, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_IMKOT, JAL_AK, JAL_AK],
    &[JAL_IMKOT, JAL_IMKOT],
    &[],
    &[JAL_XIL],
    &[JAL_XIL, JAL_MEJRAH],
    &[JAL_XIL, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_XIL, JAL_AK],
    &[JAL_XIL, JAL_AK, JAL_MEJRAH],
    &[JAL_XIL, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_XIL, JAL_AK, JAL_AK],
    &[JAL_XIL, JAL_IMKOT],
    &[JAL_XIL, JAL_IMKOT, JAL_MEJRAH],
    &[JAL_XIL, JAL_IMKOT, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_XIL, JAL_IMKOT, JAL_AK],
    &[JAL_XIL, JAL_IMKOT, JAL_AK, JAL_MEJRAH],
    &[JAL_XIL, JAL_IMKOT, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_XIL, JAL_IMKOT, JAL_AK, JAL_AK],
    &[JAL_XIL, JAL_IMKOT, JAL_IMKOT],
    &[JAL_XIL, JAL_XIL],
    &[],
    &[JAL_ZEK],
    &[JAL_ZEK, JAL_MEJRAH],
    &[JAL_ZEK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_AK],
    &[JAL_ZEK, JAL_AK, JAL_MEJRAH],
    &[JAL_ZEK, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_AK, JAL_AK],
    &[JAL_ZEK, JAL_IMKOT],
    &[JAL_ZEK, JAL_IMKOT, JAL_MEJRAH],
    &[JAL_ZEK, JAL_IMKOT, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_IMKOT, JAL_AK],
    &[JAL_ZEK, JAL_IMKOT, JAL_AK, JAL_MEJRAH],
    &[JAL_ZEK, JAL_IMKOT, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_IMKOT, JAL_AK, JAL_AK],
    &[JAL_ZEK, JAL_IMKOT, JAL_IMKOT],
    &[JAL_ZEK, JAL_XIL],
    &[JAL_ZEK, JAL_XIL, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_AK],
    &[JAL_ZEK, JAL_XIL, JAL_AK, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_AK, JAL_AK],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_AK],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_AK, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_AK, JAL_MEJRAH, JAL_MEJRAH],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_AK, JAL_AK],
    &[JAL_ZEK, JAL_XIL, JAL_IMKOT, JAL_IMKOT],
    &[JAL_ZEK, JAL_XIL, JAL_XIL],
    &[JAL_ZEK, JAL_ZEK],
    &[],
    &[],
    &[],
];

/// In-flight inferno state stored between stages.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct CustomData {
    waves: Vec<WaveData>,
    meleer_digs: u32,
    mager_revives: u32,
    west_pillar_collapse_wave: Option<u32>,
    east_pillar_collapse_wave: Option<u32>,
    south_pillar_collapse_wave: Option<u32>,
}

/// Final state of a processed wave.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct WaveData {
    stage: Stage,
    ticks_lost: u32,
    offset: Ticks,
    npcs: Vec<RoomNpc>,
    ticks: Ticks,
    start_tick: Ticks,
}

impl WaveData {
    fn to_proto(&self) -> challenge_data::InfernoWave {
        challenge_data::InfernoWave {
            stage: self.stage as i32,
            ticks_lost: self.ticks_lost,
            offset: Some(self.offset.0),
            npcs: self.npcs.iter().map(Into::into).collect(),
            ticks: self.ticks.0,
            start_tick: self.start_tick.0,
        }
    }
}

fn stage_to_wave(stage: Stage) -> u32 {
    (stage as u32) - (Stage::InfernoWave1 as u32) + 1
}

fn wave_npcs(stage: Stage) -> &'static [u32] {
    usize::try_from(stage as i32 - Stage::InfernoWave1 as i32)
        .ok()
        .and_then(|index| WAVES.get(index))
        .copied()
        .unwrap_or(&[])
}

/// Returns the challenge split marking the start of a milestone wave.
fn wave_start_split(wave: u32) -> Option<SplitType> {
    match wave {
        9 => Some(SplitType::InfernoWave9Start),
        18 => Some(SplitType::InfernoWave18Start),
        25 => Some(SplitType::InfernoWave25Start),
        35 => Some(SplitType::InfernoWave35Start),
        42 => Some(SplitType::InfernoWave42Start),
        50 => Some(SplitType::InfernoWave50Start),
        57 => Some(SplitType::InfernoWave57Start),
        60 => Some(SplitType::InfernoWave60Start),
        63 => Some(SplitType::InfernoWave63Start),
        66 => Some(SplitType::InfernoWave66Start),
        67 => Some(SplitType::InfernoWave67Start),
        68 => Some(SplitType::InfernoWave68Start),
        69 => Some(SplitType::InfernoWave69Start),
        _ => None,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Pillar {
    West,
    East,
    South,
}

impl Pillar {
    fn at(x: i32, y: i32) -> Option<Pillar> {
        match (x, y) {
            (2257, 5349) => Some(Pillar::West),
            (2274, 5351) => Some(Pillar::East),
            (2267, 5335) => Some(Pillar::South),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub struct InfernoProcessor {
    challenge: ChallengeInfo,
    data: CustomData,
    /// Client-reported start tick for this wave.
    wave_start_tick: Option<Ticks>,
    spawn_indexer: SpawnIndexer<'static>,
}

impl InfernoProcessor {
    pub fn new(
        challenge: ChallengeInfo,
        custom_data: Option<&serde_json::Value>,
    ) -> Result<InfernoProcessor, ProcessingError> {
        let data = match custom_data {
            Some(value) => {
                serde_json::from_value(value.clone()).map_err(|error| ProcessingError {
                    retriable: false,
                    message: format!("custom data deserialization failed: {error}"),
                })?
            }
            None => CustomData {
                waves: Vec::new(),
                meleer_digs: 0,
                mager_revives: 0,
                west_pillar_collapse_wave: None,
                east_pillar_collapse_wave: None,
                south_pillar_collapse_wave: None,
            },
        };

        let spawn_indexer = SpawnIndexer::new(&ARENA, wave_npcs(challenge.stage).iter().copied());
        Ok(InfernoProcessor {
            challenge,
            data,
            wave_start_tick: None,
            spawn_indexer,
        })
    }
}

#[async_trait]
impl ChallengeProcessor for InfernoProcessor {
    fn process_challenge_event(
        &mut self,
        _ctx: &mut StageContext,
        events: &mut EventCursor<'_>,
    ) -> bool {
        let event = events.current();
        match event.r#type() {
            event::Type::NpcDeath => {
                if event
                    .npc
                    .as_ref()
                    .is_some_and(|npc| npc.id == npc::id::ROCKY_SUPPORT)
                    && let Some(pillar) = Pillar::at(event.x_coord, event.y_coord)
                {
                    let wave = Some(stage_to_wave(event.stage()));
                    match pillar {
                        Pillar::West => self.data.west_pillar_collapse_wave = wave,
                        Pillar::East => self.data.east_pillar_collapse_wave = wave,
                        Pillar::South => self.data.south_pillar_collapse_wave = wave,
                    }
                }
                true
            }
            event::Type::NpcAttack => {
                match event.npc_attack.as_ref().map(event::NpcAttacked::attack) {
                    Some(NpcAttack::InfernoMeleerDig) => self.data.meleer_digs += 1,
                    Some(NpcAttack::InfernoMagerResurrect) => self.data.mager_revives += 1,
                    _ => {}
                }
                true
            }
            event::Type::InfernoWaveStart => {
                if let Some(wave_start) = &event.inferno_wave_start {
                    self.wave_start_tick = Some(Ticks(wave_start.overall_ticks));
                }
                false
            }
            event::Type::NpcSpawn => {
                if let Some(npc) = &event.npc {
                    self.spawn_indexer.track_spawn(
                        Tick(event.tick),
                        npc.id,
                        Coords {
                            x: event.x_coord,
                            y: event.y_coord,
                        },
                    );
                }
                true
            }
            _ => true,
        }
    }

    async fn on_create(&mut self, txn: &db::Transaction) -> Result<(), db::Error> {
        txn.execute(
            "INSERT INTO inferno_challenge_stats (challenge_id) VALUES ($1)",
            &[&txn.challenge_id()],
        )
        .await?;
        Ok(())
    }

    async fn on_stage_finished(
        &mut self,
        txn: &db::Transaction,
        _price_resolver: &PriceResolver,
        stored: &StoredState,
        ctx: &mut StageContext,
        stage: Stage,
        events: &MergedEvents,
    ) -> Result<ChallengeTicks, db::Error> {
        let wave = stage_to_wave(stage);

        let (ticks, start_tick) = match self.wave_start_tick {
            Some(start) => {
                if let Some(split) = wave_start_split(wave) {
                    ctx.set_challenge_split(split, start, None);
                }
                (ChallengeTicks::Set(start + events.duration()), start)
            }
            None => (
                ChallengeTicks::Add(events.duration() + WAVE_INTERVAL_TICKS),
                stored.challenge_ticks + WAVE_INTERVAL_TICKS,
            ),
        };

        let split =
            SplitType::try_from(SplitType::InfernoWave1Time as i32 + wave.cast_signed() - 1)
                .expect("wave time splits are consecutive");
        ctx.set_stage_split(split, events.last_tick(), Tick(0), true);

        self.data.waves.push(WaveData {
            stage,
            ticks_lost: events.missing_tick_count(),
            offset: events.offset(),
            npcs: ctx.npcs().cloned().collect(),
            ticks: events.duration(),
            start_tick,
        });

        txn.execute(
            "UPDATE inferno_challenge_stats
             SET meleer_digs = $1,
                 mager_revives = $2,
                 west_pillar_collapse_wave = $3,
                 east_pillar_collapse_wave = $4,
                 south_pillar_collapse_wave = $5
             WHERE challenge_id = $6",
            &[
                &self.data.meleer_digs.cast_signed(),
                &self.data.mager_revives.cast_signed(),
                &self.data.west_pillar_collapse_wave.map(u32::cast_signed),
                &self.data.east_pillar_collapse_wave.map(u32::cast_signed),
                &self.data.south_pillar_collapse_wave.map(u32::cast_signed),
                &txn.challenge_id(),
            ],
        )
        .await?;

        let indexer = std::mem::replace(&mut self.spawn_indexer, SpawnIndexer::new(&ARENA, []));
        let spawn = indexer.check(&[]);
        if spawn.is_none() && !wave_npcs(stage).is_empty() {
            metrics::record_undetermined_wave_spawn(stage);
        }
        let player = spawn.as_ref().and_then(|spawn| {
            events
                .events_for_tick(spawn.tick())
                .iter()
                .find(|event| event.r#type() == event::Type::PlayerUpdate)
                .map(|event| {
                    ARENA.to_local(Coords {
                        x: event.x_coord,
                        y: event.y_coord,
                    })
                })
        });
        spawn_index::save(txn, stage, spawn.as_ref(), player, false).await?;

        Ok(ticks)
    }

    async fn on_finish(
        &mut self,
        txn: &db::Transaction,
        stored: &StoredState,
        ctx: &mut ChallengeContext,
        final_ticks: Ticks,
    ) -> Result<(), db::Error> {
        ctx.set_challenge_split(SplitType::InfernoChallenge, final_ticks, None);
        ctx.set_challenge_split(SplitType::InfernoOverall, final_ticks, None);

        for index in 0..self.challenge.party.len() {
            if let Some(player) = ctx.player_mut(index) {
                match self.challenge.status {
                    ChallengeStatus::Completed => player.stats.inferno_completions += 1,
                    ChallengeStatus::Reset => player.stats.inferno_resets += 1,
                    ChallengeStatus::Wiped => player.stats.inferno_wipes += 1,
                    ChallengeStatus::InProgress | ChallengeStatus::Abandoned => {}
                }
            }
        }

        let times_accurate = self.challenge.status == ChallengeStatus::Completed
            && self.has_fully_recorded_up_to(Stage::InfernoWave69);
        if times_accurate && final_ticks == stored.challenge_ticks {
            persist::set_splits_accurate(txn, &self.challenge, &stored.players).await?;
        }

        Ok(())
    }

    fn custom_data(&self) -> Option<serde_json::Value> {
        Some(serde_json::to_value(&self.data).expect("custom data serializes"))
    }

    fn challenge_data(&self) -> Option<ChallengeData> {
        Some(ChallengeData {
            challenge_id: self.challenge.uuid.to_string(),
            stage_data: Some(challenge_data::StageData::Inferno(
                challenge_data::Inferno {
                    waves: self.data.waves.iter().map(WaveData::to_proto).collect(),
                },
            )),
        })
    }

    fn has_fully_recorded_up_to(&self, stage: Stage) -> bool {
        if !(Stage::InfernoWave1..=Stage::InfernoWave69).contains(&stage) {
            return false;
        }
        let recorded: BTreeSet<i32> = self
            .data
            .waves
            .iter()
            .map(|wave| wave.stage as i32)
            .collect();
        (Stage::InfernoWave1 as i32..=stage as i32).all(|value| recorded.contains(&value))
    }
}

#[cfg(test)]
mod tests {
    #![allow(clippy::too_many_lines)]
    use serde_json::json;

    use super::*;
    use crate::lifecycle::core::state::Trigger;
    use crate::lifecycle::core::types::{
        ChallengeMode, ChallengeType, JournalSeq, PlayerId, PrimaryMeleeGear, StageStatus, Uuid,
    };
    use crate::merging::fixtures::{
        NpcEvent, ServerTicks, inferno_wave_start_event, merged_events, npc_attack_event,
        npc_death_event,
    };
    use crate::players::normalize_rsn;
    use crate::processing::StoredPlayerInfo;
    use crate::processing::split::{ChallengeSplit, SavedSplit};
    use crate::processing::stats::PlayerStatsDelta;
    use crate::skill::SkillLevel;

    fn challenge_info(stage: Stage, status: ChallengeStatus) -> ChallengeInfo {
        ChallengeInfo {
            uuid: "d21f3ac1-5f2e-4d92-8a17-c04be59d13b7".parse().unwrap(),
            session_uuid: "83b18e1a-97c4-4de5-a2f6-01d9e7b2c644".parse().unwrap(),
            challenge_type: ChallengeType::Inferno,
            mode: ChallengeMode::NoMode,
            party: vec!["715".to_string()],
            party_changed: false,
            stage,
            stage_attempt: None,
            status,
            created_unix_ms: 1_786_812_480_986,
            reported_times: None,
            finished_unix_ms: None,
        }
    }

    #[test]
    fn processor_starts_with_empty_data() {
        let processor = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave1, ChallengeStatus::InProgress),
            None,
        )
        .unwrap();
        assert_eq!(
            processor.custom_data(),
            Some(json!({
                "waves": [],
                "meleerDigs": 0,
                "magerRevives": 0,
                "westPillarCollapseWave": null,
                "eastPillarCollapseWave": null,
                "southPillarCollapseWave": null,
            })),
        );
    }

    #[test]
    fn malformed_custom_data_fails_construction() {
        let error = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave1, ChallengeStatus::InProgress),
            Some(&json!({"waves": 51})),
        )
        .unwrap_err();
        assert!(!error.retriable);
        assert!(
            error
                .message
                .starts_with("custom data deserialization failed")
        );
    }

    #[test]
    fn wave_start_reports_are_captured_and_dropped() {
        let mut processor = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave25, ChallengeStatus::InProgress),
            None,
        )
        .unwrap();
        let mut ctx = StageContext::new(Stage::InfernoWave25, vec!["715".to_string()]);
        let mut events = merged_events(
            vec![inferno_wave_start_event(
                Tick(0),
                Stage::InfernoWave25,
                25,
                Ticks(852),
            )],
            StageStatus::Started,
            ServerTicks::Missing,
        );

        let mut cursor = EventCursor::new(&mut events, 0);
        assert!(!processor.process_challenge_event(&mut ctx, &mut cursor));
        assert_eq!(processor.wave_start_tick, Some(Ticks(852)));
    }

    #[test]
    fn pillar_death_events_record_collapse_waves() {
        let mut processor = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave66, ChallengeStatus::InProgress),
            None,
        )
        .unwrap();
        let mut ctx = StageContext::new(Stage::InfernoWave66, vec!["715".to_string()]);
        let mut events = merged_events(
            vec![npc_death_event(NpcEvent {
                tick: Tick(31),
                stage: Stage::InfernoWave66,
                coords: (2257, 5349),
                npc_id: npc::id::ROCKY_SUPPORT,
                room_id: 60001,
                hitpoints: SkillLevel {
                    current: 0,
                    base: 255,
                },
                ..Default::default()
            })],
            StageStatus::Started,
            ServerTicks::Missing,
        );

        let mut cursor = EventCursor::new(&mut events, 0);
        assert!(processor.process_challenge_event(&mut ctx, &mut cursor));
        assert_eq!(processor.data.west_pillar_collapse_wave, Some(66));
        assert_eq!(processor.data.east_pillar_collapse_wave, None);
        assert_eq!(processor.data.south_pillar_collapse_wave, None);
    }

    #[test]
    fn meleer_digs_and_mager_revives_accumulate() {
        let mut processor = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave43, ChallengeStatus::InProgress),
            None,
        )
        .unwrap();
        let mut ctx = StageContext::new(Stage::InfernoWave43, vec!["715".to_string()]);
        let mut events = merged_events(
            vec![
                npc_attack_event(
                    Tick(12),
                    Stage::InfernoWave43,
                    (2272, 5347),
                    7697,
                    61001,
                    NpcAttack::InfernoMeleerDig,
                    None,
                ),
                npc_attack_event(
                    Tick(30),
                    Stage::InfernoWave43,
                    (2277, 5340),
                    7699,
                    61002,
                    NpcAttack::InfernoMagerResurrect,
                    None,
                ),
                npc_attack_event(
                    Tick(55),
                    Stage::InfernoWave43,
                    (2274, 5347),
                    7697,
                    61001,
                    NpcAttack::InfernoMeleerDig,
                    None,
                ),
            ],
            StageStatus::Started,
            ServerTicks::Missing,
        );

        for index in [0, 1, 2] {
            let mut cursor = EventCursor::new(&mut events, index);
            assert!(processor.process_challenge_event(&mut ctx, &mut cursor));
        }
        assert_eq!(processor.data.meleer_digs, 2);
        assert_eq!(processor.data.mager_revives, 1);
    }

    fn wave_data(stage: Stage) -> WaveData {
        let wave = stage_to_wave(stage);
        WaveData {
            stage,
            ticks_lost: 0,
            offset: Ticks(0),
            npcs: Vec::new(),
            ticks: Ticks(61),
            start_tick: Ticks((wave - 1) * 67),
        }
    }

    #[test]
    fn full_recording_requires_every_wave_from_the_first() {
        let challenge = challenge_info(Stage::InfernoWave3, ChallengeStatus::InProgress);

        let contiguous = InfernoProcessor {
            challenge: challenge.clone(),
            data: CustomData {
                waves: vec![
                    wave_data(Stage::InfernoWave1),
                    wave_data(Stage::InfernoWave2),
                    wave_data(Stage::InfernoWave3),
                ],
                meleer_digs: 0,
                mager_revives: 0,
                west_pillar_collapse_wave: None,
                east_pillar_collapse_wave: None,
                south_pillar_collapse_wave: None,
            },
            wave_start_tick: None,
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(contiguous.has_fully_recorded_up_to(Stage::InfernoWave3));
        assert!(contiguous.has_fully_recorded_up_to(Stage::InfernoWave2));
        assert!(!contiguous.has_fully_recorded_up_to(Stage::InfernoWave4));
        assert!(!contiguous.has_fully_recorded_up_to(Stage::TobMaiden));

        let missing_waves = InfernoProcessor {
            challenge: challenge.clone(),
            data: CustomData {
                waves: vec![
                    wave_data(Stage::InfernoWave1),
                    wave_data(Stage::InfernoWave3),
                ],
                meleer_digs: 0,
                mager_revives: 0,
                west_pillar_collapse_wave: None,
                east_pillar_collapse_wave: None,
                south_pillar_collapse_wave: None,
            },
            wave_start_tick: None,
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(!missing_waves.has_fully_recorded_up_to(Stage::InfernoWave3));
        assert!(!missing_waves.has_fully_recorded_up_to(Stage::InfernoWave2));
        assert!(missing_waves.has_fully_recorded_up_to(Stage::InfernoWave1));

        let empty = InfernoProcessor {
            challenge,
            data: CustomData {
                waves: Vec::new(),
                meleer_digs: 0,
                mager_revives: 0,
                west_pillar_collapse_wave: None,
                east_pillar_collapse_wave: None,
                south_pillar_collapse_wave: None,
            },
            wave_start_tick: None,
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(!empty.has_fully_recorded_up_to(Stage::InfernoWave1));
    }

    #[tokio::test]
    async fn stage_finish_updates_challenge_ticks_from_start_time() {
        let Some(db) = db::test_database().await else {
            return;
        };
        let uuid = Uuid::new_v4();
        let mut txn = db
            .start_transaction(uuid, Trigger::Create { seq: JournalSeq(1) })
            .await
            .expect("guard should pass");

        let row = txn
            .query_one(
                "INSERT INTO challenges (uuid, type, scale) VALUES ($1, $2, $3) RETURNING id",
                &[&uuid, &(ChallengeType::Inferno as i16), &1i16],
            )
            .await
            .unwrap();
        txn.set_challenge_id(row.get(0));

        let mut processor = InfernoProcessor {
            challenge: challenge_info(Stage::InfernoWave25, ChallengeStatus::InProgress),
            data: InfernoProcessor::new(
                challenge_info(Stage::InfernoWave25, ChallengeStatus::InProgress),
                None,
            )
            .unwrap()
            .data,
            wave_start_tick: Some(Ticks(852)),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        let mut ctx = StageContext::new(Stage::InfernoWave25, vec!["715".to_string()]);
        let stored = StoredState {
            players: Vec::new(),
            challenge_ticks: Ticks(846),
            custom_data: None,
        };
        let events = merged_events(Vec::new(), StageStatus::Completed, ServerTicks::Precise(42));
        let price_resolver = PriceResolver::new(None);
        let ticks = processor
            .on_stage_finished(
                &txn,
                &price_resolver,
                &stored,
                &mut ctx,
                Stage::InfernoWave25,
                &events,
            )
            .await
            .unwrap();
        assert_eq!(ticks, ChallengeTicks::Set(Ticks(894)));
        assert_eq!(
            ctx.splits(Tick(43), true),
            vec![
                SavedSplit {
                    split: SplitType::InfernoWave25Time,
                    ticks: 42,
                    accurate: true,
                },
                SavedSplit {
                    split: SplitType::InfernoWave25Start,
                    ticks: 852,
                    accurate: false,
                },
            ],
        );
        assert_eq!(
            processor.data.waves,
            vec![WaveData {
                stage: Stage::InfernoWave25,
                ticks_lost: 42,
                offset: Ticks(42),
                npcs: Vec::new(),
                ticks: Ticks(42),
                start_tick: Ticks(852),
            }],
        );

        // Without a start time, adds the wave's ticks plus gap.
        let mut processor = InfernoProcessor::new(
            challenge_info(Stage::InfernoWave26, ChallengeStatus::InProgress),
            None,
        )
        .unwrap();
        let mut ctx = StageContext::new(Stage::InfernoWave26, vec!["715".to_string()]);
        let stored = StoredState {
            players: Vec::new(),
            challenge_ticks: Ticks(894),
            custom_data: None,
        };
        let events = merged_events(Vec::new(), StageStatus::Completed, ServerTicks::Precise(36));
        let ticks = processor
            .on_stage_finished(
                &txn,
                &price_resolver,
                &stored,
                &mut ctx,
                Stage::InfernoWave26,
                &events,
            )
            .await
            .unwrap();
        assert_eq!(ticks, ChallengeTicks::Add(Ticks(42)));
        assert_eq!(
            ctx.splits(Tick(37), true),
            vec![SavedSplit {
                split: SplitType::InfernoWave26Time,
                ticks: 36,
                accurate: true,
            }],
        );
        assert_eq!(processor.data.waves[0].start_tick, Ticks(900));
    }

    #[tokio::test]
    async fn on_finish_updates_splits_and_stats() {
        let Some(db) = db::test_database().await else {
            return;
        };
        let txn = db
            .start_transaction(Uuid::new_v4(), Trigger::Create { seq: JournalSeq(1) })
            .await
            .expect("guard should pass");

        for (stage, status, expected) in [
            (
                Stage::InfernoWave69,
                ChallengeStatus::Completed,
                PlayerStatsDelta {
                    inferno_completions: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::InfernoWave50,
                ChallengeStatus::Reset,
                PlayerStatsDelta {
                    inferno_resets: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::InfernoWave15,
                ChallengeStatus::Wiped,
                PlayerStatsDelta {
                    inferno_wipes: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::InfernoWave10,
                ChallengeStatus::Abandoned,
                PlayerStatsDelta::default(),
            ),
        ] {
            let mut processor = InfernoProcessor::new(challenge_info(stage, status), None).unwrap();
            let mut ctx = ChallengeContext::new(vec!["715".to_string()]);
            let stored = StoredState {
                players: vec![StoredPlayerInfo {
                    id: PlayerId(1),
                    gear: PrimaryMeleeGear::Unknown,
                }],
                challenge_ticks: Ticks(3168),
                custom_data: None,
            };
            processor
                .on_finish(&txn, &stored, &mut ctx, Ticks(3168))
                .await
                .unwrap();

            assert_eq!(
                ctx.challenge_splits().collect::<Vec<_>>(),
                vec![
                    (
                        SplitType::InfernoChallenge,
                        ChallengeSplit {
                            ticks: Ticks(3168),
                            accurate: None,
                        },
                    ),
                    (
                        SplitType::InfernoOverall,
                        ChallengeSplit {
                            ticks: Ticks(3168),
                            accurate: None,
                        },
                    ),
                ],
                "{status:?}",
            );
            let stats: Vec<_> = ctx.players().iter().map(|player| player.stats).collect();
            assert_eq!(stats, [expected], "{status:?}");
        }
    }

    #[tokio::test]
    #[expect(clippy::too_many_lines)]
    async fn finish_retroactively_applies_accuracy() {
        let Some(db) = db::test_database().await else {
            return;
        };
        let uuid = Uuid::new_v4();
        let mut txn = db
            .start_transaction(uuid, Trigger::Create { seq: JournalSeq(1) })
            .await
            .expect("guard should pass");

        let row = txn
            .query_one(
                "INSERT INTO challenges (uuid, type, scale) VALUES ($1, $2, $3) RETURNING id",
                &[&uuid, &(ChallengeType::Inferno as i16), &1i16],
            )
            .await
            .unwrap();
        let challenge_id: i32 = row.get(0);
        txn.set_challenge_id(challenge_id);

        let username = format!("715 {}", &uuid.to_string()[..7]);
        let row = txn
            .query_one(
                "INSERT INTO players (username, normalized_username) VALUES ($1, $2) RETURNING id",
                &[&username, &normalize_rsn(&username)],
            )
            .await
            .unwrap();
        let player_id: i32 = row.get(0);

        let row = txn
            .query_one(
                "INSERT INTO challenge_splits (challenge_id, type, scale, ticks, accurate)
                 VALUES ($1, $2, $3, $4, $5) RETURNING id",
                &[
                    &challenge_id,
                    &(SplitType::InfernoWave25Time as i16),
                    &1i16,
                    &42i32,
                    &true,
                ],
            )
            .await
            .unwrap();
        let time_split_id: i32 = row.get(0);
        txn.execute(
            "INSERT INTO personal_best_history (player_id, challenge_split_id) VALUES ($1, $2)",
            &[&player_id, &time_split_id],
        )
        .await
        .unwrap();

        let row = txn
            .query_one(
                "INSERT INTO challenge_splits (challenge_id, type, scale, ticks, accurate)
                 VALUES ($1, $2, $3, $4, $5) RETURNING id",
                &[
                    &challenge_id,
                    &(SplitType::InfernoWave25Start as i16),
                    &1i16,
                    &852i32,
                    &false,
                ],
            )
            .await
            .unwrap();
        let start_split_id: i32 = row.get(0);

        let mut challenge = challenge_info(Stage::InfernoWave69, ChallengeStatus::Completed);
        challenge.uuid = uuid;
        challenge.party = vec![username.clone()];
        let full_recording = CustomData {
            waves: (Stage::InfernoWave1 as i32..=Stage::InfernoWave69 as i32)
                .map(|value| wave_data(Stage::try_from(value).unwrap()))
                .collect(),
            meleer_digs: 0,
            mager_revives: 0,
            west_pillar_collapse_wave: None,
            east_pillar_collapse_wave: None,
            south_pillar_collapse_wave: None,
        };
        let stored = StoredState {
            players: vec![StoredPlayerInfo {
                id: PlayerId(player_id),
                gear: PrimaryMeleeGear::Unknown,
            }],
            challenge_ticks: Ticks(5940),
            custom_data: None,
        };

        // Recorded time doesn't match, so everything remains inaccurate.
        let mut processor = InfernoProcessor {
            challenge: challenge.clone(),
            data: full_recording.clone(),
            wave_start_tick: None,
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        let mut ctx = ChallengeContext::new(vec![username.clone()]);
        processor
            .on_finish(&txn, &stored, &mut ctx, Ticks(5938))
            .await
            .unwrap();
        let row = txn
            .query_one(
                "SELECT accurate FROM challenge_splits WHERE id = $1",
                &[&start_split_id],
            )
            .await
            .unwrap();
        assert!(!row.get::<_, bool>(0));

        // Recorded time matches.
        let mut processor = InfernoProcessor {
            challenge,
            data: full_recording,
            wave_start_tick: None,
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        let mut ctx = ChallengeContext::new(vec![username]);
        processor
            .on_finish(&txn, &stored, &mut ctx, Ticks(5940))
            .await
            .unwrap();

        let rows = txn
            .query(
                "SELECT accurate FROM challenge_splits WHERE challenge_id = $1",
                &[&challenge_id],
            )
            .await
            .unwrap();
        assert_eq!(rows.len(), 2);
        assert!(rows.iter().all(|row| row.get::<_, bool>(0)));

        let row = txn
            .query_one(
                "SELECT count(*) FROM personal_best_history WHERE player_id = $1
                 AND challenge_split_id = $2",
                &[&player_id, &start_split_id],
            )
            .await
            .unwrap();
        assert_eq!(row.get::<_, i64>(0), 1);
        let row = txn
            .query_one(
                "SELECT count(*) FROM personal_best_history WHERE player_id = $1
                 AND challenge_split_id = $2",
                &[&player_id, &time_split_id],
            )
            .await
            .unwrap();
        assert_eq!(row.get::<_, i64>(0), 1);
    }
}
