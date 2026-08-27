/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * RTW88IEEE80211.hpp — 802.11 state machine for rtw88 macOS port.
 *
 * Responsibilities:
 *  - Drives the Linux rtw88 driver (rtw_core_start/stop, rtw_tx, etc.)
 *  - Manages scan, authenticate, associate, 4-way handshake
 *  - Converts between mbuf_t and sk_buff for the driver
 *  - Delivers decrypted data frames as Ethernet to RTW88PCIDevice
 *  - Accepts Ethernet output frames and wraps them as 802.11 data frames
 */
#pragma once

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLocks.h>
#include <sys/mbuf.h>
#include <net/ethernet.h>
#include <kern/thread_call.h>

/* Opaque C driver handle */
struct rtw_dev;
struct pci_dev;
struct ieee80211_hw;
struct ieee80211_vif;
struct ieee80211_sta;
struct ieee80211_channel;
struct sk_buff;

class RTW88PCIDevice;

/* ------------------------------------------------------------------ */
/*  BSS descriptor (scan result)                                        */
/* ------------------------------------------------------------------ */
struct RTW88BSS {
    char   ssid[33];
    uint8_t ssid_len;
    uint8_t bssid[6];
    int16_t rssi;
    uint16_t freq;
    uint8_t  channel;
    uint32_t capabilities;
    uint16_t beacon_interval;
    uint8_t  rates[15];
    uint8_t  nrates;
    uint32_t cipher;       /* selected pairwise WLAN_CIPHER_SUITE_* */
    uint32_t group_cipher; /* selected group WLAN_CIPHER_SUITE_* */
    uint32_t akm;
    uint32_t last_seen_scan;
    /* Raw IE data for association */
    uint8_t  ies[512];
    uint16_t ies_len;
    RTW88BSS *next;
};

/* ------------------------------------------------------------------ */
/*  Connection state                                                     */
/* ------------------------------------------------------------------ */
enum RTW88State {
    RTW88_STATE_IDLE = 0,
    RTW88_STATE_SCANNING,
    RTW88_STATE_AUTHENTICATING,
    RTW88_STATE_ASSOCIATING,
    RTW88_STATE_HANDSHAKING,
    RTW88_STATE_CONNECTED,
    RTW88_STATE_DISCONNECTING,
};

/* ------------------------------------------------------------------ */
/*  RTW88IEEE80211                                                       */
/* ------------------------------------------------------------------ */
class RTW88IEEE80211 : public OSObject {
    OSDeclareDefaultStructors(RTW88IEEE80211)

public:
    static RTW88IEEE80211 *create(RTW88PCIDevice *dev, struct pci_dev *pci);

    bool      init(RTW88PCIDevice *dev, struct pci_dev *pci);
    void      free() override;

    /* Called by RTW88PCIDevice */
    IOReturn  start();       /* probe: chip info, efuse, register hw */
    void      stop();        /* full teardown */
    IOReturn  powerOn();     /* enable: rtw_core_start */
    void      powerOff();    /* disable: rtw_core_stop */
    void      handleInterrupt();
    UInt32    outputPacket(mbuf_t m);
    void      getMACAddress(uint8_t *mac);
    bool      dataTxBatchingActive() const;
    void      kickDataTx();

    /* Called from compat layer (ieee80211_rx_irqsafe) */
    void      rxFrame(struct sk_buff *skb);
    void      txStatus(struct sk_buff *skb);
    void      scanDone(bool aborted);

    /* Control interface — called from RTW88UserClient */
    IOReturn  cmdScan();
    IOReturn  cmdConnect(const char *ssid, const char *password);
    IOReturn  cmdConnectWithPMK(const char *ssid, const uint8_t pmk[32]);
    IOReturn  cmdDisconnect();
    IOReturn  cmdPowerOn();
    IOReturn  cmdPowerOff();
    IOReturn  cmdGetState(struct RTW88StateResult *result);
    IOReturn  cmdGetBSSList(uint8_t *buf, uint32_t *len);
    IOReturn  cmdGetAirportBSSList(RTW88BSS *records, uint32_t *count);
    IOReturn  cmdGetRSSI(int *rssi);
    IOReturn  cmdGetPerf(struct RTW88PerfResult *result);
    IOReturn  cmdSetTxRate(uint8_t rate);

private:
    IOReturn  cmdConnectInternal(const char *ssid, const char *password,
                                 const uint8_t *pmk);
    /* State machine internals */
    void      processRxMgmt(struct sk_buff *skb);
    void      processRxData(struct sk_buff *skb);
    void      processScanResult(struct sk_buff *skb);
    void      processAssocResponse(struct sk_buff *skb);

