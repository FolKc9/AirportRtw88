/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * RTW88PCIDevice.hpp — IOEthernetController subclass for PCIe rtw88 chips.
 *
 * Approach mirrors itlwm: present a transparent Ethernet interface to macOS
 * while doing 802.11 management internally.  The 802.11 state machine lives
 * in RTW88IEEE80211; this class handles IOKit life-cycle and the Ethernet
 * framing visible to macOS network stack.
 */
#pragma once

/* Pull in mbuf_t and related kernel types before any IOKit network headers */
#include <sys/kernel_types.h>

#ifdef RTW88_AIRPORT
#include "Airport/Apple80211.h"
using RTW88ControllerBase = IO80211Controller;
class RTW88AirportInterface;
#ifdef IO80211FAMILY_V2
class RTW88AirportSkywalkInterface;
#endif
#else
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
using RTW88ControllerBase = IOEthernetController;
#endif
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/network/IOMbufMemoryCursor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>

/* Forward declarations */
class RTW88IEEE80211;
class RTW88UserClient;

/* RTW88PCIDevice --------------------------------------------------------- */
class RTW88PCIDevice : public RTW88ControllerBase {
    OSDeclareDefaultStructors(RTW88PCIDevice)

    friend class RTW88UserClient;
    friend class RTW88IEEE80211;

public:
    /* IOService */
    bool     init(OSDictionary *props) override;
    bool     start(IOService *provider) override;
    void     stop(IOService *provider) override;
    void     free() override;
    IOReturn powerStateWillChangeTo(IOPMPowerFlags flags, unsigned long state,
                                     IOService *actor) override;

    /* IONetworkController */
    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;
    IOReturn enable(IONetworkInterface *iface) override;
    IOReturn disable(IONetworkInterface *iface) override;
    IOReturn setMaxPacketSize(UInt32 maxSize) override;
    IOReturn getMaxPacketSize(UInt32 *maxSize) const override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;
    bool     configureInterface(IONetworkInterface *iface) override;
    bool     createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    UInt32   outputPacket(mbuf_t m, void *param) override;
    IOReturn outputStart(IONetworkInterface *iface, IOOptionBits options) override;
    /* Provide a gated output queue so outputPacket() is serialized through the
     * work loop.  Without this the stack calls outputPacket() concurrently,
     * racing rtw_pci_tx_write_data's BD fill (which reads ring->r.wp before the
     * irq_lock) and leaving a zeroed descriptor that stalls the TX DMA engine. */
    IOOutputQueue *createOutputQueue() override;

#ifdef RTW88_AIRPORT
    /* Native IO80211/CoreWLAN bridge. Kept behind a build flag so the proven
     * Ethernet-mode kext remains byte-for-byte independent of this target. */
    IONetworkInterface *createInterface() override;
#ifdef IO80211FAMILY_V2
    IOReturn enable(IO80211SkywalkInterface *iface) override;
    IOReturn disable(IO80211SkywalkInterface *iface) override;
    bool isCommandProhibited(int command) override;
    SInt32 handleCardSpecific(IO80211SkywalkInterface *iface,
                              unsigned long command, void *data,
                              bool isSet) override;
    SInt32 enableFeature(IO80211FeatureCode code, void *data) override;
    IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *iface,
                               apple80211_version_data *data) override;
    IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *iface,
                                 apple80211_version_data *data) override;
    IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *iface,
                                  apple80211_capability_data *data) override;
    IOReturn getPOWER(IO80211SkywalkInterface *iface,
                      apple80211_power_data *data) override;
    IOReturn setPOWER(IO80211SkywalkInterface *iface,
                      apple80211_power_data *data) override;
    IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *iface,
                             apple80211_country_code_data *data) override;
    IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *iface,
                             apple80211_country_code_data *data) override;
    IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *iface,
                               apple80211_debug_command *data) override;
