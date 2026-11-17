package com.torirs.client;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;

/**
 * The manifest the prod app boots: the server's manifest_latest.ini, with
 * every network endpoint pointed at the relay (IapTunnel).
 *
 * The server's copy is written for the BROWSER (transport=ws, the page's own
 * origin for /boot and /io). The native client instead dials plain TCP, so:
 *
 *   [net:boot]   transport=tcp, host=127.0.0.1, port=<relay>
 *   [js5:boot]   enabled, inheriting that endpoint; no 443 fallback (the relay
 *                is the only way out, and it already goes to 443)
 *   [io:boot]    the relay too: io_server's /boot files and plugin files
 *
 * [cache:boot] dir is kept: relative to the manifest it lands in the data
 * root (../cache.<world>), which JS5 fills as the client asks. It must exist
 * before boot (executor_prepare_js5_cache opens, never creates, it).
 *
 * Written fresh on every launch, because the relay's port is whatever the
 * system gave it this time.
 */
final class ProdManifest
{
    private ProdManifest()
    {
    }

    /** Write manifests/manifest_android.ini beside `latest` and return it. */
    static File write(File latest, int relayPort) throws IOException
    {
        File out = new File(latest.getParentFile(), "manifest_android.ini");
        StringBuilder sb = new StringBuilder();
        sb.append("; manifest_android.ini: written by the prod app from ")
                .append(latest.getName())
                .append(" on every launch (ProdManifest.java). Edits are lost.\n");
        String section = "";
        String cacheDir = null;
        try( BufferedReader r = new BufferedReader(new InputStreamReader(
                new FileInputStream(latest), StandardCharsets.UTF_8)) )
        {
            String line;
            while( (line = r.readLine()) != null )
            {
                String t = line.trim();
                if( t.startsWith("[") && t.endsWith("]") )
                {
                    section = t.substring(1, t.length() - 1);
                    if( dropped(section) )
                        continue;
                    sb.append(line).append('\n');
                    continue;
                }
                if( dropped(section) )
                    continue;
                int eq = t.indexOf('=');
                String key = eq > 0 && !t.startsWith(";") ? t.substring(0, eq).trim() : "";
                if( section.equals("net:boot") )
                {
                    if( key.equals("transport") )
                        line = "transport=tcp";
                    else if( key.equals("host") )
                        line = "host=127.0.0.1";
                    else if( key.equals("port") )
                        line = "port=" + relayPort;
                }
                else if( section.equals("cache:boot") && key.equals("dir") )
                {
                    cacheDir = t.substring(eq + 1).trim();
                }
                sb.append(line).append('\n');
            }
        }
        sb.append("\n[js5:boot]\nenabled=true\nfallback_port=0\n");
        sb.append("\n[io:boot]\nhost=127.0.0.1\nport=").append(relayPort).append('\n');

        if( cacheDir == null || cacheDir.isEmpty() )
            throw new IOException(latest.getName() + " names no [cache:boot] dir");
        File cache = new File(latest.getParentFile(), cacheDir);
        if( !cache.isDirectory() && !cache.mkdirs() )
            throw new IOException("cannot create the cache directory " + cache);

        try( Writer w = new OutputStreamWriter(new FileOutputStream(out), StandardCharsets.UTF_8) )
        {
            w.write(sb.toString());
        }
        return out;
    }

    /* Replaced wholesale by the two sections appended above, and the
     * launcher-only [derived:*] blocks the client never reads. */
    private static boolean dropped(String section)
    {
        return section.startsWith("js5:") || section.startsWith("io:") || section.startsWith("derived:");
    }
}
