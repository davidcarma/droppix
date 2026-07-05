package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class TetherProbeTest {
    @Test fun probeIsExactMagic() {
        val p = TetherProbe.encodeProbe()
        assertArrayEquals(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte(),'Q'.code.toByte()), p)
        assertTrue(TetherProbe.isProbe(p, p.size))
        assertFalse(TetherProbe.isProbe(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte()), 3))
    }

    @Test fun replyMatchesSharedVector() {
        val b = TetherProbe.encodeReply(40000, "abc", "Nexus 10")
        val want = intArrayOf(0x44,0x50,0x58,0x52,0x9C,0x40,0x03,0x61,0x62,0x63,
                              0x08,0x4E,0x65,0x78,0x75,0x73,0x20,0x31,0x30)
            .map { it.toByte() }.toByteArray()
        assertArrayEquals(want, b)
    }

    @Test fun replyRoundTrips() {
        val b = TetherProbe.encodeReply(51234, "dev-xyz", "Pixel")
        val r = TetherProbe.decodeReply(b, b.size)!!
        assertEquals(51234, r.wakePort); assertEquals("dev-xyz", r.id); assertEquals("Pixel", r.name)
    }

    @Test fun decodeRejectsBadMagicAndTruncation() {
        assertNull(TetherProbe.decodeReply(byteArrayOf('X'.code.toByte(),'X'.code.toByte(),'X'.code.toByte(),'X'.code.toByte()), 4))
        assertNull(TetherProbe.decodeReply(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte(),'R'.code.toByte(),0x9C.toByte()), 5))
    }

    @Test fun encodeClampsOversizedFieldsToDeclaredLength() {
        val b = TetherProbe.encodeReply(1, "x".repeat(300), "n")
        assertEquals(255.toByte(), b[6])
        val r = TetherProbe.decodeReply(b, b.size)!!
        assertEquals(1, r.wakePort)
        assertEquals(255, r.id.length)
        assertEquals("n", r.name)
    }
}