#else
    IOReturn getHardwareAddressForInterface(IO80211Interface *iface,
                                             IOEthernetAddress *addr) override;
    SInt32 monitorModeSetEnabled(IO80211Interface *iface, bool enabled,
                                  UInt32 dlt) override;
    SInt32 apple80211Request(unsigned int requestType, int requestNumber,
                             IO80211Interface *iface, void *data) override;
    bool useAppleRSNSupplicant(IO80211Interface *iface) override;
    int outputRaw80211Packet(IO80211Interface *iface, mbuf_t m) override;
    SInt32 enableVirtualInterface(IO80211VirtualInterface *iface) override;
    SInt32 disableVirtualInterface(IO80211VirtualInterface *iface) override;
    IO80211VirtualInterface *createVirtualInterface(ether_addr *addr,
                                                     uint role) override;
    SInt32 apple80211VirtualRequest(uint requestType, int requestNumber,
                                    IO80211VirtualInterface *iface,
                                    void *data) override;
    SInt32 stopDMA() override;
    UInt32 hardwareOutputQueueDepth(IO80211Interface *iface) override;
    SInt32 performCountryCodeOperation(IO80211Interface *iface,
                                        IO80211CountryCodeOp op) override;
    SInt32 enableFeature(IO80211FeatureCode code, void *data) override;
    void requestPacketTx(void *arg, UInt value) override;
    int bpfOutputPacket(OSObject *object, UInt dlt, mbuf_t m) override;
#endif

    void notifyAirportScanDone(bool aborted);
    void notifyAirportLinkIdle();
    void notifyAirportLinkUp();
    void notifyAirportLinkDown(uint16_t reason);
#endif

    /* IOEthernetController */
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn setHardwareAddress(const IOEthernetAddress *addr) override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;

    /* IOUserClient creation */
    IOReturn newUserClient(task_t owningTask, void *securityID,
                           UInt32 type, OSDictionary *properties,
                           IOUserClient **handler) override;

    /* Called from interrupt handler */
    void handleInterrupt(IOInterruptEventSource *src, int count);

    /* Resume a flow-control-stalled output queue once the IRQ bottom-half has
     * freed BE ring slots.  Called via a C trampoline from the compat layer. */
    void resumeTxIfStalled();
    /* exp10: discard packets queued for an association that has just died and
     * clear stale flow-control state before the automatic reassociation. */
    void resetTxFlowForReconnect();
    void flushRxAfterNapiPoll();

    /* Called from RTW88IEEE80211 to deliver RX frames to macOS */
    void injectRxFrame(mbuf_t m);
    /* The workloop the RX/interrupt path runs on. RTW88IEEE80211 attaches its
     * RX reorder flush timer here so all frame delivery is serialized on one
     * thread (injectRxFrame's queue+flush is not safe against concurrent
     * callers). */
    IOWorkLoop *getRxWorkLoop() const { return _workLoop; }
    /* Allocate an input mbuf via the IONetworkController allocator (sets
     * m_len and pkthdr.len consistently — required for inputPacket). */
    mbuf_t allocateInputPacket(uint32_t len);

    /* DMA helpers — used by Linux compat dma_alloc_coherent */
    void *allocCoherent(size_t size, IOPhysicalAddress *phys);
    void  freeCoherent(size_t size, void *virt, IOPhysicalAddress phys);
    void  freeCoherentByPhys(IOPhysicalAddress phys);
    /* Bounce buffer helpers for dma_map_single / dma_sync_single_for_cpu */
    void  setBounceOrigVA(IOPhysicalAddress phys, void *orig_va);
    void  syncBounceForCpu(IOPhysicalAddress dma, size_t size);
    void  noteDmaMap(size_t size, int dir);
    void  noteDmaRxCopy(size_t size);
    void  noteTxStall();

    /* NativeV2 exp2: preallocated TX bounce pool.  The hot TX path used to
     * allocate/prepare/release one IOBufferMemoryDescriptor per packet.
     * Pooling keeps those DMA-safe <4GB buffers mapped for the lifetime of
     * the device and only leases a slot until TX completion. */
    bool              initTxBouncePool();
    void              releaseTxBouncePool();
    IOPhysicalAddress mapTxBounce(const void *src, size_t size);
    bool              unmapTxBounce(IOPhysicalAddress phys);
    uint64_t dmaTxMaps() const { return _dmaTxMaps; }
    uint64_t dmaRxMaps() const { return _dmaRxMaps; }
    uint64_t dmaTxBytes() const { return _dmaTxBytes; }
    uint64_t dmaRxBytes() const { return _dmaRxBytes; }
    uint64_t dmaRxCopyBytes() const { return _dmaRxCopyBytes; }
    uint64_t txStallEvents() const { return _txStallEvents; }
    uint64_t txPoolHits() const { return _txPoolHits; }
    uint64_t txPoolFallbacks() const { return _txPoolFallbacks; }
    uint32_t txPoolInUse() const { return _txPoolInUse; }
    uint32_t txPoolPeak() const { return _txPoolPeak; }
    uint64_t rxQueuedPackets() const { return _rxQueuedPackets; }
    uint64_t rxFlushes() const { return _rxFlushes; }
    uint64_t txBatchPackets() const { return _txBatchPackets; }
    uint64_t txBatchKicks() const { return _txBatchKicks; }
    uint64_t txBatchTimerKicks() const { return _txBatchTimerKicks; }
    uint64_t txBatchImmediateKicks() const { return _txBatchImmediateKicks; }
    uint32_t txBatchPending() const { return _txBatchPending; }
    uint32_t txBatchMaxPending() const { return _txBatchMaxPending; }

    /* PCI config space — used by Linux compat pci_read/write_config_* */
    UInt8  pciReadByte(int offset);
    UInt16 pciReadWord(int offset);
    UInt32 pciReadDword(int offset);
    void   pciWriteByte(int offset, UInt8 val);
    void   pciWriteWord(int offset, UInt16 val);
    void   pciWriteDword(int offset, UInt32 val);
    int    pciFindCapability(int cap);

    /* 802.11 state machine accessors */
    RTW88IEEE80211 *get80211() { return _ieee80211; }

    /* MMIO base — used by compat ioremap shim */
    volatile void *mmioBase() const { return _mmioBase; }

