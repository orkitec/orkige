/**************************************************************
	created:	2026/07/30 at 16:00
	filename: 	OrkigeHttp.java
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

package com.orkitec.orkige;

import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.HttpURLConnection;
import java.net.NoRouteToHostException;
import java.net.ProtocolException;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.concurrent.ConcurrentHashMap;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * The Java half of the engine's Android HTTP transport: one blocking exchange
 * driven from a native worker thread, streaming its response back through
 * native callbacks.
 *
 * <p>The platform's own HTTP stack is used - not a bundled library - so a
 * request is verified against the trust anchors the DEVICE maintains, which
 * includes the user-installed and enterprise certificates and the app's
 * Network Security Config, and so it inherits the platform's proxy settings.
 * Nothing here decides anything: the security policy (which schemes exist,
 * whether a redirect may be followed, the size cap and the timeouts) lives in
 * the engine's pure policy code and reaches this class only as arguments and
 * as the native callbacks' stop verdicts.</p>
 *
 * <p>ONE EXCHANGE PER CALL. Redirects are deliberately NOT followed here: a
 * 3xx is reported like any other answer and the engine decides, so the rule
 * that a secure request can never be redirected onto a plain one is applied in
 * exactly one place for every platform.</p>
 *
 * <p>THE THREADING CONTRACT: {@link #perform} blocks its calling thread, which
 * is a native worker - never the game thread. {@link #cancel} may be called
 * from any other thread and unblocks a transfer that is waiting on the
 * network.</p>
 */
public final class OrkigeHttp
{
	//--- the result codes perform() answers with (mirrored in the native side)
	/** the exchange ran to its end (an HTTP status is an answer, not a failure) */
	public static final int RESULT_OK = 0;
	/** the host could not be resolved or connected to */
	public static final int RESULT_CONNECT = 1;
	/** the TLS handshake or certificate verification failed */
	public static final int RESULT_TLS = 2;
	/** the connect or read deadline passed */
	public static final int RESULT_TIMEOUT = 3;
	/** cancel() reached this exchange */
	public static final int RESULT_CANCELLED = 4;
	/** anything else the platform stack refused to do */
	public static final int RESULT_TRANSPORT = 5;
	/** a native callback asked to stop (the cap, a write failure, a cancel) */
	public static final int RESULT_STOPPED = 6;

	//! how many body bytes one native hand-over carries
	private static final int CHUNK_BYTES = 32 * 1024;

	//! the live exchanges, so cancel() can reach the connection blocking on IO
	private static final ConcurrentHashMap<Long, HttpURLConnection> LIVE =
		new ConcurrentHashMap<Long, HttpURLConnection>();
	//! the exchanges cancel() has already been asked for
	private static final ConcurrentHashMap<Long, Boolean> CANCELLED =
		new ConcurrentHashMap<Long, Boolean>();

	private OrkigeHttp() {}

	//--- the native callbacks (registered from the transport's C++ side) -----

	/**
	 * The response head arrived. @param headers name,value,name,value,...
	 * @return 0 to go on reading the body, non-zero to stop (the engine refuses
	 * an announced size over the cap before a single body byte is read).
	 */
	private static native int nativeHead(long token, int status,
		String[] headers, long contentLength, String finalUrl);

	/**
	 * A body chunk arrived; only the first @p length bytes of @p chunk are it.
	 * @return 0 to go on, non-zero to stop (cap exceeded, write failed,
	 * cancelled) - the engine already knows which and says so in the answer.
	 */
	private static native int nativeBody(long token, byte[] chunk, int length);

	/** the platform's own words for a failure, kept for the answer's reason */
	private static native void nativeReason(long token, String reason);

	//--- the transport surface ----------------------------------------------

