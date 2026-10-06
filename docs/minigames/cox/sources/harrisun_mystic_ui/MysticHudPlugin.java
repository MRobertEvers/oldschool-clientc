package com.pluginideahub.mystichud;

import com.google.inject.Provides;
import java.awt.Rectangle;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.events.BeforeRender;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.VarbitChanged;
import net.runelite.api.gameval.InterfaceID;
import net.runelite.api.gameval.VarPlayerID;
import net.runelite.api.gameval.VarbitID;
import net.runelite.api.widgets.Widget;
import net.runelite.api.widgets.WidgetPositionMode;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.input.MouseAdapter;
import net.runelite.client.input.MouseManager;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.overlay.OverlayManager;

/**
 * Rebuilds the minimap/orb area as a rectangular block sitting directly above the
 * inventory, in either resizable layout.
 *
 * All widget ids below were read from the interface cache export. Group 160 is the orbs
 * interface and is the same whichever layout is up; the toplevel is 164 in resizable
 * modern and 161 in resizable classic, and only two of its children move between them.
 * The circular minimap shape is nothing but mask sprite 1178 sitting on the
 * contentType-1338 map widget, so clearing that sprite id yields a square map.
 */
@Slf4j
@PluginDescriptor(
	name = "Harrisun Mystic UI",
	description = "Minimap and orbs as a rectangular block above the inventory",
	tags = {"minimap", "orbs", "layout", "hud", "ui", "mystic", "harrisun"}
)
public class MysticHudPlugin extends Plugin
{
	// The two resizable toplevels. The gameval names are inverted from intuition, so the
	// WidgetID names are given here as the readable ones: TOPLEVEL_PRE_EOC is the resizable
	// MODERN bottom-line layout, TOPLEVEL_OSRS_STRETCH is resizable CLASSIC's old-school box.
	// Read a gameval name alone and you will pick the wrong group.
	static final int TOPLEVEL_MODERN = InterfaceID.TOPLEVEL_PRE_EOC;      // 164
	static final int TOPLEVEL_CLASSIC = InterfaceID.TOPLEVEL_OSRS_STRETCH; // 161

	// children INSIDE the minimap block are identical in both layouts
	static final int MINIMAP_INNER = 22;   // inner container the map widgets live in
	static final int MINIMAP_MAP = 30;     // contentType 1338, the actual map draw target
	static final int MINIMAP_COMPASS = 29; // contentType 1339, round compass + its mask
	static final int MINIMAP_RING = 32;    // sprite 1177 circular surround
	static final int ORB_HOST = 33;        // 207x197 container group 160 loads into

	// ...and these two do NOT. verified against the gameval constants rather than guessed:
	// MAP_CONTAINER is 164:92 / 161:95, SIDE_CONTAINER is 164:96 / 161:73
	static final int MINIMAP_BLOCK_MODERN = 92;  // 211x207 container holding minimap + orbs
	static final int MINIMAP_BLOCK_CLASSIC = 95;
	static final int INV_PANEL_MODERN = 96;      // 204x275 inventory panel
	static final int INV_PANEL_CLASSIC = 73;

	/**
	 * The world map interface. Our host widget is invisible but sets noClickThrough, and
	 * with the xp orb on it is 276 wide across the top right, which is where the world
	 * map's own close button sits. So it ate that click and the map could not be closed.
	 * While the map is open the block stops swallowing clicks.
	 */
	static final int WORLDMAP_GROUP = InterfaceID.WORLDMAP;

	/** True when the id is one of the two resizable toplevels we can lay out in. */
	static boolean isResizableToplevel(int toplevel)
	{
		return toplevel == TOPLEVEL_MODERN || toplevel == TOPLEVEL_CLASSIC;
	}

	/** The minimap block's child id for a given toplevel. */
	static int minimapBlockChild(int toplevel)
	{
		return toplevel == TOPLEVEL_CLASSIC ? MINIMAP_BLOCK_CLASSIC : MINIMAP_BLOCK_MODERN;
	}

	/** The inventory panel's child id for a given toplevel. */
	static int invPanelChild(int toplevel)
	{
		return toplevel == TOPLEVEL_CLASSIC ? INV_PANEL_CLASSIC : INV_PANEL_MODERN;
	}

	// orbs (group 160): container / capsule-backing child per orb
	static final int ORBS = 160;
	static final int HP = 7, HP_BACK = 8;
	static final int PRAYER = 18, PRAYER_BACK = 19;
	static final int RUN = 26, RUN_BACK = 27;
	static final int SPEC = 34, SPEC_BACK = 35;
	// 160:6 carries sprite 1196, the XP disc. 160:48 (sprites 4547 + 3018) is the
	// activity adviser and 160:43 (1609 + 1668) is the store orb; both fall to the sweep
	static final int XP_ORB = 6;
	static final int WORLD_MAP_ORB = 49;
	// the glossy sphere art inside each orb (fill sprite + status overlay containers);
	// 160:16 stays visible, the hp icon turned out to live under it
	static final int[] SPHERES = {11, 12, 14, 22, 23, 30, 31, 38, 39, 41};
	// group-160 root children we lay out ourselves; everything else at root level is
	// hidden outright, which catches stray orbs whatever their child id turns out to be
	static final int[] PLACED = {HP, PRAYER, RUN, SPEC, XP_ORB, WORLD_MAP_ORB};
	// the value text and icon children of each orb container
	static final int[] ORB_TEXT = {10, 21, 29, 37};
	static final int[] ORB_ICONS = {17, 25, 33, 42};

	// set when any widget value actually changed this frame, so the expensive subtree
	// revalidate only runs when something moved
	private boolean dirty;
	// frames still to force a revalidate for. the client rebuilds the orb children just
	// after login with our values already in place, so change detection sees "nothing
	// moved" and never revalidates, leaving icons and numbers unrendered until something
	// else nudges the layout
	private int settle;
	// current orb row height; compresses when the window is short
	private int rowH = ROW_H;
	// current map width; insets when engulfed by the inventory panel
	private int mapW = NATIVE_MAP;

