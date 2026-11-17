package net.runelite.client.plugins.torirs;

import java.awt.BorderLayout;
import java.awt.Font;
import java.awt.GridLayout;
import java.text.DateFormat;
import java.util.Date;
import javax.swing.BorderFactory;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingConstants;
import javax.swing.SwingUtilities;
import net.runelite.client.ui.ColorScheme;
import net.runelite.client.ui.PluginPanel;

/** The sidebar panel: who this client is signed in as, and the buttons. */
final class ToriRSPanel extends PluginPanel
{
	private final JLabel iapLine = new JLabel();
	private final JLabel gateLine = new JLabel();
	private final JLabel codeLabel = new JLabel("", SwingConstants.CENTER);
	private final JLabel message = new JLabel();
	final JButton signIn = new JButton("Sign in");
	final JButton signOut = new JButton("Sign out");
	final JButton cancel = new JButton("Cancel");
	final JButton reopen = new JButton("Open the page again");
	private String waitingUrl;
	private final JPanel accounts = new JPanel(new GridLayout(0, 1, 0, 6));
	private final JLabel accountsTitle = new JLabel("Your accounts");

	ToriRSPanel()
	{
		setLayout(new BorderLayout());
		setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));
		JPanel col = new JPanel();
		col.setLayout(new BoxLayout(col, BoxLayout.Y_AXIS));

		JLabel title = new JLabel("ToriRS");
		title.setFont(title.getFont().deriveFont(Font.BOLD, 16f));
		col.add(title);
		col.add(wrap("Sign in here before you log in. Approving happens in your browser, so your password manager fills it."));
		col.add(gap());
		col.add(iapLine);
		col.add(gateLine);
		col.add(gap());
		codeLabel.setFont(new Font(Font.MONOSPACED, Font.BOLD, 22));
		codeLabel.setAlignmentX(LEFT_ALIGNMENT);
		col.add(codeLabel);
		col.add(message);
		col.add(gap());

		JPanel buttons = new JPanel(new GridLayout(0, 1, 0, 6));
		buttons.add(signIn);
		buttons.add(reopen);
		buttons.add(cancel);
		buttons.add(signOut);
		buttons.setAlignmentX(LEFT_ALIGNMENT);
		col.add(buttons);
		col.add(gap());
		accountsTitle.setFont(accountsTitle.getFont().deriveFont(Font.BOLD));
		accountsTitle.setVisible(false);
		col.add(accountsTitle);
		accounts.setAlignmentX(LEFT_ALIGNMENT);
		col.add(accounts);
		add(col, BorderLayout.NORTH);
		message.setForeground(ColorScheme.LIGHT_GRAY_COLOR);
		waiting(null, null, null);
	}

	private static JLabel wrap(String text)
	{
		JLabel l = new JLabel("<html><body style='width:190px'>" + text + "</body></html>");
		l.setForeground(ColorScheme.LIGHT_GRAY_COLOR);
		return l;
	}

	private static JLabel gap()
	{
		JLabel l = new JLabel(" ");
		return l;
	}

	/** Called on any thread. */
	void show(ToriRSSignIn signIn)
	{
		ToriRSSignIn.State s = signIn.snapshot();
		String iap = signIn.iap == null ? null
			: signIn.iapSignedIn() ? "mrobertevers.com: " + s.iapEmail + until(s.iapSessionExpiresAt)
			: "mrobertevers.com: not signed in";
		String gate = !signIn.configured() ? "Not a ToriRS launch (no torirs.gate.api)."
			: signIn.gateSignedIn() ? "ToriRS: " + s.gateName + until(s.gateTicketExpiresAt)
			: "ToriRS: not signed in";
		boolean all = signIn.iapSignedIn() && signIn.gateSignedIn();
		SwingUtilities.invokeLater(() ->
		{
			iapLine.setText(iap == null ? "" : iap);
			iapLine.setVisible(iap != null);
			gateLine.setText(gate);
			this.signIn.setEnabled(signIn.configured() && !all && waitingUrl == null);
			signOut.setEnabled(s.iapRefresh != null || s.gateRefresh != null);
		});
	}

	private static String until(long when)
	{
		// The session and the ticket last a day and are refreshed two hours
		// before they run out: this is when the current one would.
		return " (good until " + DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(new Date(when)) + ")";
	}

	/** A code waits for approval in the browser; nulls when nothing waits. */
	void waiting(String what, String code, String url)
	{
		waitingUrl = url;
		SwingUtilities.invokeLater(() ->
		{
			codeLabel.setText(code == null ? "" : code);
			codeLabel.setVisible(code != null);
			if (code != null)
			{
				message.setText("<html><body style='width:190px'>Approve " + what + " in the browser. It should show this code.</body></html>");
			}
			reopen.setVisible(url != null);
			cancel.setVisible(url != null);
			signIn.setEnabled(url == null);
		});
	}

	/**
	 * The linked player accounts, one Play button each (the browser client's
	 * Play Now cards). Null or empty hides the section.
	 */
	void accounts(java.util.List<String> names, java.util.function.Consumer<String> onPlay)
	{
		SwingUtilities.invokeLater(() ->
		{
			accounts.removeAll();
			boolean any = names != null && !names.isEmpty();
			accountsTitle.setVisible(any);
			if (any)
			{
				for (String name : names)
				{
					JButton play = new JButton("Play " + name.replace('\u00a0', ' '));
					play.addActionListener(e -> onPlay.accept(name));
					accounts.add(play);
				}
			}
			accounts.revalidate();
			accounts.repaint();
		});
	}

	String waitingUrl()
	{
		return waitingUrl;
	}

	void say(String text)
	{
		SwingUtilities.invokeLater(() -> message.setText(text == null ? "" : "<html><body style='width:190px'>" + text + "</body></html>"));
	}
}
