package com.torirs.client;

import android.content.Context;
import android.util.Base64;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;

import javax.net.ssl.SSLSocket;

/**
 * The prod client's way out: a relay on 127.0.0.1 that carries the native
 * client's plain connections to torirs.mrobertevers.com over TLS, signed in.
 *
 * <h2>Why a relay</h2>
 *
 * The C client has no TLS (platform_x_http.c, net_transport_tcp.c and the JS5
 * client all dial plain sockets), and the server is reachable only through the
 * proxy on :443, behind two sign-ins whose cookies the client knows nothing
 * of. Rather than give the C side a TLS stack and a cookie jar, the launcher
 * points every endpoint of the manifest at this relay, and the relay adds what
 * the C side lacks. Nothing in src/ changes for it.
 *
 * <h2>Two kinds of connection, told apart by their first byte</h2>
 *
 * The same rule torirsserver uses on its own port (torirs_server_ws.h): the
 * game's login opens with opcode 14 and JS5 with 15, an HTTP request with a
 * method name in capitals.
 *
 *   HTTP (io_server's /boot files and plugin files, the account server's
 *   /oauth, /game and /account routes): the request head is rewritten -- the
 *   real Host, and a Cookie header with the IaP session and the gate ticket in
 *   place of whatever the client sent -- and the rest is piped both ways. The
 *   client speaks HTTP/1.0 with Connection: close (platform_x_http.h), so one
 *   connection is one request and the head is the only thing to rewrite.
 *
 *   Anything else is the game or JS5 byte stream. torirsserver behind the gate
 *   refuses raw TCP (it cannot carry the cookie), so the stream rides a
 *   WebSocket instead: upgraded with both cookies, then each chunk the client
 *   writes goes up as one binary frame and every frame's payload comes back
 *   down as plain bytes. The server reads a WebSocket as a byte stream
 *   (torirs_server_ws.h), so where the chunks are cut does not matter.
 *
 * One thread pair per connection, which on a phone means a handful: the game,
 * JS5, and an HTTP request now and then.
 */
final class IapTunnel
{
    private static final String TAG = "torirs";
    private static final String WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    private static final int HEAD_MAX = 32 * 1024;
    private static final int FRAME_MAX = 32 * 1024 * 1024;

    private static IapTunnel running;

    final int port;
    private final Context app;
    private final ServerSocket server;
    private final SecureRandom random = new SecureRandom();

