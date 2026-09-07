package com.pcmic360.app

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.IBinder
import android.text.InputType
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Space
import android.widget.TextView
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

class MainActivity : Activity(), PcMicService.Listener {

    private var service: PcMicService? = null
    private var bound = false
    private val prefs by lazy { getSharedPreferences("pcmic360", Context.MODE_PRIVATE) }
    private val logLines = ArrayDeque<String>()
    private var lastSafeShown = false

    private lateinit var pageScroll: ScrollView
    private lateinit var ipEdit: EditText
    private lateinit var connectButton: Button
    private lateinit var connectionPill: TextView
    private lateinit var hookPill: TextView
    private lateinit var lanePill: TextView
    private lateinit var hookDetailValue: TextView
    private lateinit var micDeviceButton: Button
    private lateinit var selectedMicValue: TextView
    private lateinit var micRefreshButton: Button
    private lateinit var liveButton: Button
    private lateinit var muteButton: Button
    private lateinit var holdButton: Button
    private lateinit var inputLevelSeek: SeekBar
    private lateinit var inputLevelValue: TextView
    private lateinit var gainSeek: SeekBar
    private lateinit var gainValue: TextView
    private lateinit var gateSeek: SeekBar
    private lateinit var gateValue: TextView
    private lateinit var gateState: TextView
    private lateinit var levelMeter: LevelMeterView
    private lateinit var framesValue: TextView
    private lateinit var dropsValue: TextView
    private lateinit var headsetValue: TextView
    private lateinit var transportValue: TextView
    private lateinit var logView: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var retryHookButton: Button

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val local = binder as? PcMicService.LocalBinder ?: return
            service = local.service()
            bound = true
            service?.addListener(this@MainActivity)
            val gate = prefs.getInt("gate", 220)
            val inputLevel = prefs.getInt("input_level", 100)
            val micGain = prefs.getInt("mic_gain", 100)
            val micDeviceId = prefs.getInt("mic_device_id", -1)
            service?.setGateThreshold(gate)
            service?.setInputLevel(inputLevel)
            service?.setMicGain(micGain)
            service?.refreshMicDevices()
            service?.selectMicDevice(micDeviceId)
            render(service?.snapshot() ?: UiState(gateThreshold = gate, inputLevelPercent = inputLevel, micGainPercent = micGain))
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            bound = false
            service = null
            render(UiState(statusText = "Service disconnected"))
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = C.bg
        window.navigationBarColor = C.bg
        buildUi()

