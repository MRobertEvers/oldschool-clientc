//! Colosseum challenge processing.

use std::collections::BTreeSet;
use std::sync::LazyLock;

use async_trait::async_trait;
use blert::npc::id::{JAVELIN_COLOSSUS, MANTICORE, SERPENT_SHAMAN, SHOCKWAVE_COLOSSUS};
use blert::{Tick, Ticks};
use serde::{Deserialize, Serialize};

use super::StoredState;
use super::challenge_processor::{
    ChallengeContext, ChallengeProcessor, ChallengeTicks, EventCursor, RoomNpc, StageContext,
};
use super::db;
use super::spawn_index::{self, Arena, SpawnIndexer};
use super::split::SplitType;
use crate::lifecycle::core::types::{
    ChallengeInfo, ChallengeStatus, ProcessingError, Stage, StageStatus,
};
use crate::merging::MergedEvents;
use crate::metrics;
use crate::price::PriceResolver;
use crate::proto::{ChallengeData, Coords, challenge_data, event};

/// ID increment between consecutive levels of a handicap.
const HANDICAP_LEVEL_INCREMENT: u32 = 30;

const NUM_HANDICAPS: usize = 14;

static ARENA: LazyLock<Arena> = LazyLock::new(|| {
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
        &[
            SERPENT_SHAMAN,
            JAVELIN_COLOSSUS,
            MANTICORE,
            SHOCKWAVE_COLOSSUS,
        ],
    )
});

const WAVES: [&[u32]; 12] = [
    &[SERPENT_SHAMAN],
    &[SERPENT_SHAMAN, JAVELIN_COLOSSUS],
    &[SERPENT_SHAMAN, JAVELIN_COLOSSUS, JAVELIN_COLOSSUS],
    &[SERPENT_SHAMAN, MANTICORE],
    &[SERPENT_SHAMAN, JAVELIN_COLOSSUS, MANTICORE],
    &[
        SERPENT_SHAMAN,
        JAVELIN_COLOSSUS,
        JAVELIN_COLOSSUS,
        MANTICORE,
    ],
    &[JAVELIN_COLOSSUS, MANTICORE, SHOCKWAVE_COLOSSUS],
    &[
        JAVELIN_COLOSSUS,
        JAVELIN_COLOSSUS,
        MANTICORE,
        SHOCKWAVE_COLOSSUS,
    ],
    &[JAVELIN_COLOSSUS, MANTICORE, MANTICORE],
    &[JAVELIN_COLOSSUS, JAVELIN_COLOSSUS, MANTICORE, MANTICORE],
    &[JAVELIN_COLOSSUS, MANTICORE, MANTICORE, SHOCKWAVE_COLOSSUS],
    &[],
];

fn wave_index(stage: Stage) -> i32 {
    (stage as i32) - (Stage::ColosseumWave1 as i32)
}

fn wave_npcs(stage: Stage) -> &'static [u32] {
    usize::try_from(wave_index(stage))
        .ok()
        .and_then(|index| WAVES.get(index))
        .copied()
        .unwrap_or(&[])
}

/// In-flight Colosseum state stored between stages.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct CustomData {
    waves: Vec<WaveData>,
    /// Leveled ID of every handicap picked in wave order.
    handicaps: Vec<u32>,
    /// Times each base handicap has been picked, indexed by base ID.
    handicap_levels: [u32; NUM_HANDICAPS],
}

/// Final state of a processed wave.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct WaveData {
    stage: Stage,
    ticks_lost: u32,
    offset: Ticks,
    handicap: u32,
    options: Vec<u32>,
    npcs: Vec<RoomNpc>,
}

impl WaveData {
    fn to_proto(&self) -> challenge_data::ColosseumWave {
        challenge_data::ColosseumWave {
            stage: self.stage as i32,
            ticks_lost: self.ticks_lost,
            offset: Some(self.offset.0),
            handicap_chosen: self.handicap,
            handicap_options: self.options.clone(),
            npcs: self.npcs.iter().map(Into::into).collect(),
        }
    }
}

