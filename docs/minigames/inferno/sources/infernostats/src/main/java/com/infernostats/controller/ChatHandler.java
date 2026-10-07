package com.infernostats.controller;

import com.infernostats.InfernoStatsConfig;
import com.infernostats.InfernoStatsPlugin;
import com.infernostats.events.*;
import com.infernostats.model.Location;
import com.infernostats.model.Wave;
import com.infernostats.model.WaveSplit;
import com.infernostats.model.WaveState;
import com.infernostats.view.TimeFormatting;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.events.ChatMessage;
import net.runelite.client.chat.ChatColorType;
import net.runelite.client.chat.ChatMessageBuilder;
import net.runelite.client.chat.ChatMessageManager;
import net.runelite.client.chat.QueuedMessage;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Inject;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Slf4j
public class ChatHandler {
  @Inject
  private Client client;

  @Inject
  private EventBus eventBus;

  @Inject
  private ChatMessageManager chatMessageManager;

  private final InfernoStatsPlugin plugin;
  private final InfernoStatsConfig config;

  private static final Pattern TZHAAR_WAVE_MESSAGE = Pattern.compile("Wave: (\\d+)");
  private static final String TZHAAR_WAVE_COMPLETED_MESSAGE = "Wave completed!";
  private static final String TZHAAR_DEFEATED_MESSAGE = "You have been defeated!";
  private static final Pattern TZHAAR_DURATION_MESSAGE =
      Pattern.compile("Duration: (\\d{1,2}):(\\d{2})(?:\\.(\\d{2}))?");
  private static final Pattern TZHAAR_PAUSED_MESSAGE =
      Pattern.compile("The (?:Inferno|Fight Cave) has been paused. You may now log out.");

  @Inject
  protected ChatHandler(InfernoStatsPlugin plugin, InfernoStatsConfig config) {
    this.plugin = plugin;
    this.config = config;
  }

  @Subscribe
  public void onChatMessage(ChatMessage e) {
    final String message = Text.removeTags(e.getMessage());
    if (e.getType() != ChatMessageType.SPAM && e.getType() != ChatMessageType.GAMEMESSAGE) {
      return;
    }

    if (message.equals(TZHAAR_DEFEATED_MESSAGE)) {
      eventBus.post(new RunCompletedEvent(WaveState.FAILED));
      eventBus.post(new TimerStoppedEvent());
      return;
    }

    Matcher durationMatcher = TZHAAR_DURATION_MESSAGE.matcher(message);
    if (durationMatcher.find()) {
      if (plugin.getTimerState() != TimerHandler.TimerState.RUNNING) {
        return;
      }

      final int duration = extractDuration(durationMatcher);
      eventBus.post(new RunCompletedEvent(WaveState.SUCCESS, duration));
      eventBus.post(new TimerStoppedEvent());
      return;
    }

    if (TZHAAR_PAUSED_MESSAGE.matcher(message).find()) {
      eventBus.post(new TimerPausedEvent());
      return;
    }

    if (message.equals(TZHAAR_WAVE_COMPLETED_MESSAGE)) {
      eventBus.post(new WaveFinishedEvent(WaveState.FINISHED));

      if (config.showIdleTicksInChatbox()) {
        final String waveMessage = new ChatMessageBuilder()
            .append(ChatColorType.HIGHLIGHT)
            .append("Idle Ticks: " + this.plugin.getCurrentWave().getIdleTicks())
            .build();

        chatMessageManager.queue(
            QueuedMessage.builder()
                .type(ChatMessageType.CONSOLE)
                .runeLiteFormattedMessage(waveMessage)
                .build());
      }

      if (config.waveTimes()) {
        final String waveMessage = new ChatMessageBuilder()
            .append(ChatColorType.HIGHLIGHT)
            .append("Wave Completed in: " + TimeFormatting.getCurrentWaveTime(this.plugin.getCurrentWave()))
            .build();

        chatMessageManager.queue(
            QueuedMessage.builder()
                .type(ChatMessageType.CONSOLE)
                .runeLiteFormattedMessage(waveMessage)
                .build());
      }

      return;
    }

    Matcher matcher = TZHAAR_WAVE_MESSAGE.matcher(message);
    if (matcher.find()) {
      final int waveId = Integer.parseInt(matcher.group(1));
      Wave wave = new Wave(waveId, this.plugin.getRunDuration());

      if (wave.getId() == 1) {
        wave.setDuration(0);
        if (this.plugin.isInInferno()) {
          // The first wave message is 10 ticks after the timer starts
          eventBus.post(new TimerStartedEvent(10));
        } else if (this.plugin.isInFightCaves()) {
          // Fight caves does not have a delay on the timer
          eventBus.post(new TimerStartedEvent(0));
        } else {
          return;
        }
      } else if (config.tzhaarTimerState() == TimerHandler.TimerState.PAUSED) {
        eventBus.post(new TimerStartedEvent(0));
      }

      if (this.plugin.isInInferno()) {
        wave.setLocation(Location.INFERNO);
      } else if (this.plugin.isInFightCaves()) {
        wave.setLocation(Location.FIGHT_CAVES);
      } else {
        return;
      }

      eventBus.post(new WaveStartedEvent(wave));

      if (config.splitTimes() && wave.isSplit()) {
        final ChatMessageBuilder chatMessageBuilder = new ChatMessageBuilder()
            .append(ChatColorType.HIGHLIGHT)
            .append("Wave Split: ")
            .append(TimeFormatting.formatDuration(wave.start()));

        if (config.showTargetSplitTimes()) {
          chatMessageBuilder.append(WaveSplit.GoalDifference(wave));
        }

        final String splitMessage = chatMessageBuilder.build();

        chatMessageManager.queue(
            QueuedMessage.builder()
                .type(ChatMessageType.CONSOLE)
                .runeLiteFormattedMessage(splitMessage)
                .build());
      }

      if (config.predictedCompletionTime() && wave.isSplit()) {
        if (wave.getLocation() == Location.FIGHT_CAVES)
          return;

        wave.setPace(WaveSplit.PredictedTime(wave));

        final String predictedMessage = new ChatMessageBuilder()
            .append(ChatColorType.HIGHLIGHT)
            .append("Predicted Time: " + TimeFormatting.formatDuration(wave.getPace()))
            .build();

        chatMessageManager.queue(
            QueuedMessage.builder()
                .type(ChatMessageType.CONSOLE)
                .runeLiteFormattedMessage(predictedMessage)
                .build());
      }
    }
  }

  public static int extractDuration(Matcher matcher) {
    int minutes = Integer.parseInt(matcher.group(1));
    int seconds = Integer.parseInt(matcher.group(2));
    int hundredths = matcher.group(3) != null ? Integer.parseInt(matcher.group(3)) : -1;

    double totalSeconds;
    if (hundredths >= 0) {
      // precise timing is turned on
      totalSeconds = minutes * 60 + seconds + hundredths / 100.0;
    } else {
      // imprecise: round up to next valid tick
      totalSeconds = minutes * 60 + seconds;
      double remainder = totalSeconds % TimeFormatting.SECONDS_PER_TICK;
      if (remainder > 0) {
        totalSeconds += (TimeFormatting.SECONDS_PER_TICK - remainder);
      }
    }

    return (int) Math.round(totalSeconds / TimeFormatting.SECONDS_PER_TICK);
  }
}