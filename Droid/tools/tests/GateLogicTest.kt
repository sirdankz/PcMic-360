package com.pcmic360.app

fun main() {
    fun check(condition: Boolean, message: String) {
        if (!condition) error(message)
    }

    val closed = GateLogic.evaluate(rms = 500.0, threshold = 2000, nowMs = 1000, priorHoldUntilMs = 0, hangoverMs = 180)
    check(!closed.open, "quiet signal must close gate")

    val opened = GateLogic.evaluate(rms = 2500.0, threshold = 2000, nowMs = 1000, priorHoldUntilMs = 0, hangoverMs = 180)
    check(opened.open, "signal above threshold must open gate")
    check(opened.holdUntilMs == 1180L, "hangover deadline incorrect")

    val hang = GateLogic.evaluate(rms = 100.0, threshold = 2000, nowMs = 1100, priorHoldUntilMs = opened.holdUntilMs, hangoverMs = 180)
    check(hang.open, "gate must remain open during hangover")

    val expired = GateLogic.evaluate(rms = 100.0, threshold = 2000, nowMs = 1180, priorHoldUntilMs = opened.holdUntilMs, hangoverMs = 180)
    check(!expired.open, "gate must close when hangover expires")

    val disabled = GateLogic.evaluate(rms = 0.0, threshold = 0, nowMs = 2000, priorHoldUntilMs = 0, hangoverMs = 180)
    check(disabled.open, "threshold 0 must disable gate")

    println("GateLogic: ALL TESTS PASSED")
}
