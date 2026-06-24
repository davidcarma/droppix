package com.droppix.app.decode

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Build
import android.view.Surface

// Hardware H.264 decode straight onto a Surface. SPS/PPS arrive in-band, so we
// configure with only width/height and let the codec sync on the first IDR.
class VideoDecoder(surface: Surface, width: Int, height: Int) {
    private val codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
    private val info = MediaCodec.BufferInfo()

    init {
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
        if (Build.VERSION.SDK_INT >= 30) {
            fmt.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
        }
        codec.configure(fmt, surface, null, 0)
        codec.start()
    }

    fun submit(nal: ByteArray, ptsUs: Long) {
        val inIndex = codec.dequeueInputBuffer(10_000)
        if (inIndex >= 0) {
            val buf = codec.getInputBuffer(inIndex)!!
            buf.clear()
            buf.put(nal)
            codec.queueInputBuffer(inIndex, 0, nal.size, ptsUs, 0)
        }
        var outIndex = codec.dequeueOutputBuffer(info, 0)
        while (outIndex >= 0) {
            codec.releaseOutputBuffer(outIndex, true)  // render to the surface
            outIndex = codec.dequeueOutputBuffer(info, 0)
        }
    }

    fun release() {
        try { codec.stop() } catch (_: Exception) {}
        codec.release()
    }
}