    private IapTunnel(Context context) throws IOException
    {
        app = context.getApplicationContext();
        /* Loopback only: nothing else on the network may borrow the sign-ins. */
        server = new ServerSocket(0, 16, InetAddress.getByName("127.0.0.1"));
        port = server.getLocalPort();
        Thread accept = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                acceptLoop();
            }
        }, "iap-tunnel");
        accept.setDaemon(true);
        accept.start();
        Log.i(TAG, "iap tunnel: 127.0.0.1:" + port + " -> " + BuildConfig.SERVER_HOST + ":443");
    }

    /** The relay for this process, started on first use. It lives as long as
     *  the process does: the client's sockets are dialled at its port. */
    static synchronized IapTunnel start(Context context) throws IOException
    {
        if( running == null )
            running = new IapTunnel(context);
        return running;
    }

    static synchronized IapTunnel current()
    {
        return running;
    }

    private void acceptLoop()
    {
        for( ;; )
        {
            final Socket local;
            try
            {
                local = server.accept();
            }
            catch( IOException e )
            {
                Log.e(TAG, "iap tunnel: accept failed, relay stopped", e);
                return;
            }
            Thread t = new Thread(new Runnable()
            {
                @Override
                public void run()
                {
                    serve(local);
                }
            }, "iap-tunnel-conn");
            t.setDaemon(true);
            t.start();
        }
    }

    private void serve(Socket local)
    {
        SSLSocket up = null;
        try
        {
            local.setTcpNoDelay(true);
            InputStream lin = new BufferedInputStream(local.getInputStream(), 16 * 1024);
            int first = lin.read();
            if( first < 0 )
                return;
            String cookies = Session.get(app).cookieHeader();
            up = Tls.connect(app, BuildConfig.SERVER_HOST, 443, Http.TIMEOUT_MS);
            up.setTcpNoDelay(true);
            up.setKeepAlive(true);
            if( first >= 'A' && first <= 'Z' )
                relayHttp(first, lin, local, up, cookies);
            else
                relayStream(first, lin, local, up, cookies);
        }
        catch( Session.SignInRequired e )
        {
            Log.w(TAG, "iap tunnel: " + e.getMessage());
        }
        catch( Exception e )
        {
            Log.w(TAG, "iap tunnel: connection ended: " + e);
        }
        finally
        {
            closeQuietly(local);
            closeQuietly(up);
        }
    }

    /* ---- HTTP ------------------------------------------------------------ */

    private void relayHttp(int first, InputStream lin, Socket local, final SSLSocket up,
            String cookies) throws IOException
    {
        byte[] head = readHead(first, lin);
        String[] lines = new String(head, StandardCharsets.ISO_8859_1).split("\r\n");
        StringBuilder out = new StringBuilder();
        out.append(lines[0]).append("\r\n");
        for( int i = 1; i < lines.length; i++ )
        {
            String line = lines[i];
            if( line.isEmpty() )
                continue;
            String name = line.split(":", 2)[0].trim();
            if( name.equalsIgnoreCase("Host") || name.equalsIgnoreCase("Cookie") )
                continue;
            out.append(line).append("\r\n");
        }
        out.append("Host: ").append(BuildConfig.SERVER_HOST).append("\r\n");
        out.append("Cookie: ").append(cookies).append("\r\n\r\n");

        OutputStream upOut = up.getOutputStream();
        upOut.write(out.toString().getBytes(StandardCharsets.ISO_8859_1));
        upOut.flush();

        /* The body, and anything after it, up; the answer down until the
         * server closes (Connection: close). */
        pump("iap-tunnel-http-up", lin, upOut);
        copy(up.getInputStream(), local.getOutputStream());
    }

    /** The request head, up to and including the blank line. */
    private static byte[] readHead(int first, InputStream in) throws IOException
    {
        ByteArrayOutputStream head = new ByteArrayOutputStream(512);
        head.write(first);
        int matched = 0;
        while( matched < 4 )
        {
            int b = in.read();
            if( b < 0 )
                throw new EOFException("request head cut off");
            head.write(b);
            matched = (b == (matched % 2 == 0 ? '\r' : '\n')) ? matched + 1 : (b == '\r' ? 1 : 0);
            if( head.size() > HEAD_MAX )
                throw new IOException("request head too long");
        }
        return head.toByteArray();
    }

    /* ---- the game and JS5, over a WebSocket ------------------------------ */

    private void relayStream(int first, InputStream lin, Socket local, SSLSocket up,
            String cookies) throws IOException
    {
        byte[] nonce = new byte[16];
        random.nextBytes(nonce);
        String key = Base64.encodeToString(nonce, Base64.NO_WRAP);
        String request = "GET /ws HTTP/1.1\r\n"
                + "Host: " + BuildConfig.SERVER_HOST + "\r\n"
                + "Upgrade: websocket\r\n"
                + "Connection: Upgrade\r\n"
                + "Sec-WebSocket-Key: " + key + "\r\n"
                + "Sec-WebSocket-Version: 13\r\n"
                + "User-Agent: torirs-android\r\n"
                + "Cookie: " + cookies + "\r\n\r\n";
        final OutputStream upOut = up.getOutputStream();
        upOut.write(request.getBytes(StandardCharsets.ISO_8859_1));
        upOut.flush();

        InputStream upIn = new BufferedInputStream(up.getInputStream(), 64 * 1024);
        int b0 = upIn.read();
        if( b0 < 0 )
            throw new EOFException("server closed before answering the upgrade");
        String answer = new String(readHead(b0, upIn), StandardCharsets.ISO_8859_1);
        String status = answer.split("\r\n", 2)[0];
        if( !status.matches("HTTP/1\\.[01] 101.*") )
            throw new IOException("upgrade refused: " + status);
        if( !answer.contains(accept(key)) )
            throw new IOException("upgrade answered with the wrong Sec-WebSocket-Accept");

        final OutputStream localOut = local.getOutputStream();
        final InputStream localIn = lin;
        final int firstByte = first;
        final Object writeLock = new Object();

        Thread upward = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                byte[] buf = new byte[16 * 1024];
                try
                {
                    buf[0] = (byte)firstByte;
                    int n = 1 + readAvailable(localIn, buf, 1);
                    while( n > 0 )
                    {
                        synchronized( writeLock )
                        {
                            writeFrame(upOut, 0x2, buf, n);
                        }
                        n = localIn.read(buf);
                    }
                    synchronized( writeLock )
                    {
                        writeFrame(upOut, 0x8, buf, 0);
                    }
                }
                catch( IOException ignored )
                {
                    /* Either side went away; the downward loop notices too. */
                }
                finally
                {
                    closeQuietly(up);
                }
            }
        }, "iap-tunnel-ws-up");
        upward.setDaemon(true);
        upward.start();

        /* Downward, on this thread: frames to plain bytes. */
        byte[] small = new byte[8];
        for( ;; )
        {
            int h0 = upIn.read();
            int h1 = upIn.read();
            if( h0 < 0 || h1 < 0 )
                return;
            int opcode = h0 & 0x0f;
            long len = h1 & 0x7f;
            if( len == 126 )
            {
                readFully(upIn, small, 2);
                len = ((small[0] & 0xff) << 8) | (small[1] & 0xff);
            }
            else if( len == 127 )
            {
                readFully(upIn, small, 8);
                len = 0;
                for( int i = 0; i < 8; i++ )
                    len = (len << 8) | (small[i] & 0xff);
            }
            byte[] mask = null;
            if( (h1 & 0x80) != 0 )
            {
                mask = new byte[4];
                readFully(upIn, mask, 4);
            }
            if( len > FRAME_MAX )
                throw new IOException("frame of " + len + " bytes");
            byte[] payload = new byte[(int)len];
            readFully(upIn, payload, payload.length);
            if( mask != null )
                for( int i = 0; i < payload.length; i++ )
                    payload[i] ^= mask[i & 3];

            if( opcode == 0x0 || opcode == 0x1 || opcode == 0x2 )
            {
                localOut.write(payload);
                localOut.flush();
            }
            else if( opcode == 0x8 )
            {
                return;
            }
            else if( opcode == 0x9 )
            {
                synchronized( writeLock )
                {
                    writeFrame(upOut, 0xA, payload, payload.length);
                }
            }
            /* 0xA (pong) and anything unknown: nothing to do. */
        }
    }

    /** One masked, final client frame (RFC 6455 5.2: a client always masks). */
    private void writeFrame(OutputStream out, int opcode, byte[] data, int len) throws IOException
    {
        byte[] header = new byte[14];
        int h = 0;
        header[h++] = (byte)(0x80 | opcode);
        if( len < 126 )
        {
            header[h++] = (byte)(0x80 | len);
        }
        else if( len < 65536 )
        {
            header[h++] = (byte)(0x80 | 126);
            header[h++] = (byte)(len >>> 8);
            header[h++] = (byte)len;
        }
        else
        {
            header[h++] = (byte)(0x80 | 127);
            for( int i = 7; i >= 0; i-- )
                header[h++] = (byte)(i >= 4 ? 0 : (len >>> (8 * i)));
        }
        byte[] mask = new byte[4];
        random.nextBytes(mask);
        System.arraycopy(mask, 0, header, h, 4);
        h += 4;
        byte[] masked = new byte[len];
        for( int i = 0; i < len; i++ )
            masked[i] = (byte)(data[i] ^ mask[i & 3]);
        out.write(header, 0, h);
        out.write(masked, 0, len);
        out.flush();
    }

    private static String accept(String key)
    {
        try
        {
            MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
            byte[] d = sha1.digest((key + WS_GUID).getBytes(StandardCharsets.ISO_8859_1));
            return Base64.encodeToString(d, Base64.NO_WRAP);
        }
        catch( java.security.NoSuchAlgorithmException e )
        {
            throw new IllegalStateException(e);
        }
    }

    /* ---- plumbing -------------------------------------------------------- */

    /** Whatever is already buffered after the first byte, without blocking. */
    private static int readAvailable(InputStream in, byte[] buf, int off) throws IOException
    {
        int avail = Math.min(in.available(), buf.length - off);
        return avail > 0 ? Math.max(0, in.read(buf, off, avail)) : 0;
    }

    private static void readFully(InputStream in, byte[] buf, int len) throws IOException
    {
        int done = 0;
        while( done < len )
        {
            int n = in.read(buf, done, len - done);
            if( n < 0 )
                throw new EOFException();
            done += n;
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException
    {
        byte[] buf = new byte[16 * 1024];
        int n;
        while( (n = in.read(buf)) > 0 )
        {
            out.write(buf, 0, n);
            out.flush();
        }
    }

    /** Copy in -> out on a thread of its own. The caller's direction ends the
     *  connection, which ends this one with it. */
    private static void pump(String name, final InputStream in, final OutputStream out)
    {
        Thread t = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                try
                {
                    copy(in, out);
                }
                catch( IOException ignored )
                {
                    /* The other direction ends the connection. */
                }
            }
        }, name);
        t.setDaemon(true);
        t.start();
    }

    private static void closeQuietly(Socket s)
    {
        if( s == null )
            return;
        try
        {
            s.close();
        }
        catch( IOException ignored )
        {
        }
    }
}
