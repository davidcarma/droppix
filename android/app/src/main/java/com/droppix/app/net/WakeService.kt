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
}
