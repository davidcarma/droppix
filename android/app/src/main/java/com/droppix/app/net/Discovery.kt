package com.droppix.app.net

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.ArrayDeque

private const val TAG = "Discovery"
private const val SERVICE_TYPE = "_droppix._tcp"

/**
 * Wraps [NsdManager] to discover droppix hosts advertising `_droppix._tcp` over mDNS.
 *
 * NSD's classic discoverServices/resolveService APIs (used here, not the API 34
 * registerServiceInfoCallback) only allow one resolveService call in flight at a time;
 * calling it again before the previous resolve's listener fires throws. Resolves are
 * therefore serialized through a simple FIFO queue: only one resolve is outstanding,
 * and the next queued resolve is started from the previous resolve's onResolveSucceeded
 * or onResolveFailed callback.
 */
class Discovery(private val ctx: Context) {

    private val nsdManager: NsdManager by lazy {
        ctx.getSystemService(Context.NSD_SERVICE) as NsdManager
    }
    private val mainHandler = Handler(Looper.getMainLooper())

    private var discoveryListener: NsdManager.DiscoveryListener? = null
    private var started = false

    private val resolveQueue = ArrayDeque<NsdServiceInfo>()
    private var resolveInFlight = false

    private var onFound: ((name: String, host: String, port: Int) -> Unit)? = null
    private var onLost: ((name: String) -> Unit)? = null

    fun start(
        onFound: (name: String, host: String, port: Int) -> Unit,
        onLost: (name: String) -> Unit
    ) {
        if (started) return
        this.onFound = onFound
        this.onLost = onLost

        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {
                Log.i(TAG, "discovery started for $serviceType")
            }

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "service found: ${serviceInfo.serviceName}")
                enqueueResolve(serviceInfo)
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "service lost: ${serviceInfo.serviceName}")
                val name = serviceInfo.serviceName
                mainHandler.post { this@Discovery.onLost?.invoke(name) }
            }

            override fun onDiscoveryStopped(serviceType: String) {
                Log.i(TAG, "discovery stopped for $serviceType")
            }

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.w(TAG, "start discovery failed for $serviceType: error $errorCode")
                started = false
                discoveryListener = null
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.w(TAG, "stop discovery failed for $serviceType: error $errorCode")
            }
        }

        discoveryListener = listener
        started = true
        try {
            nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
        } catch (e: Exception) {
            Log.w(TAG, "discoverServices threw: ${e.message}")
            started = false
            discoveryListener = null
        }
    }

    fun stop() {
        val listener = discoveryListener
        discoveryListener = null
        resolveQueue.clear()
        resolveInFlight = false
        onFound = null
        onLost = null
        if (!started || listener == null) {
            started = false
            return
        }
        started = false
        try {
            nsdManager.stopServiceDiscovery(listener)
        } catch (e: Exception) {
            Log.w(TAG, "stopServiceDiscovery threw: ${e.message}")
        }
    }

    private fun enqueueResolve(serviceInfo: NsdServiceInfo) {
        resolveQueue.add(serviceInfo)
        maybeStartNextResolve()
    }

    private fun maybeStartNextResolve() {
        if (resolveInFlight) return
        val next = resolveQueue.poll() ?: return
        resolveInFlight = true

        val listener = object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "resolve failed for ${serviceInfo.serviceName}: error $errorCode")
                resolveInFlight = false
                maybeStartNextResolve()
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val host = serviceInfo.host?.hostAddress
                val name = serviceInfo.serviceName
                val port = serviceInfo.port
                resolveInFlight = false
                if (host != null) {
                    mainHandler.post { this@Discovery.onFound?.invoke(name, host, port) }
                } else {
                    Log.w(TAG, "resolved $name with null host address")
                }
                maybeStartNextResolve()
            }
        }

        try {
            nsdManager.resolveService(next, listener)
        } catch (e: Exception) {
            Log.w(TAG, "resolveService threw: ${e.message}")
            resolveInFlight = false
            maybeStartNextResolve()
        }
    }
}
