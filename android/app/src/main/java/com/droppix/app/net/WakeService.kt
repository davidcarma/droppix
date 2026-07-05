package com.droppix.app.net

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketException
import java.util.concurrent.atomic.AtomicBoolean

private const val TAG = "WakeService"
private const val SERVICE_TYPE = "_droppix-client._tcp"
private const val RECV_BUFFER_SIZE = 64

/**
 * Advertises this tablet as `_droppix-client._tcp` over mDNS and listens on the
 * advertised UDP port for a "wake" datagram (see [Wake]) from the PC. On a valid
 * wake, invokes the caller's callback on the main thread with the PC's address and
 * the stream port to dial back.
 */
class WakeService(private val ctx: Context) {

    private val nsdManager: NsdManager by lazy {
        ctx.getSystemService(Context.NSD_SERVICE) as NsdManager
    }
    private val mainHandler = Handler(Looper.getMainLooper())

    private var socket: DatagramSocket? = null
    private var receiveThread: Thread? = null
    private var discoverySocket: DatagramSocket? = null
    private var discoveryThread: Thread? = null
    private var registrationListener: NsdManager.RegistrationListener? = null
    private val running = AtomicBoolean(false)

    fun start(onWake: (host: String, port: Int) -> Unit) {
        if (running.get()) return
        running.set(true)

        val sock = try {
            DatagramSocket(0)
        } catch (e: Exception) {
            Log.w(TAG, "failed to bind wake socket: ${e.message}")
            running.set(false)
            return
        }
        socket = sock
        val localPort = sock.localPort

        val discSock = try { DatagramSocket(null).apply {
            reuseAddress = true; bind(java.net.InetSocketAddress(TetherProbe.PORT))
        } } catch (e: Exception) {
            Log.w(TAG, "tether-discovery bind failed: ${e.message}"); null
        }
        discoverySocket = discSock
        if (discSock != null) {
            val name = DeviceIdentity.displayName(ctx)
            val id = DeviceIdentity.stableId(ctx)
            val t = Thread({ discoveryLoop(discSock, localPort, id, name) }, "TetherDiscovery-recv")
            discoveryThread = t; t.start()
        }

        val info = NsdServiceInfo().apply {
            serviceName = DeviceIdentity.displayName(ctx)
            serviceType = SERVICE_TYPE
            port = localPort
            setAttribute("id", DeviceIdentity.stableId(ctx))
        }

        val regListener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "registered ${serviceInfo.serviceName} on port $localPort")
            }

            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "registration failed for ${serviceInfo.serviceName}: error $errorCode")
            }

            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "unregistered ${serviceInfo.serviceName}")
            }

            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "unregistration failed for ${serviceInfo.serviceName}: error $errorCode")
            }
        }
        registrationListener = regListener
        try {
            nsdManager.registerService(info, NsdManager.PROTOCOL_DNS_SD, regListener)
        } catch (e: Exception) {
            Log.w(TAG, "registerService threw: ${e.message}")
            registrationListener = null
        }

        val thread = Thread({ receiveLoop(sock, onWake) }, "WakeService-recv")
        receiveThread = thread
        thread.start()
    }

    fun stop() {
        if (!running.compareAndSet(true, false)) return

        val regListener = registrationListener
        registrationListener = null
        if (regListener != null) {
            try {
                nsdManager.unregisterService(regListener)
            } catch (e: Exception) {
                Log.w(TAG, "unregisterService threw: ${e.message}")
            }
        }

        // Closing the socket unblocks the blocked receive() call in the background thread.
        socket?.close()
        socket = null

        discoverySocket?.close(); discoverySocket = null
        discoveryThread?.let { if (it != Thread.currentThread()) try { it.join(1000) } catch (_: InterruptedException) {} }
        discoveryThread = null

        val thread = receiveThread
        receiveThread = null
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(1000)
            } catch (e: InterruptedException) {
                Log.w(TAG, "interrupted waiting for receive thread to stop")
            }
        }
    }

    private fun receiveLoop(sock: DatagramSocket, onWake: (host: String, port: Int) -> Unit) {
        val buffer = ByteArray(RECV_BUFFER_SIZE)
        val packet = DatagramPacket(buffer, buffer.size)
        while (running.get()) {
            try {
                sock.receive(packet)
            } catch (e: SocketException) {
                // Expected when stop() closes the socket to unblock receive(); exit cleanly.
                break
            } catch (e: Exception) {
                if (running.get()) {
                    Log.w(TAG, "receive failed: ${e.message}")
                }
                break
            }
            val port = Wake.decode(packet.data, packet.length)
            val host = packet.address?.hostAddress
            if (port != null && host != null) {
                mainHandler.post {
                    if (running.get()) {
                        onWake(host, port)
                    }
                }
            }
        }
    }

    private fun discoveryLoop(sock: DatagramSocket, wakePort: Int, id: String, name: String) {
        val buf = ByteArray(64)
        val pkt = DatagramPacket(buf, buf.size)
        while (running.get()) {
            try { sock.receive(pkt) } catch (e: SocketException) { break }
              catch (e: Exception) { if (running.get()) Log.w(TAG, "discovery recv: ${e.message}"); break }
            if (TetherProbe.isProbe(pkt.data, pkt.length)) {
                val reply = TetherProbe.encodeReply(wakePort, id, name)
                try { sock.send(DatagramPacket(reply, reply.size, pkt.address, pkt.port)) }
                catch (e: Exception) { Log.w(TAG, "discovery reply: ${e.message}") }
            }
        }
    }
}