	int rowH()
	{
		return rowH;
	}

	// live orb states, read off VarbitChanged rather than polled in the overlay's render.
	// written on the client thread, read on the draw thread, hence volatile.
	private volatile boolean specArmed;
	private volatile boolean running;
	private volatile boolean staminaActive;
	private volatile boolean quickPrayersOn;
	private volatile HealthState healthState = HealthState.NORMAL;

	enum HealthState
	{
		NORMAL,
		POISONED,
		VENOMED,
		DISEASED
	}

	boolean specArmed()
	{
		return specArmed;
	}

	boolean running()
	{
		return running;
	}

	boolean staminaActive()
	{
		return staminaActive;
	}

	boolean quickPrayersOn()
	{
		return quickPrayersOn;
	}

	HealthState healthState()
	{
		return healthState;
	}

	/**
	 * Re-reads every orb state. Cheap enough to do wholesale on any varbit or varp
	 * change rather than working out which one moved, and that way a state can never be
	 * missed because its var was not in a list.
	 */
	private void readOrbStates()
	{
		specArmed = client.getVarpValue(VarPlayerID.SA_ATTACK) != 0;
		running = client.getVarpValue(VarPlayerID.OPTION_RUN) != 0;
		staminaActive = client.getVarbitValue(VarbitID.STAMINA_ACTIVE) != 0;
		quickPrayersOn = client.getVarbitValue(VarbitID.QUICKPRAYER_ACTIVE) != 0;

		// venom lives inside the poison varp as a magnitude, it has no var of its own.
		// order and thresholds taken from RuneLite's own StatusBarsOverlay.
		int poison = client.getVarpValue(VarPlayerID.POISON);
		if (poison >= VENOM_THRESHOLD)
		{
			healthState = HealthState.VENOMED;
		}
		else if (poison > 0)
		{
			healthState = HealthState.POISONED;
		}
		else if (client.getVarpValue(VarPlayerID.DISEASE) > 0)
		{
			healthState = HealthState.DISEASED;
		}
		else
		{
			healthState = HealthState.NORMAL;
		}
	}

	// poison varp counts up; at or past this it is venom instead
	private static final int VENOM_THRESHOLD = 1000000;

	// geometry, all relative to the 211-wide minimap block
	// extra block width so an orb beside the map is not clipped off the edge. needed for
	// the xp orb and for the world map orb: both live in this strip, because anything
	// sitting ON the map gets its clicks eaten by the map container, which is drawn over
	// them and cannot be reordered.
	static final int SIDE_PAD = 72;
	static final int NATIVE_MAP = 152; // the engine's fixed minimap draw HEIGHT
	static final int INV_W = 204;      // inventory panel width; the map matches it
	// the map widget sits at (0,0) in its container: the ENGINE anchors the minimap
	// draw at the container origin (proven by the map bleeding exactly 7px left and 8px
	// up of the frame with the stock offsets), so widget offsets must be zero for the
	// draw, the widget and the frame to coincide
	static final int MAP_TOP = 0;
	static final int ROW_GAP = 6; // a bar of frame art sits between map and orb row
	static final int ROW_H = 40;
	static final int BLOCK_GAP = 6; // wide enough for a vertical bar between blocks
	static final int ORB_W = 57;           // stock orb container size
	static final int ORB_H = 34;
	// painted compass, offset from the map's top-left corner. inset 8 so its outer ring
	// (3px beyond the disc) clears the 4px frame band with a 1px breath instead of
	// sitting on it
	static final int COMPASS_INSET = 11; // ring starts at 8: exactly clear of the 8px silver corner
	static final int COMPASS_SIZE = 26;
	// world map orb inset, chosen by rendering the variants and looking: 5 puts the
	// orb's outer edge 5px off its corner, the same as the compass ring on the opposite
	// corner, clear of the 4px frame band
	// vertical inset to the bottom divider; the horizontal inset adds the 6px side bar
	// so the visible gap reads the same on both axes
	static final int ORB_INSET = 6;  // lower, snug to the bottom divider
	static final int ORB_INSET_X = 12;
	// height of the whole side stack in resizable modern: 204x275 panel plus two 36px
	// tab rows. used as a STABLE anchor so the block does not jump when a tab opens
	static final int INV_STACK_H = 275 + 2 * 36;
	// bump when the meaning of the saved drag offsets changes
	static final int LAYOUT_VERSION = 3;
	// bumped EVERY build; painted on screen so a stale client is instantly obvious
	static final String BUILD_TAG = "b104-hub";

	@Inject
	private Client client;

	@Inject
	private ClientThread clientThread;

	@Inject
	private OverlayManager overlayManager;

	@Inject
	private MysticHudConfig config;

	@Inject
	private MysticHudFrameOverlay frameOverlay;

	@Inject
	private MouseManager mouseManager;

	@Inject
	private ConfigManager configManager;

	// the debug readout is a support tool, not a feature: it paints the build tag and the
	// numbers the layout is working from. gated here so it cannot do anything in a normal
	// client even with the config switched on.
	@Inject
	@javax.inject.Named("developerMode")
	private boolean developerMode;

	boolean debugReadout()
	{
		return developerMode && config.orbDebug();
	}

	// live drag state; offsets persist through the hidden posX/posY config keys.
	// written on the AWT thread, read on the client thread
	private volatile Rectangle mapBounds;
	// the map's OWN rect. same as mapBounds at full width; inset inside it at native
	// width. anything that must sit on the map rather than on the frame uses this.
	private volatile Rectangle mapDrawRect;
	private volatile int offX;
	private volatile int offY;
	private boolean dragging;
	private int lastLoggedOffY = Integer.MIN_VALUE;
	private int grabX, grabY, dragBaseX, dragBaseY;

