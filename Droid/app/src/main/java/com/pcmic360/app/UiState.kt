package com.pcmic360.app

data class MicDevice(
    val id: Int,
    val name: String,
    val type: String
)

data class UiState(
    val xboxIp: String = "",
    val connected: Boolean = false,
    val bannerSeen: Boolean = false,
    val installed: Boolean = false,
    val hookStatus: String = "Not installed",
    val live: Boolean = false,
    val wantLive: Boolean = false,
    val safeToUnload: Boolean = false,
    val laneLocked: Boolean = false,
    val lane: String = "0",
    val packetBytes: String = "0x0",
    val physicalHeadset: String = "?",
    val virtualHeadset: String = "?",
    val framesSent: Long = 0,
    val drops: Long = 0,
    val rms: Double = 0.0,
    val gateOpen: Boolean = false,
    val gateThreshold: Int = 220,
    val inputLevelPercent: Int = 100,
    val micGainPercent: Int = 100,
    val micDevices: List<MicDevice> = listOf(MicDevice(-1, "System default", "Automatic")),
    val selectedMicDeviceId: Int = -1,
    val selectedMicDeviceName: String = "System default",
    val routedMicDeviceName: String = "—",
    val baseMuted: Boolean = false,
    val holdActive: Boolean = false,
    val statusText: String = "Disconnected"
) {
    val effectiveMuted: Boolean get() = baseMuted.xor(holdActive)
}
