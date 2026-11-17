package net.runelite.client.plugins.torirs;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.Duration;
import java.util.function.Consumer;

/**
 * The two sign-ins a ToriRS world behind the public proxy asks for, kept and
 * refreshed here, and handed to the game socket as one Cookie header.
 *
 * <ul>
 * <li>The IaP ({@code torirs.iap}, https://auth.mrobertevers.com): an account
 *     session, {@code __Secure-iap}, without which the proxy answers nothing
 *     on torirs.mrobertevers.com. Asked for only when the world is reached
 *     through the proxy.
 * <li>The world's sign-in gate ({@code torirs.gate.api}): a ticket,
 *     {@code torirs_gate}, without which torirsserver refuses the socket.
 * </ul>
 *
 * Both are got by the device-code grant (RFC 8628): the plugin asks for a
 * code, opens the browser at the approval page -- where the person signs in on
 * the ordinary sign-in form, so the operating system's or browser's password
 * filler fills it, or is already signed in and presses Approve -- and polls
 * until it is approved. Each answer carries a 30-day rotating refresh token;
 * the session and the ticket last a day and are refreshed here before they
 * run out, so a client in use never asks again.
 *
 * The game socket reads the header from the system property
 * {@code torirs.cookie} at every connect (the gamepack's TorirsNet), so a
 * sign-in made after the client started counts from the next login.
 *
 * Kept in {@code ~/.runelite/torirs/signin.json}, mode 0600.
 */
final class ToriRSSignIn
{
	static final String IAP_COOKIE = "__Secure-iap";
	static final String GATE_COOKIE = "torirs_gate";
	private static final String DEVICE_GRANT = "urn:ietf:params:oauth:grant-type:device_code";
	/** Refresh what has less than this left. */
	private static final long REFRESH_MARGIN_MS = 2 * 60 * 60 * 1000L;

	/** Where each part lives; null iap: the world is not behind the proxy. */
	final String iap;
	final String gateApi;
	final String gatePage;
	private final Path stateFile;
	private final String device;

