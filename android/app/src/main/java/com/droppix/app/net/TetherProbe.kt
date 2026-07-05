package com.droppix.app.net

/**
 * Codec for tether-discovery datagrams (mDNS-independent USB-link discovery).
 * Probe: ASCII "DPXQ" (4 bytes). Reply: "DPXR" u16 wakePort(BE) u8 idLen id u8 nameLen name.
 * Byte-identical to the host codec (see host/src/tether_discovery.*).
 *
 * id/name are clamped to 255 bytes on encode so the declared length byte always
 * equals the emitted payload length (mirrors the host's std::min<size_t>(_, 255)).
 */
object TetherProbe {
    const val PORT = 27010

    fun encodeProbe(): ByteArray = byteArrayOf(
        'D'.code.toByte(), 'P'.code.toByte(), 'X'.code.toByte(), 'Q'.code.toByte())

    fun isProbe(b: ByteArray, len: Int): Boolean =
        len == 4 && b[0]=='D'.code.toByte() && b[1]=='P'.code.toByte() &&
        b[2]=='X'.code.toByte() && b[3]=='Q'.code.toByte()

    data class Reply(val wakePort: Int, val id: String, val name: String)

    fun encodeReply(wakePort: Int, id: String, name: String): ByteArray {
        val idB = id.toByteArray(Charsets.UTF_8)
        val nameB = name.toByteArray(Charsets.UTF_8)
        val idN = minOf(idB.size, 255)
        val nameN = minOf(nameB.size, 255)
        val out = ArrayList<Byte>()
        out.add('D'.code.toByte()); out.add('P'.code.toByte())
        out.add('X'.code.toByte()); out.add('R'.code.toByte())
        out.add(((wakePort ushr 8) and 0xFF).toByte()); out.add((wakePort and 0xFF).toByte())
        out.add(idN.toByte()); out.addAll(idB.copyOf(idN).toList())
        out.add(nameN.toByte()); out.addAll(nameB.copyOf(nameN).toList())
        return out.toByteArray()
    }

    fun decodeReply(b: ByteArray, len: Int): Reply? {
        if (len < 7 || b[0]!='D'.code.toByte() || b[1]!='P'.code.toByte() ||
            b[2]!='X'.code.toByte() || b[3]!='R'.code.toByte()) return null
        val port = ((b[4].toInt() and 0xFF) shl 8) or (b[5].toInt() and 0xFF)
        var i = 6
        val idLen = b[i].toInt() and 0xFF; i++
        if (i + idLen > len) return null
        val id = String(b, i, idLen, Charsets.UTF_8); i += idLen
        if (i >= len) return null
        val nameLen = b[i].toInt() and 0xFF; i++
        if (i + nameLen > len) return null
        val name = String(b, i, nameLen, Charsets.UTF_8)
        return Reply(port, id, name)
    }
}
