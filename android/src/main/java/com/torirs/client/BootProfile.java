package com.torirs.client;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * One boot manifest on the device, and whether it can actually be booted.
 *
 * <h2>Why the boot menu reads the filesystem and not a built-in list</h2>
 *
 * A profile is not a thing the app knows about -- it is a manifest file that
 * somebody pushed to the device. Hard-coding the list would mean a new manifest
 * is unbootable until the APK is rebuilt, and worse, a profile whose cache was
 * never pushed would still be offered and would fail deep inside the client
 * with a cache error. Discovering them instead makes the menu describe what is
 * really there.
 *
 * <h2>What "valid" means</h2>
 *
 * A manifest names the cache directory it needs, as {@code [cache:boot] dir=},
 * resolved relative to the manifest's own directory (see
 * src/bootmanifest/bootmanifest.h). This class resolves that the same way and
 * checks the directory exists and is non-empty. That single check is what turns
 * "the client started and then died" into a greyed-out row with a reason on it,
 * which is the whole reason the boot screen is worth having on a device where
 * there is no console to read.
 *
 * <p>The second check is the TRANSPORT. This lane does not link the embedded
 * server (see the android block in src/platform/platform.mk), and a manifest
 * asking for {@code [net:boot] transport=embed} therefore cannot boot here.
 * That failure is otherwise SILENT -- net_transport_embed.c compiles to a stub
 * without -DTORIRS_EMBED_SERVER=1, so the client comes up and connects to
 * nothing, which looks like a hung login rather than a misconfigured profile.
 * Refusing it by name in the menu is what turns it into a sentence.
 *
 * <p>This is a deliberately SHALLOW read of the manifest -- two keys. The client
 * is the thing that parses manifests, and a second full parser in Java would be
 * a second set of opinions about the format, drifting the moment the real one
 * gains a key.
 */
final class BootProfile
{
    /** The manifest file itself. */
    final File manifest;
    /** Display name: the manifest's filename without its prefix or extension. */
    final String name;
    /** The cache directory the manifest names, resolved. Null when unstated. */
    final File cacheDir;
    /** Null when this profile can boot; otherwise why it cannot. */
    final String problem;
    /**
     * What this profile needs from the network before it can show anything, or
     * null when it is self-contained.
     *
     * Distinct from {@link #problem} because it is NOT a refusal: the server may
     * well be running, and the menu has no way to find out without a network
     * call it would have to block on. What it does mean is that the profile must
     * never be chosen AUTOMATICALLY when a self-contained one exists -- a boot
     * menu whose countdown picks a profile that cannot come up alone leaves the
     * user staring at a failure they did not choose.
     */
    final String needsServer;

    private BootProfile(File manifest, String name, File cacheDir, String problem,
                        String needsServer)
    {
        this.manifest = manifest;
        this.name = name;
        this.cacheDir = cacheDir;
        this.problem = problem;
        this.needsServer = needsServer;
    }

    boolean isBootable()
    {
        return problem == null;
    }

    /** Bootable AND needs nothing outside the device. What the countdown picks. */
    boolean isSelfContained()
    {
        return problem == null && needsServer == null;
    }

    /**
     * The data root: the app's own external files directory.
     *
     * Chosen over anything else on the device because it is the one location an
     * app can always read and write with no permission on every API level from
     * 21 to today, and which `adb push` can still reach. It is also removed when
     * the app is uninstalled, which is the right lifetime for a cache.
     */
    static File dataRoot(Context context)
    {
        File external = context.getExternalFilesDir(null);
        /* getExternalFilesDir returns null when external storage is not
         * mounted -- rare, but real. Internal storage is always there and the
         * client cannot tell the difference; only the push command changes. */
        return external != null ? external : context.getFilesDir();
    }

    static File manifestDir(Context context)
    {
        return new File(dataRoot(context), "manifests");
    }

    /**
     * Every manifest on the device, bootable ones first and then by name.
     *
     * Unbootable profiles are LISTED, not hidden. A profile that is missing its
     * cache is the single most likely thing to be wrong on a fresh device, and a
     * menu that silently omitted it would leave the user wondering why the
     * profile they pushed is not there.
     */
    static List<BootProfile> discover(Context context)
    {
        List<BootProfile> found = new ArrayList<>();
        File dir = manifestDir(context);
        File[] files = dir.listFiles();

        if( files == null )
            return found;

        for( File f : files )
        {
            if( !f.isFile() || !f.getName().endsWith(".ini") )
                continue;
            found.add(read(f));
        }

        /* Three tiers, best first: boots alone, boots with a server, cannot
         * boot. The tier is what the countdown's default reads, so the order
         * here is a behaviour and not only a presentation. */
        Collections.sort(found, (a, b) -> {
            int ta = a.tier();
            int tb = b.tier();
            if( ta != tb )
                return ta - tb;
            return a.name.compareToIgnoreCase(b.name);
        });
        return found;
    }

