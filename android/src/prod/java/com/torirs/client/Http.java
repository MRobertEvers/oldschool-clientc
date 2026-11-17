package com.torirs.client;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;

import javax.net.ssl.HttpsURLConnection;

/**
 * The prod app's few HTTPS requests: the IaP's JSON API, the torirs gate's
 * sign-in form, and the game pack's files. Blocking; never on the UI thread.
 */
final class Http
{
    static final int TIMEOUT_MS = 20000;

    private Http()
    {
    }

    /** One answer: its status, its Location, and its body. */
    static final class Response
    {
        int status;
        String location;
        byte[] body = new byte[0];

        String text()
        {
            return new String(body, StandardCharsets.UTF_8);
        }
    }

    /**
     * One request. `cookie` is the whole Cookie header or null; `contentType`
     * and `body` go together or are both null. Any status is an answer here;
     * only no answer at all is an IOException.
     */
    static Response request(
            Context context,
            String method,
            String url,
            String cookie,
            String contentType,
            byte[] body) throws IOException, GeneralSecurityException
    {
        HttpsURLConnection conn = Tls.open(context, new URL(url), TIMEOUT_MS);
        try
        {
            conn.setRequestMethod(method);
            conn.setRequestProperty("User-Agent", "torirs-android");
            if( cookie != null )
                conn.setRequestProperty("Cookie", cookie);
            if( body != null )
            {
                conn.setDoOutput(true);
                conn.setRequestProperty("Content-Type", contentType);
                conn.setFixedLengthStreamingMode(body.length);
                try( OutputStream out = conn.getOutputStream() )
                {
                    out.write(body);
                }
            }
            Response r = new Response();
            r.status = conn.getResponseCode();
            r.location = conn.getHeaderField("Location");
            InputStream in = r.status >= 400 ? conn.getErrorStream() : conn.getInputStream();
            if( in != null )
            {
                try( InputStream s = in )
                {
                    r.body = readAll(s);
                }
            }
            return r;
        }
        finally
        {
            conn.disconnect();
        }
    }

    static Response postJson(Context context, String url, String cookie, String json)
            throws IOException, GeneralSecurityException
    {
        return request(context, "POST", url, cookie, "application/json",
                json.getBytes(StandardCharsets.UTF_8));
    }

    static Response postForm(Context context, String url, String cookie, String... keyValues)
            throws IOException, GeneralSecurityException
    {
        StringBuilder sb = new StringBuilder();
        for( int i = 0; i + 1 < keyValues.length; i += 2 )
        {
            if( sb.length() > 0 )
                sb.append('&');
            sb.append(URLEncoder.encode(keyValues[i], "UTF-8"))
                    .append('=')
                    .append(URLEncoder.encode(keyValues[i + 1], "UTF-8"));
        }
        return request(context, "POST", url, cookie, "application/x-www-form-urlencoded",
                sb.toString().getBytes(StandardCharsets.UTF_8));
    }

    /** Reports bytes so far of a download, and its size when the server said. */
    interface Progress
    {
        void onBytes(long done, long total);
    }

    /**
     * GET `url` into `out`, hashing it on the way: returns the body's SHA-256
     * as lowercase hex. Anything but a 200 is an IOException that says what
     * came back, because a 302 here means the sign-in ended, and writing the
     * sign-in page to disk as a library would be the worst way to find out.
     */
    static String download(
            Context context,
            String url,
            String cookie,
            File out,
            Progress progress) throws IOException, GeneralSecurityException
    {
        HttpsURLConnection conn = Tls.open(context, new URL(url), TIMEOUT_MS);
        try
        {
            conn.setRequestProperty("User-Agent", "torirs-android");
            if( cookie != null )
                conn.setRequestProperty("Cookie", cookie);
            int status = conn.getResponseCode();
            if( status != HttpURLConnection.HTTP_OK )
                throw new HttpStatusException(status, url);
            long total = conn.getContentLength();
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[64 * 1024];
            long done = 0;
            long lastReport = 0;
            try( InputStream in = conn.getInputStream();
                 OutputStream fo = new FileOutputStream(out) )
            {
                int n;
                while( (n = in.read(buf)) > 0 )
                {
                    fo.write(buf, 0, n);
                    sha.update(buf, 0, n);
                    done += n;
                    if( progress != null && done - lastReport >= 256 * 1024 )
                    {
                        progress.onBytes(done, total);
                        lastReport = done;
                    }
                }
            }
            if( progress != null )
                progress.onBytes(done, total);
            return hex(sha.digest());
        }
        finally
        {
            conn.disconnect();
        }
    }

    /** A request the server answered with something other than what was asked. */
    static final class HttpStatusException extends IOException
    {
        final int status;

        HttpStatusException(int status, String url)
        {
            super("HTTP " + status + " for " + url);
            this.status = status;
        }
    }

    static byte[] readAll(InputStream in) throws IOException
    {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] buf = new byte[16 * 1024];
        int n;
        while( (n = in.read(buf)) > 0 )
            bytes.write(buf, 0, n);
        return bytes.toByteArray();
    }

    static String hex(byte[] bytes)
    {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for( byte b : bytes )
            sb.append(String.format("%02x", b & 0xff));
        return sb.toString();
    }
}
