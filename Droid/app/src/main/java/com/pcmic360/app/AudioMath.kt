package com.pcmic360.app

import kotlin.math.floor
import kotlin.math.roundToInt
import kotlin.math.sqrt

object AudioMath {
    fun rms(samples: ShortArray): Double {
        if (samples.isEmpty()) return 0.0
        var sum = 0.0
        for (sample in samples) {
            val v = sample.toDouble()
            sum += v * v
        }
        return sqrt(sum / samples.size.toDouble())
    }

    fun resampleLinear(input: ShortArray, outputCount: Int): ShortArray {
        require(outputCount > 0)
        if (input.isEmpty()) return ShortArray(outputCount)
        if (input.size == outputCount) return input.copyOf()
        if (input.size == 1) return ShortArray(outputCount) { input[0] }
        if (outputCount == 1) return shortArrayOf(input[0])

        val output = ShortArray(outputCount)
        val scale = (input.size - 1).toDouble() / (outputCount - 1).toDouble()
        for (i in 0 until outputCount) {
            val position = i * scale
            val left = floor(position).toInt().coerceIn(0, input.lastIndex)
            val right = (left + 1).coerceAtMost(input.lastIndex)
            val frac = position - left.toDouble()
            val value = input[left].toDouble() * (1.0 - frac) + input[right].toDouble() * frac
            output[i] = value.roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
        }
        return output
    }

    fun scaleInPlace(samples: ShortArray, scalar: Double) {
        if (scalar == 1.0) return
        for (i in samples.indices) {
            val value = (samples[i].toDouble() * scalar).roundToInt()
                .coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
            samples[i] = value.toShort()
        }
    }

    fun toPcm16BigEndian(samples: ShortArray): ByteArray {
        val out = ByteArray(samples.size * 2)
        var p = 0
        for (sample in samples) {
            val value = sample.toInt() and 0xFFFF
            out[p++] = ((value ushr 8) and 0xFF).toByte()
            out[p++] = (value and 0xFF).toByte()
        }
        return out
    }
}