        val serviceIntent = Intent(this, PcMicService::class.java)
        startService(serviceIntent)
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE)
    }

    override fun onDestroy() {
        if (bound) {
            service?.removeListener(this)
            unbindService(serviceConnection)
        }
        bound = false
        super.onDestroy()
    }

    override fun onState(state: UiState) {
        runOnUiThread { render(state) }
    }

    override fun onLog(line: String) {
        runOnUiThread {
            val oldPageY = if (::pageScroll.isInitialized) pageScroll.scrollY else 0
            val stamp = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
            logLines.addLast("[$stamp] $line")
            while (logLines.size > 500) logLines.removeFirst()
            logView.text = logLines.joinToString("\n")
            // The diagnostics log must never steal focus or drag the main page
            // to the bottom when the Xbox emits its once-per-second status lines.
            if (::pageScroll.isInitialized && !pageScroll.isPressed) {
                pageScroll.post { if (!pageScroll.isPressed) pageScroll.scrollTo(0, oldPageY) }
            }
        }
    }

    private fun buildUi() {
        pageScroll = ScrollView(this).apply {
            setBackgroundColor(C.bg)
            isFillViewport = true
            descendantFocusability = ViewGroup.FOCUS_BEFORE_DESCENDANTS
        }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(18), dp(18), dp(28))
        }
        pageScroll.addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        root.addView(text("PcMic-360", 28f, C.text, true).apply { letterSpacing = 0.01f })
        root.addView(text("Use your Android microphone as the Xbox 360 voice-chat mic.", 13f, C.subtext).apply {
            setPadding(0, dp(4), 0, dp(8))
        })
        root.addView(text("Privacy: PcMic-360 asks only for Microphone permission, and only when you press Start Live Mic. Audio goes directly to the Xbox IP you enter. No location, Nearby Devices, contacts, files, camera, analytics, accounts, or cloud upload.", 11f, C.subtext).apply {
            setPadding(dp(10), dp(9), dp(10), dp(9))
            background = rounded(C.panel2, 9f, C.border)
        })
        root.addView(space(12))

        val connectCard = card()
        root.addView(connectCard)
        connectCard.addView(sectionTitle("Xbox Connection", "PcMic-360.xex listens on TCP 36000"))
        val connectRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        ipEdit = EditText(this).apply {
            setText(prefs.getString("xbox_ip", "") ?: "")
            hint = "192.168.1.186"
            setHintTextColor(C.muted)
            setTextColor(C.text)
            textSize = 16f
            setSingleLine(true)
            inputType = InputType.TYPE_CLASS_PHONE
            setPadding(dp(13), 0, dp(13), 0)
            background = rounded(C.panel2, 10f, C.border)
        }
        connectRow.addView(ipEdit, LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginEnd = dp(10) })
        connectButton = actionButton("CONNECT", C.accent)
        connectRow.addView(connectButton, LinearLayout.LayoutParams(dp(118), dp(48)))
        connectCard.addView(connectRow)

        val pills = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(12), 0, 0)
        }
        connectionPill = pill("OFFLINE", C.muted, C.panel2)
        hookPill = pill("HOOK —", C.muted, C.panel2)
        lanePill = pill("LANE —", C.muted, C.panel2)
        pills.addView(connectionPill, LinearLayout.LayoutParams(0, dp(34), 1f).apply { marginEnd = dp(6) })
        pills.addView(hookPill, LinearLayout.LayoutParams(0, dp(34), 1f).apply { marginStart = dp(3); marginEnd = dp(3) })
        pills.addView(lanePill, LinearLayout.LayoutParams(0, dp(34), 1f).apply { marginStart = dp(6) })
        connectCard.addView(pills)
        hookDetailValue = text("Hook: waiting for connection", 11f, C.subtext).apply { setPadding(0, dp(8), 0, 0) }
        connectCard.addView(hookDetailValue)

        root.addView(space(12))

        val micCard = card()
        root.addView(micCard)
        micCard.addView(sectionTitle("Microphone", "Choose the exact Android input before starting Live Mic"))
        selectedMicValue = text("Selected input: System default", 12f, C.text, true).apply {
            setPadding(dp(10), dp(9), dp(10), dp(9))
            background = rounded(C.panel2, 9f, C.border)
        }
        micCard.addView(selectedMicValue)
        val micDeviceRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        micDeviceButton = actionButton("SELECT MICROPHONE", C.accent).apply {
            gravity = Gravity.CENTER
            textSize = 12f
        }
        micRefreshButton = actionButton("REFRESH", C.panel2, C.border)
        micDeviceRow.addView(micDeviceButton, LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginEnd = dp(8) })
        micDeviceRow.addView(micRefreshButton, LinearLayout.LayoutParams(dp(96), dp(48)))
        micCard.addView(micDeviceRow, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(8) })

        liveButton = actionButton("START LIVE MIC", C.green).apply { textSize = 16f }
        micCard.addView(liveButton, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(54)).apply { topMargin = dp(10) })

        val muteRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(10), 0, 0)
        }
        muteButton = actionButton("MUTE", C.red)
        holdButton = actionButton("HOLD TO MUTE", C.panel2, C.border)
        muteRow.addView(muteButton, LinearLayout.LayoutParams(0, dp(50), 1f).apply { marginEnd = dp(5) })
        muteRow.addView(holdButton, LinearLayout.LayoutParams(0, dp(50), 1f).apply { marginStart = dp(5) })
        micCard.addView(muteRow)

        root.addView(space(12))

        val processingCard = card()
        root.addView(processingCard)
        processingCard.addView(sectionTitle("Mic Levels", "Digital controls only — Android does not expose universal hardware mic gain"))

        val inputHeader = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        inputHeader.addView(text("Mic Input Level", 13f, C.text, true), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        inputLevelValue = pill("100%", C.accent, C.panel2)
        inputHeader.addView(inputLevelValue, LinearLayout.LayoutParams(dp(72), dp(32)))
        processingCard.addView(inputHeader)
        inputLevelSeek = SeekBar(this).apply {
            max = 100
            progress = prefs.getInt("input_level", 100).coerceIn(0, 100)
        }
        processingCard.addView(inputLevelSeek, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)))

        val gainHeader = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(8), 0, 0) }
        gainHeader.addView(text("Mic Gain", 13f, C.text, true), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        gainValue = pill("1.00×", C.green, C.panel2)
        gainHeader.addView(gainValue, LinearLayout.LayoutParams(dp(72), dp(32)))
        processingCard.addView(gainHeader)
        gainSeek = SeekBar(this).apply {
            max = 400
            progress = prefs.getInt("mic_gain", 100).coerceIn(0, 400)
        }
        processingCard.addView(gainSeek, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)))
        processingCard.addView(text("Input Level attenuates 0–100%. Gain applies 0–4× digital amplification after it. 100% / 1.00× is unity.", 10f, C.subtext))

        root.addView(space(12))

        val gateCard = card()
        root.addView(gateCard)
        val gateHeader = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val gateTitle = sectionTitle("Voice Gate", "Set the threshold above the live RMS you want blocked; closed gate sends exact digital silence")
        gateHeader.addView(gateTitle, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        gateValue = pill("RMS 220", C.warn, C.warnDim)
        gateHeader.addView(gateValue, LinearLayout.LayoutParams(dp(92), dp(34)))
        gateCard.addView(gateHeader)

        gateSeek = SeekBar(this).apply {
            max = PcMicService.GATE_MAX_RMS
            progress = prefs.getInt("gate", 220).coerceIn(0, PcMicService.GATE_MAX_RMS)
            setPadding(0, dp(4), 0, 0)
        }
        gateCard.addView(gateSeek, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        val scaleRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        scaleRow.addView(text("OFF", 10f, C.muted), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        scaleRow.addView(text("${PcMicService.GATE_MAX_RMS} RMS", 10f, C.muted).apply { gravity = Gravity.END }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        gateCard.addView(scaleRow)
        levelMeter = LevelMeterView(this)
        gateCard.addView(levelMeter, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(22)).apply { topMargin = dp(9) })
        gateState = text("GATE CLOSED", 11f, C.subtext, true).apply {
            gravity = Gravity.END
            setPadding(0, dp(7), 0, 0)
        }
        gateCard.addView(gateState)

        root.addView(space(12))

        val activityCard = card()
        root.addView(activityCard)
        activityCard.addView(sectionTitle("Activity", "Live transport and virtual-headset status"))
        val statsRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val frames = statBox("FRAMES SENT")
        framesValue = frames.second
        val drops = statBox("DROPS")
        dropsValue = drops.second
        statsRow.addView(frames.first, LinearLayout.LayoutParams(0, dp(76), 1f).apply { marginEnd = dp(5) })
        statsRow.addView(drops.first, LinearLayout.LayoutParams(0, dp(76), 1f).apply { marginStart = dp(5) })
        activityCard.addView(statsRow)
        headsetValue = text("Headset: waiting for status", 12f, C.subtext).apply { setPadding(0, dp(10), 0, 0) }
        transportValue = text("Transport: disconnected", 12f, C.subtext).apply { setPadding(0, dp(4), 0, 0) }
        activityCard.addView(headsetValue)
        activityCard.addView(transportValue)

        root.addView(space(12))

        val diagCard = card()
        root.addView(diagCard)
        diagCard.addView(sectionTitle("Diagnostics & Safety", "Hook status, copyable log, and safe Xbox gateway restore"))

        val diagRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        retryHookButton = actionButton("RETRY HOOK", C.warnDim, C.warn)
        val report = actionButton("REPORT STATUS", C.panel2, C.border)
        diagRow.addView(retryHookButton, LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginEnd = dp(5) })
        diagRow.addView(report, LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginStart = dp(5) })
        diagCard.addView(diagRow)

        val restore = actionButton("RESTORE HOOKS (SAFE TO UNLOAD)", C.redDark, C.red)
        diagCard.addView(restore, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply { topMargin = dp(9) })

        val logHeader = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(12), 0, dp(6))
        }
        logHeader.addView(text("SESSION LOG", 11f, C.subtext, true), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        val copyLog = actionButton("COPY LOG", C.panel2, C.border)
        val clearLog = actionButton("CLEAR", C.panel2, C.border)
        logHeader.addView(copyLog, LinearLayout.LayoutParams(dp(96), dp(38)).apply { marginEnd = dp(5) })
        logHeader.addView(clearLog, LinearLayout.LayoutParams(dp(72), dp(38)))
        diagCard.addView(logHeader)

        logView = text("PcMic-360 Android ready.", 11f, C.subtext).apply {
            typeface = Typeface.MONOSPACE
            setPadding(dp(10), dp(10), dp(10), dp(10))
            isFocusable = false
            isFocusableInTouchMode = false
            setTextIsSelectable(false)
        }
        logScroll = ScrollView(this).apply {
            background = rounded(C.bg, 9f, C.border)
            isFillViewport = true
            descendantFocusability = ViewGroup.FOCUS_BLOCK_DESCENDANTS
            addView(logView, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        diagCard.addView(logScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(180)))

        copyLog.setOnClickListener {
            val body = logLines.joinToString("\n").ifBlank { "PcMic-360 Android ready." }
            val clipboard = getSystemService(ClipboardManager::class.java)
            clipboard.setPrimaryClip(ClipData.newPlainText("PcMic-360 session log", body))
            Toast.makeText(this, "PcMic-360 log copied", Toast.LENGTH_SHORT).show()
        }
        clearLog.setOnClickListener {
            logLines.clear()
            logView.text = "PcMic-360 log cleared."
        }

        retryHookButton.setOnClickListener { service?.retryHookInstall() }
        report.setOnClickListener { service?.reportStatus() }
        restore.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle("Restore Xbox hooks and end session?")
                .setMessage("PcMic-360 will stop Live Mic and ask the Xbox to restore the original voice gateways. The app will only show SAFE after the Xbox replies SAFE_TO_UNLOAD. This does not remove the XEX itself from memory; unload the XEX from your Xbox loader only after SAFE appears.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Restore") { _, _ -> service?.restoreAndSafeUnload() }
                .show()
        }

        connectButton.setOnClickListener {
            val current = service?.snapshot()
            if (current?.connected == true) {
                service?.disconnect()
            } else {
                val ip = ipEdit.text.toString().trim()
                prefs.edit().putString("xbox_ip", ip).apply()
                ipEdit.clearFocus()
                getSystemService(InputMethodManager::class.java)?.hideSoftInputFromWindow(ipEdit.windowToken, 0)
                service?.connect(ip)
            }
        }

        micDeviceButton.setOnClickListener { showMicPicker() }
        micRefreshButton.setOnClickListener {
            service?.refreshMicDevices()
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        }

        inputLevelSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                prefs.edit().putInt("input_level", progress).apply()
                service?.setInputLevel(progress)
                updateInputLevelLabel(progress)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        })

        gainSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val snapped = (progress / 5) * 5
                if (fromUser && snapped != progress) gainSeek.progress = snapped
                prefs.edit().putInt("mic_gain", snapped).apply()
                service?.setMicGain(snapped)
                updateGainLabel(snapped)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        })

        liveButton.setOnClickListener {
            val current = service?.snapshot() ?: return@setOnClickListener
            if (current.wantLive || current.live) {
                service?.stopLive()
            } else {
                if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 360)
                } else {
                    service?.startLive()
                }
            }
        }

        muteButton.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            service?.toggleMute()
        }

        holdButton.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    service?.setHold(true)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    service?.setHold(false)
                    true
                }
                else -> true
            }
        }

        gateSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val snapped = (progress / 50) * 50
                if (fromUser && snapped != progress) gateSeek.progress = snapped
                prefs.edit().putInt("gate", snapped).apply()
                service?.setGateThreshold(snapped)
                updateGateLabel(snapped)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        })

        updateInputLevelLabel(inputLevelSeek.progress)
        updateGainLabel(gainSeek.progress)
        updateGateLabel(gateSeek.progress)
        setContentView(pageScroll)
    }

    private fun render(state: UiState) {
        connectionPill.text = when {
            state.safeToUnload -> "SAFE"
            state.connected -> "ONLINE"
            state.statusText.contains("Connecting", true) || state.statusText.contains("Reconnecting", true) -> "WAITING"
            else -> "OFFLINE"
        }
        setPillColor(connectionPill, when {
            state.safeToUnload -> C.green
            state.connected -> C.green
            state.statusText.contains("ing", true) -> C.warn
            else -> C.muted
        })

        hookPill.text = when {
            state.installed -> "HOOK ✓"
            state.hookStatus.contains("fail", true) || state.hookStatus.contains("not confirmed", true) -> "HOOK !"
            state.hookStatus.contains("install", true) || state.hookStatus.contains("check", true) || state.hookStatus.contains("wait", true) -> "HOOK …"
            else -> "HOOK —"
        }
        setPillColor(hookPill, when {
            state.installed -> C.green
            state.hookStatus.contains("fail", true) || state.hookStatus.contains("not confirmed", true) -> C.red
            state.hookStatus.contains("install", true) || state.hookStatus.contains("check", true) || state.hookStatus.contains("wait", true) -> C.warn
            else -> C.muted
        })
        hookDetailValue.text = "Hook: ${state.hookStatus}"
        hookDetailValue.setTextColor(when {
            state.installed -> C.green
            state.hookStatus.contains("fail", true) || state.hookStatus.contains("not confirmed", true) -> C.red
            else -> C.subtext
        })

        lanePill.text = if (state.laneLocked) "LANE ${state.lane}" else "LANE —"
        setPillColor(lanePill, if (state.laneLocked) C.green else C.muted)

        connectButton.text = if (state.connected) "DISCONNECT" else "CONNECT"
        setButtonColor(connectButton, if (state.connected) C.redDark else C.accent, if (state.connected) C.red else null)
        ipEdit.isEnabled = !state.connected

        selectedMicValue.text = "Selected input: ${state.selectedMicDeviceName}"
        micDeviceButton.text = "SELECT MICROPHONE"
        micDeviceButton.isEnabled = !state.wantLive && !state.live
        micRefreshButton.isEnabled = !state.wantLive && !state.live
        micDeviceButton.alpha = if (micDeviceButton.isEnabled) 1f else 0.5f
        micRefreshButton.alpha = if (micRefreshButton.isEnabled) 1f else 0.5f

        if (inputLevelSeek.progress != state.inputLevelPercent) inputLevelSeek.progress = state.inputLevelPercent
        if (gainSeek.progress != state.micGainPercent) gainSeek.progress = state.micGainPercent
        updateInputLevelLabel(state.inputLevelPercent)
        updateGainLabel(state.micGainPercent)

        retryHookButton.isEnabled = state.connected && state.bannerSeen && !state.installed && !state.safeToUnload
        retryHookButton.alpha = if (retryHookButton.isEnabled) 1f else 0.45f

        liveButton.isEnabled = state.connected && state.installed && !state.safeToUnload
        liveButton.alpha = if (liveButton.isEnabled) 1f else 0.45f
        liveButton.text = if (state.wantLive || state.live) "STOP LIVE MIC" else "START LIVE MIC"
        setButtonColor(liveButton, if (state.wantLive || state.live) C.redDark else C.green, null)

        muteButton.text = if (state.baseMuted) "UNMUTE" else "MUTE"
        setButtonColor(muteButton, if (state.baseMuted) C.greenDark else C.red, null)
        holdButton.text = when {
            state.holdActive && state.effectiveMuted -> "TEMP MUTED"
            state.holdActive && !state.effectiveMuted -> "TEMP UNMUTED"
            state.baseMuted -> "HOLD TO UNMUTE"
            else -> "HOLD TO MUTE"
        }

        if (gateSeek.progress != state.gateThreshold) gateSeek.progress = state.gateThreshold
        updateGateLabel(state.gateThreshold)
        levelMeter.setLevel(state.rms, state.gateThreshold)
        val liveRms = state.rms.roundToInt().coerceAtLeast(0)
        gateState.text = when {
            state.effectiveMuted -> "MUTED • RMS $liveRms"
            state.gateThreshold <= 0 -> "GATE OFF • RMS $liveRms"
            state.gateOpen -> "GATE OPEN • RMS $liveRms"
            else -> "GATE CLOSED • RMS $liveRms"
        }
        gateState.setTextColor(when {
            state.effectiveMuted -> C.red
            state.gateOpen -> C.green
            else -> C.subtext
        })

        framesValue.text = state.framesSent.toString()
        dropsValue.text = state.drops.toString()
        headsetValue.text = "Physical headset: ${state.physicalHeadset}    Virtual headset: ${state.virtualHeadset}"
        transportValue.text = "Transport: ${state.statusText}    PCM: ${state.packetBytes}    Mic: ${state.routedMicDeviceName}"

        if (state.safeToUnload && !lastSafeShown) {
            lastSafeShown = true
            AlertDialog.Builder(this)
                .setTitle("HOOKS RESTORED")
                .setMessage("The original Xbox voice gateways were restored and the Android mic/network session ended. PcMic-360.xex is now safe to unload from your Xbox loader.")
                .setPositiveButton("OK", null)
                .show()
        } else if (!state.safeToUnload) {
            lastSafeShown = false
        }
    }

    private fun showMicPicker() {
        val state = service?.snapshot() ?: return
        if (state.wantLive || state.live) {
            AlertDialog.Builder(this)
                .setTitle("Stop Live Mic first")
                .setMessage("Stop Live Mic before changing the Android input device.")
                .setPositiveButton("OK", null)
                .show()
            return
        }
        service?.refreshMicDevices()
        val refreshed = service?.snapshot() ?: state
        val devices = refreshed.micDevices
        val labels = devices.map { it.name }.toTypedArray()
        val checked = devices.indexOfFirst { it.id == refreshed.selectedMicDeviceId }.coerceAtLeast(0)
        AlertDialog.Builder(this)
            .setTitle("Microphone input")
            .setSingleChoiceItems(labels, checked) { dialog, which ->
                val chosen = devices.getOrNull(which) ?: return@setSingleChoiceItems
                if (service?.selectMicDevice(chosen.id) == true) {
                    prefs.edit().putInt("mic_device_id", chosen.id).apply()
                }
                dialog.dismiss()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 360) {
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                service?.startLive()
            } else {
                AlertDialog.Builder(this)
                    .setTitle("Microphone permission needed")
                    .setMessage("PcMic-360 only needs microphone access to stream the input you choose directly to your Xbox. No other runtime permissions are requested.")
                    .setPositiveButton("OK", null)
                    .show()
            }
        }
    }

    private fun updateInputLevelLabel(value: Int) {
        inputLevelValue.text = "$value%"
    }

    private fun updateGainLabel(value: Int) {
        gainValue.text = String.format(Locale.US, "%.2f×", value / 100.0)
    }

    private fun updateGateLabel(value: Int) {
        gateValue.text = if (value <= 0) "GATE OFF" else "RMS $value"
        gateValue.setTextColor(if (value <= 0) C.subtext else C.warn)
    }

    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(15), dp(14), dp(15), dp(15))
        background = rounded(C.panel, 14f, C.border)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    }

    private fun sectionTitle(title: String, subtitle: String): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        addView(text(title, 16f, C.text, true))
        addView(text(subtitle, 11f, C.subtext).apply { setPadding(0, dp(2), 0, dp(11)) })
    }

    private fun text(value: String, size: Float, color: Int, bold: Boolean = false): TextView = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(color)
        if (bold) setTypeface(typeface, Typeface.BOLD)
    }

    private fun pill(value: String, color: Int, backgroundColor: Int): TextView = text(value, 10f, color, true).apply {
        gravity = Gravity.CENTER
        background = rounded(backgroundColor, 10f, C.border)
        isAllCaps = true
    }

    private fun setPillColor(view: TextView, color: Int) {
        view.setTextColor(color)
        view.background = rounded(C.panel2, 10f, C.border)
    }

    private fun actionButton(value: String, fill: Int, stroke: Int? = null): Button = Button(this).apply {
        text = value
        textSize = 12f
        setTextColor(C.text)
        setTypeface(typeface, Typeface.BOLD)
        isAllCaps = false
        background = rounded(fill, 11f, stroke)
        setPadding(dp(8), 0, dp(8), 0)
    }

    private fun setButtonColor(button: Button, fill: Int, stroke: Int?) {
        button.background = rounded(fill, 11f, stroke)
    }

    private fun statBox(label: String): Pair<LinearLayout, TextView> {
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            background = rounded(C.panel2, 10f, C.border)
        }
        val value = text("0", 20f, C.text, true).apply { gravity = Gravity.CENTER }
        box.addView(value)
        box.addView(text(label, 9f, C.subtext, true).apply { gravity = Gravity.CENTER })
        return box to value
    }

    private fun rounded(fill: Int, radiusDp: Float, stroke: Int? = null): GradientDrawable = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        setColor(fill)
        cornerRadius = dpF(radiusDp)
        if (stroke != null) setStroke(dp(1), stroke)
    }

    private fun space(heightDp: Int): View = Space(this).apply {
        layoutParams = LinearLayout.LayoutParams(1, dp(heightDp))
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).roundToInt()
    private fun dpF(value: Float): Float = value * resources.displayMetrics.density

    private object C {
        val bg = Color.rgb(11, 17, 27)
        val panel = Color.rgb(20, 28, 40)
        val panel2 = Color.rgb(24, 34, 49)
        val border = Color.rgb(38, 51, 71)
        val text = Color.rgb(242, 246, 252)
        val subtext = Color.rgb(152, 166, 184)
        val muted = Color.rgb(113, 128, 150)
        val accent = Color.rgb(59, 130, 246)
        val green = Color.rgb(55, 200, 138)
        val greenDark = Color.rgb(29, 93, 72)
        val warn = Color.rgb(240, 184, 75)
        val warnDim = Color.rgb(58, 49, 28)
        val red = Color.rgb(231, 90, 99)
        val redDark = Color.rgb(106, 39, 48)
    }
}
