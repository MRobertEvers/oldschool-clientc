package net.runelite.client.plugins.torirs;

import java.awt.Color;
import java.awt.Font;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.util.LinkBrowser;

/**
 * Signs this client in to a ToriRS world behind its sign-in gate (and the
 * mrobertevers.com IaP in front of it), from the sidebar. See ToriRSSignIn.
 *
 * Found by RuneLite's core plugin scan (it walks the whole classpath under
 * net.runelite.client.plugins), so run-runelite.sh only has to put
 * torirs-plugin.jar on the classpath; it also works in --safe-mode.
 */
@PluginDescriptor(
	name = "ToriRS",
	description = "Sign in to the ToriRS world: the IaP and the world's sign-in gate, approved in your browser",
	tags = {"torirs", "login", "sign in"}
)
public class ToriRSPlugin extends Plugin
{
	@Inject
	private ClientToolbar clientToolbar;

	@Inject
	private Client client;

	@Inject
	private ClientThread clientThread;

	private ToriRSSignIn signIn;
	private ToriRSPanel panel;
	private NavigationButton button;
	private ScheduledExecutorService worker;

	@Override
	protected void startUp()
	{
		signIn = ToriRSSignIn.fromSystemProperties();
		panel = new ToriRSPanel();
		worker = Executors.newSingleThreadScheduledExecutor(r ->
		{
			Thread t = new Thread(r, "torirs-signin");
			t.setDaemon(true);
			return t;
		});
		panel.signIn.addActionListener(e -> worker.execute(this::runSignIn));
		panel.cancel.addActionListener(e -> signIn.cancel());
		panel.reopen.addActionListener(e ->
		{
			if (panel.waitingUrl() != null)
			{
				openBrowser(panel.waitingUrl());
			}
		});
		panel.signOut.addActionListener(e -> worker.execute(() ->
		{
			signIn.signOut();
			panel.accounts(null, null);
			panel.say("Signed out.");
			panel.show(signIn);
		}));

		button = NavigationButton.builder()
			.tooltip("ToriRS sign-in")
			.icon(icon())
			.priority(1)
			.panel(panel)
			.build();
		clientToolbar.addNavigation(button);

		worker.execute(() ->
		{
			signIn.load();
			signIn.refreshDue();
			panel.show(signIn);
			loadAccounts();
			if (signIn.configured() && !(signIn.iapSignedIn() && signIn.gateSignedIn()))
			{
				// Not signed in: show the panel, so the login screen's
				// "cannot connect" has its reason in plain sight.
				javax.swing.SwingUtilities.invokeLater(() -> clientToolbar.openPanel(button));
				panel.say("Not signed in. Press Sign in.");
			}
		});
		// Refresh well before anything runs out: the session and ticket last a day.
		worker.scheduleWithFixedDelay(() ->
		{
			signIn.refreshDue();
			panel.show(signIn);
		}, 10, 10, TimeUnit.MINUTES);
	}

	@Override
	protected void shutDown()
	{
		if (signIn != null)
		{
			signIn.cancel();
		}
		clientToolbar.removeNavigation(button);
		worker.shutdownNow();
	}

	private void runSignIn()
	{
		try
		{
			signIn.signIn(new ToriRSSignIn.Progress()
			{
				@Override
				public void waiting(String what, String userCode, String url)
				{
					panel.waiting(what, userCode, url);
					panel.show(signIn);
				}

				@Override
				public void said(String line)
				{
					panel.waiting(null, null, null);
					panel.say(line);
					panel.show(signIn);
				}
			}, this::openBrowser);
			panel.waiting(null, null, null);
			panel.say("Signed in. Pick an account below, or log in to the game.");
			loadAccounts();
		}
		catch (InterruptedException e)
		{
			panel.waiting(null, null, null);
			panel.say("Cancelled.");
		}
		catch (IOException | RuntimeException e)
		{
			panel.waiting(null, null, null);
			panel.say("Sign-in failed: " + e.getMessage());
		}
		panel.show(signIn);
	}

	/** The web login's linked player accounts, as Play buttons. */
	private void loadAccounts()
	{
		if (!signIn.configured() || !signIn.gateSignedIn())
		{
			panel.accounts(null, null);
			return;
		}
		try
		{
			panel.accounts(signIn.players(), name -> worker.execute(() -> play(name)));
		}
		catch (IOException | RuntimeException e)
		{
			panel.say("Could not list your accounts: " + e.getMessage());
		}
	}

	/**
	 * Play a linked account: a one-time login from the gate, then the login
	 * itself, started as the login screen's button would -- the gamepack's
	 * TorirsLogin sends the token (auth type TOKEN) where the password goes.
	 */
	private void play(String player)
	{
		String[] answer;
		try
		{
			answer = signIn.play(player);
		}
		catch (IOException | RuntimeException e)
		{
			panel.say(e.getMessage());
			return;
		}
		String token = answer[0];
		String name = answer[1];
		clientThread.invokeLater(() ->
		{
			if (client.getGameState() != GameState.LOGIN_SCREEN)
			{
				panel.say("Log out first, then press Play.");
				return;
			}
			System.setProperty("torirs.otl", token);
			client.setUsername(name);
			// Never sent: the token takes the password's place. The login
			// screen only wants the field filled.
			client.setPassword("torirs-one-time-login");
			client.setGameState(GameState.LOGGING_IN);
			panel.say("Logging in as " + name.replace('\u00a0', ' ') + "...");
		});
	}

	/**
	 * Google Chrome when it is installed on this Mac (torirs.browser names
	 * another app, "default" the system default), else the default browser.
	 */
	private void openBrowser(String url)
	{
		String app = System.getProperty("torirs.browser", "Google Chrome");
		boolean mac = System.getProperty("os.name", "").toLowerCase().contains("mac");
		if (mac && !"default".equals(app) && new File("/Applications/" + app + ".app").isDirectory())
		{
			try
			{
				new ProcessBuilder("open", "-a", app, url).start();
				return;
			}
			catch (IOException e)
			{
				// fall back to the default browser
			}
		}
		LinkBrowser.browse(url);
	}

	/** A small "T" badge; no resource file to ship. */
	private static BufferedImage icon()
	{
		BufferedImage img = new BufferedImage(16, 16, BufferedImage.TYPE_INT_ARGB);
		Graphics2D g = img.createGraphics();
		g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
		g.setColor(new Color(0xD9, 0x7A, 0x1E));
		g.fillRoundRect(0, 0, 16, 16, 5, 5);
		g.setColor(Color.WHITE);
		g.setFont(new Font(Font.SANS_SERIF, Font.BOLD, 12));
		g.drawString("T", 4, 13);
		g.dispose();
		return img;
	}
}