    private static BootProfile read(File manifest)
    {
        String name = displayName(manifest.getName());
        String dirValue = null;
        String transport = null;
        String source = null;
        String host = null;

        try( BufferedReader r = new BufferedReader(new FileReader(manifest)) )
        {
            String line;
            String section = "";

            while( (line = r.readLine()) != null )
            {
                line = line.trim();
                if( line.isEmpty() || line.startsWith(";") || line.startsWith("#") )
                    continue;
                if( line.startsWith("[") )
                {
                    /* Section headers are `[type:name]`. Only two sections
                     * matter here; every other one is the client's business,
                     * not the menu's. */
                    section = line.toLowerCase();
                    continue;
                }

                int eq = line.indexOf('=');
                if( eq <= 0 )
                    continue;
                String key = line.substring(0, eq).trim();
                String value = stripComment(line.substring(eq + 1).trim());

                if( section.equals("[cache:boot]") && key.equalsIgnoreCase("dir") )
                    dirValue = value;
                else if( section.equals("[cache:boot]") && key.equalsIgnoreCase("source") )
                    source = value;
                else if( section.equals("[net:boot]") && key.equalsIgnoreCase("transport") )
                    transport = value;
                else if( section.equals("[net:boot]") && key.equalsIgnoreCase("host") )
                    host = value;
            }
        }
        catch( Exception e )
        {
            return new BootProfile(manifest, name, null, "unreadable: " + e.getMessage(), null);
        }

        /*
         * Checked before the cache, because it is unconditional: an embed
         * profile cannot boot here however complete its data is, so reporting a
         * missing cache first would send the user off to push hundreds of
         * megabytes that would not have helped.
         */
        if( transport != null && transport.equalsIgnoreCase("embed") )
        {
            return new BootProfile(manifest, name, null,
                    "needs the embedded server - this build is a client only", null);
        }

        /*
         * `[cache:boot] source=ondemand` means the CACHE ITSELF is streamed
         * from the game server (the 2004 on-demand protocol), so with no server
         * reachable the client cannot read a single archive -- App_Init asserts
         * on it rather than limping. That is correct behaviour, and it is
         * exactly why such a profile must not be what a countdown picks by
         * itself: the user would see an abort they did not ask for.
         *
         * It is still offered, because the server may well be running.
         */
        String serverNote = null;
        if( source != null && source.equalsIgnoreCase("ondemand") )
        {
            serverNote = "streams its cache from "
                    + (host != null && !host.isEmpty() ? host : "the game server");
        }

        if( dirValue == null || dirValue.isEmpty() )
        {
            /*
             * No dir= at all is legitimate -- the manifest header calls it
             * "stream every boot, write nothing down", and the client defaults
             * the location itself. Nothing for this check to verify, so the
             * profile is offered rather than refused.
             */
            return new BootProfile(manifest, name, null, null, serverNote);
        }

        if( dirValue.startsWith("idb:") )
        {
            /* A web build's IndexedDB database name. It cannot boot here, and
             * saying so is better than resolving it as a relative directory --
             * which is exactly what BootManifest_CacheLocationIsIdb exists to
             * stop callers doing. */
            return new BootProfile(manifest, name, null, "web-only cache (idb:)", null);
        }

        File cache = dirValue.startsWith("/")
                ? new File(dirValue)
                : new File(manifest.getParentFile(), dirValue);

        /*
         * Resolved through getCanonicalFile so a `../cache.osrs239` in the
         * manifest becomes the real path -- which is the whole reason the data
         * layout on the device mirrors the repo's.
         */
        try
        {
            cache = cache.getCanonicalFile();
        }
        catch( Exception ignored )
        {
            /* Keep the unresolved form; the existence check below still works
             * and the path shown in the menu is still recognisable. */
        }

        if( !cache.isDirectory() )
            return new BootProfile(manifest, name, cache, "cache missing: " + cache.getName(), serverNote);

        String[] contents = cache.list();
        if( contents == null || contents.length == 0 )
            return new BootProfile(manifest, name, cache, "cache empty: " + cache.getName(), serverNote);

        return new BootProfile(manifest, name, cache, null, serverNote);
    }

    /** 0 = boots alone, 1 = boots with a server, 2 = cannot boot. */
    int tier()
    {
        if( problem != null )
            return 2;
        return needsServer != null ? 1 : 0;
    }

    /** `manifest_osrs239_rs2012.ini` -> `osrs239 rs2012`. */
    private static String displayName(String filename)
    {
        String s = filename;
        if( s.endsWith(".ini") )
            s = s.substring(0, s.length() - 4);
        if( s.startsWith("manifest_") )
            s = s.substring("manifest_".length());
        return s.replace('_', ' ');
    }

    /** INI values may carry a trailing `;` or `#` comment. */
    private static String stripComment(String value)
    {
        int cut = value.length();
        for( int i = 0; i < value.length(); i++ )
        {
            char c = value.charAt(i);
            if( c == ';' || c == '#' )
            {
                cut = i;
                break;
            }
        }
        return value.substring(0, cut).trim();
    }
}
