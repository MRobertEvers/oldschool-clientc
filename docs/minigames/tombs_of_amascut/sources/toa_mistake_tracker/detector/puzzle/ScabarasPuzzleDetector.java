package com.toamistaketracker.detector.puzzle;

import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;

import static com.toamistaketracker.RaidRoom.SCABARAS_PUZZLE;

/**
 *
 */
@Slf4j
@Singleton
public class ScabarasPuzzleDetector extends BaseMistakeDetector {

    public ScabarasPuzzleDetector() {
    }

    @Override
    public void cleanup() {
    }

    @Override
    public RaidRoom getRaidRoom() {
        return SCABARAS_PUZZLE;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        return mistakes;
    }

    @Override
    public void afterDetect() {
    }
}