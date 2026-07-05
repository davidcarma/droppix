package com.droppix.app.ui

import android.app.Activity
import android.content.Context
import android.hardware.usb.UsbAccessory
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.util.Log
import java.io.FileInputStream
import java.io.FileOutputStream
import kotlin.concurrent.thread

/** THROWAWAY M0 spike: open the USB accessory and echo every byte back so the host
 *  tool can measure AOA round-trip + throughput. Replaced by the real transport in M4. */
class AoaEchoActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val mgr = getSystemService(Context.USB_SERVICE) as UsbManager
        val acc: UsbAccessory? =
            intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY) ?: mgr.accessoryList?.firstOrNull()
        if (acc == null) { Log.w("aoa-echo", "no accessory"); finish(); return }
        val pfd = mgr.openAccessory(acc)
        if (pfd == null) { Log.w("aoa-echo", "openAccessory returned null"); finish(); return }
        Log.i("aoa-echo", "accessory opened; echoing")
        thread(name = "aoa-echo") {
            val fis = FileInputStream(pfd.fileDescriptor)
            val fos = FileOutputStream(pfd.fileDescriptor)
            val buf = ByteArray(16384)
            try {
                while (true) {
                    val n = fis.read(buf)
                    if (n < 0) break
                    fos.write(buf, 0, n); fos.flush()
                }
            } catch (e: Exception) {
                Log.w("aoa-echo", "echo ended: ${e.message}")
            } finally {
                try { pfd.close() } catch (_: Exception) {}
            }
        }
    }
}
