package com.pcmic360.app

/**
 * Pure state machine for the PcMic-360 automatic hook-install handshake.
 * It deliberately has no Android dependencies so the protocol flow can be
 * unit-tested on a normal JVM.
 */
class HookInstallCoordinator(private val maxAttempts: Int = 2) {
    data class Decision(
        val sendStatus: Boolean = false,
        val sendInstall: Boolean = false,
        val hookStatus: String? = null
    )

    private var protocolSeen = false
    private var installed = false
    private var installInFlight = false
    private var waitingForStatus = false
    private var attempts = 0
    private var failed = false

    @Synchronized
    fun resetSession() {
        protocolSeen = false
        installed = false
        installInFlight = false
        waitingForStatus = false
        attempts = 0
        failed = false
    }

    @Synchronized
    fun onProtocolSeen(): Decision {
        protocolSeen = true
        if (installed) return Decision(hookStatus = "Installed")
        if (installInFlight) return Decision(hookStatus = "Installing…")
        if (waitingForStatus) return Decision(hookStatus = "Checking hook state…")
        failed = false
        attempts = 0
        waitingForStatus = true
        return Decision(sendStatus = true, hookStatus = "Checking hook state…")
    }

    @Synchronized
    fun onStatus(isInstalled: Boolean): Decision {
        if (isInstalled) {
            installed = true
            installInFlight = false
            waitingForStatus = false
            failed = false
            return Decision(hookStatus = "Installed")
        }
        installed = false
        if (!protocolSeen || installInFlight || failed) return Decision()
        if (waitingForStatus || attempts == 0) {
            waitingForStatus = false
            return requestInstallLocked()
        }
        return Decision()
    }

    @Synchronized
    fun onStatusWaitTimeout(): Decision {
        if (!protocolSeen || installed || installInFlight || failed || !waitingForStatus) return Decision()
        waitingForStatus = false
        return requestInstallLocked()
    }

    @Synchronized
    fun onInstallResult(ok: Boolean): Decision {
        installInFlight = false
        if (ok) {
            installed = true
            waitingForStatus = false
            failed = false
            return Decision(hookStatus = "Installed")
        }
        installed = false
        if (attempts >= maxAttempts) {
            failed = true
            waitingForStatus = false
            return Decision(hookStatus = "Install failed")
        }
        waitingForStatus = true
        return Decision(sendStatus = true, hookStatus = "Install failed — checking state…")
    }

    @Synchronized
    fun onInstallTimeout(): Decision {
        if (!installInFlight || installed) return Decision()
        installInFlight = false
        if (attempts >= maxAttempts) {
            // Do one final status query. A slow resolver may have completed but
            // its V700_INSTALL line could have been missed during a reconnect.
            waitingForStatus = true
            return Decision(sendStatus = true, hookStatus = "Install confirmation late — checking…")
        }
        waitingForStatus = true
        return Decision(sendStatus = true, hookStatus = "Install taking longer — checking…")
    }

    @Synchronized
    fun onFinalStatusTimeout(): Decision {
        if (!waitingForStatus || installed || installInFlight) return Decision()
        if (attempts >= maxAttempts) {
            failed = true
            waitingForStatus = false
            return Decision(hookStatus = "Install not confirmed")
        }
        waitingForStatus = false
        return requestInstallLocked()
    }

    @Synchronized
    fun manualRetry(): Decision {
        if (!protocolSeen || installed || installInFlight) return Decision()
        failed = false
        attempts = 0
        waitingForStatus = true
        return Decision(sendStatus = true, hookStatus = "Retrying hook check…")
    }

    @Synchronized fun isInstalled(): Boolean = installed
    @Synchronized fun isInstallInFlight(): Boolean = installInFlight
    @Synchronized fun isWaitingForStatus(): Boolean = waitingForStatus
    @Synchronized fun attemptCount(): Int = attempts
    @Synchronized fun hasFailed(): Boolean = failed

    private fun requestInstallLocked(): Decision {
        if (attempts >= maxAttempts) {
            failed = true
            return Decision(hookStatus = "Install not confirmed")
        }
        attempts++
        installInFlight = true
        waitingForStatus = false
        return Decision(
            sendInstall = true,
            hookStatus = "Installing hook ($attempts/$maxAttempts)…"
        )
    }
}
