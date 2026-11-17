package com.torirs.client;

import android.content.Context;
import android.os.Build;

import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URL;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.Collections;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SNIHostName;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

/**
 * TLS to the proxy, for every connection the prod app makes.
 *
 * <h2>Why not the platform defaults alone</h2>
 *
 * mrobertevers.com's certificate is Let's Encrypt's, and its chain ends at
 * ISRG Root X1 (by way of the cross-signed Root YR). Android trusts X1 only
 * from 7.1.1 on; before that the chain used to end at DST Root CA X3, which
 * Let's Encrypt stopped serving in 2024. So on the Android 5.1 phone this
 * lane is built against, a stock HttpsURLConnection to the proxy fails with
 * "Trust anchor for certification path not found". The app carries X1
 * (res/raw/isrg_root_x1.pem, the root itself, valid to 2035) and trusts a
 * chain that ends at either it or anything the system trusts. Nothing is
 * trusted LESS than the platform would: the system's answer is asked first.
 *
 * <h2>Why raw sockets need two extra steps</h2>
 *
 * The relay (IapTunnel) speaks HTTP and WebSocket over an SSLSocket of its own,
 * which -- unlike HttpsURLConnection -- neither sends SNI nor checks the
 * hostname by itself. Without SNI the proxy answers with its bare-domain
 * certificate, not the wildcard; without the check, any certificate the trust
 * store accepts would do. {@link #connect} does both.
 */
final class Tls
{
    private static SSLSocketFactory factory;

    private Tls()
    {
    }

    static synchronized SSLSocketFactory factory(Context context)
            throws GeneralSecurityException, IOException
    {
        if( factory != null )
            return factory;

        final X509TrustManager system = x509(trustManagers(null));

        KeyStore bundled = KeyStore.getInstance(KeyStore.getDefaultType());
        bundled.load(null, null);
        CertificateFactory cf = CertificateFactory.getInstance("X.509");
        try( InputStream in = context.getResources().openRawResource(R.raw.isrg_root_x1) )
        {
            bundled.setCertificateEntry("isrg_root_x1", cf.generateCertificate(in));
        }
        final X509TrustManager own = x509(trustManagers(bundled));

        X509TrustManager either = new X509TrustManager()
        {
            @Override
            public void checkClientTrusted(X509Certificate[] chain, String authType)
                    throws CertificateException
            {
                system.checkClientTrusted(chain, authType);
            }

            @Override
            public void checkServerTrusted(X509Certificate[] chain, String authType)
                    throws CertificateException
            {
                try
                {
                    system.checkServerTrusted(chain, authType);
                }
                catch( CertificateException notBySystem )
                {
                    own.checkServerTrusted(chain, authType);
                }
            }

            @Override
            public X509Certificate[] getAcceptedIssuers()
            {
                X509Certificate[] a = system.getAcceptedIssuers();
                X509Certificate[] b = own.getAcceptedIssuers();
                X509Certificate[] all = new X509Certificate[a.length + b.length];
                System.arraycopy(a, 0, all, 0, a.length);
                System.arraycopy(b, 0, all, a.length, b.length);
                return all;
            }
        };

        SSLContext ctx = SSLContext.getInstance("TLS");
        ctx.init(null, new TrustManager[] { either }, null);
        factory = ctx.getSocketFactory();
        return factory;
    }

    /** An HTTPS connection with this trust, its own SNI and hostname check
     *  (HttpsURLConnection does both), and no redirect-following: every
     *  caller reads a 30x itself, because a 302 to the sign-in page is an
     *  answer ("the session ended"), not somewhere to go. */
    static HttpsURLConnection open(Context context, URL url, int timeoutMs)
            throws IOException, GeneralSecurityException
    {
        HttpsURLConnection conn = (HttpsURLConnection)url.openConnection();
        conn.setSSLSocketFactory(factory(context));
        conn.setInstanceFollowRedirects(false);
        conn.setConnectTimeout(timeoutMs);
        conn.setReadTimeout(timeoutMs);
        conn.setUseCaches(false);
        return conn;
    }

    /** A TLS socket to host:port, SNI sent and the certificate's name checked. */
    static SSLSocket connect(Context context, String host, int port, int timeoutMs)
            throws IOException, GeneralSecurityException
    {
        Socket plain = new Socket();
        plain.connect(new InetSocketAddress(host, port), timeoutMs);
        SSLSocket socket = (SSLSocket)factory(context).createSocket(plain, host, port, true);
        try
        {
            sendSni(socket, host);
            socket.setSoTimeout(timeoutMs);
            socket.startHandshake();
            socket.setSoTimeout(0);
            if( !HttpsURLConnection.getDefaultHostnameVerifier().verify(host, socket.getSession()) )
                throw new SSLPeerUnverifiedException("certificate is not for " + host);
            return socket;
        }
        catch( IOException | RuntimeException e )
        {
            socket.close();
            throw e;
        }
    }

    /* API 24 has a public way; before it, the platform's own SSLSocket has had
     * setHostname since API 17 (what HttpsURLConnection itself calls). */
    private static void sendSni(SSLSocket socket, String host)
    {
        if( Build.VERSION.SDK_INT >= 24 )
        {
            SSLParameters p = socket.getSSLParameters();
            p.setServerNames(Collections.singletonList(new SNIHostName(host)));
            socket.setSSLParameters(p);
            return;
        }
        try
        {
            Method m = socket.getClass().getMethod("setHostname", String.class);
            m.invoke(socket, host);
        }
        catch( ReflectiveOperationException e )
        {
            throw new IllegalStateException("this platform's SSLSocket cannot send SNI", e);
        }
    }

    private static TrustManager[] trustManagers(KeyStore keyStore) throws GeneralSecurityException
    {
        TrustManagerFactory tmf =
                TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        tmf.init(keyStore);
        return tmf.getTrustManagers();
    }

    private static X509TrustManager x509(TrustManager[] managers)
    {
        for( TrustManager m : managers )
            if( m instanceof X509TrustManager )
                return (X509TrustManager)m;
        throw new IllegalStateException("no X509TrustManager");
    }
}