	private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(15)).build();
	private final Gson gson = new Gson();

	/** What is kept. Fields are read by Gson. */
	static final class State
	{
		String iapEmail;
		String iapSession;
		long iapSessionExpiresAt;
		String iapRefresh;
		String gateName;
		String gateTicket;
		long gateTicketExpiresAt;
		String gateRefresh;
	}

	private State state = new State();
	private volatile boolean cancelled;

	ToriRSSignIn(String iap, String gateApi, String gatePage, Path stateFile, String device)
	{
		this.iap = iap;
		this.gateApi = gateApi;
		this.gatePage = gatePage;
		this.stateFile = stateFile;
		this.device = device;
	}

	static ToriRSSignIn fromSystemProperties()
	{
		String iap = prop("torirs.iap", null);
		String gateApi = prop("torirs.gate.api", null);
		String gatePage = prop("torirs.gate.page", gateApi);
		String home = System.getProperty("user.home");
		Path file = Paths.get(prop("torirs.state", home + "/.runelite/torirs/signin.json"));
		String host;
		try
		{
			host = java.net.InetAddress.getLocalHost().getHostName();
		}
		catch (IOException e)
		{
			host = "this computer";
		}
		return new ToriRSSignIn(trim(iap), trim(gateApi), trim(gatePage), file, "RuneLite on " + host);
	}

	private static String prop(String key, String fallback)
	{
		String v = System.getProperty(key);
		return v == null || v.isEmpty() ? fallback : v;
	}

	private static String trim(String base)
	{
		return base == null ? null : base.replaceAll("/+$", "");
	}

	boolean configured()
	{
		return gateApi != null;
	}

	synchronized State snapshot()
	{
		State s = gson.fromJson(gson.toJson(state), State.class);
		return s;
	}

	boolean iapSignedIn()
	{
		return iap == null || (state.iapSession != null && state.iapSessionExpiresAt > System.currentTimeMillis());
	}

	boolean gateSignedIn()
	{
		return state.gateTicket != null && state.gateTicketExpiresAt > System.currentTimeMillis();
	}

	// ------------------------------------------------------------------ state

	synchronized void load()
	{
		try
		{
			if (Files.exists(stateFile))
			{
				State s = gson.fromJson(Files.readString(stateFile), State.class);
				if (s != null)
				{
					state = s;
				}
			}
		}
		catch (IOException | RuntimeException e)
		{
			state = new State();
		}
		publish();
	}

	private synchronized void save()
	{
		try
		{
			Files.createDirectories(stateFile.getParent());
			Path tmp = stateFile.resolveSibling(stateFile.getFileName() + ".part");
			Files.writeString(tmp, gson.toJson(state));
			try
			{
				Files.setPosixFilePermissions(tmp, PosixFilePermissions.fromString("rw-------"));
			}
			catch (UnsupportedOperationException ignored)
			{
				// not a POSIX filesystem
			}
			Files.move(tmp, stateFile, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
		}
		catch (IOException e)
		{
			// The sign-in still works for this run; it is asked for again next time.
		}
		publish();
	}

	/** The Cookie header the game socket sends (TorirsNet reads it at connect). */
	private void publish()
	{
		StringBuilder c = new StringBuilder();
		if (iap != null && state.iapSession != null)
		{
			c.append(IAP_COOKIE).append('=').append(state.iapSession);
		}
		if (state.gateTicket != null)
		{
			if (c.length() > 0)
			{
				c.append("; ");
			}
			c.append(GATE_COOKIE).append('=').append(state.gateTicket);
		}
		if (c.length() > 0)
		{
			System.setProperty("torirs.cookie", c.toString());
		}
		else
		{
			System.clearProperty("torirs.cookie");
		}
	}

	// ---------------------------------------------------------------- refresh

	/** Refresh whatever runs out soon. Quiet: a failure leaves that part signed out. */
	void refreshDue()
	{
		long soon = System.currentTimeMillis() + REFRESH_MARGIN_MS;
		if (iap != null && state.iapRefresh != null && state.iapSessionExpiresAt < soon)
		{
			try
			{
				JsonObject body = new JsonObject();
				body.addProperty("grant_type", "refresh_token");
				body.addProperty("refresh_token", state.iapRefresh);
				Reply r = postJson(iap + "/api/device/token", body);
				if (r.status == 200)
				{
					takeIap(r.json);
				}
				else if ("invalid_grant".equals(str(r.json, "error_code")))
				{
					synchronized (this)
					{
						state.iapSession = state.iapRefresh = null;
					}
					save();
				}
			}
			catch (IOException e)
			{
				// offline: try again on the next tick
			}
		}
		if (state.gateRefresh != null && state.gateTicketExpiresAt < soon && iapSignedIn())
		{
			try
			{
				Reply r = postForm(gateApi + "/device/token", "grant_type=refresh_token&refresh_token=" + enc(state.gateRefresh));
				if (r.status == 200)
				{
					takeGate(r.json);
				}
				else if ("invalid_grant".equals(str(r.json, "error")))
				{
					synchronized (this)
					{
						state.gateTicket = state.gateRefresh = null;
					}
					save();
				}
			}
			catch (IOException e)
			{
				// offline: try again on the next tick
			}
		}
	}

	// ---------------------------------------------------------------- sign in

	/** What the panel shows while a code waits: the code, and the page opened. */
	interface Progress
	{
		void waiting(String what, String userCode, String url);

		void said(String line);
	}

	void cancel()
	{
		cancelled = true;
	}

	/**
	 * Every sign-in still missing, in order: the IaP first (the gate is behind
	 * it). Blocks until approved, denied, expired or cancelled. Run it off the
	 * Swing thread.
	 */
	void signIn(Progress progress, Consumer<String> openBrowser) throws IOException, InterruptedException
	{
		cancelled = false;
		refreshDue();
		if (!iapSignedIn())
		{
			JsonObject body = new JsonObject();
			body.addProperty("device", device);
			Reply a = postJson(iap + "/api/device/authorize", body);
			if (a.status != 200)
			{
				throw new IOException("mrobertevers.com sign-in: " + describe(a));
			}
			String url = str(a.json, "verification_uri_complete");
			progress.waiting("mrobertevers.com", str(a.json, "user_code"), url);
			openBrowser.accept(url);
			JsonObject grant = new JsonObject();
			grant.addProperty("grant_type", DEVICE_GRANT);
			grant.addProperty("device_code", str(a.json, "device_code"));
			JsonObject done = poll(a.json, () -> postJson(iap + "/api/device/token", grant), "error_code");
			takeIap(done);
			progress.said("Signed in to mrobertevers.com as " + state.iapEmail + ".");
		}
		if (!gateSignedIn())
		{
			Reply a = postForm(gateApi + "/device/authorize", "device=" + enc(device));
			if (a.status != 200)
			{
				throw new IOException("ToriRS sign-in: " + describe(a));
			}
			// The page is opened on the public https host whatever address the
			// API was reached by: the gate's cookie is Secure, and a browser
			// keeps it from https only.
			String url = gatePage + str(a.json, "verification_path_complete");
			progress.waiting("ToriRS", str(a.json, "user_code"), url);
			openBrowser.accept(url);
			String form = "grant_type=" + enc(DEVICE_GRANT) + "&device_code=" + enc(str(a.json, "device_code"));
			JsonObject done = poll(a.json, () -> postForm(gateApi + "/device/token", form), "error");
			takeGate(done);
			progress.said("Signed in to ToriRS as " + state.gateName + ".");
		}
	}

	interface Call
	{
		Reply send() throws IOException;
	}

	private JsonObject poll(JsonObject authorize, Call call, String errorKey) throws IOException, InterruptedException
	{
		long interval = Math.max(1, authorize.has("interval") ? authorize.get("interval").getAsLong() : 5) * 1000L;
		long deadline = System.currentTimeMillis() + (authorize.has("expires_in") ? authorize.get("expires_in").getAsLong() : 600) * 1000L;
		while (System.currentTimeMillis() < deadline)
		{
			Thread.sleep(interval);
			if (cancelled)
			{
				throw new InterruptedException("cancelled");
			}
			Reply r = call.send();
			if (r.status == 200)
			{
				return r.json;
			}
			String error = str(r.json, errorKey);
			if ("authorization_pending".equals(error))
			{
				continue;
			}
			if ("slow_down".equals(error))
			{
				interval += 5000;
				continue;
			}
			if ("access_denied".equals(error))
			{
				throw new IOException("the sign-in was denied in the browser");
			}
			if ("expired_token".equals(error))
			{
				throw new IOException("the code expired before it was approved");
			}
			throw new IOException(describe(r));
		}
		throw new IOException("the code expired before it was approved");
	}

	private void takeIap(JsonObject j)
	{
		synchronized (this)
		{
			state.iapEmail = str(j, "email");
			state.iapSession = str(j, "session");
			state.iapSessionExpiresAt = System.currentTimeMillis() + j.get("session_expires_in").getAsLong() * 1000L;
			state.iapRefresh = str(j, "refresh_token");
		}
		save();
	}

	private void takeGate(JsonObject j)
	{
		synchronized (this)
		{
			state.gateName = str(j, "name");
			state.gateTicket = str(j, "ticket");
			state.gateTicketExpiresAt = System.currentTimeMillis() + j.get("ticket_expires_in").getAsLong() * 1000L;
			state.gateRefresh = str(j, "refresh_token");
		}
		save();
	}

	/** Sign out of both, here and on the servers (best effort). */
	void signOut()
	{
		State s = snapshot();
		if (s.gateRefresh != null)
		{
			try
			{
				postForm(gateApi + "/device/revoke", "refresh_token=" + enc(s.gateRefresh));
			}
			catch (IOException ignored)
			{
				// signed out here regardless
			}
		}
		if (iap != null && s.iapRefresh != null)
		{
			try
			{
				JsonObject body = new JsonObject();
				body.addProperty("refresh_token", s.iapRefresh);
				postJson(iap + "/api/device/revoke", body);
			}
			catch (IOException ignored)
			{
				// signed out here regardless
			}
		}
		synchronized (this)
		{
			state = new State();
		}
		save();
	}

	// --------------------------------------------------------- linked accounts

	/**
	 * The player accounts linked to this web login (the gate's
	 * POST /account/players), as the browser client's Play Now cards show them.
	 */
	java.util.List<String> players() throws IOException
	{
		Reply r = postSignedIn("/account/players", "");
		if (r.status != 200)
		{
			throw new IOException("the account list: " + describe(r));
		}
		java.util.List<String> out = new java.util.ArrayList<>();
		String all = str(r.json, "players");
		if (all != null)
		{
			for (String name : all.split("\n"))
			{
				if (!name.isEmpty())
				{
					out.add(name);
				}
			}
		}
		return out;
	}

	/**
	 * A one-time login for a linked account (POST /account/play): { token,
	 * name }, the name being the one the server will check the token against.
	 */
	String[] play(String player) throws IOException
	{
		Reply r = postSignedIn("/account/play", "player=" + enc(player));
		if (r.status != 200 || str(r.json, "token") == null)
		{
			throw new IOException("Play " + player + ": " + describe(r));
		}
		String name = str(r.json, "name");
		return new String[]{str(r.json, "token"), name != null ? name : player};
	}

	/** A form post behind the gate's ticket check: the ticket's cookie, and the IaP's through the proxy. */
	private Reply postSignedIn(String path, String form) throws IOException
	{
		String cookie = System.getProperty("torirs.cookie");
		HttpRequest.Builder b = HttpRequest.newBuilder(URI.create(gateApi + path))
			.timeout(Duration.ofSeconds(20))
			.header("Content-Type", "application/x-www-form-urlencoded")
			.POST(HttpRequest.BodyPublishers.ofString(form));
		if (cookie != null)
		{
			b.header("Cookie", cookie);
		}
		return send(b.build());
	}

	// ------------------------------------------------------------------- http

	static final class Reply
	{
		int status;
		JsonObject json;
		String body;
	}

	private Reply postJson(String url, JsonObject body) throws IOException
	{
		HttpRequest req = HttpRequest.newBuilder(URI.create(url))
			.timeout(Duration.ofSeconds(20))
			.header("Content-Type", "application/json")
			.POST(HttpRequest.BodyPublishers.ofString(gson.toJson(body)))
			.build();
		return send(req);
	}

	/** A form post to the gate; through the proxy it needs the IaP session too. */
	private Reply postForm(String url, String form) throws IOException
	{
		HttpRequest.Builder b = HttpRequest.newBuilder(URI.create(url))
			.timeout(Duration.ofSeconds(20))
			.header("Content-Type", "application/x-www-form-urlencoded")
			.POST(HttpRequest.BodyPublishers.ofString(form));
		if (iap != null && state.iapSession != null)
		{
			b.header("Cookie", IAP_COOKIE + "=" + state.iapSession);
		}
		return send(b.build());
	}

	private Reply send(HttpRequest req) throws IOException
	{
		try
		{
			HttpResponse<String> res = http.send(req, HttpResponse.BodyHandlers.ofString());
			Reply r = new Reply();
			r.status = res.statusCode();
			r.body = res.body();
			try
			{
				r.json = gson.fromJson(r.body, JsonObject.class);
			}
			catch (RuntimeException e)
			{
				r.json = null;
			}
			if (r.json == null)
			{
				r.json = new JsonObject();
			}
			return r;
		}
		catch (InterruptedException e)
		{
			Thread.currentThread().interrupt();
			throw new IOException("interrupted");
		}
	}

	private static String describe(Reply r)
	{
		String why = str(r.json, "error_description");
		if (why == null)
		{
			why = str(r.json, "error");
		}
		if (r.status == 401 || r.status == 302)
		{
			return "not signed in to mrobertevers.com (" + r.status + ")";
		}
		return (why != null ? why : "HTTP " + r.status);
	}

	private static String str(JsonObject j, String key)
	{
		return j != null && j.has(key) && !j.get(key).isJsonNull() ? j.get(key).getAsString() : null;
	}

	private static String enc(String s)
	{
		return URLEncoder.encode(s == null ? "" : s, StandardCharsets.UTF_8);
	}
}