#[derive(Debug)]
pub struct ColosseumProcessor {
    challenge: ChallengeInfo,
    data: CustomData,
    /// Leveled ID of the handicap chosen this wave.
    selected_handicap: Option<u32>,
    /// Leveled IDs of the handicaps offered this wave.
    wave_handicap_options: Vec<u32>,
    spawn_indexer: SpawnIndexer<'static>,
}

impl ColosseumProcessor {
    pub fn new(
        challenge: ChallengeInfo,
        custom_data: Option<&serde_json::Value>,
    ) -> Result<ColosseumProcessor, ProcessingError> {
        let data = match custom_data {
            Some(value) => {
                serde_json::from_value(value.clone()).map_err(|error| ProcessingError {
                    retriable: false,
                    message: format!("custom data deserialization failed: {error}"),
                })?
            }
            None => CustomData {
                waves: Vec::new(),
                handicaps: Vec::new(),
                handicap_levels: [0; NUM_HANDICAPS],
            },
        };

        let spawn_indexer = SpawnIndexer::new(&ARENA, wave_npcs(challenge.stage).iter().copied());
        Ok(ColosseumProcessor {
            challenge,
            data,
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer,
        })
    }

    /// Returns the leveled ID of a base handicap at its current level.
    fn level_handicap(&self, handicap: u32) -> u32 {
        let level = self
            .data
            .handicap_levels
            .get(handicap as usize)
            .copied()
            .unwrap_or(0);
        handicap + level * HANDICAP_LEVEL_INCREMENT
    }

    fn extra_wave_npcs(&self, stage: Stage) -> &'static [u32] {
        let dynamic_duo = (event::ColosseumHandicap::DynamicDuo as i32).cast_unsigned();
        let picked = self.data.handicap_levels[dynamic_duo as usize] > 0;
        if picked && wave_npcs(stage).contains(&SHOCKWAVE_COLOSSUS) {
            &[SHOCKWAVE_COLOSSUS]
        } else {
            &[]
        }
    }
}

#[async_trait]
impl ChallengeProcessor for ColosseumProcessor {
    fn process_challenge_event(
        &mut self,
        _ctx: &mut StageContext,
        events: &mut EventCursor<'_>,
    ) -> bool {
        if events.current().r#type() == event::Type::ColosseumHandicapChoice {
            let event = events.current();
            let base = event.handicap.unwrap_or(0).cast_unsigned();
            let selected = self.level_handicap(base);
            let options = event
                .handicap_options
                .iter()
                .map(|&option| self.level_handicap(option.cast_unsigned()))
                .collect();

            self.selected_handicap = Some(selected);
            self.wave_handicap_options = options;

            if let Some(level) = self.data.handicap_levels.get_mut(base as usize) {
                *level += 1;
            }

            match self
                .data
                .handicaps
                .iter()
                .position(|&handicap| handicap + HANDICAP_LEVEL_INCREMENT == selected)
            {
                Some(index) => self.data.handicaps[index] = selected,
                None => self.data.handicaps.push(base),
            }
        }