private:
    bool     attachDevice();
    bool     setupInterrupt();
    bool     setupDMA();
    void     teardown();
    const char *chipDisplayName() const;
    void     publishHardwareIdentity();

    bool     setupMediumDict();
    void     addMedium(OSDictionary *mediums, IOMediumType type, UInt64 speed);

    static void interruptOccurred(OSObject *owner,
                                   IOInterruptEventSource *src, int count);

    void debugTimerFired(IOTimerEventSource *src);
    void rxFlushTimerFired(IOTimerEventSource *src);
    void txKickTimerFired(IOTimerEventSource *src);
#ifdef RTW88_AIRPORT
    void airportScanDoneTimerFired(IOTimerEventSource *src);
    static IOReturn setAirportLinkUpGated(OSObject *owner, void *arg0,
                                           void *arg1, void *arg2, void *arg3);
    static IOReturn setAirportLinkDownGated(OSObject *owner, void *arg0,
                                             void *arg1, void *arg2, void *arg3);
#endif
    void flushRxBatch();
    void clearRxPendingQueue();
    void noteTxBatchSubmission(size_t packetLen, uint8_t urgency);
    void flushTxBatch(bool timerKick = false, bool immediateKick = false);

    IOPCIDevice            *_pciDev       = nullptr;
    IOMemoryMap            *_mmioMap      = nullptr;
    volatile void          *_mmioBase     = nullptr;
    IOWorkLoop             *_workLoop     = nullptr;
    IOCommandGate          *_cmdGate      = nullptr;
    IOInterruptEventSource *_intrSrc      = nullptr;
    IOTimerEventSource     *_debugTimer   = nullptr;
    IOTimerEventSource     *_rxFlushTimer = nullptr;
    IOTimerEventSource     *_txKickTimer  = nullptr;
#ifdef RTW88_AIRPORT
    IOTimerEventSource     *_airportScanDoneTimer = nullptr;
    RTW88AirportInterface  *_iface        = nullptr;
#ifdef IO80211FAMILY_V2
    RTW88AirportSkywalkInterface *_skywalk = nullptr;
#endif
#else
    IOEthernetInterface    *_iface        = nullptr;
