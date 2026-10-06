package Toacito;

import Toacito.lobby.Lobby;
import com.google.inject.Provides;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;

import javax.inject.Inject;

@Slf4j
@PluginDescriptor(name = "TOA modern", description = "Some features for Tombs of Amascut",tags = {"pajau","toa","Tombs","of","amascut"})
public class ToacitoPlugin extends Plugin {
	@Inject
	private Client client;

	@Inject
	private ToacitoConfig config;

	@Inject
	private Lobby lobby;

	private Sala[] rooms = null;

	public static final int AKKHA_PATH_REGION = 14674;
	public static final int BABA_REGION = 15188;
	public static final int WARDEN_P12_REGION = 15184;

	@Provides
	ToacitoConfig getConfig(ConfigManager configManager) {
		return (ToacitoConfig) configManager.getConfig(ToacitoConfig.class);
	}

	@Override
	protected void startUp(){
		if (this.rooms==null){
			this.rooms=new Sala[]{}; //add rooms

			for(Sala wea: this.rooms){
				wea.init();
				wea.load();
			}
		}
	}

	@Override
	protected void shutDown(){
		for(Sala wea: this.rooms){
			wea.unload();
		}
	}

	@Subscribe
	void onGameStateChanged(GameStateChanged event){
		this.lobby.onGameStateChanged(event);
	}

	@Subscribe
	void onGameTick(GameTick event)throws Exception{
		this.lobby.onGameTick(event);
	}

	@Subscribe
	void onGameObjectSpawned(GameObjectSpawned event){
		this.lobby.onGameObjectSpawned(event);
	}
}
