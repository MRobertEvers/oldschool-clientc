package com.torirs.client;

import android.content.Context;
import android.util.Base64;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The game pack: the native client the prod app runs, fetched from the server
 * instead of installed with the APK.
 *
 * <h2>What the server publishes</h2>
 *
 * tools/deploy/build_osrs239_package.py --android builds the client for every
 * ABI from the same commit as the world it deploys, and serves under
 * https://torirs.mrobertevers.com/android/ (both sign-ins required):
 *
 *   latest.json       { format, jni_abi, commit, content, built, world,
 *                       manifest, data: {path, sha256, size},
 *                       libs: { <abi>: {path, sha256, size} }, apk }
 *   latest.json.sig   ECDSA P-256 / SHA-256 over latest.json's exact bytes,
 *                     made with a key that lives only on the server
 *   <abi>/libtorirs-<hash>.so   the client
 *   data-<hash>.zip   what the client opens with fopen: the manifest and the
 *                     revconfig INIs (everything else it asks io_server for)
 *
 * <h2>What the app trusts</h2>
 *
 * latest.json only with a good signature from the key in
 * res/raw/gamepack_signing_pub.pem, and every file only if its SHA-256 is the
 * one latest.json names. TLS to the proxy already says the bytes came from
 * torirs.mrobertevers.com; the signature also says the server that BUILT them
 * meant them, so the proxy box in between cannot hand a phone code to run.
 *
 * <h2>When a new one takes effect</h2>
 *
 * A loaded .so cannot be unloaded, so a process runs one game pack for its
 * whole life. The launcher checks on every start; a newer pack found while an
 * older one is loaded restarts the process (LauncherActivity.restart).
 */
final class GamePack
{
    /**
     * The JNI contract this APK was built for: TORIRS_ANDROID_JNI_ABI in
     * src/platform/platform_android.h. BUMP BOTH TOGETHER. A pack with another
     * number is refused, and the person is asked to install the new APK.
     */
    static final int JNI_ABI = 1;

    private static final String TAG = "torirs";
    private static String loadedPath;

    /** What latest.json says, after its signature checked out. */
    static final class Release
    {
        String commit;
        String built;
        String world;
        String manifest;
        int jniAbi;
        String apk;
        String abi;
        String libPath;
        String libSha;
        long libSize;
        String dataPath;
        String dataSha;
        long dataSize;
    }

    private GamePack()
    {
    }

    static synchronized String loadedPath()
    {
        return loadedPath;
    }

    static String baseUrl()
    {
        String p = BuildConfig.GAMEPACK_PATH;
        return "https://" + BuildConfig.SERVER_HOST + p.substring(0, p.lastIndexOf('/') + 1);
    }

    /**
     * The ABI this PROCESS runs as. The APK carries no native library, so
     * Android starts it as the device's primary ABI; os.arch is that, which
     * Build.SUPPORTED_ABIS (the device's whole list) is not.
     */
    static String processAbi()
    {
        String arch = System.getProperty("os.arch", "").toLowerCase(Locale.US);
        if( arch.equals("aarch64") || arch.equals("arm64") )
            return "arm64-v8a";
        if( arch.startsWith("arm") )
            return "armeabi-v7a";
        if( arch.equals("x86_64") || arch.equals("amd64") )
            return "x86_64";
        return arch;
    }

    /** Fetch and check latest.json. */
    static Release fetch(Context context, String cookie)
            throws IOException, GeneralSecurityException
    {
        String url = "https://" + BuildConfig.SERVER_HOST + BuildConfig.GAMEPACK_PATH;
        Http.Response json = Http.request(context, "GET", url, cookie, null, null);
        if( json.status != 200 )
            throw new Http.HttpStatusException(json.status, url);
        Http.Response sig = Http.request(context, "GET", url + ".sig", cookie, null, null);
        if( sig.status != 200 )
            throw new Http.HttpStatusException(sig.status, url + ".sig");

        Signature verifier = Signature.getInstance("SHA256withECDSA");
        verifier.initVerify(publicKey(context));
        verifier.update(json.body);
        if( !verifier.verify(sig.body) )
            throw new GeneralSecurityException("latest.json is not signed by the game pack key");

        try
        {
            JSONObject j = new JSONObject(json.text());
            if( j.getInt("format") != 1 )
                throw new IOException("latest.json format " + j.getInt("format") + " is newer than this app");
            Release r = new Release();
            r.commit = j.getString("commit");
            r.built = j.optString("built", "");
            r.world = j.optString("world", "");
            r.manifest = j.getString("manifest");
            r.jniAbi = j.getInt("jni_abi");
            r.apk = j.optString("apk", "");
            r.abi = processAbi();
            JSONObject data = j.getJSONObject("data");
            r.dataPath = data.getString("path");
            r.dataSha = data.getString("sha256");
            r.dataSize = data.getLong("size");
            JSONObject libs = j.getJSONObject("libs");
            if( libs.has(r.abi) )
            {
                JSONObject lib = libs.getJSONObject(r.abi);
                r.libPath = lib.getString("path");
                r.libSha = lib.getString("sha256");
                r.libSize = lib.getLong("size");
            }
            return r;
        }
        catch( JSONException e )
        {
            throw new IOException("latest.json: " + e.getMessage());
        }
    }