	private final MouseAdapter mouse = new MouseAdapter()
	{
		@Override
		public MouseEvent mousePressed(MouseEvent e)
		{
			Rectangle b = mapBounds;
			// the painted compass has no widget, so its click is handled here: face
			// north and consume so the map underneath cannot turn it into a walk
			if (b != null && config.showCompass()
				&& e.getButton() == MouseEvent.BUTTON1 && !e.isShiftDown())
			{
				Rectangle cb = compassBounds(mapRect());
				cb.grow(3, 3); // forgiving edge
				boolean hit = hits(cb, e);
				log.debug("MHUD compass click win=({},{}) cb={} hit={}",
					e.getX(), e.getY(), cb, hit);
				if (hit)
				{
					clientThread.invokeLater(() -> client.setCameraYawTarget(0));
					e.consume();
					return e;
				}
			}
			// shift, not alt: Windows and several window managers swallow alt-drag.
			// free mode only: attached is glued and ignores position entirely
			Rectangle zone = blockZone();
			if (e.isShiftDown() && !attachInventory() && zone != null && hits(zone, e))
			{
				dragging = true;
				grabX = e.getX();
				grabY = e.getY();
				dragBaseX = offX;
				dragBaseY = offY;
				e.consume();
			}
			return e;
		}

		@Override
		public MouseEvent mouseDragged(MouseEvent e)
		{
			if (dragging)
			{
				offX = dragBaseX + e.getX() - grabX;
				offY = dragBaseY + e.getY() - grabY;
				log.debug("MHUD drag off=({},{})", offX, offY);
				e.consume();
			}
			return e;
		}

		@Override
		public MouseEvent mouseReleased(MouseEvent e)
		{
			if (dragging)
			{
				dragging = false;
				configManager.setConfiguration(MysticHudConfig.GROUP, "posX", offX);
				configManager.setConfiguration(MysticHudConfig.GROUP, "posY", offY);
				e.consume();
			}
			return e;
		}
	};

	/**
	 * Scrolling over the map zooms it, the way the vanilla minimap does. The engine's own
	 * wheel handling is bound to the stock minimap position, so once the block moves it
	 * no longer lines up; this drives our own zoom instead and consumes the event so the
	 * scene behind cannot zoom the camera at the same time.
	 */
	/**
	 * Whether mouse coordinates are canvas-space or stretched-display-space depends on
	 * the user's stretched mode, which can change between sessions (both cases are in
	 * the logs). So a hit accepts EITHER interpretation of the point.
	 */
	private boolean hits(Rectangle r, java.awt.event.MouseEvent e)
	{
		if (r.contains(e.getPoint()))
		{
			return true;
		}
		if (client.isStretchedEnabled())
		{
			java.awt.Dimension real = client.getRealDimensions();
			java.awt.Dimension stretched = client.getStretchedDimensions();
			if (stretched.width > 0 && stretched.height > 0)
			{
				return r.contains(
					e.getX() * real.width / stretched.width,
					e.getY() * real.height / stretched.height);
			}
		}
		return false;
	}

	/** The scroll/drag zone: the whole block plus a forgiving margin. */
	private Rectangle blockZone()
	{
		Rectangle b = mapBounds;
		if (b == null)
		{
			return null;
		}
		return new Rectangle(b.x - 8, b.y - 8,
			b.width + 16, b.height + ROW_GAP + rowH + 24);
	}

	// no wheel listener: zoom belongs entirely to the client's native minimap zoom,
	// which follows the real minimap widget and keeps clicks and drawing in agreement

	// widget -> original geometry so shutDown puts everything back
	private final Map<Widget, int[]> saved = new HashMap<>();
	private final Map<Widget, Boolean> savedHidden = new HashMap<>();
	// x position modes we override, so shutDown hands them back like everything else
	private final Map<Widget, Integer> savedXPositionModes = new HashMap<>();
	private final Map<Widget, Integer> savedOpacity = new HashMap<>();

	// the game's own minimap zoom, captured so shutDown can hand it back

	// the transparent minimap mask override; its hole IS the whole rectangle.
	// 1178 is the draw mask. The client layout script also swaps in 2154/3513 as
	// resize click masks, so all three must be the same size or 204px visual maps can
	// still click against the old stock shape.
	static final int MASK_SPRITE = 1178;
	static final int CLICK_MASK_SPRITE = 2154;
	static final int BOND_CLICK_MASK_SPRITE = 3513;
	// the map widget draws through THIS id, not 1178. the engine keeps its own copy of the
	// shape it built from a mask sprite, keyed by sprite id, and if stock 1178 is drawn even
	// once before our override lands that copy is the circle. it then survives the override,
	// the sprite cache reset and every settings change, which is the minimap coming up round
	// on some logins and square on others. an id the engine has never drawn has no copy, so it
	// has to build the shape from our mask. every id from here up is ours; the game's own
	// sprites stop at 8560 (rev240)
	static final int DRAW_MASK_BASE = 50000;
	static final int[] MAP_MASK_SPRITES = {
		MASK_SPRITE, CLICK_MASK_SPRITE, BOND_CLICK_MASK_SPRITE
	};

	/**
	 * A separate id for every mask SIZE, not one id. The stored shape is keyed by id alone, so
	 * resizing the mask under the same id keeps the old shape: log in with True map width on and
	 * the id is built 152 wide, turn it off and the map keeps drawing 152 wide in a 204 frame.
	 * A new size gets a new id and so a fresh shape.
	 */
	static int drawMaskSpriteFor(int w, int h)
	{
		return DRAW_MASK_BASE + w * 64 + (h & 63);
	}
	private final Map<Integer, net.runelite.api.SpritePixels> previousMasks = new HashMap<>();
	// the private id the map currently draws through, -1 while none is installed
	private int drawMaskSprite = -1;
	private net.runelite.api.SpritePixels mask;
	private int maskW, maskH;