    void      doAuthenticate();
    void      doAssociate();
    void      setConnectedChandef(struct ieee80211_channel *chan);
    void      doHandshake(const uint8_t *eapol, uint32_t len);
    void      doDisconnect();
    void      handleLinkLoss(const char *reason, bool watchdog, uint16_t apReason);
    void      scheduleReconnect(const char *reason);
    void      runConnectWork();
    void      markConnected();
    void      armConnectedWatchdog();
    void      clearKeys();
    void      releaseSta();
    bool      abortActiveScan(bool waitForIdle);
    void      restoreConnectedChannel();
    bool      installKey(struct ieee80211_key_conf **slot, bool pairwise,
                         uint8_t keyidx, uint32_t cipher,
                         const uint8_t *tk, uint8_t tk_len);

    bool      buildAssocReq(uint8_t *buf, uint32_t *len);
    bool      buildAuthReq(uint8_t *buf, uint32_t *len);

    /* WPA2 4-way handshake */
    void      handleEAPOL(const uint8_t *data, uint32_t len);
    bool      deriveKeys(const uint8_t *anonce, const uint8_t *snonce);
    void      sendEAPOLKey(int step, const uint8_t *replay_counter,
                            bool install, bool ack, bool mic);

    /* A-MPDU BlockAck (aggregation) negotiation */
    bool      htAllowed() const;   /* HT/VHT/A-MPDU usable on this link? */
    void      startTxAggregation();
    void      sendAddbaRequest(uint8_t tid);
    void      sendAddbaResponse(uint8_t tid, uint8_t dialog,
                                uint16_t req_param, uint16_t ba_timeout,
                                uint16_t status);
    void      handleBackAction(const uint8_t *b, uint32_t len);

    /* RX A-MPDU reorder + delivery */
    void      deliverDataFrame(struct sk_buff *skb);   /* strip 802.11, inject */
    bool      decryptRxCcmp(struct sk_buff *skb);
    bool      acceptRxCcmpPn(bool group, uint8_t tid, uint64_t pn);
    void      deAmsdu(const uint8_t *data, uint32_t len);
    void      deliverEthernet(const uint8_t *da, const uint8_t *sa,
                              uint16_t ethertype,
                              const uint8_t *payload, uint32_t paylen);
    void      rxBaSetup(uint8_t tid, uint16_t ssn, uint16_t bufsize);
    void      rxBaTeardown(uint8_t tid);
    void      rxBaTeardownAll();
    void      rxReorderInput(uint8_t tid, struct sk_buff *skb, uint16_t sn);
    void      rxReorderArmTimer();
    void      rxReorderFlushStale();
    static void reorderTimerFired(OSObject *owner, IOTimerEventSource *t);

    /* Frame transmission helpers */
    bool      txMgmtFrame(const uint8_t *frame, uint32_t len);
    bool      txNullFunc(bool powerSave);
    bool      txProbeRequest();
    bool      txDataFrame(mbuf_t m);
    struct sk_buff *mbufToSkb(mbuf_t m);
    mbuf_t    skbToMbuf(struct sk_buff *skb);

    /* Driver callbacks installed in rtw_dev */
    static void compat_rx_frame(void *kext_hw, struct sk_buff *skb);
    static void compat_tx_status(void *kext_hw, struct sk_buff *skb);
    static void compat_scan_done(void *kext_hw, bool aborted);

    /* Timer callback for state machine timeouts */
    static void timerFired(OSObject *owner, IOTimerEventSource *timer);
    void        onTimer();

    /* Connect thread_call — runs doAuthenticate off the IOUserClient thread */
    static void connectTCFn(thread_call_param_t self, thread_call_param_t);
    thread_call_t _connectTC = nullptr;

