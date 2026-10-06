package com.toamistaketracker.detector.tracker;

import com.toamistaketracker.Raider;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Player;
import net.runelite.api.Varbits;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.OverheadTextChanged;
import net.runelite.api.events.VarbitChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.HashSet;
import java.util.Set;

@Slf4j
@Singleton
public class VengeanceTracker extends BaseRaidTracker {

    public static final String VENGEANCE_TEXT = "Taste vengeance!";

    @Getter
    private final Set<String> raidersVengeance = new HashSet<>();
    private final Set<String> raidersChatVengeance = new HashSet<>();

    @Override
    public void cleanup() {
        raidersVengeance.clear();
        raidersChatVengeance.clear();
    }

    @Override
    public void afterDetect() {
        cleanup();
    }

    @Subscribe
    public void onGameTick(GameTick event) {
        raidersVengeance.removeAll(raidersChatVengeance);
    }

    @Subscribe
    public void onVarbitChanged(VarbitChanged event) {
        if (event.getVarbitId() == Varbits.VENGEANCE_ACTIVE && event.getValue() == 0) {
            // Local player just procc'd veng
            if (client.getLocalPlayer() != null && raidState.isRaider(client.getLocalPlayer())) {
                raidersVengeance.add(client.getLocalPlayer().getName());
            }
        }
    }

    @Subscribe
    public void onOverheadTextChanged(OverheadTextChanged event) {
        if (!(event.getActor() instanceof Player) || event.getActor().getName() == null) return;

        String name = Text.sanitize(event.getActor().getName());
        if (isOtherVengeance(name, event.getOverheadText())) {
            raidersVengeance.add(name);
        }
    }

    @Subscribe
    public void onChatMessage(ChatMessage event) {
        if (event.getType() != ChatMessageType.PUBLICCHAT) return;

        String name = Text.sanitize(event.getName());
        if (isOtherVengeance(name, event.getMessage())) {
            raidersChatVengeance.add(name);
        }
    }

    public boolean didPopVengeance(Raider raider) {
        return raidersVengeance.contains(raider.getName());
    }

    /**
     * Determine if the given message is a vengeance message from another non-local player
     *
     * @param name    The name of the sender of the message
     * @param message The message
     * @return True if a non-local player has the vengeance text, false otherwise
     */
    private boolean isOtherVengeance(String name, String message) {
        if (client.getLocalPlayer() != null &&
                client.getLocalPlayer().getName() != null &&
                client.getLocalPlayer().getName().equals(name)) {
            return false;
        }

        return VENGEANCE_TEXT.equals(message);
    }
}

