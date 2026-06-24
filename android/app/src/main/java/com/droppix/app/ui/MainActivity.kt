package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import com.droppix.app.R
import com.droppix.app.decode.VideoDecoder
import com.droppix.app.net.StreamListener
import com.droppix.app.net.TransportClient
import com.droppix.app.protocol.Protocol
import kotlin.concurrent.thread

class MainActivity : Activity() {
    private companion object { const val TAG = "droppix"; const val HOST = "127.0.0.1"; const val PORT = 27000 }

    @Volatile private var running = false
    private var netThread: Thread? = null
    @Volatile private var decoder: VideoDecoder? = null
    private lateinit var surfaceView: DisplaySurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_main)
        surfaceView = findViewById(R.id.surface)
    }

    override fun onResume() {
        super.onResume()
        running = true
        surfaceView.awaitSurface { surface -> startStreaming(surface) }
    }

    private fun startStreaming(surface: Surface) {
        netThread = thread(name = "droppix-net") {
            val client = TransportClient()
            val listener = object : StreamListener {
                override fun onConfig(config: Protocol.Config) {
                    Log.i(TAG, "CONFIG ${config.width}x${config.height}@${config.fps}")
                    runOnUiThread {
                        surfaceView.holder.setFixedSize(config.width, config.height)
                    }
                    decoder?.release()
                    decoder = VideoDecoder(surface, config.width, config.height)
                }
                override fun onVideo(video: Protocol.Video) {
                    decoder?.submit(video.nal, video.ptsUs)
                }
            }
            try {
                client.run(HOST, PORT, 1920, 1080, resources.displayMetrics.densityDpi,
                    listener) { running }
            } catch (e: Exception) {
                Log.w(TAG, "stream ended: ${e.message}")
            }
        }
    }

    override fun onPause() {
        super.onPause()
        running = false
        netThread?.join(1500)
        if (netThread?.isAlive == true) Log.w(TAG, "net thread did not exit within 1.5s")
        netThread = null
        decoder?.release()
        decoder = null
    }
}
