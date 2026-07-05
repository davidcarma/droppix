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
        thread(name = "aoa-echo") {
            // The host is still claiming the interface when we first open, which can EIO the
            // device pipe. Retry open+echo a few times so we ride past that setup race.
            val buf = ByteArray(16384)
            var attempt = 0
            while (attempt < 20) {
                attempt++
                val pfd = mgr.openAccessory(acc)
                if (pfd == null) { Thread.sleep(200); continue }
                Log.i("aoa-echo", "accessory opened (attempt $attempt); echoing")
                var moved = false
                try {
                    val fis = FileInputStream(pfd.fileDescriptor)
                    val fos = FileOutputStream(pfd.fileDescriptor)
                    while (true) {
                        val n = fis.read(buf)
                        if (n < 0) break
                        fos.write(buf, 0, n); fos.flush()
                        moved = true
                    }
                    break  // clean EOF: host finished
                } catch (e: Exception) {
                    Log.w("aoa-echo", "echo attempt $attempt ended: ${e.message}")
                    try { pfd.close() } catch (_: Exception) {}
                    if (moved) break     // real data flowed then ended — done
                    Thread.sleep(200)    // errored before any data — retry
                }
            }
        }
    }
}
