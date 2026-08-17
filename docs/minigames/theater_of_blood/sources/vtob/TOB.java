package bot;

import java.awt.event.KeyEvent;
import java.util.Arrays;
import java.util.List;

import Bot.VScript;
import GUI.GUI;
import GUI.guicomponents.GUISettingsProvider;
import GUI.loadouts.EquipmentLoadout;
import GUI.loadouts.EquipmentLoadout.Gears;
import GUI.loadouts.InventoryLoadout;
import GUI.loadouts.PrayerLoadout;
import GUI.loadouts.choice.InventoryChoice;
import GUI.loadouts.choices.ItemGroups;
import GUI.loadouts.choices.PrayerGroups;
import bosses.NycolasVasilias;
import bosses.PestilentBloat;
import bosses.TheMaidenofSugadinti;
import bosses.TobBoss;
import interfaces.Drawable;
import simple.api.actions.SimpleNpcActions;
import simple.api.script.Category;
import simple.api.script.LoopingScript;
import simple.api.script.ScriptManifest;

@ScriptManifest(author = "Vainiven & FVZ", category = Category.MONEYMAKING, description = "Dikke lul drie bier script", discord = "Vainven#6986", name = "V-TOB", servers = {
		"Xeros" }, version = "0.1")

public class TOB extends VScript implements GUISettingsProvider, LoopingScript {

	private final PrayerLoadout prayerLoadout = new PrayerLoadout(PrayerGroups.MELEE_PRAYER, PrayerGroups.RANGED_PRAYER,
			PrayerGroups.MAGIC_PRAYER);
	private final InventoryLoadout inventoryLoadout = new InventoryLoadout(
			new InventoryChoice(ItemGroups.MELEE_POTION, 1, 0), new InventoryChoice(ItemGroups.RANGED_POTION, 1, 0),
			new InventoryChoice(ItemGroups.MAGIC_POTION, 1, 0), new InventoryChoice(ItemGroups.FOOD, 19, 4),
			new InventoryChoice(ItemGroups.PRAYER_POTION, 5, 0), new InventoryChoice(ItemGroups.PET, 1, 0),
			new InventoryChoice(ItemGroups.SPEC_WEAPONS, 1, 0));
	final EquipmentLoadout loadouts = new EquipmentLoadout(Gears.MELEE, Gears.RANGED, Gears.MAGIC);
	private final GUI gui;
	private final TobBoss[] bosses = { new TheMaidenofSugadinti(), new PestilentBloat(), new NycolasVasilias() };
	private TobBoss currentBoss = getCurrentRoom();

	public TOB() {
		super(null);
		gui = new GUI(this, new GUISettingsProvider() {

			@Override
			public List<Drawable> getLoadouts() {
				return Arrays.asList(loadouts.getGear(Gears.MELEE));
			}

			@Override
			public String getName() {
				return "Melee Loadout";
			}

		}, new GUISettingsProvider() {

			@Override
			public List<Drawable> getLoadouts() {
				return Arrays.asList(loadouts.getGear(Gears.MAGIC));
			}

			@Override
			public String getName() {
				return "Magic Loadout";
			}

		}, new GUISettingsProvider() {

			@Override
			public List<Drawable> getLoadouts() {
				return Arrays.asList(loadouts.getGear(Gears.RANGED));
			}

			@Override
			public String getName() {
				return "Ranged Loadout";
			}
		});
		bank.forceBank();
	}

	@Override
	public List<Drawable> getLoadouts() {
		return Arrays.asList(prayerLoadout, inventoryLoadout);
	}

	@Override
	public void onProcess() {
		if (!gui.isVisible()) {
			if (currentBoss == null) {
				if (players.isAtTOB()) {
					if (!players.useHealingBox() && !bank.bank(inventoryLoadout, loadouts)) {
						if (players.inParty("ToB Party (")) {
							if (players.isPartyLeader()) {
								handleToBPartyDialogue();
							}
						} else {
							players.createParty();
						}
					}
				} else {
					ctx.teleporter.teleportStringPath("Minigames", "Theatre of Blood");
				}
			} else // Room fight
			if (currentBoss.getBoss() != null) {
				inventory.equip(loadouts.getGear(currentBoss.getLoadout()));
				prayers.usePrayers(currentBoss.getPrayers().toArray(new PrayerGroups[0]), prayerLoadout, false);
				inventory.dropEmptyVials();
				inventory.usePotions();
				inventory.eat();
				if (!currentBoss.move() && !currentBoss.getBoss().equals(ctx.players.getLocal().getInteracting())) {
					currentBoss.getBoss().interact(SimpleNpcActions.FIRST);
				}

				// When boss is dead
			} else if (currentBoss.isDead()) {
				if (!currentBoss.getToNextRoom()) {
					prayers.disablePrayers(currentBoss.getPrayers().toArray(new PrayerGroups[0]), prayerLoadout, false);
					currentBoss = getCurrentRoom();
				}
			} else {
				currentBoss.goToBoss();
			}
		}
	}

	public void handleToBPartyDialogue() {
		final String widgetText = ctx.widgets.populate().filter(365).next().getText();
		final int widgetId = ctx.widgets.getBackDialogId();

		if (!ctx.dialogue.dialogueOpen() && !ctx.objects.populate().filter(32653).isEmpty()) {
			ctx.objects.nearest().next().interact(502);
			ctx.onCondition(() -> ctx.dialogue.dialogueOpen());
		} else if (widgetId == 2469) {
			ctx.keyboard.clickKey(KeyEvent.VK_1);
		} else if (widgetId == 363 && (widgetText.contains("1/1") || widgetText.contains("2/2")
				|| widgetText.contains("3/3") || widgetText.contains("4/4") || widgetText.contains("5/5"))) {
			ctx.menuActions.sendAction(679, 3634, 50, 367);
		} else if (widgetId == 2459) {
			ctx.keyboard.clickKey(KeyEvent.VK_1);
			ctx.onCondition(() -> (currentBoss = getCurrentRoom()) != null, 250, 40);
		} else {
			ctx.sleep(1000);
		}
	}

	public TobBoss getCurrentRoom() {
		for (final TobBoss boss : bosses) {
			if (players.inRegion(boss.getRegions())) {
				return boss;
			}
		}
		return null;
	}

	@Override
	public int loopDuration() {
		return 200;
	}

}