package com.droppix.app.stats

import org.junit.Assert.assertEquals
import org.junit.Test

class RateMeterTest {
    @Test fun countsEventsWithinWindow() {
        val m = RateMeter(1000)
        var t = 10_000L
        repeat(30) { m.mark(t); t += 10 }   // 30 events over 300ms, all within 1s window
        assertEquals(30.0, m.ratePerSec(t), 0.001)
    }

    @Test fun dropsEventsOlderThanWindow() {
        val m = RateMeter(1000)
        m.mark(0); m.mark(100)
        // far future: both events are older than the window
        assertEquals(0.0, m.ratePerSec(5000), 0.001)
    }
}
