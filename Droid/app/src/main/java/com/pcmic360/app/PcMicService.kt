package com.pcmic360.app

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.nio.charset.StandardCharsets
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import kotlin.math.max
import kotlin.math.roundToInt

class PcMicService : Service() {

    interface Listener {
        fun onState(state: UiState)
        fun onLog(line: String)
    }

    inner class LocalBinder : Binder() {
        fun service(): PcMicService = this@PcMicService
    }

    private val binder = LocalBinder()
    private val listeners = CopyOnWriteArrayList<Listener>()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val stateLock = Any()
    private val sendLock = Any()
    private val audioQueue = ArrayBlockingQueue<ByteArray>(8)
    private val commandQueue = LinkedBlockingQueue<String>(64)
    private val destroyed = AtomicBoolean(false)

    @Volatile private var uiState = UiState()
    @Volatile private var wantConnected = false
    @Volatile private var targetIp = ""
    @Volatile private var networkThread: Thread? = null
    @Volatile private var socket: Socket? = null
    @Volatile private var output: OutputStream? = null
    private val hookCoordinator = HookInstallCoordinator(maxAttempts = 2)
    @Volatile private var restorePending = false
    @Volatile private var hookTimeoutGeneration = 0L
    @Volatile private var lastSendError = ""

    @Volatile private var audioThread: Thread? = null
    @Volatile private var senderThread: Thread? = null
    @Volatile private var recorder: AudioRecord? = null
    @Volatile private var gateUntilMs = 0L
    @Volatile private var framesSent = 0L
    @Volatile private var drops = 0L
    @Volatile private var lastUiAudioUpdate = 0L

