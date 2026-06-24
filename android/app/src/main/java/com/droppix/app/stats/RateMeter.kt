package com.droppix.app.stats

// Sliding-window event rate. Not thread-safe; called from the single net thread.
class RateMeter(private val windowMs: Long = 1000) {
    private val times = ArrayDeque<Long>()
    fun mark(nowMs: Long) { times.addLast(nowMs); trim(nowMs) }
    fun ratePerSec(nowMs: Long): Double {
        trim(nowMs)
        return times.size * 1000.0 / windowMs
    }
    private fun trim(nowMs: Long) {
        while (times.isNotEmpty() && nowMs - times.first() > windowMs) times.removeFirst()
    }
}