	/**
	 * Run ONE HTTP exchange to its end, blocking the calling thread.
	 *
	 * @param token the engine's handle for this exchange, echoed to every
	 *              callback
	 * @param url the absolute URL (already parsed and accepted by the policy)
	 * @param method the method token, sent verbatim
	 * @param headerNames,headerValues the request headers, in submission order
	 * @param body the entity body, or null for a request that carries none
	 * @param connectTimeoutMs,readTimeoutMs the deadlines, both derived from
	 *        the caller's whole-request timeout
	 * @return one of the RESULT_* codes
	 */
	public static int perform(long token, String url, String method,
		String[] headerNames, String[] headerValues, byte[] body,
		int connectTimeoutMs, int readTimeoutMs)
	{
		HttpURLConnection connection = null;
		try
		{
			connection = (HttpURLConnection) new URL(url).openConnection();
			if (connection instanceof HttpsURLConnection)
			{
				// the TLS floor: the platform default still offers older
				// protocol versions on some releases, and a floor that depends
				// on the OS version is not a floor
				((HttpsURLConnection) connection).setSSLSocketFactory(
					TlsFloorFactory.INSTANCE);
			}
			// the engine follows redirects itself so the no-downgrade rule is
			// applied in ONE place for every platform
			connection.setInstanceFollowRedirects(false);
			// no ambient identity and no stale answers: never a shared response
			// cache, and a request carries exactly what the caller put in it
			connection.setUseCaches(false);
			connection.setDefaultUseCaches(false);
			connection.setConnectTimeout(connectTimeoutMs);
			connection.setReadTimeout(readTimeoutMs);
			setMethod(connection, method);
			if (headerNames != null)
			{
				for (int at = 0; at < headerNames.length; ++at)
				{
					connection.setRequestProperty(headerNames[at],
						headerValues[at]);
				}
			}
			LIVE.put(Long.valueOf(token), connection);
			if (CANCELLED.containsKey(Long.valueOf(token)))
			{
				// cancel() arrived between submit and here
				return RESULT_CANCELLED;
			}
			if (body != null)
			{
				connection.setDoOutput(true);
				connection.setFixedLengthStreamingMode(body.length);
				OutputStream out = connection.getOutputStream();
				try
				{
					out.write(body);
					out.flush();
				}
				finally
				{
					out.close();
				}
			}
			final int status = connection.getResponseCode();
			if (nativeHead(token, status, collectHeaders(connection),
				connection.getContentLengthLong(),
				connection.getURL().toString()) != 0)
			{
				return RESULT_STOPPED;
			}
			// a 4xx/5xx body arrives on the error stream; it is still the
			// server's answer and the caller asked for it
			InputStream in = (status >= 400) ? connection.getErrorStream()
				: connection.getInputStream();
			if (in != null)
			{
				try
				{
					final byte[] chunk = new byte[CHUNK_BYTES];
					int read;
					while ((read = in.read(chunk)) > 0)
					{
						if (nativeBody(token, chunk, read) != 0)
						{
							return RESULT_STOPPED;
						}
					}
				}
				finally
				{
					in.close();
				}
			}
			return RESULT_OK;
		}
		catch (SocketTimeoutException failure)
		{
			nativeReason(token, describe(failure));
			return RESULT_TIMEOUT;
		}
		catch (SSLException failure)
		{
			nativeReason(token, describe(failure));
			return RESULT_TLS;
		}
		catch (UnknownHostException failure)
		{
			nativeReason(token, describe(failure));
			return RESULT_CONNECT;
		}
		catch (ConnectException failure)
		{
			nativeReason(token, describe(failure));
			return RESULT_CONNECT;
		}
		catch (NoRouteToHostException failure)
		{
			nativeReason(token, describe(failure));
			return RESULT_CONNECT;
		}
		catch (InterruptedIOException failure)
		{
			// disconnect() from cancel() surfaces here on some releases
			if (CANCELLED.containsKey(Long.valueOf(token)))
			{
				return RESULT_CANCELLED;
			}
			nativeReason(token, describe(failure));
			return RESULT_TIMEOUT;
		}
		catch (Throwable failure)
		{
			if (CANCELLED.containsKey(Long.valueOf(token)))
			{
				// a disconnect() mid-flight throws whatever the stream was in
				// the middle of; the caller asked for exactly this
				return RESULT_CANCELLED;
			}
			nativeReason(token, describe(failure));
			return RESULT_TRANSPORT;
		}
		finally
		{
			LIVE.remove(Long.valueOf(token));
			CANCELLED.remove(Long.valueOf(token));
			if (connection != null)
			{
				connection.disconnect();
			}
		}
	}

	/**
	 * Abort the exchange @p token, from any thread. A transfer blocked on the
	 * network is unblocked at once; one that has not opened its connection yet
	 * sees the flag and never does.
	 */
	public static void cancel(long token)
	{
		CANCELLED.put(Long.valueOf(token), Boolean.TRUE);
		final HttpURLConnection connection = LIVE.get(Long.valueOf(token));
		if (connection != null)
		{
			try
			{
				connection.disconnect();
			}
			catch (Throwable ignored)
			{
				// a connection already tearing itself down is exactly the
				// outcome asked for
			}
		}
	}