    private lateinit var audioManager: AudioManager
    private val audioDeviceCallback = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
            refreshMicDevices()
        }

        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
            refreshMicDevices()
        }
    }

    override fun onCreate() {
        super.onCreate()
        audioManager = getSystemService(AudioManager::class.java)
        audioManager.registerAudioDeviceCallback(audioDeviceCallback, mainHandler)
        createNotificationChannel()
        refreshMicDevices()
        ensureSenderThread()
        ensureNetworkThread()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_TOGGLE_MUTE -> toggleMute()
            ACTION_STOP_LIVE -> stopLive()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        destroyed.set(true)
        wantConnected = false
        stopAudioEngine()
        closeSocketAsync()
        try { audioManager.unregisterAudioDeviceCallback(audioDeviceCallback) } catch (_: Throwable) {}
        super.onDestroy()
    }

    fun addListener(listener: Listener) {
        listeners.addIfAbsent(listener)
        listener.onState(snapshot())
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }

    fun snapshot(): UiState = synchronized(stateLock) { uiState }

    fun connect(ip: String) {
        val clean = ip.trim()
        if (clean.isEmpty()) {
            log("Enter the Xbox's local IP address first.")
            return
        }
        targetIp = clean
        wantConnected = true
        commandQueue.clear()
        audioQueue.clear()
        restorePending = false
        hookCoordinator.resetSession()
        hookTimeoutGeneration++
        updateState {
            it.copy(
                xboxIp = clean,
                safeToUnload = false,
                installed = false,
                hookStatus = "Waiting for PcMic-360 XEX…",
                statusText = if (it.connected) "Connected" else "Connecting…"
            )
        }
        closeSocketAsync()
        ensureNetworkThread()
        log("Connecting to Xbox $clean:${PcMicProtocol.PORT}…")
    }

    fun disconnect() {
        if (snapshot().wantLive || snapshot().live) stopLive()
        wantConnected = false
        restorePending = false
        commandQueue.clear()
        audioQueue.clear()
        closeSocketAsync()
        updateState {
            it.copy(
                connected = false,
                bannerSeen = false,
                installed = false,
                hookStatus = "Not installed",
                live = false,
                laneLocked = false,
                statusText = "Disconnected"
            )
        }
        log("Disconnected from Xbox.")
    }

    fun startLive(): Boolean {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            log("Microphone permission is not granted.")
            return false
        }
        val current = snapshot()
        if (!current.connected || !current.installed) {
            log("Connect to the Xbox and wait for the automatic hook install first.")
            return false
        }
        if (current.wantLive) return true

        updateState { it.copy(wantLive = true, safeToUnload = false) }
        promoteToForeground()
        if (!sendCommand(PcMicProtocol.CMD_LIVE)) {
            updateState { it.copy(wantLive = false, live = false) }
            stopForeground(STOP_FOREGROUND_REMOVE)
            log("Live Mic could not start because the Xbox command channel is not connected.")
            return false
        }
        startAudioEngine()
        log("Live Android microphone starting…")
        return true
    }

    fun stopLive() {
        val current = snapshot()
        if (current.connected && (current.live || current.wantLive)) {
            sendCommand(PcMicProtocol.CMD_STOP)
        }
        updateState { it.copy(wantLive = false, live = false, gateOpen = false, rms = 0.0) }
        stopAudioEngine()
        audioQueue.clear()
        stopForeground(STOP_FOREGROUND_REMOVE)
        log("Live microphone stopped.")
    }

    fun toggleMute() {
        val before = snapshot().effectiveMuted
        updateState { it.copy(baseMuted = !it.baseMuted, holdActive = false) }
        val after = snapshot().effectiveMuted
        if (after && !before) queueImmediateSilence()
        updateNotification()
        log(if (snapshot().baseMuted) "Microphone muted." else "Microphone unmuted.")
    }

    fun setHold(active: Boolean) {
        val before = snapshot().effectiveMuted
        updateState { it.copy(holdActive = active) }
        val after = snapshot().effectiveMuted
        if (after && !before) queueImmediateSilence()
    }

    fun setGateThreshold(value: Int) {
        updateState { it.copy(gateThreshold = value.coerceIn(0, GATE_MAX_RMS)) }
    }

    fun setInputLevel(value: Int) {
        updateState { it.copy(inputLevelPercent = value.coerceIn(0, 100)) }
    }

    fun setMicGain(value: Int) {
        updateState { it.copy(micGainPercent = value.coerceIn(0, 400)) }
    }

    fun refreshMicDevices() {
        if (!::audioManager.isInitialized) return
        val found = ArrayList<MicDevice>()
        found += MicDevice(-1, "System default", "Automatic")
        try {
            audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)
                .filter { it.isSource }
                .sortedWith(compareBy<AudioDeviceInfo> { micTypePriority(it.type) }.thenBy { it.productName.toString() })
                .forEach { device ->
                    val type = micTypeLabel(device.type)
                    val product = device.productName?.toString()?.trim().orEmpty()
                    val name = when {
                        product.isEmpty() -> type
                        product.equals(type, ignoreCase = true) -> product
                        else -> "$product • $type"
                    }
                    found += MicDevice(device.id, name, type)
                }
        } catch (t: Throwable) {
            log("Could not enumerate microphone devices: ${t.message ?: t.javaClass.simpleName}")
        }

        val currentId = snapshot().selectedMicDeviceId
        val selected = found.firstOrNull { it.id == currentId } ?: found.first()
        updateState {
            it.copy(
                micDevices = found,
                selectedMicDeviceId = selected.id,
                selectedMicDeviceName = selected.name
            )
        }
    }

    fun selectMicDevice(deviceId: Int): Boolean {
        val current = snapshot()
        if (current.wantLive || current.live) {
            log("Stop Live Mic before changing the input device.")
            return false
        }
        val selected = current.micDevices.firstOrNull { it.id == deviceId }
            ?: current.micDevices.firstOrNull { it.id == -1 }
            ?: MicDevice(-1, "System default", "Automatic")
        updateState {
            it.copy(
                selectedMicDeviceId = selected.id,
                selectedMicDeviceName = selected.name,
                routedMicDeviceName = "—"
            )
        }
        log("Microphone input selected: ${selected.name}")
        return true
    }

    fun reportStatus() {
        if (!sendCommand(PcMicProtocol.CMD_STATUS)) log("Xbox is not connected.")
    }

    fun restoreAndSafeUnload() {
        if (!snapshot().connected) {
            log("No Xbox connection to restore.")
            return
        }
        updateState { it.copy(wantLive = false, live = false, safeToUnload = false) }
        stopAudioEngine()
        audioQueue.clear()
        stopForeground(STOP_FOREGROUND_REMOVE)
        restorePending = true
        if (sendCommand(PcMicProtocol.CMD_RESTORE)) {
            log("Restore command queued. Waiting for the Xbox to confirm SAFE_TO_UNLOAD…")
            mainHandler.postDelayed({
                if (restorePending && snapshot().connected) {
                    log("Restore is still waiting for SAFE_TO_UNLOAD. Do NOT unload the XEX yet; use COPY LOG if this remains stuck.")
                    updateState { it.copy(statusText = "Restore not confirmed") }
                }
            }, RESTORE_ACK_TIMEOUT_MS)
        } else {
            restorePending = false
            log("Restore command could not be queued because the Xbox is not connected.")
        }
    }

    private fun ensureNetworkThread() {
        val existing = networkThread
        if (existing?.isAlive == true) return
        networkThread = thread(name = "PcMic-Network", isDaemon = true) { networkLoop() }
    }

    private fun networkLoop() {
        var lastError = ""
        var lastErrorAt = 0L
        while (!destroyed.get()) {
            if (!wantConnected || targetIp.isBlank()) {
                SystemClock.sleep(100)
                continue
            }

            val ip = targetIp
            var local: Socket? = null
            try {
                local = Socket()
                local.tcpNoDelay = true
                local.soTimeout = 500
                local.connect(InetSocketAddress(ip, PcMicProtocol.PORT), 3000)
                socket = local
                output = local.getOutputStream()
                hookCoordinator.resetSession()
                hookTimeoutGeneration++
                updateState {
                    it.copy(
                        xboxIp = ip,
                        connected = true,
                        bannerSeen = false,
                        installed = false,
                        hookStatus = "Waiting for PcMic-360 protocol…",
                        live = false,
                        laneLocked = false,
                        statusText = "Connected"
                    )
                }
                log("Connected to Xbox $ip:${PcMicProtocol.PORT}")
                mainHandler.postDelayed({
                    if (wantConnected && socket === local && snapshot().connected && !snapshot().bannerSeen) {
                        log("TCP is connected but PcMic-360 protocol has not appeared yet. Verify the correct PcMic-360.xex is loaded; continuing to listen for READY/status.")
                        sendCommand(PcMicProtocol.CMD_STATUS)
                    }
                }, PROTOCOL_HELLO_TIMEOUT_MS)
                lastError = ""
                readLoop(local)
            } catch (t: Throwable) {
                val message = t.message ?: t.javaClass.simpleName
                val now = SystemClock.elapsedRealtime()
                if (message != lastError || now - lastErrorAt >= 5000) {
                    log("Xbox connection pending: $message")
                    lastError = message
                    lastErrorAt = now
                }
            } finally {
                if (socket === local) {
                    socket = null
                    output = null
                }
                try { local?.close() } catch (_: Throwable) {}
                audioQueue.clear()
                commandQueue.clear()
                updateState {
                    it.copy(
                        connected = false,
                        bannerSeen = false,
                        installed = false,
                        hookStatus = if (wantConnected) "Waiting for reconnect…" else "Not installed",
                        live = false,
                        laneLocked = false,
                        statusText = if (wantConnected) "Reconnecting…" else "Disconnected"
                    )
                }
                if (wantConnected && !destroyed.get()) SystemClock.sleep(1000)
            }
        }
    }

    private fun readLoop(s: Socket) {
        val input = s.getInputStream()
        val line = ByteArrayOutputStream(512)
        while (!destroyed.get() && wantConnected && socket === s) {
            val value: Int
            try {
                value = input.read()
            } catch (_: SocketTimeoutException) {
                continue
            }
            if (value < 0) return
            when (value) {
                '\n'.code -> {
                    val text = line.toByteArray().toString(StandardCharsets.US_ASCII).trimEnd('\r')
                    line.reset()
                    if (text.isNotEmpty()) handleXexLine(text)
                }
                else -> if (line.size() < 16384) line.write(value)
            }
        }
    }

    private fun handleXexLine(text: String) {
        log("XEX → $text")

        // Do not depend on one exact banner string. A compatible PcMic XEX can
        // be identified by HELLO, READY, or its V700 status stream.
        val compatibleHello = text.contains("VOICE_DECODE_UNIVERSAL_SYSTEM_MIC_") && text.contains("HELLO")
        val compatibleReady = text.startsWith("READY ") && text.contains("O_install") && text.contains("AUDIO=A570")
        val compatibleStatus = text.startsWith("V700_STATUS")
        if ((compatibleHello || compatibleReady || compatibleStatus) && !snapshot().bannerSeen) {
            updateState { it.copy(bannerSeen = true, statusText = "PcMic-360 XEX ready") }
            log(
                when {
                    compatibleHello -> "PcMic-360 XEX protocol detected from HELLO."
                    compatibleReady -> "PcMic-360 XEX protocol detected from READY."
                    else -> "PcMic-360 XEX protocol detected from status stream."
                }
            )
            applyHookDecision(hookCoordinator.onProtocolSeen())
        }

        if (text.startsWith("V700_RESOLVE") && text.contains("ok=0")) {
            updateState { it.copy(hookStatus = "Resolver failed") }
            log("Voice resolver reported failure. Use COPY LOG and send the V700_RESOLVE/V701_SCAN lines for diagnosis.")
        }

        if (text.startsWith("V702_HEADSET_RESOLVE") && text.contains("ok=0")) {
            log("Virtual-headset resolver reported failure.")
        }

        if (text.startsWith("V700_INSTALL")) {
            val ok = text.contains("ok=1")
            val decision = hookCoordinator.onInstallResult(ok)
            updateState {
                it.copy(
                    installed = ok,
                    safeToUnload = false,
                    hookStatus = decision.hookStatus ?: if (ok) "Installed" else it.hookStatus
                )
            }
            if (ok) {
                log("Voice hook installed and confirmed by Xbox.")
                if (snapshot().wantLive) sendCommand(PcMicProtocol.CMD_LIVE)
            } else {
                log("Xbox reported that the hook install failed. Checking status before retrying.")
            }
            applyHookDecision(decision)
        }

        if (text.startsWith("V700_START") && text.contains("ok=1")) {
            val fields = PcMicProtocol.parseFields(text)
            if (fields["mode"] == "2") {
                updateState { it.copy(live = true, wantLive = true, statusText = "Live mic active") }
                log("Xbox confirmed Live Mic mode.")
            }
        }

        if (text.startsWith("V700_START") && text.contains("ok=0")) {
            updateState { it.copy(live = false, statusText = "Live Mic start failed") }
            log("Xbox rejected Live Mic start. Copy the log for diagnosis.")
        }

        if (text.startsWith("V700_STOP")) {
            updateState { it.copy(live = false) }
        }

        if (text.startsWith("V700_STATUS")) {
            val f = PcMicProtocol.parseFields(text)
            val xboxInstalled = f["installed"] == "1"
            val locked = f["locked"] == "1"
            val decision = hookCoordinator.onStatus(xboxInstalled)
            updateState {
                it.copy(
                    installed = xboxInstalled || hookCoordinator.isInstalled(),
                    hookStatus = decision.hookStatus ?: if (xboxInstalled) "Installed" else it.hookStatus,
                    laneLocked = locked,
                    lane = f["lane"] ?: it.lane,
                    packetBytes = f["bytes"] ?: it.packetBytes,
                    physicalHeadset = f["physicalHeadset"] ?: it.physicalHeadset,
                    virtualHeadset = f["virtualHeadset"] ?: it.virtualHeadset
                )
            }
            applyHookDecision(decision)
            if (xboxInstalled && snapshot().wantLive && !snapshot().live) {
                sendCommand(PcMicProtocol.CMD_LIVE)
            }
        }

        if (text.startsWith("V700_DISCOVERY") && text.contains("locked=1")) {
            val f = PcMicProtocol.parseFields(text)
            updateState {
                it.copy(
                    laneLocked = true,
                    lane = f["lane"] ?: it.lane,
                    packetBytes = f["bytes"] ?: it.packetBytes
                )
            }
        }

        if (text.contains("SAFE_TO_UNLOAD")) {
            restorePending = false
            wantConnected = false
            hookTimeoutGeneration++
            stopAudioEngine()
            audioQueue.clear()
            stopForeground(STOP_FOREGROUND_REMOVE)
            updateState {
                it.copy(
                    safeToUnload = true,
                    installed = false,
                    hookStatus = "Restored — safe to unload",
                    live = false,
                    wantLive = false,
                    laneLocked = false,
                    statusText = "HOOKS RESTORED — SESSION ENDED"
                )
            }
            log("SAFE_TO_UNLOAD received. Xbox voice gateways are restored; Android audio and reconnect are stopped. The XEX itself remains loaded until your Xbox loader unloads it.")
        }
    }

    fun retryHookInstall() {
        val current = snapshot()
        if (!current.connected) {
            log("Connect to the Xbox before retrying the hook.")
            return
        }
        if (!current.bannerSeen) {
            log("PcMic-360 protocol has not been detected yet; waiting for Xbox HELLO/READY/status.")
            sendCommand(PcMicProtocol.CMD_STATUS)
            return
        }
        if (current.installed) {
            log("Voice hook is already installed.")
            return
        }
        log("Manual hook retry requested.")
        applyHookDecision(hookCoordinator.manualRetry())
    }

    private fun applyHookDecision(decision: HookInstallCoordinator.Decision) {
        decision.hookStatus?.let { status -> updateState { it.copy(hookStatus = status) } }

        if (decision.sendStatus) {
            if (sendCommand(PcMicProtocol.CMD_STATUS)) {
                scheduleHookStatusTimeout()
            }
        }

        if (decision.sendInstall) {
            val attempt = hookCoordinator.attemptCount()
            log("Installing PcMic-360 voice hook automatically (attempt $attempt/2)…")
            if (sendCommand(PcMicProtocol.CMD_INSTALL)) {
                scheduleHookInstallTimeout()
            } else {
                log("Could not send hook-install command. Checking connection before retrying.")
                applyHookDecision(hookCoordinator.onInstallResult(false))
            }
        }
    }

    private fun scheduleHookStatusTimeout() {
        val generation = ++hookTimeoutGeneration
        mainHandler.postDelayed({
            if (generation != hookTimeoutGeneration || !wantConnected || !snapshot().connected || snapshot().safeToUnload) return@postDelayed
            val decision = hookCoordinator.onStatusWaitTimeout()
            if (decision.sendInstall || decision.hookStatus != null) {
                if (decision.sendInstall) log("No status reply arrived in time; proceeding with the automatic hook install.")
                applyHookDecision(decision)
            }
        }, HOOK_STATUS_WAIT_MS)
    }

    private fun scheduleHookInstallTimeout() {
        val generation = ++hookTimeoutGeneration
        mainHandler.postDelayed({
            if (generation != hookTimeoutGeneration || !wantConnected || !snapshot().connected || snapshot().safeToUnload) return@postDelayed
            val decision = hookCoordinator.onInstallTimeout()
            if (decision.sendStatus || decision.hookStatus != null) {
                log("Hook install has not confirmed yet; asking the Xbox for its real status before retrying.")
                applyHookDecision(decision)
                if (decision.sendStatus) scheduleFinalHookStatusTimeout()
            }
        }, HOOK_INSTALL_TIMEOUT_MS)
    }

    private fun scheduleFinalHookStatusTimeout() {
        val generation = hookTimeoutGeneration
        mainHandler.postDelayed({
            if (generation != hookTimeoutGeneration || !wantConnected || !snapshot().connected || snapshot().safeToUnload) return@postDelayed
            val decision = hookCoordinator.onFinalStatusTimeout()
            if (decision.hookStatus != null || decision.sendInstall) {
                if (decision.sendInstall) {
                    log("Status still did not confirm the hook; making one final controlled install attempt.")
                } else if (hookCoordinator.hasFailed()) {
                    log("HOOK FAILED: the Xbox never confirmed installation after two attempts. Tap RETRY HOOK or COPY LOG for diagnosis.")
                }
                applyHookDecision(decision)
            }
        }, HOOK_FINAL_STATUS_WAIT_MS)
    }

    private fun sendCommand(command: String): Boolean {
        if (!snapshot().connected || output == null) {
            lastSendError = "socket not connected"
            log("Command $command could not be queued: $lastSendError")
            return false
        }
        val queued = commandQueue.offer(command)
        if (!queued) {
            lastSendError = "command queue full"
            log("Command $command could not be queued: $lastSendError")
            return false
        }
        lastSendError = ""
        return true
    }

    private fun sendAudioDirect(pcm: ByteArray): Boolean {
        return writeBytesDirect(PcMicProtocol.audioPacket(pcm))
    }

    private fun writeCommandDirect(command: String): Boolean {
        return writeBytesDirect((command + "\n").toByteArray(StandardCharsets.US_ASCII))
    }

    private fun writeBytesDirect(data: ByteArray): Boolean {
        val out = output ?: return false
        if (!snapshot().connected) return false
        return try {
            synchronized(sendLock) {
                out.write(data)
                out.flush()
            }
            lastSendError = ""
            true
        } catch (t: Throwable) {
            lastSendError = t.message ?: t.javaClass.simpleName
            false
        }
    }

    private fun closeSocket() {
        val s = socket
        socket = null
        output = null
        try { s?.shutdownInput() } catch (_: Throwable) {}
        try { s?.shutdownOutput() } catch (_: Throwable) {}
        try { s?.close() } catch (_: Throwable) {}
    }

    private fun closeSocketAsync() {
        thread(name = "PcMic-SocketClose", isDaemon = true) { closeSocket() }
    }

    private fun ensureSenderThread() {
        if (senderThread?.isAlive == true) return
        senderThread = thread(name = "PcMic-Outbound", isDaemon = true) {
            while (!destroyed.get()) {
                // Commands always have priority over audio. This guarantees that
                // S/T/Q/P/O never execute on Android's main/UI thread and cannot
                // be delayed behind microphone frames.
                val command = commandQueue.poll()
                if (command != null) {
                    if (!writeCommandDirect(command)) {
                        log("Command $command send failed on background network writer: ${lastSendError.ifBlank { "socket not connected" }}")
                    }
                    continue
                }

                val frame = audioQueue.poll(20, TimeUnit.MILLISECONDS) ?: continue
                val current = snapshot()
                if (!current.wantLive || !current.connected) continue
                if (sendAudioDirect(frame)) {
                    framesSent++
                    if (framesSent % 5L == 0L) {
                        updateState { it.copy(framesSent = framesSent, drops = drops) }
                    }
                }
            }
        }
    }

    private fun startAudioEngine() {
        if (audioThread?.isAlive == true) return
        audioThread = thread(name = "PcMic-AudioCapture", isDaemon = true) {
            captureLoop()
        }
    }

    private fun captureLoop() {
        var localRecorder: AudioRecord? = null
        try {
            val record = buildRecorder()
            localRecorder = record
            recorder = record
            val inputRate = record.sampleRate
            val inputSamplesPerFrame = max(1, (inputRate / 50.0).roundToInt())
            val inputFrame = ShortArray(inputSamplesPerFrame)
            record.startRecording()
            val routed = try { record.routedDevice } catch (_: Throwable) { null }
            val routedName = routed?.let { micDeviceDisplayName(it) } ?: snapshot().selectedMicDeviceName
            updateState { it.copy(routedMicDeviceName = routedName) }
            log("Android microphone: ${inputRate} Hz capture → 16000 Hz, 320 samples / 20 ms • input=$routedName")

            var previousGateOpen = false
            var gateStateInitialized = false
            while (!destroyed.get() && snapshot().wantLive) {
                var filled = 0
                while (filled < inputFrame.size && snapshot().wantLive) {
                    val n = record.read(
                        inputFrame,
                        filled,
                        inputFrame.size - filled,
                        AudioRecord.READ_BLOCKING
                    )
                    if (n <= 0) {
                        if (n < 0) throw IllegalStateException("AudioRecord.read failed: $n")
                        continue
                    }
                    filled += n
                }
                if (filled != inputFrame.size || !snapshot().wantLive) break

                var samples = AudioMath.resampleLinear(inputFrame, PcMicProtocol.SAMPLES)
                val now = SystemClock.elapsedRealtime()
                val current = snapshot()
                val processingScale = (current.inputLevelPercent / 100.0) * (current.micGainPercent / 100.0)
                AudioMath.scaleInPlace(samples, processingScale)
                val rms = AudioMath.rms(samples)
                val threshold = current.gateThreshold
                val rawGateOpen = if (threshold <= 0) {
                    true
                } else {
                    if (rms >= threshold) gateUntilMs = now + GATE_HANGOVER_MS
                    now < gateUntilMs
                }
                val effectiveMuted = current.effectiveMuted
                val actualGateOpen = rawGateOpen && !effectiveMuted

                if (!gateStateInitialized || previousGateOpen != actualGateOpen) {
                    if (!actualGateOpen) audioQueue.clear()
                    log(
                        if (actualGateOpen) {
                            "Voice gate OPEN: RMS ${rms.roundToInt()} >= threshold $threshold."
                        } else {
                            "Voice gate CLOSED: RMS ${rms.roundToInt()} < threshold $threshold; sending exact digital silence."
                        }
                    )
                    gateStateInitialized = true
                }
                previousGateOpen = actualGateOpen

                if (effectiveMuted || !rawGateOpen) {
                    samples = ShortArray(PcMicProtocol.SAMPLES)
                }

                val pcm = AudioMath.toPcm16BigEndian(samples)
                if (!audioQueue.offer(pcm)) {
                    audioQueue.poll()
                    drops++
                    audioQueue.offer(pcm)
                }

                if (now - lastUiAudioUpdate >= 80) {
                    lastUiAudioUpdate = now
                    updateState {
                        it.copy(
                            rms = rms,
                            gateOpen = actualGateOpen,
                            framesSent = framesSent,
                            drops = drops
                        )
                    }
                }
            }
        } catch (t: Throwable) {
            log("Microphone error: ${t.message ?: t.javaClass.simpleName}")
            updateState { it.copy(wantLive = false, live = false, gateOpen = false) }
        } finally {
            recorder = null
            try { localRecorder?.stop() } catch (_: Throwable) {}
            try { localRecorder?.release() } catch (_: Throwable) {}
                mainHandler.post { stopForeground(STOP_FOREGROUND_REMOVE) }
        }
    }

    private fun buildRecorder(): AudioRecord {
        // Prefer the normal MIC path. VOICE_COMMUNICATION can apply aggressive
        // AGC on some phones, lifting room noise above a user-selected gate.
        val sources = intArrayOf(MediaRecorder.AudioSource.MIC, MediaRecorder.AudioSource.VOICE_COMMUNICATION)
        val rates = intArrayOf(16000, 48000, 44100, 32000)
        var lastError: Throwable? = null
        val selectedId = snapshot().selectedMicDeviceId
        val preferredDevice = if (selectedId >= 0) findInputDevice(selectedId) else null

        for (source in sources) {
            for (rate in rates) {
                try {
                    val minBuffer = AudioRecord.getMinBufferSize(
                        rate,
                        AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT
                    )
                    if (minBuffer <= 0) continue
                    val frameBytes = max(1, (rate / 50.0).roundToInt()) * 2
                    val bufferBytes = max(minBuffer, frameBytes * 8)
                    val record = AudioRecord.Builder()
                        .setAudioSource(source)
                        .setAudioFormat(
                            AudioFormat.Builder()
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(rate)
                                .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                                .build()
                        )
                        .setBufferSizeInBytes(bufferBytes)
                        .build()
                    if (record.state == AudioRecord.STATE_INITIALIZED) {
                        if (selectedId >= 0 && preferredDevice != null) {
                            val routed = try { record.setPreferredDevice(preferredDevice) } catch (_: Throwable) { false }
                            if (!routed) log("Android could not force ${snapshot().selectedMicDeviceName}; system routing will be used.")
                        } else if (selectedId >= 0 && preferredDevice == null) {
                            log("Selected microphone is no longer connected; using system default.")
                        }
                        return record
                    }
                    record.release()
                } catch (t: Throwable) {
                    lastError = t
                }
            }
        }
        throw IllegalStateException("No compatible mono PCM16 microphone format", lastError)
    }

    private fun stopAudioEngine() {
        updateState { it.copy(wantLive = false, live = false, gateOpen = false, rms = 0.0) }
        val local = recorder
        try { local?.stop() } catch (_: Throwable) {}
        audioThread?.interrupt()
        audioThread = null
        recorder = null
        audioQueue.clear()
    }

    private fun queueImmediateSilence() {
        audioQueue.clear()
        if (snapshot().wantLive) audioQueue.offer(ByteArray(PcMicProtocol.PCM_BYTES))
    }

    private fun promoteToForeground() {
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= 30) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun updateNotification() {
        if (!snapshot().wantLive) return
        try {
            val manager = getSystemService(NotificationManager::class.java)
            manager.notify(NOTIFICATION_ID, buildNotification())
        } catch (_: SecurityException) {
            // Notification permission is intentionally not requested.
            // The foreground-service notice remains managed by Android.
        }
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this,
            10,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val muteIntent = PendingIntent.getService(
            this,
            11,
            Intent(this, PcMicService::class.java).setAction(ACTION_TOGGLE_MUTE),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stopIntent = PendingIntent.getService(
            this,
            12,
            Intent(this, PcMicService::class.java).setAction(ACTION_STOP_LIVE),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val muted = snapshot().baseMuted
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentTitle("PcMic-360")
            .setContentText(if (muted) "Microphone muted" else "Streaming microphone to Xbox 360")
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(openIntent)
            .addAction(Notification.Action.Builder(0, if (muted) "Unmute" else "Mute", muteIntent).build())
            .addAction(Notification.Action.Builder(0, "Stop", stopIntent).build())
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "PcMic-360 Live Microphone",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Keeps PcMic-360 microphone streaming active in the background"
                setShowBadge(false)
            }
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }

    private fun findInputDevice(id: Int): AudioDeviceInfo? {
        if (!::audioManager.isInitialized) return null
        return try {
            audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS).firstOrNull { it.isSource && it.id == id }
        } catch (_: Throwable) {
            null
        }
    }

    private fun micDeviceDisplayName(device: AudioDeviceInfo): String {
        val type = micTypeLabel(device.type)
        val product = device.productName?.toString()?.trim().orEmpty()
        return when {
            product.isEmpty() -> type
            product.equals(type, ignoreCase = true) -> product
            else -> "$product • $type"
        }
    }

    private fun micTypePriority(type: Int): Int = when (type) {
        AudioDeviceInfo.TYPE_WIRED_HEADSET,
        AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_USB_DEVICE,
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
        AudioDeviceInfo.TYPE_BLE_HEADSET -> 0
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> 1
        else -> 2
    }

    private fun micTypeLabel(type: Int): String = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "Built-in mic"
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Wired headset mic"
        AudioDeviceInfo.TYPE_USB_HEADSET -> "USB headset mic"
        AudioDeviceInfo.TYPE_USB_DEVICE -> "USB audio input"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth headset mic"
        AudioDeviceInfo.TYPE_BLE_HEADSET -> "Bluetooth LE headset mic"
        AudioDeviceInfo.TYPE_LINE_ANALOG -> "Analog line input"
        AudioDeviceInfo.TYPE_LINE_DIGITAL -> "Digital line input"
        AudioDeviceInfo.TYPE_TELEPHONY -> "Telephony input"
        else -> "Audio input"
    }

    private fun updateState(transform: (UiState) -> UiState) {
        val newState = synchronized(stateLock) {
            uiState = transform(uiState)
            uiState
        }
        mainHandler.post {
            for (listener in listeners) listener.onState(newState)
        }
    }

    private fun log(message: String) {
        mainHandler.post {
            for (listener in listeners) listener.onLog(message)
        }
    }

    companion object {
        private const val CHANNEL_ID = "pcmic360_live"
        private const val NOTIFICATION_ID = 360
        private const val GATE_HANGOVER_MS = 180L
        const val GATE_MAX_RMS = 20000
        private const val PROTOCOL_HELLO_TIMEOUT_MS = 2500L
        private const val HOOK_STATUS_WAIT_MS = 1200L
        private const val HOOK_INSTALL_TIMEOUT_MS = 15000L
        private const val HOOK_FINAL_STATUS_WAIT_MS = 1800L
        private const val RESTORE_ACK_TIMEOUT_MS = 5000L
        private const val ACTION_TOGGLE_MUTE = "com.pcmic360.app.TOGGLE_MUTE"
        private const val ACTION_STOP_LIVE = "com.pcmic360.app.STOP_LIVE"
    }
}