#endif
    IOGatedOutputQueue     *_txQueue      = nullptr;

    RTW88IEEE80211         *_ieee80211    = nullptr;
    RTW88UserClient        *_userClient   = nullptr;

    IOEthernetAddress       _macAddr;
    bool                    _enabled      = false;
    bool                    _initialized  = false;

    /* TX flow control: set when outputPacket() stalls the gated queue because
     * the BE ring is nearly full; cleared when the IRQ completion path frees
     * slots and resumes the queue.  See createOutputQueue()/outputPacket(). */
    volatile bool           _txStalled    = false;

    /* NativeV2 perf diagnostics. Approximate counters are sufficient here;
     * they are intentionally lock-free to avoid perturbing the hot path. */
    volatile uint64_t       _dmaTxMaps       = 0;
    volatile uint64_t       _dmaRxMaps       = 0;
    volatile uint64_t       _dmaTxBytes      = 0;
    volatile uint64_t       _dmaRxBytes      = 0;
    volatile uint64_t       _dmaRxCopyBytes  = 0;
    volatile uint64_t       _txStallEvents   = 0;
    volatile uint64_t       _txPoolHits      = 0;
    volatile uint64_t       _txPoolFallbacks = 0;
    volatile uint32_t       _txPoolInUse     = 0;
    volatile uint32_t       _txPoolPeak      = 0;
    volatile uint64_t       _rxQueuedPackets = 0;
    volatile uint64_t       _rxFlushes       = 0;

    /* NativeV2 exp12 adaptive TX A-MPDU doorbell batching telemetry/state.
     * These counters were exposed by the public accessors above but the
     * original exp11 package accidentally omitted the backing members,
     * causing the kext translation units to fail compilation. */
    volatile uint64_t       _txBatchPackets        = 0;
    volatile uint64_t       _txBatchKicks          = 0;
    volatile uint64_t       _txBatchTimerKicks     = 0;
    volatile uint64_t       _txBatchImmediateKicks = 0;
    volatile uint32_t       _txBatchPending        = 0;
    volatile uint32_t       _txBatchMaxPending     = 0;

    /* TX DMA pool: one 1 MiB physically-contiguous region split into 256
     * 4 KiB slots.  4 KiB comfortably covers a normal 802.11 data frame plus
     * the rtw88 TX descriptor; oversize/overflow cases fall back to the old
     * per-map allocation path. */
    static constexpr uint32_t kTxBouncePoolSlots = 256;
    static constexpr size_t   kTxBounceSlotSize  = 4096;
    IOBufferMemoryDescriptor *_txBouncePoolDesc  = nullptr;
    void                     *_txBouncePoolVirt  = nullptr;
    IOPhysicalAddress         _txBouncePoolPhys  = 0;
    bool                      _txBounceUsed[kTxBouncePoolSlots] = {};
    uint32_t                  _txBounceCursor    = 0;
    IOSimpleLock             *_txBounceLock      = nullptr;

    /* RX delivery batching.  The IONetworkInterface input queue is explicitly
     * lockless, so NAPI must never touch it while the controller workloop is
     * flushing.  NAPI appends to this driver-owned queue under _rxFlushLock;
     * the workloop detaches the batch, calls inputPacket(), then flushes it. */
    static constexpr uint32_t kRxPendingLimit = 2048;
    IOSimpleLock             *_rxFlushLock       = nullptr;
    bool                      _rxFlushPending    = false;
    mbuf_t                    _rxPendingHead     = nullptr;
    mbuf_t                    _rxPendingTail     = nullptr;
    uint32_t                  _rxPendingCount    = 0;
    volatile uint64_t         _rxQueueDrops      = 0;

    /* Linked-list of allocated DMA buffers for cleanup */
    struct DMAEntry {
        IOBufferMemoryDescriptor *desc;
        void    *virt;        /* kernel VA of the bounce/coherent buffer */
        IOPhysicalAddress phys;
        size_t   size;
        void    *orig_va;     /* original CPU VA for bounce mappings (NULL=coherent) */
        DMAEntry *next;
    };
    DMAEntry *_dmaList        = nullptr;
    IOSimpleLock *_dmaLock    = nullptr;

    /* Entries whose desc->complete()/release() was deferred because
     * preemption was disabled at free time (TX-completion interrupt path).
     * Drained by drainPendingFree() from preemption-enabled contexts. */
    DMAEntry     *_dmaPendingFree    = nullptr;
    IOSimpleLock *_pendingFreeLock   = nullptr;
    void          drainPendingFree();

    /* Back-pointer passed to compat layer */
    struct pci_dev *_compatPciDev = nullptr;
};