    /* exp10: self-healing association state.  The original port left the
     * driver's STA/BSS association live after an AP deauth/disassoc, so a
     * subsequent connect inherited stale state until the user power-cycled
     * Wi-Fi.  Keep the last requested network and recover it automatically. */
    bool              _autoReconnectEnabled = false;
    bool              _reconnectInProgress = false;
    bool              _reconnectNeedsRadioReset = false;
    bool              _radioRecoveryUsed = false;
    uint32_t          _reconnectAttemptStreak = 0;
    volatile uint64_t _linkLossEvents = 0;
    volatile uint64_t _autoReconnectAttempts = 0;
    volatile uint64_t _autoReconnectSuccesses = 0;
    volatile uint64_t _radioRecoveryCount = 0;
    volatile uint64_t _beaconWatchdogRecoveries = 0;
    uint16_t          _lastApDisconnectReason = 0;

    /* Connected-link watchdog.  It only becomes authoritative after at least
     * one target beacon/probe response has been observed on this association,
     * avoiding false recoveries on firmware configurations that filter
     * beacons entirely. */
    volatile uint64_t _targetBeaconCount = 0;
    uint64_t          _lastTargetBeaconCount = 0;
    uint8_t           _beaconMissWindows = 0;
    bool              _targetBeaconEverSeen = false;

    /* Manual passive scan fallback for chips/firmware without scan offload */
    static void manualScanTCFn(thread_call_param_t self, thread_call_param_t);
    void        runManualScan();
    thread_call_t _manualScanTC = nullptr;

    /* ---------------------------------------------------------------- */
    RTW88PCIDevice    *_parent        = nullptr;
    struct rtw_dev    *_rtwdev        = nullptr;
    struct ieee80211_hw *_hw          = nullptr;
    struct ieee80211_vif *_vif        = nullptr;
    struct ieee80211_sta *_sta        = nullptr;
    size_t              _staAllocSize = 0;
    struct pci_dev     *_pcidev       = nullptr;

    IOWorkLoop         *_wl           = nullptr;
    IOCommandGate      *_gate         = nullptr;
    IOTimerEventSource *_timer        = nullptr;
    IOLock             *_lock         = nullptr;

    RTW88State          _state        = RTW88_STATE_IDLE;
    RTW88State          _scanReturnState = RTW88_STATE_IDLE;
    bool                _powered      = false;
    uint8_t             _macAddr[6]   = {};
    /* Immutable hardware identity snapshot.  User-client and Airport status
     * requests can arrive while the Linux-side rtw_dev/hw objects are being
     * stopped or rebuilt.  Never dereference those objects from a status
     * query; cache the identity once, while start() owns a valid instance. */
    uint16_t            _fwVersion    = 0;
    uint8_t             _fwSubVersion = 0;
    char                _chipName[32] = {};
    bool                _scanOffloadSupported = false;
    uint32_t            _timeoutMs    = 0;

    /* Scan results */
    RTW88BSS           *_bssList      = nullptr;
    uint32_t            _bssCount     = 0;
    IOLock             *_bssLock      = nullptr;
    uint32_t            _scanGeneration = 0;
    struct ieee80211_channel *_manualScanChannels[256] = {};
    uint32_t            _manualScanChannelCount = 0;
    volatile bool       _manualScanAbort = false;
    volatile bool       _manualScanOnHomeChannel = false;
    bool                _manualScanFallbackLogged = false;

    /* Target BSS for connection */
    RTW88BSS            _targetBSS    = {};
    char                _password[64] = {};

