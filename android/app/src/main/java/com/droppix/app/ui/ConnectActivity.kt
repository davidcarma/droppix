package com.droppix.app.ui

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.droppix.app.R
import com.droppix.app.net.Discovery
import com.droppix.app.net.WakeService

class ConnectActivity : AppCompatActivity() {
    private lateinit var pcList: ListView
    private lateinit var manualAddr: EditText
    private lateinit var connectBtn: Button
    private lateinit var status: TextView
    private lateinit var reconnectBtn: Button

    private lateinit var discovery: Discovery
    private lateinit var wakeService: WakeService
    private lateinit var pcListAdapter: ArrayAdapter<String>
    private data class DiscoveredPc(val name: String, val host: String, val port: Int)
    private val discoveredPcs = mutableListOf<DiscoveredPc>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)

        pcList = findViewById(R.id.pc_list)
        manualAddr = findViewById(R.id.manual_addr)
        connectBtn = findViewById(R.id.connect_btn)
        status = findViewById(R.id.status)
        reconnectBtn = findViewById(R.id.reconnect_btn)

        discovery = Discovery(this)
        wakeService = WakeService(this)
        pcListAdapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, mutableListOf<String>())
        pcList.adapter = pcListAdapter
        pcList.setOnItemClickListener { _, _, position, _ ->
            val pc = discoveredPcs.getOrNull(position) ?: return@setOnItemClickListener
            status.text = "Connecting to ${pc.name} (${pc.host}:${pc.port})..."
            connectTo(pc.host, pc.port)
        }

        connectBtn.setOnClickListener { onConnectClicked() }
        updateReconnectRow()
        reconnectBtn.setOnClickListener { onReconnectClicked() }
    }

    override fun onResume() {
        super.onResume()
        discovery.start(
            onFound = { name, host, port -> onPcFound(name, host, port) },
            onLost = { name -> onPcLost(name) }
        )
        wakeService.start { host, port -> showWakeConfirm(host, port) }
    }

    override fun onPause() {
        super.onPause()
        discovery.stop()
        wakeService.stop()
        discoveredPcs.clear()
        refreshPcListAdapter()
    }

    private fun showWakeConfirm(host: String, port: Int) {
        AlertDialog.Builder(this)
            .setTitle("Connect to $host?")
            .setPositiveButton("Connect") { _, _ -> connectTo(host, port) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun onPcFound(name: String, host: String, port: Int) {
        val idx = discoveredPcs.indexOfFirst { it.name == name }
        val pc = DiscoveredPc(name, host, port)
        if (idx >= 0) {
            discoveredPcs[idx] = pc
        } else {
            discoveredPcs.add(pc)
        }
        refreshPcListAdapter()
    }

    private fun onPcLost(name: String) {
        discoveredPcs.removeAll { it.name == name }
        refreshPcListAdapter()
    }

    private fun refreshPcListAdapter() {
        pcListAdapter.clear()
        pcListAdapter.addAll(discoveredPcs.map { "${it.name} (${it.host}:${it.port})" })
        pcListAdapter.notifyDataSetChanged()
    }

    private fun onConnectClicked() {
        val raw = manualAddr.text.toString().trim()
        if (raw.isEmpty()) {
            status.text = "Enter an address, e.g. 192.168.1.50:27000"
            return
        }
        val (host, port) = parseHostPort(raw)
        if (host.isEmpty()) {
            status.text = "Invalid address: $raw"
            return
        }
        status.text = "Connecting to $host:$port..."
        connectTo(host, port)
    }

    private fun onReconnectClicked() {
        val prefs = getSharedPreferences("droppix", MODE_PRIVATE)
        val lastHost = prefs.getString("last_host", null) ?: return
        val lastPort = prefs.getInt("last_port", 27000)
        status.text = "Reconnecting to $lastHost:$lastPort..."
        connectTo(lastHost, lastPort)
    }

    private fun updateReconnectRow() {
        val prefs = getSharedPreferences("droppix", MODE_PRIVATE)
        val lastHost = prefs.getString("last_host", null)
        if (lastHost == null) {
            reconnectBtn.visibility = View.GONE
        } else {
            reconnectBtn.visibility = View.VISIBLE
            val lastPort = prefs.getInt("last_port", 27000)
            reconnectBtn.text = "Reconnect to $lastHost:$lastPort"
        }
    }

    private fun parseHostPort(raw: String): Pair<String, Int> {
        val idx = raw.lastIndexOf(':')
        if (idx <= 0) return Pair(raw, 27000)
        val host = raw.substring(0, idx)
        val portStr = raw.substring(idx + 1)
        val port = portStr.toIntOrNull() ?: return Pair("", 0)
        return Pair(host, port)
    }

    fun connectTo(host: String, port: Int) {
        getSharedPreferences("droppix", MODE_PRIVATE).edit()
            .putString("last_host", host).putInt("last_port", port).apply()
        startActivity(Intent(this, StreamActivity::class.java)
            .putExtra("host", host).putExtra("port", port))
    }
}
