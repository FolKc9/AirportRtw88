#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_exp44b_uplink_safe.py <AirportRtw88-root> <rtw88-stable-root>")
root = Path(sys.argv[1])
up = Path(sys.argv[2])


def replace_once(path: Path, old: str, new: str, label: str):
    s = path.read_text()
    if new in s:
        print(f"{label}: already applied")
        return
    if old not in s:
        raise SystemExit(f"ERROR: anchor not found for {label}: {path}")
    path.write_text(s.replace(old, new, 1))
    print(f"{label}: applied")

# Keep the AirPort lifecycle, POWER handlers, link-state notifications,
# medium dictionary and output-queue resume behavior exactly as EXP40.
# Only touch data-path behavior after a link is fully connected.

# 1) Release temporary 6 Mbps bootstrap after secure association.
p = root / "src/kext/RTW88IEEE80211.cpp"
replace_once(
    p,
    "    /* Keys and the BSD link are now valid.  Begin uplink BlockAck\n     * negotiation; output remains immediate/non-A-MPDU until the AP accepts. */\n    startTxAggregation();",
    "    /* EXP44b: preserve EXP40 AirPort lifecycle; only release the temporary\n     * 6 Mbps protected-data bootstrap once the link is fully connected. */\n    if (_hw) rtw88_set_fix_rate(_hw, 0xff);\n\n    /* Keys and the BSD link are now valid. Begin uplink BlockAck negotiation. */\n    startTxAggregation();",
    "release 6 Mbps bootstrap after connect",
)

# Retry TX ADDBA from the existing connected watchdog without touching power/link state.
replace_once(
    p,
    "void RTW88IEEE80211::armConnectedWatchdog()\n{\n    if (_timer && _state == RTW88_STATE_CONNECTED)\n        _timer->setTimeoutMS(4000);\n}",
    "void RTW88IEEE80211::armConnectedWatchdog()\n{\n    if (_state == RTW88_STATE_CONNECTED && !_txBaActive)\n        startTxAggregation();\n    if (_timer && _state == RTW88_STATE_CONNECTED)\n        _timer->setTimeoutMS(4000);\n}",
    "retry TX ADDBA while connected",
)

# 2) Pull-model safety: check resources before dequeue, but preserve the original
# EXP40 queue-resume path and all interface lifecycle behavior.
p = root / "src/kext/RTW88PCIDevice.cpp"
old = '''    uint32_t drained = 0;\n    mbuf_t packet = nullptr;\n    while (drained < 32 &&\n           iface->dequeueOutputPackets(1, &packet) == kIOReturnSuccess) {\n        /* configureOutputPullModel requested work-loop synchronization, so\n         * outputStart already runs under the same gate used for PCIe work.\n         * Enqueuing into the legacy IOGatedOutputQueue here creates a second\n         * stopped queue on IO80211 and strands DHCP before it reaches TX. */\n        UInt32 ret = outputPacket(packet, nullptr);\n        if (ret != kIOReturnOutputSuccess)\n            return kIOReturnNoResources;\n        drained++;\n        packet = nullptr;\n    }'''
new = '''    uint32_t drained = 0;\n    mbuf_t packet = nullptr;\n    while (drained < 32) {\n        /* EXP44b: never dequeue an mbuf unless the BE ring has room. Keep the\n         * EXP40 output-thread/queue lifecycle otherwise unchanged. */\n        if (rtw88_be_tx_avail() < kRTW88TxBulkStallAvail) {\n            flushTxBatch();\n            if (!_txStalled) noteTxStall();\n            _txStalled = true;\n            return kIOReturnNoResources;\n        }\n        if (iface->dequeueOutputPackets(1, &packet) != kIOReturnSuccess)\n            break;\n        UInt32 ret = outputPacket(packet, nullptr);\n        if (ret != kIOReturnOutputSuccess) {\n            if (ret != kIOReturnOutputDropped)\n                freePacket(packet);\n            return kIOReturnNoResources;\n        }\n        drained++;\n        packet = nullptr;\n    }'''
replace_once(p, old, new, "pull-model pre-dequeue backpressure")

# IMPORTANT: do NOT modify resumeTxIfStalled(), setupMediumDict(), enable(),
# disable(), notifyAirportLinkIdle/Up/Down(), APPLE80211_IOC_POWER, or link state.

# 3) Restore true host-deferred A-MPDU doorbell batching only for aggregate-eligible data.
p = up / "drivers/net/wireless/realtek/rtw88/tx.c"
s = p.read_text()
if "EXP44b host-deferred AMPDU kick" not in s:
    decl = "\tstruct rtw_tx_pkt_info pkt_info = {0};"
    if decl not in s:
        raise SystemExit("ERROR: rtw_tx pkt_info anchor missing")
    s = s.replace(decl, decl + "\n#ifdef RTW88_MACOS\n\tbool exp44b_defer_kick = false;\n#endif", 1)
    anchor = "\trtw88_exp7_tx_diag_record(&pkt_info);"
    if anchor not in s:
        raise SystemExit("ERROR: TX diag anchor missing")
    s = s.replace(anchor, anchor + "\n#ifdef RTW88_MACOS\n\t/* EXP44b host-deferred AMPDU kick: management/EAPOL/pre-BA stays immediate. */\n\texp44b_defer_kick = pkt_info.ampdu_en;\n#endif", 1)
    kick = "\trtw_hci_tx_kick_off(rtwdev);"
    pos = s.find(kick, s.find("void rtw_tx("))
    if pos < 0:
        raise SystemExit("ERROR: rtw_tx kick anchor missing")
    repl = "#ifdef RTW88_MACOS\n\tif (!exp44b_defer_kick)\n\t\trtw_hci_tx_kick_off(rtwdev);\n#else\n\trtw_hci_tx_kick_off(rtwdev);\n#endif"
    s = s[:pos] + repl + s[pos+len(kick):]
    p.write_text(s)
    print("restore true AMPDU doorbell batching: applied")
else:
    print("restore true AMPDU doorbell batching: already applied")

# 4) Restored connected channel remains on firmware RA, not legacy 6 Mbps.
p = root / "src/compat/rtw88_compat.c"
replace_once(
    p,
    "    /* Keep restore/resume behaviour identical to the initial connection. */\n    rtwdev->dm_info.fix_rate = DESC_RATE6M;",
    "    /* EXP44b: preserve automatic firmware RA on an already-established link. */\n    rtwdev->dm_info.fix_rate = 0xff;",
    "keep firmware RA after channel restore",
)

# Version marker only; no interface capability/medium changes.
plist = root / "AirportRtw88Legacy.kext/Contents/Info.plist"
s = plist.read_text()
s = s.replace("1.4.10-exp40", "1.5.1-exp44b-uplink-safe", 1)
s = s.replace("1.4.10", "1.5.1", 1)
plist.write_text(s)
print("version marker: 1.5.1-exp44b-uplink-safe")