	@Provides
	MysticHudConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(MysticHudConfig.class);
	}

	@Override
	protected void startUp()
	{
		// the free-mode anchor changed meaning, so any position saved against the old one
		// is nonsense. drop it once rather than leave the block parked somewhere absurd.
		if (config.layoutVersion() < LAYOUT_VERSION)
		{
			configManager.setConfiguration(MysticHudConfig.GROUP, "posX", 0);
			configManager.setConfiguration(MysticHudConfig.GROUP, "posY", 0);
			configManager.setConfiguration(MysticHudConfig.GROUP, "layoutVersion", LAYOUT_VERSION);
		}
		offX = config.posX();
		offY = config.posY();
		// high priority: at the default the client's own handlers see the event first and
		// our scroll zoom and drag never fire
		mouseManager.registerMouseListener(1, mouse);
		overlayManager.add(frameOverlay);
		settle = 60;
		clientThread.invoke(this::apply);
	}

	@Override
	protected void shutDown()
	{
		mouseManager.unregisterMouseListener(mouse);
		overlayManager.remove(frameOverlay);
		mapBounds = null;
		clientThread.invoke(this::restoreAll);
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged e)
	{
		// widgets SURVIVE region loads (LOADING), so the saved baselines must too;
		// clearing there once orphaned our edits and let modified values compound.
		// only a login-screen rebuild actually discards the widgets.
		if (e.getGameState() == GameState.LOGIN_SCREEN)
		{
			saved.clear();
			savedHidden.clear();
			savedXPositionModes.clear();
			savedOpacity.clear();
			mapBounds = null;
			// NOT previousMasks: the sprite override table is client-wide and outlives the
			// login screen. clearing the saved originals here made the next login save our
			// own transparent mask as the "original", so turning the plugin off after a relog
			// left the stock minimap square
		}
		if (e.getGameState() == GameState.LOGGED_IN)
		{
			settle = 60; // ~a second of forced revalidates while the interfaces build
			// VarbitChanged only tells us about CHANGES, so without this the orbs read
			// as walking, unarmed and healthy until the player happens to toggle
			// something. the vars are already set by the time we are logged in.
			readOrbStates();
		}
	}

	@Subscribe
	public void onVarbitChanged(VarbitChanged e)
	{
		// fires for varps as well as varbits, so this one subscription covers all of it
		readOrbStates();

		// switching between resizable classic and modern swaps the whole toplevel under us,
		// so the widgets we had are gone and the two child ids have moved. force a settle
		// to rebuild against the new one rather than wait for something else to nudge it.
		if (e.getVarbitId() == VarbitID.RESIZABLE_STONE_ARRANGEMENT)
		{
			settle = 60;
		}
	}

	@Subscribe
	public void onConfigChanged(ConfigChanged e)
	{
		// ANY group, not just ours: a resource pack being enabled, disabled or swapped
		// changes the sprite override table, and the overlay's art is memoised by sprite
		// id, so without this it keeps drawing the old pack until the plugin restarts
		frameOverlay.clearArtCaches();

		// the drag itself writes posX/posY; rebuilding on those would reset the widget
		// sprite cache on every mouse release and flicker the map
		if (MysticHudConfig.GROUP.equals(e.getGroup())
			&& !"posX".equals(e.getKey()) && !"posY".equals(e.getKey()))
		{
			clientThread.invoke(() ->
			{
				restoreAll();
				settle = 10;
				apply();
			});
		}
	}

	@Subscribe
	public void onBeforeRender(BeforeRender e)
	{
		// layout clientscripts rerun on resize and tab switches and stomp our values.
		// BeforeRender fires after scripts and before draw, so setting here always wins
		// and never visibly fights them.
		apply();
	}

	private Widget top(int child)
	{
		return client.getWidget(client.getTopLevelInterfaceId() << 16 | child);
	}

	/** The minimap block, whichever layout is up. */
	private Widget minimapBlock()
	{
		return top(minimapBlockChild(client.getTopLevelInterfaceId()));
	}

	/**
	 * Attach is a MODERN-only feature. In resizable classic the inventory panel does not
	 * sit where the glue assumes, so attaching does nothing useful there and only takes
	 * away the drag. Read through here everywhere rather than the config directly, so the
	 * drag guard, the layout and the overlay's bridged seam cannot disagree about it.
	 */
	boolean attachInventory()
	{
		return config.attachInventory()
			&& client.getTopLevelInterfaceId() != TOPLEVEL_CLASSIC;
	}

	/** The inventory panel, whichever layout is up. Also used by the overlay. */
	Widget invPanel()
	{
		return top(invPanelChild(client.getTopLevelInterfaceId()));
	}

	private Widget orb(int child)
	{
		return client.getWidget(ORBS << 16 | child);
	}

	/** True while the world map is up, which is when our block must stop eating clicks. */
	private boolean worldMapOpen()
	{
		Widget w = client.getWidget(WORLDMAP_GROUP << 16);
		return w != null && !w.isHidden();
	}

	boolean active()
	{
		// NOT isResized(): that is true for both resizable layouts and false for fixed, so
		// it cannot say WHICH resizable layout is up, and the child ids differ between them
		if (client.getGameState() != GameState.LOGGED_IN
			|| !isResizableToplevel(client.getTopLevelInterfaceId()))
		{
			return false;
		}
		Widget block = minimapBlock();
		return block != null && !block.isHidden();
	}

	/**
	 * The map container's TRUE width. The engine paints the minimap at native 152 and no
	 * wider: stretching the container to the inventory's 204 does not buy more map, it
	 * leaves the east 52px unpainted (the black strip) and, because the engine centres the
	 * player on ITS 152, puts the real map centre 26px left of the frame's centre. Clicks
	 * are taken against the engine's rect, so aiming by the frame lands 26px east, which
	 * is the walk offset. At native width, frame, draw and clicks all share a centre.
	 */
	private int cfgMapW()
	{
		return config.nativeMapWidth() ? NATIVE_MAP : INV_W;
	}

	/** Left padding that centres a native-width map inside the full-width frame. */
	private int mapInset()
	{
		return (INV_W - cfgMapW()) / 2;
	}

	private int cfgMapH()
	{
		// the engine ALWAYS draws 152 tall (native), anchored to the container bottom, so
		// below 152 the difference spills out as live map above the frame rather than
		// cropping. exposed anyway: the spill is a look to judge, not a crash, and 152
		// is still the only height where draw, container and frame coincide exactly.
		return config.mapHeight();
	}

	int[] orbChildren()
	{
		switch (config.orbOrder())
		{
			case HP_PRAYER_RUN_SPEC:
				return new int[]{HP, PRAYER, RUN, SPEC};
			case HP_PRAYER_SPEC_RUN:
				return new int[]{HP, PRAYER, SPEC, RUN};
			case SPEC_PRAYER_RUN_HP:
			default:
				return new int[]{SPEC, PRAYER, RUN, HP};
		}
	}

	/** Block width shared by the four orb slots, inside the 6px steel border. */
	int slotWidth()
	{
		return (mapW - 12 - 3 * BLOCK_GAP) / 4;
	}

	/** Left inset of the block strip, centring the leftover so both margins match. */
	int rowInsetX()
	{
		return (mapW - (4 * slotWidth() + 3 * BLOCK_GAP)) / 2;
	}

	/** The FRAME's rect on canvas, or null while the layout is not applied. */
	Rectangle layoutBounds()
	{
		return mapBounds;
	}

	/**
	 * The rect the map is actually painted in. Identical to the frame at full width; inset
	 * inside it at native width. The compass rides on this so it stays on the map's corner
	 * rather than floating in the padding beside it.
	 */
	Rectangle mapRect()
	{
		Rectangle r = mapDrawRect;
		return r != null ? r : mapBounds;
	}

	/** Screen rect of the painted compass, given the map's bounds. */
	static Rectangle compassBounds(Rectangle map)
	{
		return new Rectangle(map.x + COMPASS_INSET, map.y + COMPASS_INSET,
			COMPASS_SIZE, COMPASS_SIZE);
	}

	private void apply()
	{
		if (!active())
		{
			// stop the overlays painting and stop the compass/drag hit tests firing
			// against a stale rect while we are in fixed or classic layout
			mapBounds = null;
			return;
		}

		Widget block = minimapBlock();
		Widget parent = block.getParent();
		if (parent == null)
		{
			return;
		}

		dirty = settle > 0;
		if (settle > 0)
		{
			settle--;
		}

		// READ THE SLIDERS EVERY FRAME. these were only loaded once in startUp, so moving
		// Position X/Y changed the config and nothing else, which is why the block would
		// not move in any mode. while dragging, the live drag value wins.
		if (!dragging)
		{
			offX = config.posX();
			offY = config.posY();
		}

		// ZOOM IS NEVER TOUCHED, not even the enable flag: enabling it is the only
		// click-related change in the build where the walk offset returned

		Widget inv = invPanel();
		boolean invOpen = inv != null && !inv.isHidden() && inv.getHeight() > 0;
		Widget inner = top(MINIMAP_INNER);
		Widget host = top(ORB_HOST);

		if (config.hideWhenClosed())
		{
			hide(inner, !invOpen);
			hide(host, !invOpen);
			if (!invOpen)
			{
				mapBounds = null;
				return;
			}
		}
		else
		{
			hide(inner, false);
			hide(host, false);
		}

		// THE MAP IS ITS NATIVE SIZE, ALWAYS. compact-orbs, the shipping plugin that does
		// this exact job, never resizes the minimap draw widget, only its containers.
		// every draw glitch we had came from fighting that.
		rowH = config.orbRowHeight();
		int blockH = MAP_TOP + cfgMapH() + ROW_GAP + rowH;
		int avail = parent.getHeight();
		if (blockH > avail)
		{
			rowH = Math.max(24, rowH - (blockH - avail));
			blockH = MAP_TOP + cfgMapH() + ROW_GAP + rowH;
		}
		if (blockH > avail)
		{
			mapBounds = null; // overlay must not paint while the layout is not applied
			return; // window too small, leave stock layout alone
		}

		// anchor: attached glues onto the inventory panel; free mode anchors to where the
		// panel ALWAYS sits (not whether it is open, which jumps ~275px). Position X/Y
		// shift from there in every mode.
		boolean attached = attachInventory();
		int anchorTop = invOpen ? inv.getRelativeY() : parent.getHeight() - 2 * 36;
		int bottom;
		if (attached)
		{
			// glued: position sliders and drag do NOT apply in attach mode
			bottom = invOpen ? anchorTop + 2 : blockH;
		}
		else
		{
			// free mode: default parks the block at the TOP; drag/sliders move it down
			bottom = blockH + offY;
		}
		// attach ignores horizontal offset too, or a leftover free-mode drag shifts the
		// glued block off the inventory's edges
		int dx = attached ? 0 : Math.max(-(parent.getWidth() - 211), Math.min(200, offX));
		int targetY = Math.max(0, Math.min(avail - blockH, bottom - blockH));
		if (offY != lastLoggedOffY)
		{
			log.debug("MHUD move offY={} bottom={} blockH={} targetY={} attached={}",
				offY, bottom, blockH, targetY, attached);
			lastLoggedOffY = offY;
		}
		layout(targetY, dx, blockH);
	}

	private void layout(int targetY, int dx, int blockH)
	{
		// the block is EXACTLY the map width: the engine sizes the minimap draw from
		// this container (right-anchored), proven by the bleed being precisely
		// blockW - mapW on the left with right/bottom flush. any extra width becomes
		// visible map outside the frame.
		// the strip is there if EITHER orb needs it
		boolean sideStrip = config.showExtraOrbs() || config.showWorldMap();
		int blockW = INV_W + (sideStrip ? SIDE_PAD : 0);
		Widget block = minimapBlock();
		Widget parent = block == null ? null : block.getParent();
		if (block == null || parent == null)
		{
			mapBounds = null;
			return;
		}
		dirty |= set(block, null, null, blockW, parent.getHeight());

		// THE CONTAINER IS THE MAP. the engine draws into MAP_MINIMAP_GRAPHIC9, a child
		// that sizes itself to this container, NOT into the mask widget we position. so
		// the container must be EXACTLY the map rect: absolute width (its stock mode
		// tracks the block, which is wider), height exactly the map height.
		Widget inner = top(MINIMAP_INNER);
		if (inner != null)
		{
			inner.setWidthMode(net.runelite.api.widgets.WidgetSizeMode.ABSOLUTE);
			// inset by mapInset so a native-width map sits centred in the full-width frame:
			// the engine draws and clicks against THIS container, so centring it here is
			// what puts the map's centre on the frame's centre
			dirty |= set(inner, dx + (blockW - INV_W) + mapInset(), targetY,
				cfgMapW(), cfgMapH());
			// the stock UI guards the minimap with invisible MAP_NOCLICK shims so clicks
			// cannot fall through to the full-bleed 3D scene; our relocated block has no
			// shim under it, so the containers themselves must stop the fall-through
			// not while the world map is open, or the block swallows its close button
			inner.setNoClickThrough(!worldMapOpen());
		}

		Widget host = top(ORB_HOST);
		if (host != null)
		{
			dirty |= set(host, dx, targetY, blockW, blockH + 4);
			host.setNoClickThrough(!worldMapOpen());
		}

		Widget map = top(MINIMAP_MAP);
		if (map == null)
		{
			mapBounds = null;
			return;
		}
		// the mask widget just needs to cover the container so the transparent mask
		// override kills the circle everywhere.
		// NOT native width here: shrinking this to NATIVE_MAP (b53, on a theory that the
		// click centre ignored the declared width) moved the drawn map out of the frame
		// and did not change the offset at all. the draw really does follow this width.
		dirty |= set(map, 0, 0, cfgMapW(), cfgMapH());
		if (config.squareMinimap())
		{
			overrideMask(cfgMapW(), cfgMapH());
			drawThroughOurMask(map);
		}
		else
		{
			clearMaskOverride();
		}

		dirty |= hide(top(MINIMAP_RING), true); // no circular surround around a rectangle

		// the real compass widget draws BEFORE the map and cannot sit on it; the frame
		// overlay paints a small rotating compass instead
		dirty |= hide(top(MINIMAP_COMPASS), true);

		// flat blocks: ALL orb art goes, including icons and numbers; the overlay paints
		// its own on top of the blocks. the widgets stay for clicks and menus only.
		boolean flat = config.drawBlocks();
		dirty |= hide(orb(HP_BACK), flat);
		dirty |= hide(orb(PRAYER_BACK), flat);
		dirty |= hide(orb(RUN_BACK), flat);
		dirty |= hide(orb(SPEC_BACK), flat);
		for (int c : SPHERES)
		{
			dirty |= hide(orb(c), flat);
		}
		for (int c : ORB_ICONS)
		{
			dirty |= hide(orb(c), flat);
		}
		for (int t : ORB_TEXT)
		{
			dirty |= hide(orb(t), flat);
		}
		// sweep every root-level child of the orbs group: anything we do not place
		// ourselves gets hidden, which catches the stray orbs whatever their id.
		// the four orb CONTAINERS are exempt, and so is anything parented to them, or the
		// sweep takes the value text with it whenever the client rebuilds those children
		Widget orbRoot = orb(0);
		Widget[] rootKids = orbRoot != null ? orbRoot.getStaticChildren() : null;
		if (rootKids != null)
		{
			for (Widget k : rootKids)
			{
				int child = k.getId() & 0xffff;
				boolean ours = false;
				for (int p : PLACED)
				{
					ours |= child == p;
				}
				for (int t : ORB_TEXT)
				{
					ours |= child == t;
				}
				if (!ours)
				{
					dirty |= hide(k, true);
				}
			}
		}
		boolean extras = config.showExtraOrbs();
		dirty |= hide(orb(XP_ORB), !extras);
		// left exactly where the client puts it. we used to hide it and paint our own in
		// the map's corner, which meant forwarding the click through menuAction to reach
		// the stock op, and a synthesised menu action is the sort of thing the hub's
		// classifier is entitled to object to. not worth it for an orb.
		// switched off it is made INVISIBLE, not hidden, shrunk or moved. the game's ctrl+m
		// world map shortcut belongs to this widget and only reaches it while it is unhidden
		// and actually on screen at a real size: hiding it, collapsing it to 0x0 and parking it
		// off to the side each killed ctrl+m. so it stays at its stock size in its stock spot
		// (the map's corner, where clicks go to the map anyway) with every part fully see-through
		Widget worldOrb = orb(WORLD_MAP_ORB);
		dirty |= hide(worldOrb, false);
		if (worldOrb != null && !config.showWorldMap())
		{
			boolean moved = stockPosition(worldOrb);
			if (moved)
			{
				// the orbs are their own interface nested in the block, and revalidateDeep only
				// walks static children, so without this a move is stored and never applied
				worldOrb.revalidate();
			}
			dirty |= moved;
			seeThrough(worldOrb, true, 0);
		}

		// settle the containers so canvas positions below are current. only when
		// something actually moved, otherwise this walks the subtree every frame
		if (dirty)
		{
			revalidateDeep(block);
		}

		// ---- canvas-space placement ----
		// the containers do not always land exactly where the originals say (scripts
		// force some of them), so everything below is positioned against where the map
		// ACTUALLY is on screen and self-corrects each frame
		if (host == null)
		{
			mapBounds = null;
			return;
		}
		// everything anchors to the CONTAINER's real on-screen rectangle: that is where
		// the engine actually draws the map. published as mapBounds so the overlay,
		// compass and scroll zone all agree.
		Widget innerW = top(MINIMAP_INNER);
		net.runelite.api.Point innerLoc = innerW == null ? null : innerW.getCanvasLocation();
		net.runelite.api.Point hostLoc = host.getCanvasLocation();
		if (innerLoc == null || hostLoc == null)
		{
			mapBounds = null;
			return;
		}
		int hx = hostLoc.getX();
		int hy = hostLoc.getY();

		// nudge: aligns all of OUR chrome onto wherever the engine actually paints the
		// map, tuned live by eye; the container itself stays put
		// the container is now the MAP's rect, which may be inset inside the frame. the
		// frame, the orb row and everything else we paint still span the full width, so
		// visX is walked back out to the frame's left edge and the map's own rect is
		// published separately for the things that must sit on the map itself
		int inset = mapInset();
		int visX = innerLoc.getX() - inset + config.frameNudgeX();
		int visY = innerLoc.getY() + config.frameNudgeY();
		mapW = INV_W;
		int mapH = innerW.getHeight();
		mapDrawRect = new Rectangle(visX + inset, visY, cfgMapW(), mapH);
		int rowCanvasY = visY + mapH + ROW_GAP;
		int slot = slotWidth();
		int[] order = orbChildren();
		for (int i = 0; i < order.length; i++)
		{
			Widget w = orb(order[i]);
			if (w != null)
			{
				// centre the orb art inside its slot; the container is 57 wide but the
				// clickable circle occupies x 27..53, so shift left to balance it
				// never centre upwards into the map when the row is shorter than the orb
				int cx = visX + rowInsetX() + i * (slot + BLOCK_GAP) + (slot - ORB_W) / 2;
				int cy = rowCanvasY + Math.max(0, (rowH - ORB_H) / 2);
				if (set(w, cx - hx, cy - hy, null, null))
				{
					w.revalidate();
				}
			}
		}

		// xp orb beside the map's left edge
		if (extras)
		{
			Widget xp = orb(XP_ORB);
			if (xp != null)
			{
				// below the compass slot
				if (set(xp, visX - 38 - hx, visY + 42 - hy, null, null))
				{
					xp.revalidate();
				}
			}
		}

		// world map orb in the same strip, under the xp orb. it CANNOT sit on the map: the
		// map takes the click and walks you there instead (tried again in b99, same result),
		// and widget draw order is not ours to change. out here it is the stock widget in
		// clear space, so it answers its own click and needs nothing synthesised.
		if (config.showWorldMap())
		{
			Widget world = orb(WORLD_MAP_ORB);
			if (world != null)
			{
				// the SAME x as the xp orb above it. both are the stock 57 wide widget
				// with the visible orb offset the same way inside it, so compensating for
				// that on one and not the other is what put this 27px left of the xp orb.
				seeThrough(world, false, 0);
				boolean moved = setAbsoluteX(world, visX - 38 - hx);
				moved |= set(world, null, visY + (extras ? 96 : 42) - hy, null, null);
				if (moved)
				{
					world.revalidate();
				}
			}
		}


		mapBounds = new Rectangle(visX, visY, mapW, mapH);
	}

	/**
	 * The engine sizes the minimap from global mask sprites, not just from the widget.
	 * The draw mask and click masks must share the same rectangular dimensions, or the
	 * map can look 204px wide while walk clicks still resolve against the old stock mask.
	 */
	private void overrideMask(int w, int h)
	{
		Map<Integer, net.runelite.api.SpritePixels> overrides = client.getSpriteOverrides();
		if (overrides == null)
		{
			return;
		}
		if (mask == null || maskW != w || maskH != h)
		{
			// fully transparent, so nothing of the stock circle survives
			mask = client.createSpritePixels(new int[w * h], w, h);
			maskW = w;
			maskH = h;
		}
		boolean changed = false;
		for (int sprite : MAP_MASK_SPRITES)
		{
			if (overrides.get(sprite) != mask)
			{
				if (!previousMasks.containsKey(sprite))
				{
					previousMasks.put(sprite, overrides.get(sprite));
				}
				overrides.put(sprite, mask);
				changed = true;
			}
		}
		int drawId = drawMaskSpriteFor(w, h);
		if (drawMaskSprite != drawId)
		{
			if (drawMaskSprite >= 0)
			{
				overrides.remove(drawMaskSprite);
			}
			drawMaskSprite = drawId;
		}
		if (overrides.get(drawId) != mask)
		{
			overrides.put(drawId, mask);
			changed = true;
		}
		if (changed)
		{
			// the client caches widget sprites after first draw; without this reset a
			// relog or resource-pack refresh re-caches the circle and the square is lost
			client.getWidgetSpriteCache().reset();
		}
	}

	/**
	 * Only once our mask is actually registered under the private id: pointing the widget at
	 * it any earlier would ask the engine for a sprite that does not exist.
	 */
	private void drawThroughOurMask(Widget map)
	{
		Map<Integer, net.runelite.api.SpritePixels> overrides = client.getSpriteOverrides();
		if (overrides != null && mask != null && drawMaskSprite >= 0
			&& overrides.get(drawMaskSprite) == mask && map.getSpriteId() != drawMaskSprite)
		{
			map.setSpriteId(drawMaskSprite);
		}
	}

	private void clearMaskOverride()
	{
		// point the map back at the stock mask BEFORE the private id loses its override
		Widget map = top(MINIMAP_MAP);
		if (map != null && map.getSpriteId() >= DRAW_MASK_BASE)
		{
			map.setSpriteId(MASK_SPRITE);
		}
		Map<Integer, net.runelite.api.SpritePixels> drawOverrides = client.getSpriteOverrides();
		if (drawMaskSprite >= 0 && drawOverrides != null)
		{
			drawOverrides.remove(drawMaskSprite);
		}
		drawMaskSprite = -1;
		if (previousMasks.isEmpty())
		{
			return;
		}
		Map<Integer, net.runelite.api.SpritePixels> overrides = client.getSpriteOverrides();
		if (overrides != null)
		{
			for (Map.Entry<Integer, net.runelite.api.SpritePixels> e : previousMasks.entrySet())
			{
				if (e.getValue() != null)
				{
					overrides.put(e.getKey(), e.getValue());
				}
				else
				{
					overrides.remove(e.getKey());
				}
			}
			client.getWidgetSpriteCache().reset();
		}
		previousMasks.clear();
		mask = null;
	}

	// ---- save/restore plumbing ----

	private void remember(Widget w)
	{
		saved.computeIfAbsent(w, k -> new int[]{
			k.getOriginalX(), k.getOriginalY(), k.getOriginalWidth(), k.getOriginalHeight()
		});
	}

	/** Returns true when a value actually changed, so callers can skip revalidating. */
	private boolean set(Widget w, Integer x, Integer y, Integer width, Integer height)
	{
		remember(w);
		boolean changed = false;
		if (x != null && w.getOriginalX() != x)
		{
			w.setOriginalX(x);
			changed = true;
		}
		if (y != null && w.getOriginalY() != y)
		{
			w.setOriginalY(y);
			changed = true;
		}
		if (width != null && w.getOriginalWidth() != width)
		{
			w.setOriginalWidth(width);
			changed = true;
		}
		if (height != null && w.getOriginalHeight() != height)
		{
			w.setOriginalHeight(height);
			changed = true;
		}
		return changed;
	}

	private boolean setAbsoluteX(Widget w, int x)
	{
		remember(w);
		savedXPositionModes.putIfAbsent(w, w.getXPositionMode());
		boolean changed = false;
		if (w.getXPositionMode() != WidgetPositionMode.ABSOLUTE_LEFT)
		{
			w.setXPositionMode(WidgetPositionMode.ABSOLUTE_LEFT);
			changed = true;
		}
		if (w.getOriginalX() != x)
		{
			w.setOriginalX(x);
			changed = true;
		}
		return changed;
	}

	/** The x mode, x and y the client originally gave the widget. */
	private boolean stockPosition(Widget w)
	{
		remember(w);
		int[] stock = saved.get(w);
		Integer mode = savedXPositionModes.get(w);
		boolean changed = false;
		if (mode != null && w.getXPositionMode() != mode)
		{
			w.setXPositionMode(mode);
			changed = true;
		}
		changed |= set(w, stock[0], stock[1], null, null);
		return changed;
	}

	/**
	 * Fully transparent (or back to how it was) for the widget and everything under it. Run
	 * every frame while wanted: the client rebuilds the orb's dynamic children, and a fresh
	 * one arrives opaque.
	 */
	private void seeThrough(Widget w, boolean invisible, int depth)
	{
		if (w == null || depth > 3)
		{
			return;
		}
		if (invisible)
		{
			savedOpacity.putIfAbsent(w, w.getOpacity());
			if (w.getOpacity() != 255)
			{
				w.setOpacity(255);
			}
		}
		else
		{
			Integer original = savedOpacity.remove(w);
			if (original != null)
			{
				w.setOpacity(original);
			}
		}
		for (Widget[] kids : new Widget[][]{w.getStaticChildren(), w.getDynamicChildren()})
		{
			if (kids != null)
			{
				for (Widget k : kids)
				{
					seeThrough(k, invisible, depth + 1);
				}
			}
		}
	}

	private boolean hide(Widget w, boolean hidden)
	{
		if (w == null)
		{
			return false;
		}
		savedHidden.computeIfAbsent(w, Widget::isSelfHidden);
		if (w.isSelfHidden() == hidden)
		{
			return false;
		}
		w.setHidden(hidden);
		return true;
	}

	private void restoreAll()
	{
		for (Map.Entry<Widget, int[]> e : saved.entrySet())
		{
			int[] v = e.getValue();
			Widget w = e.getKey();
			w.setOriginalX(v[0]);
			w.setOriginalY(v[1]);
			w.setOriginalWidth(v[2]);
			w.setOriginalHeight(v[3]);
		}
		for (Map.Entry<Widget, Boolean> e : savedHidden.entrySet())
		{
			e.getKey().setHidden(e.getValue());
		}
		for (Map.Entry<Widget, Integer> e : savedXPositionModes.entrySet())
		{
			e.getKey().setXPositionMode(e.getValue());
		}
		for (Map.Entry<Widget, Integer> e : savedOpacity.entrySet())
		{
			e.getKey().setOpacity(e.getValue());
		}
		clearMaskOverride();
		Widget innerW = top(MINIMAP_INNER);
		if (innerW != null)
		{
			innerW.setNoClickThrough(false);
			// stock width mode tracks the parent block
			innerW.setWidthMode(net.runelite.api.widgets.WidgetSizeMode.MINUS);
		}
		Widget hostW = top(ORB_HOST);
		if (hostW != null)
		{
			hostW.setNoClickThrough(false);
		}

		List<Widget> roots = new ArrayList<>(saved.keySet());
		saved.clear();
		savedHidden.clear();
		savedXPositionModes.clear();
		savedOpacity.clear();
		mapBounds = null;
		for (Widget w : roots)
		{
			w.revalidate();
		}
		Widget block = minimapBlock();
		if (block != null)
		{
			revalidateDeep(block);
		}
	}

	private void revalidateDeep(Widget w)
	{
		w.revalidate();
		Widget[] kids = w.getStaticChildren();
		if (kids != null)
		{
			for (Widget k : kids)
			{
				revalidateDeep(k);
			}
		}
	}
}