        if events.current().r#type() == event::Type::NpcSpawn
            && let Some(npc) = &events.current().npc
        {
            let event = events.current();
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

    async fn on_create(&mut self, txn: &db::Transaction) -> Result<(), db::Error> {
        txn.execute(
            "INSERT INTO colosseum_challenge_stats (challenge_id) VALUES ($1)",
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
        let completed = events.status() == StageStatus::Completed;
        let challenge_ticks = stored.challenge_ticks + events.duration();
        let index = wave_index(stage);

        self.data.waves.push(WaveData {
            stage,
            ticks_lost: events.missing_tick_count(),
            offset: events.offset(),
            handicap: self.selected_handicap.unwrap_or(0),
            options: self.wave_handicap_options.clone(),
            npcs: ctx.npcs().cloned().collect(),
        });

        let split = SplitType::try_from(SplitType::ColosseumWave1 as i32 + index)
            .expect("wave splits are consecutive");
        ctx.set_stage_split(split, events.last_tick(), Tick(0), true);

        if completed
            && stage > Stage::ColosseumWave1
            && stage < Stage::ColosseumWave12
            && self.has_fully_recorded_up_to(stage)
        {
            let split = SplitType::try_from(SplitType::ColosseumWave3Start as i32 + index - 1)
                .expect("wave start splits are consecutive");
            ctx.set_challenge_split(
                split,
                challenge_ticks,
                Some(!self.challenge.party_changed && events.has_precise_server_tick_count()),
            );
        }

        // Store all handicaps selected, once per level.
        let handicaps: Vec<i16> = self
            .data
            .handicaps
            .iter()
            .flat_map(|&handicap| {
                let base = i16::try_from(handicap % HANDICAP_LEVEL_INCREMENT)
                    .expect("handicap bases fit in smallint");
                let level = handicap / HANDICAP_LEVEL_INCREMENT + 1;
                std::iter::repeat_n(base, level as usize)
            })
            .collect();
        txn.execute(
            "UPDATE colosseum_challenge_stats SET handicaps = $1 WHERE challenge_id = $2",
            &[&handicaps, &txn.challenge_id()],
        )
        .await?;

        let extra = self.extra_wave_npcs(stage);
        let indexer = std::mem::replace(&mut self.spawn_indexer, SpawnIndexer::new(&ARENA, []));
        let spawn = indexer.check(extra);
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
        spawn_index::save(txn, stage, spawn.as_ref(), player, !extra.is_empty()).await?;

        Ok(ChallengeTicks::Add(events.duration()))
    }

    async fn on_finish(
        &mut self,
        _txn: &db::Transaction,
        _stored: &StoredState,
        ctx: &mut ChallengeContext,
        final_ticks: Ticks,
    ) -> Result<(), db::Error> {
        ctx.set_challenge_split(SplitType::ColosseumChallenge, final_ticks, None);

        for index in 0..self.challenge.party.len() {
            if let Some(player) = ctx.player_mut(index) {
                match self.challenge.status {
                    ChallengeStatus::Completed => player.stats.colosseum_completions += 1,
                    ChallengeStatus::Reset => player.stats.colosseum_resets += 1,
                    ChallengeStatus::Wiped => player.stats.colosseum_wipes += 1,
                    ChallengeStatus::InProgress | ChallengeStatus::Abandoned => {}
                }
            }
        }

        Ok(())
    }

    fn custom_data(&self) -> Option<serde_json::Value> {
        Some(serde_json::to_value(&self.data).expect("custom data serializes"))
    }

    fn challenge_data(&self) -> Option<ChallengeData> {
        Some(ChallengeData {
            challenge_id: self.challenge.uuid.to_string(),
            stage_data: Some(challenge_data::StageData::Colosseum(
                challenge_data::Colosseum {
                    waves: self.data.waves.iter().map(WaveData::to_proto).collect(),
                    all_handicaps: self.data.handicaps.clone(),
                },
            )),
        })
    }

    fn has_fully_recorded_up_to(&self, stage: Stage) -> bool {
        if !(Stage::ColosseumWave1..=Stage::ColosseumWave12).contains(&stage) {
            return false;
        }
        let recorded: BTreeSet<i32> = self
            .data
            .waves
            .iter()
            .map(|wave| wave.stage as i32)
            .collect();
        (Stage::ColosseumWave1 as i32..=stage as i32).all(|value| recorded.contains(&value))
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;
    use crate::lifecycle::core::state::Trigger;
    use crate::lifecycle::core::types::{
        ChallengeMode, ChallengeStatus, ChallengeType, JournalSeq, PlayerId, PrimaryMeleeGear,
        StageStatus, Uuid,
    };
    use crate::merging::fixtures::{ServerTicks, colosseum_handicap_choice_event, merged_events};
    use crate::processing::StoredPlayerInfo;
    use crate::processing::split::ChallengeSplit;
    use crate::processing::stats::PlayerStatsDelta;
    use crate::proto::event::ColosseumHandicap;

    #[test]
    fn processor_starts_with_empty_data() {
        let processor = ColosseumProcessor::new(
            ChallengeInfo {
                uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
                session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
                challenge_type: ChallengeType::Colosseum,
                mode: ChallengeMode::NoMode,
                party: vec!["aSaradomin".to_string()],
                party_changed: false,
                stage: Stage::ColosseumWave1,
                stage_attempt: None,
                status: ChallengeStatus::InProgress,
                created_unix_ms: 0,
                reported_times: None,
                finished_unix_ms: None,
            },
            None,
        )
        .unwrap();
        assert_eq!(
            processor.custom_data(),
            Some(json!({
                "waves": [],
                "handicaps": [],
                "handicapLevels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
            })),
        );
    }

    #[test]
    fn malformed_custom_data_fails_construction() {
        let error = ColosseumProcessor::new(
            ChallengeInfo {
                uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
                session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
                challenge_type: ChallengeType::Colosseum,
                mode: ChallengeMode::NoMode,
                party: vec!["aSaradomin".to_string()],
                party_changed: false,
                stage: Stage::ColosseumWave1,
                stage_attempt: None,
                status: ChallengeStatus::InProgress,
                created_unix_ms: 0,
                reported_times: None,
                finished_unix_ms: None,
            },
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
    fn handicap_choices_accumulate() {
        let challenge = ChallengeInfo {
            uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
            session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
            challenge_type: ChallengeType::Colosseum,
            mode: ChallengeMode::NoMode,
            party: vec!["aSaradomin".to_string()],
            party_changed: false,
            stage: Stage::ColosseumWave1,
            stage_attempt: None,
            status: ChallengeStatus::InProgress,
            created_unix_ms: 0,
            reported_times: None,
            finished_unix_ms: None,
        };
        let mut processor = ColosseumProcessor::new(challenge.clone(), None).unwrap();
        let mut ctx = StageContext::new(Stage::ColosseumWave1, vec!["aSaradomin".to_string()]);
        let mut events = merged_events(
            vec![colosseum_handicap_choice_event(
                Tick(0),
                Stage::ColosseumWave1,
                ColosseumHandicap::Blasphemy,
                &[
                    ColosseumHandicap::Blasphemy,
                    ColosseumHandicap::Relentless,
                    ColosseumHandicap::Frailty,
                ],
            )],
            StageStatus::Started,
            ServerTicks::Missing,
        );

        let mut cursor = EventCursor::new(&mut events, 0);
        assert!(processor.process_challenge_event(&mut ctx, &mut cursor));
        assert_eq!(processor.selected_handicap, Some(4));
        assert_eq!(processor.wave_handicap_options, vec![4, 5, 12]);
        assert_eq!(processor.data.handicaps, vec![4]);

        let custom_data = processor.custom_data().unwrap();
        let mut processor = ColosseumProcessor::new(
            ChallengeInfo {
                stage: Stage::ColosseumWave2,
                ..challenge
            },
            Some(&custom_data),
        )
        .unwrap();
        let mut ctx = StageContext::new(Stage::ColosseumWave2, vec!["aSaradomin".to_string()]);
        let mut events = merged_events(
            vec![colosseum_handicap_choice_event(
                Tick(0),
                Stage::ColosseumWave2,
                ColosseumHandicap::Blasphemy,
                &[
                    ColosseumHandicap::Blasphemy,
                    ColosseumHandicap::Myopia,
                    ColosseumHandicap::Totemic,
                ],
            )],
            StageStatus::Started,
            ServerTicks::Missing,
        );

        let mut cursor = EventCursor::new(&mut events, 0);
        assert!(processor.process_challenge_event(&mut ctx, &mut cursor));
        assert_eq!(processor.selected_handicap, Some(34));
        assert_eq!(processor.wave_handicap_options, vec![34, 11, 7]);
        assert_eq!(processor.data.handicaps, vec![34]);

        assert_eq!(
            processor.custom_data(),
            Some(json!({
                "waves": [],
                "handicaps": [34],
                "handicapLevels": [0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0],
            })),
        );
    }

    #[test]
    fn full_recording_requires_every_wave_from_the_first() {
        fn wave_data(stage: Stage) -> WaveData {
            WaveData {
                stage,
                ticks_lost: 0,
                offset: Ticks(0),
                handicap: 3,
                options: vec![3, 7, 11],
                npcs: Vec::new(),
            }
        }

        let challenge = ChallengeInfo {
            uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
            session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
            challenge_type: ChallengeType::Colosseum,
            mode: ChallengeMode::NoMode,
            party: vec!["1Ogp".to_string()],
            party_changed: false,
            stage: Stage::ColosseumWave3,
            stage_attempt: None,
            status: ChallengeStatus::InProgress,
            created_unix_ms: 0,
            reported_times: None,
            finished_unix_ms: None,
        };

        let contiguous = ColosseumProcessor {
            challenge: challenge.clone(),
            data: CustomData {
                waves: vec![
                    wave_data(Stage::ColosseumWave1),
                    wave_data(Stage::ColosseumWave2),
                    wave_data(Stage::ColosseumWave3),
                ],
                handicaps: Vec::new(),
                handicap_levels: [0; NUM_HANDICAPS],
            },
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(contiguous.has_fully_recorded_up_to(Stage::ColosseumWave3));
        assert!(contiguous.has_fully_recorded_up_to(Stage::ColosseumWave2));
        assert!(!contiguous.has_fully_recorded_up_to(Stage::ColosseumWave4));
        assert!(!contiguous.has_fully_recorded_up_to(Stage::TobMaiden));

        let gapped = ColosseumProcessor {
            challenge: challenge.clone(),
            data: CustomData {
                waves: vec![
                    wave_data(Stage::ColosseumWave1),
                    wave_data(Stage::ColosseumWave2),
                    wave_data(Stage::ColosseumWave4),
                    wave_data(Stage::ColosseumWave5),
                ],
                handicaps: Vec::new(),
                handicap_levels: [0; NUM_HANDICAPS],
            },
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(!gapped.has_fully_recorded_up_to(Stage::ColosseumWave5));
        assert!(!gapped.has_fully_recorded_up_to(Stage::ColosseumWave4));
        assert!(!gapped.has_fully_recorded_up_to(Stage::ColosseumWave3));
        assert!(gapped.has_fully_recorded_up_to(Stage::ColosseumWave2));
        assert!(gapped.has_fully_recorded_up_to(Stage::ColosseumWave1));

        let empty = ColosseumProcessor {
            challenge,
            data: CustomData {
                waves: Vec::new(),
                handicaps: Vec::new(),
                handicap_levels: [0; NUM_HANDICAPS],
            },
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert!(!empty.has_fully_recorded_up_to(Stage::ColosseumWave1));
    }

    #[test]
    fn dynamic_duo_adds_extra_expected_npc() {
        let challenge = ChallengeInfo {
            uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
            session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
            challenge_type: ChallengeType::Colosseum,
            mode: ChallengeMode::NoMode,
            party: vec!["aSaradomin".to_string()],
            party_changed: false,
            stage: Stage::ColosseumWave7,
            stage_attempt: None,
            status: ChallengeStatus::InProgress,
            created_unix_ms: 0,
            reported_times: None,
            finished_unix_ms: None,
        };

        let with_dynamic_duo = ColosseumProcessor {
            challenge: challenge.clone(),
            data: CustomData {
                waves: Vec::new(),
                handicaps: vec![64, 42, 5, 9],
                handicap_levels: [0, 0, 0, 0, 3, 1, 0, 0, 0, 1, 0, 0, 2, 0],
            },
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert_eq!(
            with_dynamic_duo.extra_wave_npcs(Stage::ColosseumWave7),
            [SHOCKWAVE_COLOSSUS],
        );
        assert_eq!(
            with_dynamic_duo.extra_wave_npcs(Stage::ColosseumWave9),
            [] as [u32; 0]
        );

        let without_dynamic_duo = ColosseumProcessor {
            challenge,
            data: CustomData {
                waves: Vec::new(),
                handicaps: vec![64, 42, 5],
                handicap_levels: [0, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0, 2, 0],
            },
            selected_handicap: None,
            wave_handicap_options: Vec::new(),
            spawn_indexer: SpawnIndexer::new(&ARENA, []),
        };
        assert_eq!(
            without_dynamic_duo.extra_wave_npcs(Stage::ColosseumWave7),
            [] as [u32; 0]
        );
        assert_eq!(
            without_dynamic_duo.extra_wave_npcs(Stage::ColosseumWave9),
            [] as [u32; 0]
        );
    }

    #[tokio::test]
    async fn on_finish_records_challenge_splits_and_stats() {
        let Some(db) = db::test_database().await else {
            return;
        };
        let txn = db
            .start_transaction(Uuid::new_v4(), Trigger::Create { seq: JournalSeq(1) })
            .await
            .expect("guard should pass");

        for (stage, status, expected) in [
            (
                Stage::ColosseumWave12,
                ChallengeStatus::Completed,
                PlayerStatsDelta {
                    colosseum_completions: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::ColosseumWave5,
                ChallengeStatus::Reset,
                PlayerStatsDelta {
                    colosseum_resets: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::ColosseumWave3,
                ChallengeStatus::Wiped,
                PlayerStatsDelta {
                    colosseum_wipes: 1,
                    ..PlayerStatsDelta::default()
                },
            ),
            (
                Stage::ColosseumWave2,
                ChallengeStatus::Abandoned,
                PlayerStatsDelta::default(),
            ),
        ] {
            let mut processor = ColosseumProcessor::new(
                ChallengeInfo {
                    uuid: "a8cb035f-410a-45de-a4d3-2b0a5d8b464d".parse().unwrap(),
                    session_uuid: "5e55b41c-6a3f-4a89-9e10-c1a7d2f3b804".parse().unwrap(),
                    challenge_type: ChallengeType::Colosseum,
                    mode: ChallengeMode::NoMode,
                    party: vec!["1Ogp".to_string()],
                    party_changed: false,
                    stage,
                    stage_attempt: None,
                    status,
                    created_unix_ms: 0,
                    reported_times: None,
                    finished_unix_ms: None,
                },
                None,
            )
            .unwrap();

            let mut ctx = ChallengeContext::new(vec!["1Ogp".to_string()]);
            let stored = StoredState {
                players: vec![StoredPlayerInfo {
                    id: PlayerId(1),
                    gear: PrimaryMeleeGear::Unknown,
                }],
                challenge_ticks: Ticks(1743),
                custom_data: None,
            };
            processor
                .on_finish(&txn, &stored, &mut ctx, Ticks(1743))
                .await
                .unwrap();

            assert_eq!(
                ctx.challenge_splits().collect::<Vec<_>>(),
                vec![(
                    SplitType::ColosseumChallenge,
                    ChallengeSplit {
                        ticks: Ticks(1743),
                        accurate: None,
                    },
                )],
                "{status:?}",
            );
            let stats: Vec<_> = ctx.players().iter().map(|player| player.stats).collect();
            assert_eq!(stats, [expected], "{status:?}");
        }
    }
}
