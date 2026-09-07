package com.pcmic360.app

object PcMicProtocol {
    const val PORT = 36000
    const val RATE = 16000
    const val SAMPLES = 320
    const val PCM_BYTES = 640
    val MAGIC = byteArrayOf(0xA5.toByte(), 0x70)

    const val CMD_INSTALL = "O"
    const val CMD_LIVE = "S"
    const val CMD_STOP = "T"
    const val CMD_STATUS = "P"
    const val CMD_RESTORE = "Q"

    fun audioPacket(pcmBigEndian: ByteArray): ByteArray {
        require(pcmBigEndian.size == PCM_BYTES) { "PCM frame must be $PCM_BYTES bytes" }
        return ByteArray(MAGIC.size + PCM_BYTES).also { out ->
            MAGIC.copyInto(out, 0)
            pcmBigEndian.copyInto(out, MAGIC.size)
        }
    }

    fun parseFields(line: String): Map<String, String> {
        val out = LinkedHashMap<String, String>()
        line.split(' ').drop(1).forEach { token ->
            val i = token.indexOf('=')
            if (i > 0) {
                out[token.substring(0, i).trim()] = token.substring(i + 1).trim().trim(',')
            }
        }
        return out
    }
}