    /** Where this release's library lives on the phone. Private storage, never
     *  external: shared storage is mounted noexec, and anyone may write it. */
    static File libFile(Context context, Release r)
    {
        File dir = new File(context.getFilesDir(), "gamepack");
        return new File(dir, "libtorirs-" + r.libSha.substring(0, 16) + ".so");
    }

    /** True when this release's library is already on the phone. */
    static boolean haveLib(Context context, Release r)
    {
        File f = libFile(context, r);
        return f.isFile() && f.length() == r.libSize;
    }

    /** Download this release's library, verify it, and keep it read-only.
     *  Older ones go, except the one this process has loaded. */
    static File ensureLib(Context context, Release r, String cookie, Http.Progress progress)
            throws IOException, GeneralSecurityException
    {
        File lib = libFile(context, r);
        if( haveLib(context, r) )
            return lib;
        File dir = lib.getParentFile();
        if( !dir.isDirectory() && !dir.mkdirs() )
            throw new IOException("cannot create " + dir);
        File part = new File(dir, lib.getName() + ".part");
        String sha = Http.download(context, baseUrl() + r.libPath, cookie, part, progress);
        if( !sha.equals(r.libSha) || part.length() != r.libSize )
        {
            part.delete();
            throw new IOException("the downloaded game pack does not match latest.json");
        }
        if( !part.renameTo(lib) )
            throw new IOException("cannot move the game pack into place");
        /* Android 14 refuses to load code from a writable file; read-only is
         * the right state for it on every version anyway. */
        lib.setReadOnly();
        File[] old = dir.listFiles();
        if( old != null )
        {
            for( File f : old )
            {
                if( !f.equals(lib) && !f.getAbsolutePath().equals(loadedPath()) )
                    f.delete();
            }
        }
        return lib;
    }

    /** Unpack this release's data zip into the data root, unless it already
     *  is (a marker remembers which one was). */
    static void ensureData(Context context, File dataRoot, Release r, String cookie,
            Http.Progress progress) throws IOException, GeneralSecurityException
    {
        File marker = new File(dataRoot, ".gamepack_data");
        if( r.dataSha.equals(readSmall(marker)) )
            return;
        File zip = new File(context.getCacheDir(), "gamepack-data.zip");
        String sha = Http.download(context, baseUrl() + r.dataPath, cookie, zip, progress);
        if( !sha.equals(r.dataSha) )
        {
            zip.delete();
            throw new IOException("the downloaded game data does not match latest.json");
        }
        String root = dataRoot.getCanonicalPath() + File.separator;
        byte[] buf = new byte[64 * 1024];
        try( ZipInputStream in = new ZipInputStream(new FileInputStream(zip)) )
        {
            ZipEntry e;
            while( (e = in.getNextEntry()) != null )
            {
                File out = new File(dataRoot, e.getName());
                if( !out.getCanonicalPath().startsWith(root) )
                    throw new IOException("data zip names a path outside the data root: " + e.getName());
                if( e.isDirectory() )
                {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try( OutputStream o = new FileOutputStream(out) )
                {
                    int n;
                    while( (n = in.read(buf)) > 0 )
                        o.write(buf, 0, n);
                }
            }
        }
        zip.delete();
        try( OutputStream o = new FileOutputStream(marker) )
        {
            o.write(r.dataSha.getBytes(StandardCharsets.UTF_8));
        }
    }

    /** Load `lib` into this process and check it speaks this APK's JNI. */
    static synchronized void load(File lib)
    {
        String path = lib.getAbsolutePath();
        if( path.equals(loadedPath) )
            return;
        if( loadedPath != null )
            throw new IllegalStateException("a game pack is already loaded: " + loadedPath);
        System.load(path);
        loadedPath = path;
        int abi = ClientActivity.nativeJniAbi();
        if( abi != JNI_ABI )
            throw new IllegalStateException("game pack speaks JNI " + abi + ", this app " + JNI_ABI);
        Log.i(TAG, "game pack loaded: " + path);
    }

    private static PublicKey publicKey(Context context) throws IOException, GeneralSecurityException
    {
        StringBuilder b64 = new StringBuilder();
        try( BufferedReader r = new BufferedReader(new InputStreamReader(
                context.getResources().openRawResource(R.raw.gamepack_signing_pub), StandardCharsets.US_ASCII)) )
        {
            String line;
            while( (line = r.readLine()) != null )
            {
                if( !line.startsWith("-----") )
                    b64.append(line.trim());
            }
        }
        byte[] der = Base64.decode(b64.toString(), Base64.DEFAULT);
        return KeyFactory.getInstance("EC").generatePublic(new X509EncodedKeySpec(der));
    }

    private static String readSmall(File f)
    {
        if( !f.isFile() )
            return "";
        try( InputStream in = new FileInputStream(f) )
        {
            return new String(Http.readAll(in), StandardCharsets.UTF_8).trim();
        }
        catch( IOException e )
        {
            return "";
        }
    }
}
