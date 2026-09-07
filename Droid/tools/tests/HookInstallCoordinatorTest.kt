import com.pcmic360.app.HookInstallCoordinator

private fun requireDecision(value: Boolean, message: String) {
    if (!value) error(message)
}

fun main() {
    // Fresh session: HELLO -> P -> status=0 -> O -> install=1.
    run {
        val h = HookInstallCoordinator()
        h.resetSession()
        var d = h.onProtocolSeen()
        requireDecision(d.sendStatus, "HELLO must request status")
        d = h.onStatus(false)
        requireDecision(d.sendInstall && h.attemptCount() == 1, "status=0 must install")
        d = h.onInstallResult(true)
        requireDecision(h.isInstalled() && d.hookStatus == "Installed", "install=1 must latch")
    }

    // Reconnect to an already-installed XEX must not send O.
    run {
        val h = HookInstallCoordinator()
        h.resetSession()
        requireDecision(h.onProtocolSeen().sendStatus, "reconnect must status-check")
        val d = h.onStatus(true)
        requireDecision(h.isInstalled() && !d.sendInstall, "installed status must skip O")
    }

    // If no V700_STATUS arrives, fallback still sends O.
    run {
        val h = HookInstallCoordinator()
        h.resetSession()
        h.onProtocolSeen()
        val d = h.onStatusWaitTimeout()
        requireDecision(d.sendInstall && h.attemptCount() == 1, "status timeout must install")
    }

    // Slow/failing first install: timeout -> P -> status=0 -> one retry -> success.
    run {
        val h = HookInstallCoordinator()
        h.resetSession()
        h.onProtocolSeen()
        h.onStatus(false)
        var d = h.onInstallTimeout()
        requireDecision(d.sendStatus, "install timeout must re-check status")
        d = h.onStatus(false)
        requireDecision(d.sendInstall && h.attemptCount() == 2, "second attempt must be controlled")
        h.onInstallResult(true)
        requireDecision(h.isInstalled(), "second attempt should latch success")
    }

    // Two failed installs must stop retrying forever.
    run {
        val h = HookInstallCoordinator()
        h.resetSession()
        h.onProtocolSeen()
        h.onStatus(false)
        requireDecision(h.onInstallResult(false).sendStatus, "first failure must status-check")
        requireDecision(h.onStatus(false).sendInstall, "first failure may retry once")
        val d = h.onInstallResult(false)
        requireDecision(h.hasFailed() && !d.sendInstall, "second failure must stop")
    }

    println("HookInstallCoordinator: ALL TESTS PASSED")
}
