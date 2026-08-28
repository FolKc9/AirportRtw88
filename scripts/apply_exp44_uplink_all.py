#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_exp44_uplink_all.py <AirportRtw88-root> <rtw88-stable-root>")
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

# 1) Do not leave protected data permanently pinned to the 6 Mbps bootstrap rate.
p = root / "src/kext/RTW88IEEE80211.cpp"
replace_once(
    p,
    "    /* Keys and the BSD link are now valid.  Begin uplink BlockAck\n     * negotiation; output remains immediate/non-A-MPDU until the AP accepts. */\n    startTxAggregation();",
    "    /* EXP44: bootstrap at 6 Mbps is only for association/EAPOL reliability.\n     * Once the link is fully connected hand data TX back to firmware RA so\n     * VHT/80 MHz, fallback and A-MPDU can operate normally. */\n    if (_hw) rtw88_set_fix_rate(_hw, 0xff);\n\n    /* Keys and the BSD link are now valid. Begin uplink BlockAck negotiation. */\n    startTxAggregation();",
    "release 6 Mbps bootstrap after connect",
)

# Retry uplink ADDBA from the connected watchdog until the AP accepts it.
replace_once(
    p,
    "void RTW88IEEE80211::armConnectedWatchdog()\n{\n    if (_timer && _state == RTW88_STATE_CONNECTED)\n        _timer->setTimeoutMS(4000);\n}",
    "void RTW88IEEE80211::armConnectedWatchdog()\n{\n    if (_state == RTW88_STATE_CONNECTED && !_txBaActive)\n        startTxAggregation();\n    if (_timer && _state == RTW88_STATE_CONNECTED)\n        _timer->setTimeoutMS(4000);\n}",
    "retry TX ADDBA while connected",
)

# 2) Correct pull-model flow control: check resources BEFORE dequeue.
p = root / "src/kext/RTW88PCIDevice.cpp"
old = '''    uint32_t drained = 0;\n    mbuf_t packet = nullptr;\n    while (drained < 32 &&\n           iface->dequeueOutputPackets(1, &packet) == kIOReturnSuccess) {\n        /* configureOutputPullModel requested work-loop synchronization, so\n         * outputStart already runs under the same gate used for PCIe work.\n         * Enqueuing into the legacy IOGatedOutputQueue here creates a second\n         * stopped queue on IO80211 and strands DHCP before it reaches TX. */\n        UInt32 ret = outputPacket(packet, nullptr);\n        if (ret != kIOReturnOutputSuccess)\n            return kIOReturnNoResources;\n        drained++;\n        packet = nullptr;\n    }'''
new = '''    uint32_t drained = 0;\n    mbuf_t packet = nullptr;\n    while (drained < 64) {\n        /* EXP44: pull-model contract requires checking hardware resources\n         * before dequeueOutputPackets(). Once dequeued, the mbuf belongs to\n         * the driver and cannot be abandoned on a later OutputStall. */\n        if (rtw88_be_tx_avail() < kRTW88TxBulkStallAvail) {\n            flushTxBatch();\n            if (!_txStalled) noteTxStall();\n            _txStalled = true;\n            return kIOReturnNoResources;\n        }\n        if (iface->dequeueOutputPackets(1, &packet) != kIOReturnSuccess)\n            break;\n        UInt32 ret = outputPacket(packet, nullptr);\n        if (ret != kIOReturnOutputSuccess) {\n            /* With the pre-dequeue resource check this should only be a real\n             * transmit failure, never normal ring backpressure. */\n            if (ret != kIOReturnOutputDropped)\n                freePacket(packet);\n            return kIOReturnNoResources;\n        }\n        drained++;\n        packet = nullptr;\n    }'''
replace_once(p, old, new, "pull-model pre-dequeue backpressure")

# In pull mode the interface output thread owns retry scheduling; do not service
# the legacy IOGatedOutputQueue after reclaim.
replace_once(
    p,
    '''    if (_txStalled && rtw88_be_tx_avail() >= kRTW88TxResumeAvail) {\n        _txStalled = false;\n        if (_txQueue)\n            _txQueue->service(IOBasicOutputQueue::kServiceAsync);\n    }''',
    '''    if (_txStalled && rtw88_be_tx_avail() >= kRTW88TxResumeAvail) {\n        _txStalled = false;\n        /* EXP44: outputStart() is the active IO80211 pull path. Returning\n         * kIOReturnNoResources makes its output thread retry; servicing the\n         * separate legacy IOGatedOutputQueue here wakes the wrong queue. */\n    }''',
    "remove wrong-queue TX resume",
)

# Publish a realistic 1x1 VHT80 medium ceiling instead of legacy 54 Mbps.
replace_once(
    p,
    "    addMedium(mediums, kIOMediumIEEE80211, 54);",
    "    addMedium(mediums, kIOMediumIEEE80211, 433);",
    "publish VHT link medium",
)

# 3) Restore actual host-deferred A-MPDU doorbell batching in upstream tx.c.
p = up / "drivers/net/wireless/realtek/rtw88/tx.c"
s = p.read_text()
if "EXP44 host-deferred AMPDU kick" not in s:
    decl = "\tstruct rtw_tx_pkt_info pkt_info = {0};"
    if decl not in s:
        raise SystemExit("ERROR: rtw_tx pkt_info anchor missing")
    s = s.replace(decl, decl + "\n#ifdef RTW88_MACOS\n\tbool exp44_defer_kick = false;\n#endif", 1)
    anchor = "\trtw88_exp7_tx_diag_record(&pkt_info);"
    if anchor not in s:
        raise SystemExit("ERROR: TX diag anchor missing")
    s = s.replace(anchor, anchor + "\n#ifdef RTW88_MACOS\n\t/* EXP44 host-deferred AMPDU kick: only aggregated data is batched. */\n\texp44_defer_kick = pkt_info.ampdu_en;\n#endif", 1)
    kick = "\trtw_hci_tx_kick_off(rtwdev);"
    pos = s.find(kick, s.find("void rtw_tx("))
    if pos < 0:
        raise SystemExit("ERROR: rtw_tx kick anchor missing")
    repl = "#ifdef RTW88_MACOS\n\tif (!exp44_defer_kick)\n\t\trtw_hci_tx_kick_off(rtwdev);\n#else\n\trtw_hci_tx_kick_off(rtwdev);\n#endif"
    s = s[:pos] + repl + s[pos+len(kick):]
    p.write_text(s)
    print("restore true AMPDU doorbell batching: applied")
else:
    print("restore true AMPDU doorbell batching: already applied")

# 4) Remove the permanent 6 Mbps pin from channel restore too. Keep the initial
# bootstrap assignment; markConnected() above releases it after secure connect.
p = root / "src/compat/rtw88_compat.c"
replace_once(
    p,
    "    /* Keep restore/resume behaviour identical to the initial connection. */\n    rtwdev->dm_info.fix_rate = DESC_RATE6M;",
    "    /* EXP44: restored links stay under firmware RA; only a fresh connect\n     * uses the temporary 6 Mbps bootstrap before markConnected() releases it. */\n    rtwdev->dm_info.fix_rate = 0xff;",
    "keep firmware RA after channel restore",
)

# Version/flavor marker for the test artifact.
plist = root / "AirportRtw88Legacy.kext/Contents/Info.plist"
s = plist.read_text()
s = s.replace("1.4.10-exp40", "1.5.0-exp44-uplink-all", 1)
s = s.replace("1.4.10", "1.5.0", 1)
plist.write_text(s)
print("version marker: 1.5.0-exp44-uplink-all")