    /* WPA2 key material */
    uint8_t  _pmk[32]  = {};
    bool     _pmkProvided = false;
    uint8_t  _ptk[64]  = {};   /* PTK = KCK|KEK|TK */
    uint8_t  _gtk[32]  = {};
    struct ieee80211_key_conf *_ptkConf = nullptr;
    struct ieee80211_key_conf *_gtkConf = nullptr;
    bool     _ptkHardwareInstalled = false;
    uint8_t  _anonce[32] = {};
    uint8_t  _snonce[32] = {};
    uint8_t  _replayCtr[8] = {};
    uint8_t  _ccmpTxPn[6] = {};
    /* Software-CCMP RX replay windows.  A separate 64-packet window per TID
     * accepts legitimate A-MPDU reordering while still rejecting replays. */
    uint64_t _ccmpRxHighestPtk[16] = {};
    uint64_t _ccmpRxBitmapPtk[16] = {};
    uint64_t _ccmpRxHighestGtk[16] = {};
    uint64_t _ccmpRxBitmapGtk[16] = {};
    uint16_t _ccmpRxValidPtk = 0;
    uint16_t _ccmpRxValidGtk = 0;
    bool     _eapolReplayValid = false;
    bool     _eapolM1Seen = false;
    bool     _ptkInstalledThisHandshake = false;
    bool     _rxCcmpIvSkipLogged = false;
    bool     _wpa2     = false;

    /* Sequence number for TX frames */
    uint16_t _txSeq    = 0;
    /* Separate SN space for QoS data (TID 0) so the BlockAck window is gap-free */
    uint16_t _dataSeq  = 0;
    uint16_t _assocAID = 0;

    /* A-MPDU aggregation (BlockAck) state */
    bool     _txBaActive = false;  /* uplink TX BlockAck agreement established */
    uint8_t  _baTid      = 0;      /* TID carrying aggregated data (BE)        */
    uint8_t  _connChanWidth = 20;  /* negotiated operating width: 20/40/80 MHz */
    uint8_t  _baDialog   = 0;      /* rolling ADDBA-request dialog token       */
    uint16_t _baBufSize  = 64;     /* advertised BlockAck buffer/window size   */

    /* RX A-MPDU reorder buffer — one per TID with an active downlink BA.
     * Touched from the RX workloop (frame input) and the IEEE80211 workloop
     * (flush timer, disconnect teardown), so guarded by _rxBaLock.  Frames are
     * always delivered with the lock dropped to avoid holding it across the
     * network-stack input path. */
    static const uint16_t kRxBaMaxBuf   = 64;
    static const uint8_t  kRxBaNumTid   = 8;   /* QoS data TIDs 0-7 */
    static const uint32_t kReorderTimeoutMs = 60;
    struct RxReorder {
        bool      active;
        uint16_t  headSn;          /* next expected SN (12-bit, mod 4096) */
        uint16_t  bufSize;         /* reorder window size */
        uint32_t  stored;          /* frames currently buffered */
        struct sk_buff *buf[kRxBaMaxBuf];   /* indexed by SN % bufSize */
    };
    RxReorder *_rxBa[kRxBaNumTid] = {};
    IOLock    *_rxBaLock      = nullptr;
    IOTimerEventSource *_reorderTimer = nullptr;

    /* RSSI tracking */
    int      _rssi     = -100;

    /* Diagnostics: periodic logging of RX activity during scan */
    uint32_t _rxFrameCount = 0;

    /* exp4 link-quality diagnostics. These are lock-free approximate counters
     * used only by rtw88ctl perf; they must never affect the packet path. */
    volatile uint64_t _txDataFrames          = 0;
    volatile uint64_t _txEthernetFrames      = 0;
    volatile uint64_t _rxDataFrames          = 0;
    volatile uint64_t _rxProtectedFrames     = 0;
    volatile uint64_t _rxDecryptedFrames     = 0;
    volatile uint64_t _rxSoftwareCcmpOk      = 0;
    volatile uint64_t _rxSoftwareCcmpFailed  = 0;
    volatile uint64_t _rxEthernetFrames      = 0;
    volatile uint64_t _txReportRequested     = 0;
    volatile uint64_t _txReportAcked         = 0;
    volatile uint64_t _txReportFailed        = 0;
    volatile uint64_t _rxReorderInputs       = 0;
    volatile uint64_t _rxReorderStale        = 0;
    volatile uint64_t _rxReorderDuplicates   = 0;
    volatile uint64_t _rxReorderAhead        = 0;
    volatile uint64_t _rxReorderTimeoutFlush = 0;
    volatile uint64_t _rxReorderHolesSkipped = 0;
};
