package com.pcmic360.app

data class GateDecision(
    val open: Boolean,
    val holdUntilMs: Long
)

object GateLogic {
    fun evaluate(
        rms: Double,
        threshold: Int,
        nowMs: Long,
        priorHoldUntilMs: Long,
        hangoverMs: Long
    ): GateDecision {
        if (threshold <= 0) return GateDecision(open = true, holdUntilMs = priorHoldUntilMs)
        val holdUntil = if (rms >= threshold) nowMs + hangoverMs else priorHoldUntilMs
        return GateDecision(open = nowMs < holdUntil, holdUntilMs = holdUntil)
    }
}