	//--- helpers ------------------------------------------------------------

	/** the response headers flattened to name,value,name,value,... */
	private static String[] collectHeaders(HttpURLConnection connection)
	{
		final ArrayList<String> flat = new ArrayList<String>();
		for (int at = 0; ; ++at)
		{
			final String name = connection.getHeaderFieldKey(at);
			final String value = connection.getHeaderField(at);
			if (name == null && value == null)
			{
				break;	// past the end of the header list
			}
			if (name != null)
			{
				flat.add(name);
				flat.add(value != null ? value : "");
			}
			// index 0 with a null key is the status line - not a header
		}
		return flat.toArray(new String[flat.size()]);
	}

	/**
	 * Set the request method, including the ones the platform's own allow-list
	 * predates (PATCH). The engine never rewrites a caller's method, so a
	 * method the connection refuses is set on it directly rather than silently
	 * turning into something else.
	 */
	private static void setMethod(HttpURLConnection connection, String method)
		throws ProtocolException
	{
		try
		{
			connection.setRequestMethod(method);
		}
		catch (ProtocolException refused)
		{
			try
			{
				final java.lang.reflect.Field field =
					HttpURLConnection.class.getDeclaredField("method");
				field.setAccessible(true);
				field.set(connection, method);
			}
			catch (Throwable unreachable)
			{
				throw refused;
			}
		}
	}

	/** a one-line description of a failure, without a stack trace */
	private static String describe(Throwable failure)
	{
		final String message = failure.getMessage();
		final String kind = failure.getClass().getSimpleName();
		return (message == null || message.isEmpty()) ? kind
			: (kind + ": " + message);
	}

	/**
	 * An SSL socket factory that pins the protocol floor at TLS 1.2 on top of
	 * the platform's own factory - so the trust anchors, the Network Security
	 * Config and the pinning the device applies all still hold, and only the
	 * accepted protocol versions are narrowed.
	 */
	private static final class TlsFloorFactory extends SSLSocketFactory
	{
		static final TlsFloorFactory INSTANCE = new TlsFloorFactory();

		private final SSLSocketFactory mBase =
			(SSLSocketFactory) SSLSocketFactory.getDefault();

		@Override
		public String[] getDefaultCipherSuites()
		{
			return this.mBase.getDefaultCipherSuites();
		}

		@Override
		public String[] getSupportedCipherSuites()
		{
			return this.mBase.getSupportedCipherSuites();
		}

		@Override
		public Socket createSocket(Socket socket, String host, int port,
			boolean autoClose) throws java.io.IOException
		{
			return floor(this.mBase.createSocket(socket, host, port, autoClose));
		}

		@Override
		public Socket createSocket(String host, int port)
			throws java.io.IOException
		{
			return floor(this.mBase.createSocket(host, port));
		}

		@Override
		public Socket createSocket(String host, int port,
			java.net.InetAddress localHost, int localPort)
			throws java.io.IOException
		{
			return floor(this.mBase.createSocket(host, port, localHost,
				localPort));
		}

		@Override
		public Socket createSocket(java.net.InetAddress host, int port)
			throws java.io.IOException
		{
			return floor(this.mBase.createSocket(host, port));
		}

		@Override
		public Socket createSocket(java.net.InetAddress address, int port,
			java.net.InetAddress localAddress, int localPort)
			throws java.io.IOException
		{
			return floor(this.mBase.createSocket(address, port, localAddress,
				localPort));
		}

		/** narrow a fresh socket to the protocol versions still worth having */
		private static Socket floor(Socket socket)
		{
			if (!(socket instanceof SSLSocket))
			{
				return socket;
			}
			final SSLSocket secure = (SSLSocket) socket;
			final ArrayList<String> keep = new ArrayList<String>();
			for (String protocol : secure.getSupportedProtocols())
			{
				if ("TLSv1.2".equals(protocol) || "TLSv1.3".equals(protocol))
				{
					keep.add(protocol);
				}
			}
			if (!keep.isEmpty())
			{
				secure.setEnabledProtocols(keep.toArray(new String[keep.size()]));
			}
			return secure;
		}
	}
}
