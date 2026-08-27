// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// RTW88PCIDevice.cpp — IOEthernetController for PCIe rtw88 adapters

#include "RTW88PCIDevice.hpp"
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"
#ifdef RTW88_AIRPORT
#include "RTW88AirportInterface.hpp"
#ifdef IO80211FAMILY_V2
#include "RTW88AirportSkywalkInterface.hpp"
#endif
#endif

#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/network/IONetworkMedium.h>

/* The Linux-compat pci_ops that route through this class */
extern "C" {
#include "../compat/rtw88_compat.h"
}

extern "C" void rtw88_trigger_interrupt(void);
extern "C" boolean_t preemption_enabled(void);

#ifdef RTW88_AIRPORT
#define super IO80211Controller
OSDefineMetaClassAndStructors(RTW88PCIDevice, IO80211Controller)
#else
#define super IOEthernetController
OSDefineMetaClassAndStructors(RTW88PCIDevice, IOEthernetController)
#endif

/* exp3: latency-safe hysteresis.  exp2 allowed the BE ring to fall to only
 * 24 free descriptors; the user's trace showed TX-pool peak=233/256 and
 * noticeably higher loaded ping without a throughput gain.  Keep at least
 * ~96 descriptors free and resume at 160. exp4 restores the wider exp1
 * hysteresis because the user reported lower ping there; the 128-packet
 * software queue remains, so we do not reintroduce exp2-style bufferbloat. */
static constexpr unsigned int kRTW88TxStallAvail = 96;
static constexpr unsigned int kRTW88TxBulkStallAvail = 64;
static constexpr unsigned int kRTW88TxResumeAvail = 128;

/* exp12: adaptive TX aggregation without exp2/exp9-style queue growth.
 * Pure TCP ACKs get a short coalescing window; bulk data gets a wider 8-frame
 * window. Only genuinely latency-critical control traffic bypasses batching. */
static constexpr uint32_t kRTW88TxAckBatchTarget  = 4;
static constexpr uint32_t kRTW88TxBulkBatchTarget = 8;
static constexpr uint32_t kRTW88TxAckDelayUs      = 70;
static constexpr uint32_t kRTW88TxBulkDelayUs     = 180;
static constexpr size_t   kRTW88TxTinyImmediateLen = 64;

enum {
    kRTW88TxUrgencyBulk = 0,
    kRTW88TxUrgencyAck = 1,
    kRTW88TxUrgencyImmediate = 2,
};

static inline uint16_t rtw88_get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* Classify before RTW88IEEE80211::outputPacket() consumes the mbuf.  Avoid
 * the exp11 packetLen<=256 shortcut: under TCP downloads that made almost
 * every ACK ring the PCIe doorbell immediately (94% of kicks in the trace). */
static uint8_t rtw88_tx_urgency(mbuf_t m)
{
    if (!m) return kRTW88TxUrgencyImmediate;
    size_t plen = mbuf_pkthdr_len(m);
    if (plen < 14) return kRTW88TxUrgencyImmediate;

    uint8_t h[128] = {};
    size_t n = plen < sizeof(h) ? plen : sizeof(h);
    if (mbuf_copydata(m, 0, n, h) != 0)
        return plen <= kRTW88TxTinyImmediateLen ?
            kRTW88TxUrgencyImmediate : kRTW88TxUrgencyBulk;

    size_t l3 = 14;
    uint16_t eth = rtw88_get_be16(h + 12);
    if ((eth == 0x8100 || eth == 0x88a8) && n >= 18) {
        eth = rtw88_get_be16(h + 16);
        l3 = 18;
    }

    /* ARP and EAPOL must never wait behind a throughput batch. */
    if (eth == 0x0806 || eth == 0x888e)
        return kRTW88TxUrgencyImmediate;

    if (eth == 0x0800 && n >= l3 + 20) {
        uint8_t ihl = (uint8_t)((h[l3] & 0x0f) * 4);
        if (ihl < 20 || n < l3 + ihl) return kRTW88TxUrgencyImmediate;
        uint16_t total = rtw88_get_be16(h + l3 + 2);
        uint8_t proto = h[l3 + 9];
        if (proto == 1) return kRTW88TxUrgencyImmediate; /* ICMP */
        if (proto == 17 && n >= l3 + ihl + 8) {
            size_t u = l3 + ihl;
            uint16_t sp = rtw88_get_be16(h + u);
            uint16_t dp = rtw88_get_be16(h + u + 2);
            if (sp == 67 || sp == 68 || dp == 67 || dp == 68 ||
                sp == 53 || dp == 53 || sp == 123 || dp == 123)
                return kRTW88TxUrgencyImmediate;
        }
        if (proto == 6 && n >= l3 + ihl + 20) {
            size_t t = l3 + ihl;
            uint8_t thl = (uint8_t)(((h[t + 12] >> 4) & 0x0f) * 4);
            uint8_t flags = h[t + 13];
            if (flags & (0x02 | 0x01 | 0x04)) /* SYN/FIN/RST */
                return kRTW88TxUrgencyImmediate;
            if (thl >= 20 && total >= ihl + thl &&
                total == (uint16_t)(ihl + thl) && (flags & 0x10))
                return kRTW88TxUrgencyAck;
        }
    } else if (eth == 0x86dd && n >= l3 + 40) {
        uint16_t payload = rtw88_get_be16(h + l3 + 4);
        uint8_t next = h[l3 + 6];
        size_t l4 = l3 + 40;
        if (next == 58) return kRTW88TxUrgencyImmediate; /* ICMPv6 */
        if (next == 17 && n >= l4 + 8) {
            uint16_t sp = rtw88_get_be16(h + l4);
            uint16_t dp = rtw88_get_be16(h + l4 + 2);
            if (sp == 53 || dp == 53 || sp == 123 || dp == 123 ||
                sp == 546 || sp == 547 || dp == 546 || dp == 547)
                return kRTW88TxUrgencyImmediate;
        }
        if (next == 6 && n >= l4 + 20) {
            uint8_t thl = (uint8_t)(((h[l4 + 12] >> 4) & 0x0f) * 4);
            uint8_t flags = h[l4 + 13];
            if (flags & (0x02 | 0x01 | 0x04))
                return kRTW88TxUrgencyImmediate;
            if (thl >= 20 && payload == thl && (flags & 0x10))
                return kRTW88TxUrgencyAck;
        }
    }

    if (plen <= kRTW88TxTinyImmediateLen)
        return kRTW88TxUrgencyImmediate;
    return kRTW88TxUrgencyBulk;
}

/* ------------------------------------------------------------------ */
/*  PCI ops shim (C linkage, called from driver C code)                */
/* ------------------------------------------------------------------ */

static RTW88PCIDevice *g_pci_dev_instance = nullptr;

static int compat_pci_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
    if (!g_pci_dev_instance) { *val = 0xff; return -1; }
    *val = g_pci_dev_instance->pciReadByte(where);
    return 0;
}
static int compat_pci_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
    if (!g_pci_dev_instance) { *val = 0xffff; return -1; }
    *val = g_pci_dev_instance->pciReadWord(where);
    return 0;
}
static int compat_pci_read_config_dword(struct pci_dev *dev, int where, u32 *val)
{
    if (!g_pci_dev_instance) { *val = 0xffffffff; return -1; }
    *val = g_pci_dev_instance->pciReadDword(where);
    return 0;
}
static int compat_pci_write_config_byte(struct pci_dev *dev, int where, u8 val)
{
    if (!g_pci_dev_instance) return -1;
    g_pci_dev_instance->pciWriteByte(where, val);
    return 0;
}
static int compat_pci_write_config_word(struct pci_dev *dev, int where, u16 val)
{
    if (!g_pci_dev_instance) return -1;
    g_pci_dev_instance->pciWriteWord(where, val);
    return 0;
}
static int compat_pci_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
    if (!g_pci_dev_instance) return -1;
    g_pci_dev_instance->pciWriteDword(where, val);
    return 0;
}
static void *compat_ioremap(struct pci_dev *dev, int bar, size_t len)
{
    /* Already mapped at start; just return the cached base */
    return (void *)g_pci_dev_instance->mmioBase();
}
static void compat_iounmap(struct pci_dev *dev, void *addr) {}

static int compat_enable_msi(struct pci_dev *dev)
{
    /* Handled by IOInterruptEventSource in setupInterrupt() */
    return 0;
}
static void compat_disable_msi(struct pci_dev *dev) {}

static int compat_pci_find_capability(struct pci_dev *dev, int cap)
{
    if (!g_pci_dev_instance) return 0;
    return g_pci_dev_instance->pciFindCapability(cap);
}

static struct pci_ops_rtw88 _pci_io_ops = {
    .read_config_byte   = compat_pci_read_config_byte,
    .read_config_word   = compat_pci_read_config_word,
    .read_config_dword  = compat_pci_read_config_dword,
    .write_config_byte  = compat_pci_write_config_byte,
    .write_config_word  = compat_pci_write_config_word,
    .write_config_dword = compat_pci_write_config_dword,
    .ioremap            = compat_ioremap,
    .iounmap            = compat_iounmap,
    .enable_msi         = compat_enable_msi,
    .disable_msi        = compat_disable_msi,
    .pci_find_capability = compat_pci_find_capability,
};



/* DMA ops shim */
static void *compat_dma_alloc(struct device *dev, size_t size,
                               dma_addr_t *dma_handle, gfp_t flag)
{
    if (!g_pci_dev_instance) return nullptr;
    IOPhysicalAddress phys = 0;
    void *virt = g_pci_dev_instance->allocCoherent(size, &phys);
    if (dma_handle) *dma_handle = (dma_addr_t)phys;
    return virt;
}
static void compat_dma_free(struct device *dev, size_t size,
                             void *cpu_addr, dma_addr_t dma_handle)
{
    if (g_pci_dev_instance)
        g_pci_dev_instance->freeCoherent(size, cpu_addr,
                                          (IOPhysicalAddress)dma_handle);
}
static dma_addr_t compat_dma_map(struct device *dev, void *ptr,
                                   size_t size, int dir)
{
    /*
     * RX buffers remain persistent bounce mappings because rtw88 programs the
     * same RX descriptors once and reuses them.
     *
     * exp1 also allocated a fresh IOBufferMemoryDescriptor for EVERY TX map.
     * The user's trace showed ~13,900 TX maps in 30 s, mostly tiny TCP ACKs.
     * Creating/preparing/releasing a DMA descriptor for each packet is far more
     * expensive than the memcpy itself and contributes to BE-ring backpressure.
     * exp2 leases a pre-mapped 4 KiB slot from a 256-slot TX pool instead.
     */
    if (!g_pci_dev_instance || !ptr) return 0;

    g_pci_dev_instance->noteDmaMap(size, dir);

    if (dir == 1 /* DMA_TO_DEVICE */) {
        IOPhysicalAddress pooled = g_pci_dev_instance->mapTxBounce(ptr, size);
        if (pooled)
            return (dma_addr_t)pooled;
    }

    /* Safe fallback for RX, bidirectional mappings, oversize TX frames, or a
     * temporarily exhausted pool. */
    IOPhysicalAddress phys = 0;
    void *bounce = g_pci_dev_instance->allocCoherent(size, &phys);
    if (!bounce) return 0;

    g_pci_dev_instance->setBounceOrigVA((IOPhysicalAddress)phys, ptr);

    if (dir == 1 /* DMA_TO_DEVICE */ || dir == 2 /* DMA_BIDIRECTIONAL */)
        memcpy(bounce, ptr, size);

    return (dma_addr_t)phys;
}
static void compat_dma_unmap(struct device *dev, dma_addr_t addr,
                               size_t size, int dir)
{
    if (!g_pci_dev_instance) return;

    /* Pool slots are merely returned to the free list; no IOKit allocation or
     * prepare/complete/release happens on the hot completion path. */
    if (dir == 1 /* DMA_TO_DEVICE */ &&
        g_pci_dev_instance->unmapTxBounce((IOPhysicalAddress)addr))
        return;

    g_pci_dev_instance->freeCoherentByPhys((IOPhysicalAddress)addr);
}
static void compat_dma_sync_cpu(struct device *dev, dma_addr_t addr,
                                  size_t size, int dir)
{
    /*
     * RX: chip finished writing to the bounce buffer.  Copy it into the
     * original skb->data buffer so the driver can read it from there.
     */
    if (dir == 0 /* DMA_FROM_DEVICE */ && g_pci_dev_instance) {
        g_pci_dev_instance->noteDmaRxCopy(size);
        g_pci_dev_instance->syncBounceForCpu((IOPhysicalAddress)addr, size);
    }
}
static void compat_dma_sync_dev(struct device *dev, dma_addr_t addr,
                                  size_t size, int dir)
{
    /* Re-arm for next DMA: bounce is already in place, nothing to do */
}

static struct rtw88_dma_alloc_ops _dma_ops = {
    .alloc_coherent         = compat_dma_alloc,
    .free_coherent          = compat_dma_free,
    .map_single             = compat_dma_map,
    .unmap_single           = compat_dma_unmap,
    .sync_single_for_cpu    = compat_dma_sync_cpu,
    .sync_single_for_device = compat_dma_sync_dev,
};

void RTW88PCIDevice::noteDmaMap(size_t size, int dir)
{
    if (dir == 0) {
        _dmaRxMaps++;
        _dmaRxBytes += (uint64_t)size;
    } else {
        _dmaTxMaps++;
        _dmaTxBytes += (uint64_t)size;
    }
}

void RTW88PCIDevice::noteDmaRxCopy(size_t size)
{
    _dmaRxCopyBytes += (uint64_t)size;
}

void RTW88PCIDevice::noteTxStall()
{
    _txStallEvents++;
}

const char *RTW88PCIDevice::chipDisplayName() const
{
    if (!_compatPciDev)
        return "Realtek Wireless";

    switch (_compatPciDev->device) {
    case 0xB822:
        return "RTL8822BE";
    case 0xC822:
    case 0xC82F:
        return "RTL8822CE";
    case 0xC821:
    case 0xB821:
        return "RTL8821CE";
    case 0x8821:
        return "RTL8821AE";
    case 0x8812:
        return "RTL8812AE";
    case 0x8813:
        return "RTL8814AE";
    default:
        return "Realtek Wireless";
    }
}

const OSString *RTW88PCIDevice::newVendorString() const
{
    return OSString::withCString("Realtek");
}

const OSString *RTW88PCIDevice::newModelString() const
{
    return OSString::withCString(chipDisplayName());
}

void RTW88PCIDevice::publishHardwareIdentity()
{
    const char *chip = chipDisplayName();

    if (_ieee80211)
        _ieee80211->getMACAddress(_macAddr.bytes);

    setName(chip);
    setProperty(kIOVendor, "Realtek");
    setProperty(kIOModel, chip);
    setProperty(kIORevision, "rtw88");
    setProperty(kIOBuiltin, kOSBooleanTrue);
    setProperty(kIOLocation, "Internal");
    setProperty("IOUserVisibleName", chip);
    setProperty("Product Name", chip);
    setProperty("Device Name", chip);
    setProperty("Model", chip);
    setProperty("Vendor", "Realtek");

    if (_macAddr.bytes[0] || _macAddr.bytes[1] || _macAddr.bytes[2] ||
        _macAddr.bytes[3] || _macAddr.bytes[4] || _macAddr.bytes[5]) {
        OSData *mac = OSData::withBytes(_macAddr.bytes, sizeof(_macAddr.bytes));
        if (mac) {
            setProperty(kIOMACAddress, mac);
            mac->release();
        }
    }

    if (_iface) {
        _iface->setName(chip);
        _iface->setProperty(kIOVendor, "Realtek");
        _iface->setProperty(kIOModel, chip);
        _iface->setProperty(kIORevision, "rtw88");
        _iface->setProperty(kIOBuiltin, kOSBooleanTrue);
        _iface->setProperty(kIOPrimaryInterface, kOSBooleanTrue);
        _iface->setProperty(kIOLocation, "Internal");
        _iface->setProperty("IOUserVisibleName", chip);
        _iface->setProperty("Product Name", chip);
        _iface->setProperty("Device Name", chip);
        _iface->setProperty("Model", chip);
        _iface->setProperty("Vendor", "Realtek");

        if (_macAddr.bytes[0] || _macAddr.bytes[1] || _macAddr.bytes[2] ||
            _macAddr.bytes[3] || _macAddr.bytes[4] || _macAddr.bytes[5]) {
            OSData *mac = OSData::withBytes(_macAddr.bytes, sizeof(_macAddr.bytes));
            if (mac) {
                _iface->setProperty(kIOMACAddress, mac);
                mac->release();
            }
        }
    }
}

/* C-linkage trampoline registered with the compat layer; the IRQ bottom-half
 * calls it after tx_isr to resume a flow-control-stalled output queue. */
extern "C" void rtw88_tx_resume_trampoline(void)
{
    if (g_pci_dev_instance)
        g_pci_dev_instance->resumeTxIfStalled();
}

extern "C" void rtw88_napi_flush_trampoline(void)
{
    if (g_pci_dev_instance)
        g_pci_dev_instance->flushRxAfterNapiPoll();
}

/* ------------------------------------------------------------------ */
/*  IOService lifecycle                                                 */
/* ------------------------------------------------------------------ */

bool RTW88PCIDevice::init(OSDictionary *props)
{
    IOLog("rtw88: RTW88PCIDevice::init\n");
    if (!super::init(props)) return false;
    _dmaLock         = IOSimpleLockAlloc();
    _pendingFreeLock = IOSimpleLockAlloc();
    _txBounceLock    = IOSimpleLockAlloc();
    _rxFlushLock     = IOSimpleLockAlloc();
    return _dmaLock != nullptr && _pendingFreeLock != nullptr &&
           _txBounceLock != nullptr && _rxFlushLock != nullptr;
}

bool RTW88PCIDevice::createWorkLoop()
{
#ifdef RTW88_AIRPORT
    /* IO80211Interface's event user client is tied to the controller's
     * IO80211WorkLoop.  A separate generic work loop lets the BSD interface
     * appear, but airportd cannot attach its SCAN_DONE monitor. */
    _workLoop = IO80211WorkLoop::workLoop();
#else
    _workLoop = IOWorkLoop::workLoop();
#endif
    return _workLoop != nullptr;
}

IOWorkLoop *RTW88PCIDevice::getWorkLoop() const
{
    return _workLoop;
}

bool RTW88PCIDevice::start(IOService *provider)
{
    IOLog("rtw88: RTW88PCIDevice::start\n");
    if (!super::start(provider)) return false;

    _pciDev = OSDynamicCast(IOPCIDevice, provider);
    if (!_pciDev) {
        IOLog("rtw88: provider is not IOPCIDevice\n");
        return false;
    }
    _pciDev->retain();

    /* Enable bus mastering & memory space */
    _pciDev->setBusMasterEnable(true);
    _pciDev->setMemoryEnable(true);

    /* Map BAR2 — rtw88 driver hardcodes bar_id=2 in pci.c */
    _mmioMap = _pciDev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!_mmioMap) {
        IOLog("rtw88: failed to map BAR2\n");
        return false;
    }
    _mmioBase = (volatile void *)_mmioMap->getVirtualAddress();
    IOLog("rtw88: BAR2 mapped at %p, size 0x%llx\n",
          (void *)_mmioBase, (unsigned long long)_mmioMap->getLength());

    /* Install global compat ops */
    g_pci_dev_instance = this;
    rtw88_pci_io_ops   = &_pci_io_ops;
    rtw88_dma_ops      = &_dma_ops;

    /* Initialise compat runtime (workqueues, timers) */
    rtw88_compat_init();

    /* exp2: pre-map a TX bounce pool before the driver can submit H2C/data.
     * Failure is non-fatal; compat_dma_map falls back to the exp1 path. */
    if (!initTxBouncePool())
        IOLog("rtw88: exp2 TX bounce pool unavailable; using per-map fallback\n");

    /* Build the pci_dev struct for the Linux driver */
    _compatPciDev = (struct pci_dev *)IOMallocZero(sizeof(struct pci_dev));
    if (!_compatPciDev) return false;

    _compatPciDev->vendor  = _pciDev->configRead16(0x00);
    _compatPciDev->device  = _pciDev->configRead16(0x02);
    _compatPciDev->kext_dev = this;
    _compatPciDev->resource[2]     = (resource_size_t)_mmioBase;
    _compatPciDev->resource_len[2] = (resource_size_t)_mmioMap->getLength();

    IOLog("rtw88: PCI device %04x:%04x\n",
          _compatPciDev->vendor, _compatPciDev->device);

    /* super::start() normally creates this through createWorkLoop().  Keep a
     * guarded fallback for kernels that defer controller work-loop creation. */
    if (!_workLoop && !createWorkLoop())
        return false;

    _cmdGate = IOCommandGate::commandGate(this);
    if (!_cmdGate) return false;
    _workLoop->addEventSource(_cmdGate);

    /* Set up interrupt */
    if (!setupInterrupt()) {
        IOLog("rtw88: setupInterrupt failed\n");
        return false;
    }

    /* Locate firmware Resources/ and set fw dir */
    rtw88_find_fw_dir();

    /* Create 802.11 state machine */
    _ieee80211 = RTW88IEEE80211::create(this, _compatPciDev);
    if (!_ieee80211) {
        IOLog("rtw88: failed to create RTW88IEEE80211\n");
        return false;
    }

    /* Force-disable BT coexistence.  rtw_pci_probe (inside create()) has
     * already run rtw_core_init which filled rtwdev->efuse.btcoex from
     * the chip's eFuse — but the actual coex setup happens later in
     * rtw_power_on (via _ieee80211->start() below).  Override now so the
     * coex driver initialises with wifi_only=true and never enables the
     * BT-side H2C/C2H exchange that appears to be wedging BE TX. */
    rtw88_force_wifi_only();

    /* Run the full probe now so the MAC address is populated before
     * the Ethernet interface is attached.  enable() will call
     * rtw_core_start() to power on the hardware for TX/RX. */
    IOReturn probeRet = _ieee80211->start();
    if (probeRet != kIOReturnSuccess) {
        IOLog("rtw88: probe failed (0x%08x)\n", probeRet);
        return false;
    }

    /* IO80211 resolves the controller medium while it creates the BSD
     * interface.  Publish it first, matching the native AirPort lifecycle,
     * so en0 is never attached with an incomplete link configuration. */
    if (!setupMediumDict()) {
        IOLog("rtw88: failed to publish network media\n");
        return false;
    }

    /* Attach Ethernet interface — MAC is now known */
    if (!attachDevice()) return false;

#ifdef RTW88_AIRPORT
    /* Association will add kIONetworkLinkActive later.  Valid must already
     * be visible when configd first observes the newly attached interface. */
    if (!setLinkStatus(kIONetworkLinkValid)) {
        IOLog("rtw88: failed to set initial AirPort link state\n");
        return false;
    }
#endif

#ifdef RTW88_AIRPORT
    /* IO80211 expects SCAN_DONE on the controller workloop. Posting directly
     * from rtw88's scan-completion thread leaves CoreWLAN waiting even after
     * the radio is idle and the BSS cache has been populated. */
    _airportScanDoneTimer = IOTimerEventSource::timerEventSource(
        this, OSMemberFunctionCast(IOTimerEventSource::Action,
                                   this, &RTW88PCIDevice::airportScanDoneTimerFired));
    if (!_airportScanDoneTimer ||
        _workLoop->addEventSource(_airportScanDoneTimer) != kIOReturnSuccess) {
        IOLog("rtw88: failed to create AirPort scan-done timer\n");
        return false;
    }
    _airportScanDoneTimer->enable();
#endif

    /* Batch queued RX delivery on the controller workloop instead of calling
     * flushInputQueue() once for every packet.  This event source is also the
     * single runtime owner of flushInputQueue(): calling it concurrently from
     * NAPI and the timer can double-drain IONetworking's classq and free the
     * same mbuf twice. */
    _rxFlushTimer = IOTimerEventSource::timerEventSource(
        this, OSMemberFunctionCast(IOTimerEventSource::Action,
                                   this, &RTW88PCIDevice::rxFlushTimerFired));
    if (!_rxFlushTimer ||
        _workLoop->addEventSource(_rxFlushTimer) != kIOReturnSuccess) {
        IOLog("rtw88: failed to create serialized RX flush timer\n");
        return false;
    }

    _txKickTimer = IOTimerEventSource::timerEventSource(
        this, OSMemberFunctionCast(IOTimerEventSource::Action,
                                   this, &RTW88PCIDevice::txKickTimerFired));
    if (_txKickTimer)
        _workLoop->addEventSource(_txKickTimer);

    _initialized = true;
    /* Wire TX flow-control resume: fired from the IRQ bottom-half after tx_isr
     * frees BE ring slots, so a stalled output queue gets re-serviced. */
    rtw88_set_tx_resume_cb(rtw88_tx_resume_trampoline);
    rtw88_set_napi_flush_cb(rtw88_napi_flush_trampoline);
    if (_intrSrc) _intrSrc->enable();

    /* Debug: poll BE ring + HISR/HIMR every second. Logs distinguish chip
     * stop vs interrupt loss vs IMR masking when TX appears stuck. */
    _debugTimer = IOTimerEventSource::timerEventSource(
        this, OSMemberFunctionCast(IOTimerEventSource::Action,
                                   this, &RTW88PCIDevice::debugTimerFired));
    if (_debugTimer) {
        _workLoop->addEventSource(_debugTimer);
        /* Diagnostics are only needed to recover a stalled TX queue.  Polling
         * every second keeps the workloop awake while the link is idle; a
         * five-second cadence still detects a stall promptly and reduces
         * baseline battery drain. */
        _debugTimer->setTimeoutMS(5000);
    }

    IOLog("rtw88: device started successfully\n");
    registerService();   /* publish IOKit port for IOServiceOpen / rtw88ctl */
    if (_iface)
        _iface->registerService();
    return true;
}

void RTW88PCIDevice::debugTimerFired(IOTimerEventSource *src)
{
    unsigned int avail = rtw88_be_tx_avail();
    if (_txStalled && avail >= kRTW88TxResumeAvail)
        resumeTxIfStalled();
    if (_txStalled || avail < kRTW88TxStallAvail)
        rtw88_debug_dump_tx_state();
    src->setTimeoutMS(5000);   /* re-arm; diagnostics are not a heartbeat */
}

#ifdef RTW88_AIRPORT
void RTW88PCIDevice::airportScanDoneTimerFired(IOTimerEventSource *)
{
    static uint32_t scanDonePosted = 0;
    setProperty("RTW88ScanDonePosted", ++scanDonePosted, 32);
#ifdef IO80211FAMILY_V2
    if (_skywalk) {
        UInt32 messageData = 0;
        _skywalk->postMessage(APPLE80211_M_SCAN_DONE, &messageData,
                              sizeof(messageData), false);
    }
#else
    if (_iface)
        _iface->postMessage(APPLE80211_M_SCAN_DONE);
#endif
}
#endif

void RTW88PCIDevice::stop(IOService *provider)
{
    IOLog("rtw88: RTW88PCIDevice::stop\n");
    teardown();
    super::stop(provider);
}

void RTW88PCIDevice::free()
{
    /* Also cover partially-started instances where stop()/teardown() did not
     * get far enough to release these resources. */
    releaseTxBouncePool();
    if (_compatPciDev) { IOFree(_compatPciDev, sizeof(*_compatPciDev)); _compatPciDev = nullptr; }
    if (_pendingFreeLock) { drainPendingFree(); IOSimpleLockFree(_pendingFreeLock); _pendingFreeLock = nullptr; }
    if (_dmaLock)         { IOSimpleLockFree(_dmaLock);        _dmaLock        = nullptr; }
    if (_txBounceLock)    { IOSimpleLockFree(_txBounceLock);   _txBounceLock   = nullptr; }
    if (_rxFlushLock)     { clearRxPendingQueue(); IOSimpleLockFree(_rxFlushLock); _rxFlushLock = nullptr; }
    super::free();
}

void RTW88PCIDevice::teardown()
{
    /* Stop the IRQ bottom-half from calling back into us before we tear down
     * the output queue it services. */
    rtw88_set_tx_resume_cb(nullptr);
    rtw88_set_napi_flush_cb(nullptr);

    if (_debugTimer)
        _debugTimer->cancelTimeout();
    if (_rxFlushTimer)
        _rxFlushTimer->cancelTimeout();
    if (_txKickTimer)
        _txKickTimer->cancelTimeout();
#ifdef RTW88_AIRPORT
    if (_airportScanDoneTimer)
        _airportScanDoneTimer->cancelTimeout();
#endif
    flushTxBatch();
    if (_intrSrc)
        _intrSrc->disable();
    clearRxPendingQueue();
    if (_txQueue) {
        _txQueue->stop();
        _txQueue->flush();
    }

    if (_enabled) disable(_iface);
    drainPendingFree();   /* release any bounce bufs deferred during TX ISR */

    if (_ieee80211)  { _ieee80211->stop(); _ieee80211->release(); _ieee80211 = nullptr; }

    /* Driver TX rings are drained by stop(), so no pool slot can still be
     * owned by hardware beyond this point. */
    releaseTxBouncePool();

    rtw88_compat_exit();

    if (_debugTimer) { _debugTimer->cancelTimeout(); _workLoop->removeEventSource(_debugTimer); _debugTimer->release(); _debugTimer = nullptr; }
    if (_rxFlushTimer) { _rxFlushTimer->cancelTimeout(); _workLoop->removeEventSource(_rxFlushTimer); _rxFlushTimer->release(); _rxFlushTimer = nullptr; }
    if (_txKickTimer) { _txKickTimer->cancelTimeout(); _workLoop->removeEventSource(_txKickTimer); _txKickTimer->release(); _txKickTimer = nullptr; }
#ifdef RTW88_AIRPORT
    if (_airportScanDoneTimer) { _airportScanDoneTimer->cancelTimeout(); _workLoop->removeEventSource(_airportScanDoneTimer); _airportScanDoneTimer->release(); _airportScanDoneTimer = nullptr; }
#endif
    if (_intrSrc)  { _workLoop->removeEventSource(_intrSrc); _intrSrc->release();  _intrSrc = nullptr; }
    if (_cmdGate)  { _workLoop->removeEventSource(_cmdGate); _cmdGate->release();  _cmdGate = nullptr; }
    if (_txQueue)  { _txQueue->release();   _txQueue = nullptr; }
#ifdef RTW88_AIRPORT
#ifdef IO80211FAMILY_V2
    if (_iface) {
        IONetworkController::detachInterface(_iface, true);
        _iface->release();
        _iface = nullptr;
    }
    if (_skywalk) {
        detachInterface(_skywalk, true);
        _skywalk->release();
        _skywalk = nullptr;
    }
#else
    if (_iface) { detachInterface(_iface); _iface->release(); _iface = nullptr; }
#endif
#else
    if (_iface)    { detachInterface(_iface); _iface->release(); _iface = nullptr; }
#endif
    if (_workLoop) { _workLoop->release();   _workLoop = nullptr; }
    if (_mmioMap)  { _mmioMap->release();    _mmioMap = nullptr; _mmioBase = nullptr; }
    if (_pciDev)   { _pciDev->release();     _pciDev = nullptr; }

    g_pci_dev_instance = nullptr;
    rtw88_pci_io_ops   = nullptr;
    rtw88_dma_ops      = nullptr;
}

/* ------------------------------------------------------------------ */
/*  Interrupt                                                           */
/* ------------------------------------------------------------------ */

bool RTW88PCIDevice::setupInterrupt()
{
    _intrSrc = IOInterruptEventSource::interruptEventSource(
        this,
        OSMemberFunctionCast(IOInterruptEventSource::Action,
                             this, &RTW88PCIDevice::handleInterrupt),
        _pciDev, 0);

    if (!_intrSrc) {
        IOLog("rtw88: failed to create interrupt event source\n");
        return false;
    }
    _workLoop->addEventSource(_intrSrc);

    return true;
}

void RTW88PCIDevice::handleInterrupt(IOInterruptEventSource *src, int count)
{
    if (_ieee80211)
        rtw88_trigger_interrupt();
}

/* ------------------------------------------------------------------ */
/*  IOEthernetController / IONetworkController                          */
/* ------------------------------------------------------------------ */

bool RTW88PCIDevice::attachDevice()
{
#ifdef RTW88_AIRPORT
#ifdef IO80211FAMILY_V2
    _skywalk = new RTW88AirportSkywalkInterface;
    if (!_skywalk || !_skywalk->init(this)) {
        IOLog("rtw88: native Skywalk interface init failed\n");
        return false;
    }
    _skywalk->setInterfaceRole(1);
    _skywalk->setInterfaceId(1);
    if (!_skywalk->attach(this) || !attachInterface(_skywalk, this)) {
        IOLog("rtw88: native Skywalk interface attach failed\n");
        return false;
    }
    if (!IONetworkController::attachInterface((IONetworkInterface **)&_iface,
                                               true)) {
#else
    if (!attachInterface((IONetworkInterface **)&_iface, true)) {
#endif
#else
    if (!attachInterface((IONetworkInterface **)&_iface)) {
#endif
        IOLog("rtw88: attachInterface failed\n");
        return false;
    }

    publishHardwareIdentity();

    _txQueue = OSDynamicCast(IOGatedOutputQueue, getOutputQueue());
    if (_txQueue) _txQueue->retain();

#ifdef RTW88_AIRPORT
#ifdef IO80211FAMILY_V2
    IOSkywalkEthernetInterface::RegistrationInfo registration = {};
    if (!_skywalk->initRegistrationInfo(&registration, 1,
                                         sizeof(registration))) {
        IOLog("rtw88: native Skywalk registration info failed\n");
        return false;
    }
    _skywalk->mExpansionData->fRegistrationInfo =
        (IOSkywalkNetworkInterface::RegistrationInfo *)
        IOMalloc(sizeof(IOSkywalkNetworkInterface::RegistrationInfo));
    _skywalk->mExpansionData2->fRegistrationInfo =
        (IOSkywalkEthernetInterface::RegistrationInfo *)
        IOMalloc(sizeof(IOSkywalkEthernetInterface::RegistrationInfo));
    if (!_skywalk->mExpansionData->fRegistrationInfo ||
        !_skywalk->mExpansionData2->fRegistrationInfo)
        return false;
    memcpy(_skywalk->mExpansionData->fRegistrationInfo, &registration,
           sizeof(registration));
    memcpy(_skywalk->mExpansionData2->fRegistrationInfo, &registration,
           sizeof(registration));
    _skywalk->deferBSDAttach(true);
    _skywalk->start(this);
#endif
#endif
    return true;
}

#ifdef RTW88_AIRPORT
IONetworkInterface *RTW88PCIDevice::createInterface()
{
    RTW88AirportInterface *iface = new RTW88AirportInterface;
    if (!iface)
        return nullptr;
#ifdef IO80211FAMILY_V2
    if (!iface->initWithSkywalkInterfaceAndProvider(this, _skywalk)) {
#else
    if (!iface->init(this)) {
#endif
        iface->release();
        return nullptr;
    }
    return iface;
}
#endif

bool RTW88PCIDevice::setupMediumDict()
{
    OSDictionary *mediums = OSDictionary::withCapacity(4);
    if (!mediums) return false;

#ifdef RTW88_AIRPORT
    addMedium(mediums, kIOMediumIEEE80211, 54);
    addMedium(mediums, kIOMediumIEEE80211None, 0);
#else
    addMedium(mediums, kIOMediumEthernetAuto, 0);
    addMedium(mediums, kIOMediumEthernet10BaseT | kIOMediumOptionFullDuplex,  10);
    addMedium(mediums, kIOMediumEthernet100BaseTX | kIOMediumOptionFullDuplex, 100);
    addMedium(mediums, kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000);
#endif

    bool published = publishMediumDictionary(mediums);
    mediums->release();
    if (!published)
        return false;

    IONetworkMedium *autoMedium = IONetworkMedium::getMediumWithType(
        getMediumDictionary(),
#ifdef RTW88_AIRPORT
        kIOMediumIEEE80211);
    if (!autoMedium || !setCurrentMedium(autoMedium) ||
        !setSelectedMedium(autoMedium))
        return false;
#else
        kIOMediumEthernetAuto);
    if (!autoMedium || !setCurrentMedium(autoMedium) ||
        !setSelectedMedium(autoMedium))
        return false;
    setLinkStatus(kIONetworkLinkActive | kIONetworkLinkValid,
                  IONetworkMedium::getMediumWithType(
                      getMediumDictionary(),
                      kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex));
#endif
    return true;
}

void RTW88PCIDevice::addMedium(OSDictionary *mediums, IOMediumType type, UInt64 speed)
{
    IONetworkMedium *m = IONetworkMedium::medium(type, speed * 1000000ULL);
    if (m) {
        IONetworkMedium::addMedium(mediums, m);
        m->release();
    }
}

IOReturn RTW88PCIDevice::enable(IONetworkInterface *iface)
{
    IOLog("rtw88: enable\n");
    if (_enabled) return kIOReturnSuccess;
    if (!_ieee80211) return kIOReturnNotReady;

    /* Probe ran in start(); now power on the hardware for TX/RX */
    IOReturn ret = _ieee80211->powerOn();
    if (ret != kIOReturnSuccess) {
        IOLog("rtw88: powerOn failed (0x%08x)\n", ret);
        return ret;
    }

    if (_txQueue) _txQueue->start();
    if (_intrSrc) _intrSrc->enable();
    _enabled = true;
#if defined(RTW88_AIRPORT) && !defined(IO80211FAMILY_V2)
    /* A saved macOS Wi-Fi service can retain its previous green "Connected"
     * label across boot even though the fresh hardware state is IDLE.  Publish
     * the real initial link state after power-on so wifiagent performs autojoin
     * and sends APPLE80211_IOC_ASSOCIATE (including the saved PMK). */
    notifyAirportLinkIdle();
#endif
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::disable(IONetworkInterface *iface)
{
    IOLog("rtw88: disable\n");
    if (!_enabled) return kIOReturnSuccess;
    _enabled = false;
    if (_intrSrc) _intrSrc->disable();
    if (_txQueue) _txQueue->stop();
    if (_txQueue) _txQueue->flush();
    if (_txKickTimer) _txKickTimer->cancelTimeout();
    flushTxBatch();
    if (_rxFlushTimer) _rxFlushTimer->cancelTimeout();
    clearRxPendingQueue();
    if (_ieee80211) _ieee80211->powerOff();
    return kIOReturnSuccess;
}

IOOutputQueue *RTW88PCIDevice::createOutputQueue()
{
    /*
     * Without an output queue, IONetworkController delivers outputPacket()
     * straight from the networking stack, which can call it concurrently from
     * multiple threads.  rtw_pci_tx_write_data() computes the TX buffer-
     * descriptor slot from ring->r.wp and fills it BEFORE taking irq_lock
     * (only the wp increment is locked), so two concurrent submissions fill the
     * same slot and skip the next, leaving a zeroed descriptor in the BE ring.
     * The chip then stalls its TX DMA on that zero descriptor (HW rp frozen,
     * FIFO empty) and the shared DMA wedges RX too.
     *
     * An IOGatedOutputQueue runs every outputPacket() under the work-loop gate,
     * serializing submission so the ring fill is single-threaded.
     */
    return IOGatedOutputQueue::withTarget(this, getWorkLoop(), 64);
}

UInt32 RTW88PCIDevice::outputPacket(mbuf_t m, void *param)
{
    drainPendingFree();
    if (!_enabled || !_ieee80211) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }
    /*
     * Backpressure instead of dropping.  When the BE ring is nearly full,
     * rtw_pci_tx_write_data() would return -ENOSPC and rtw_tx() would FREE the
     * skb — silently dropping it.  Under a sustained transfer those dropped
     * frames (TCP ACKs/data) stall the connection.  Returning
     * kIOReturnOutputStall makes IOGatedOutputQueue hold this exact packet and
     * stop dispatching; resumeTxIfStalled() (fired from the IRQ bottom-half
     * after tx_isr frees slots) re-services the queue.  Threshold leaves
     * headroom so rtw_tx never actually hits -ENOSPC.
     */
    size_t plen = mbuf_pkthdr_len(m);
    uint8_t urgency = rtw88_tx_urgency(m);
    /* Preserve headroom for ACK/control traffic, but allow bulk upload to
     * consume a deeper portion of the ring.  This is traffic-aware rather
     * than a fixed global in-flight limit. */
    unsigned int stallAvail = urgency == kRTW88TxUrgencyBulk ?
        kRTW88TxBulkStallAvail : kRTW88TxStallAvail;
    if (rtw88_be_tx_avail() < stallAvail) {
        /* Never strand deferred descriptors behind a stopped software queue. */
        flushTxBatch();
        if (!_txStalled) noteTxStall();
        _txStalled = true;
        return kIOReturnOutputStall;
    }

    bool batchEligible = _ieee80211->dataTxBatchingActive();
    UInt32 ret = _ieee80211->outputPacket(m);
    if (ret == kIOReturnOutputSuccess && batchEligible)
        noteTxBatchSubmission(plen, urgency);
    return ret;
}

IOReturn RTW88PCIDevice::outputStart(IONetworkInterface *iface,
                                      IOOptionBits options)
{
    (void)options;
    if (!iface || !_enabled || !_ieee80211)
        return kIOReturnNotReady;

    uint32_t drained = 0;
    mbuf_t packet = nullptr;
    while (drained < 32 &&
           iface->dequeueOutputPackets(1, &packet) == kIOReturnSuccess) {
        /* configureOutputPullModel requested work-loop synchronization, so
         * outputStart already runs under the same gate used for PCIe work.
         * Enqueuing into the legacy IOGatedOutputQueue here creates a second
         * stopped queue on IO80211 and strands DHCP before it reaches TX. */
        UInt32 ret = outputPacket(packet, nullptr);
        if (ret != kIOReturnOutputSuccess)
            return kIOReturnNoResources;
        drained++;
        packet = nullptr;
    }
    if (drained) {
        setProperty("RTW88OutputStartSeen", 1, 64);
        setProperty("RTW88OutputStartPackets", drained, 64);
    }
    return kIOReturnSuccess;
}

void RTW88PCIDevice::noteTxBatchSubmission(size_t packetLen, uint8_t urgency)
{
    (void)packetLen;
    _txBatchPackets++;
    uint32_t pending = ++_txBatchPending;
    if (pending > _txBatchMaxPending) _txBatchMaxPending = pending;

    if (urgency == kRTW88TxUrgencyImmediate) {
        flushTxBatch(false, true);
        return;
    }

    uint32_t target = urgency == kRTW88TxUrgencyAck ?
        kRTW88TxAckBatchTarget : kRTW88TxBulkBatchTarget;
    uint32_t delayUs = urgency == kRTW88TxUrgencyAck ?
        kRTW88TxAckDelayUs : kRTW88TxBulkDelayUs;

    if (pending >= target) {
        flushTxBatch(false, false);
        return;
    }
    if (_txKickTimer && pending == 1) {
        uint64_t deadline = 0;
        clock_interval_to_deadline(delayUs, kMicrosecondScale, &deadline);
        _txKickTimer->wakeAtTime(deadline);
    } else if (!_txKickTimer) {
        flushTxBatch(false, false);
    }
}

void RTW88PCIDevice::flushTxBatch(bool timerKick, bool immediateKick)
{
    if (_txBatchPending == 0 || !_ieee80211) return;
    if (_txKickTimer) _txKickTimer->cancelTimeout();
    _ieee80211->kickDataTx();
    _txBatchPending = 0;
    _txBatchKicks++;
    if (timerKick) _txBatchTimerKicks++;
    if (immediateKick) _txBatchImmediateKicks++;
}

void RTW88PCIDevice::txKickTimerFired(IOTimerEventSource *src)
{
    (void)src;
    flushTxBatch(true, false);
}

void RTW88PCIDevice::resumeTxIfStalled()
{
    /* Runs on the IRQ bottom-half thread (no rtw88 locks held). Use async
     * service so we never block on the output-queue gate from here. */
    if (_txStalled && rtw88_be_tx_avail() >= kRTW88TxResumeAvail) {
        _txStalled = false;
        if (_txQueue)
            _txQueue->service(IOBasicOutputQueue::kServiceAsync);
    }
}

void RTW88PCIDevice::resetTxFlowForReconnect()
{
    /* A stopped association can leave the exact packet that caused an
     * OutputStall parked in IOGatedOutputQueue.  Sending that packet after a
     * new PTK/STA is installed is both useless and a source of renewed ring
     * pressure.  Drop only the software queue here; the rtw88 association
     * cleanup owns the hardware-side STA/key teardown. */
    if (_txKickTimer) _txKickTimer->cancelTimeout();
    flushTxBatch();
    _txStalled = false;
    if (_txQueue) {
        _txQueue->stop();
        _txQueue->flush();
        if (_enabled)
            _txQueue->start();
    }
}

IOReturn RTW88PCIDevice::getHardwareAddress(IOEthernetAddress *addr)
{
    if (!_ieee80211) return kIOReturnNotReady;
    _ieee80211->getMACAddress(addr->bytes);
    memcpy(_macAddr.bytes, addr->bytes, 6);
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setHardwareAddress(const IOEthernetAddress *addr)
{
    memcpy(_macAddr.bytes, addr->bytes, 6);
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::getMaxPacketSize(UInt32 *maxSize) const
{
    *maxSize = 2346; /* IEEE80211 max MSDU */
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setMaxPacketSize(UInt32 maxSize)
{
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::selectMedium(const IONetworkMedium *medium)
{
    if (!medium || !setSelectedMedium(medium) || !setCurrentMedium(medium))
        return kIOReturnBadArgument;
    return kIOReturnSuccess;
}

bool RTW88PCIDevice::configureInterface(IONetworkInterface *iface)
{
    if (!super::configureInterface(iface)) return false;
    IONetworkData *nd = iface->getNetworkData(kIONetworkStatsKey);
    if (nd) nd->setAccessTypes(kIONetworkDataAccessTypeRead);
#if defined(RTW88_AIRPORT) && !defined(IO80211FAMILY_V2)
    IOReturn pullRet = iface->configureOutputPullModel(
        64, kIONetworkWorkLoopSynchronous, 0,
        IONetworkInterface::kOutputPacketSchedulingModelNormal, 0);
    setProperty("RTW88OutputPullConfigured",
                pullRet == kIOReturnSuccess ? 1 : 0, 64);
    if (pullRet != kIOReturnSuccess)
        IOLog("rtw88: configureOutputPullModel failed (0x%08x)\n", pullRet);
#endif
    return true;
}

IOReturn RTW88PCIDevice::getPacketFilters(const OSSymbol *group,
                                            UInt32 *filters) const
{
    if (group->isEqualTo(kIOEthernetWakeOnLANFilterGroup)) {
        *filters = 0;
        return kIOReturnSuccess;
    }
    return super::getPacketFilters(group, filters);
}

IOReturn RTW88PCIDevice::setMulticastMode(bool active)
{
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setPromiscuousMode(bool active)
{
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::powerStateWillChangeTo(IOPMPowerFlags flags,
                                                  unsigned long state,
                                                  IOService *actor)
{
    IOLog("rtw88: powerStateWillChangeTo %lu\n", state);
    return IOPMAckImplied;
}

/* ------------------------------------------------------------------ */
/*  RX injection (called from RTW88IEEE80211 on frame receive)         */
/* ------------------------------------------------------------------ */

mbuf_t RTW88PCIDevice::allocateInputPacket(uint32_t len)
{
    /* IONetworkController::allocatePacket returns an mbuf set up exactly the
     * way inputPacket() expects: m_len and m_pkthdr.len are both set and
     * consistent across every segment of the chain.  Hand-rolling this with
     * mbuf_allocpacket left m_len inconsistent with pkthdr.len, which the
     * dlil input validator rejects with "Failed mbuf validity check: len -14". */
    return allocatePacket(len);
}

void RTW88PCIDevice::injectRxFrame(mbuf_t m)
{
    drainPendingFree();
    if (!_iface || !_enabled) {
        freePacket(m);
        return;
    }

    /* Last line of defense: never hand the networking stack a malformed
     * packet.  Validate BOTH length fields — the dlil validator panics on
     * m_len (printed as "len"), not just pkthdr.len.  mbuf_len/mbuf_pkthdr_len
     * return size_t, so a negative m_len shows up as a huge value here. */
    size_t plen = mbuf_pkthdr_len(m);
    size_t mlen = mbuf_len(m);
    if (plen < 14 || plen > 4096 || mlen < 14 || mlen > 4096) {
        IOLog("rtw88: injectRxFrame: dropping bogus mbuf (pkthdr.len=%zu m_len=%zu)\n",
              plen, mlen);
        freePacket(m);
        return;
    }

    /* IONetworkInterface's input queue has no lock.  Do not call inputPacket()
     * from NAPI: even enqueueing there can corrupt the queue while the
     * controller workloop is inside flushInputQueue().  Stage mbufs in our
     * protected queue and let flushRxBatch() perform both operations. */
    bool armFallback = false;
    bool queued = false;
    if (_rxFlushLock) {
        IOSimpleLockLock(_rxFlushLock);
        if (_rxPendingCount < kRxPendingLimit) {
            mbuf_setnextpkt(m, nullptr);
            if (_rxPendingTail)
                mbuf_setnextpkt(_rxPendingTail, m);
            else
                _rxPendingHead = m;
            _rxPendingTail = m;
            _rxPendingCount++;
            queued = true;
            if (!_rxFlushPending) {
                _rxFlushPending = true;
                armFallback = true;
            }
        }
        IOSimpleLockUnlock(_rxFlushLock);
    }

    if (!queued) {
        freePacket(m);
        _rxQueueDrops++;
        if (_rxQueueDrops == 1)
            setProperty("RTW88RxQueueDrops", 1, 64);
        IONetworkData *nd = _iface->getNetworkData(kIONetworkStatsKey);
        if (nd) {
            IONetworkStats *stats = (IONetworkStats *)nd->getBuffer();
            if (stats) stats->inputErrors++;
        }
        return;
    }

    _rxQueuedPackets++;
    if (_rxQueuedPackets == 1)
        setProperty("RTW88RxQueuedPackets", 1, 64);
    if (armFallback && _rxFlushTimer)
        _rxFlushTimer->setTimeoutMS(2);

    IONetworkData *nd = _iface->getNetworkData(kIONetworkStatsKey);
    if (nd) {
        IONetworkStats *stats = (IONetworkStats *)nd->getBuffer();
        if (stats) stats->inputPackets++;
    }
}

void RTW88PCIDevice::flushRxBatch()
{
    mbuf_t batch = nullptr;
    uint32_t batchCount = 0;
    if (_rxFlushLock) {
        IOSimpleLockLock(_rxFlushLock);
        batch = _rxPendingHead;
        batchCount = _rxPendingCount;
        _rxPendingHead = nullptr;
        _rxPendingTail = nullptr;
        _rxPendingCount = 0;
        _rxFlushPending = false;
        IOSimpleLockUnlock(_rxFlushLock);
    }

    if (!batch)
        return;

    if (_iface && _enabled) {
        uint32_t submitted = 0;
        while (batch) {
            mbuf_t next = mbuf_nextpkt(batch);
            mbuf_setnextpkt(batch, nullptr);
            _iface->inputPacket(batch, 0,
                                IONetworkInterface::kInputOptionQueuePacket);
            batch = next;
            submitted++;
        }
        _iface->flushInputQueue();
        _rxFlushes++;
        if (_rxFlushes == 1)
            setProperty("RTW88RxFlushes", 1, 64);
        if (submitted != batchCount)
            setProperty("RTW88RxBatchCountMismatch", 1, 32);
    } else {
        while (batch) {
            mbuf_t next = mbuf_nextpkt(batch);
            mbuf_setnextpkt(batch, nullptr);
            freePacket(batch);
            batch = next;
        }
    }
}

void RTW88PCIDevice::clearRxPendingQueue()
{
    mbuf_t batch = nullptr;
    if (_rxFlushLock) {
        IOSimpleLockLock(_rxFlushLock);
        batch = _rxPendingHead;
        _rxPendingHead = nullptr;
        _rxPendingTail = nullptr;
        _rxPendingCount = 0;
        _rxFlushPending = false;
        IOSimpleLockUnlock(_rxFlushLock);
    }

    while (batch) {
        mbuf_t next = mbuf_nextpkt(batch);
        mbuf_setnextpkt(batch, nullptr);
        freePacket(batch);
        batch = next;
    }
}

void RTW88PCIDevice::flushRxAfterNapiPoll()
{
    /* NAPI is not the controller workloop.  It only stages packets in the
     * driver-owned queue and requests an immediate timer callback.  The timer
     * is the sole thread allowed to touch IONetworkInterface's input queue. */
    if (!_enabled || !_rxFlushTimer)
        return;

    bool pending = false;
    if (_rxFlushLock) {
        IOSimpleLockLock(_rxFlushLock);
        pending = _rxFlushPending;
        IOSimpleLockUnlock(_rxFlushLock);
    } else {
        pending = _rxFlushPending;
    }
    if (pending)
        _rxFlushTimer->setTimeoutMS(0);
}

void RTW88PCIDevice::rxFlushTimerFired(IOTimerEventSource *src)
{
    (void)src;
    /* Sole runtime flush executor, serialized by the controller workloop. */
    flushRxBatch();
}

/* ------------------------------------------------------------------ */
/*  DMA coherent allocation                                             */
/* ------------------------------------------------------------------ */

bool RTW88PCIDevice::initTxBouncePool()
{
    if (_txBouncePoolDesc)
        return true;

    const size_t total = (size_t)kTxBouncePoolSlots * kTxBounceSlotSize;
    _txBouncePoolDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIOMemoryPhysicallyContiguous | kIODirectionInOut |
            kIOMemoryKernelUserShared,
        total,
        0x00000000FFFFFFF0ULL);
    if (!_txBouncePoolDesc)
        return false;

    if (_txBouncePoolDesc->prepare() != kIOReturnSuccess) {
        _txBouncePoolDesc->release();
        _txBouncePoolDesc = nullptr;
        return false;
    }

    _txBouncePoolVirt = _txBouncePoolDesc->getBytesNoCopy();
    _txBouncePoolPhys = _txBouncePoolDesc->getPhysicalAddress();
    if (!_txBouncePoolVirt || !_txBouncePoolPhys) {
        _txBouncePoolDesc->complete();
        _txBouncePoolDesc->release();
        _txBouncePoolDesc = nullptr;
        _txBouncePoolVirt = nullptr;
        _txBouncePoolPhys = 0;
        return false;
    }

    memset(_txBouncePoolVirt, 0, total);
    memset(_txBounceUsed, 0, sizeof(_txBounceUsed));
    _txBounceCursor = 0;
    _txPoolInUse = 0;
    _txPoolPeak = 0;

    IOLog("rtw88: exp2 TX bounce pool ready: %u x %zu bytes at PA 0x%llx\n",
          kTxBouncePoolSlots, kTxBounceSlotSize,
          (unsigned long long)_txBouncePoolPhys);
    return true;
}

void RTW88PCIDevice::releaseTxBouncePool()
{
    if (!_txBouncePoolDesc)
        return;

    if (_txBounceLock) {
        IOSimpleLockLock(_txBounceLock);
        memset(_txBounceUsed, 0, sizeof(_txBounceUsed));
        _txPoolInUse = 0;
        IOSimpleLockUnlock(_txBounceLock);
    }

    _txBouncePoolDesc->complete();
    _txBouncePoolDesc->release();
    _txBouncePoolDesc = nullptr;
    _txBouncePoolVirt = nullptr;
    _txBouncePoolPhys = 0;
}

IOPhysicalAddress RTW88PCIDevice::mapTxBounce(const void *src, size_t size)
{
    if (!_txBouncePoolDesc || !_txBouncePoolVirt || !_txBounceLock || !src ||
        size == 0 || size > kTxBounceSlotSize) {
        _txPoolFallbacks++;
        return 0;
    }

    int slot = -1;
    IOSimpleLockLock(_txBounceLock);
    for (uint32_t n = 0; n < kTxBouncePoolSlots; n++) {
        uint32_t i = (_txBounceCursor + n) % kTxBouncePoolSlots;
        if (!_txBounceUsed[i]) {
            _txBounceUsed[i] = true;
            _txBounceCursor = (i + 1) % kTxBouncePoolSlots;
            slot = (int)i;
            _txPoolInUse++;
            if (_txPoolInUse > _txPoolPeak)
                _txPoolPeak = _txPoolInUse;
            break;
        }
    }
    IOSimpleLockUnlock(_txBounceLock);

    if (slot < 0) {
        _txPoolFallbacks++;
        return 0;
    }

    void *dst = (void *)((uint8_t *)_txBouncePoolVirt +
                         (size_t)slot * kTxBounceSlotSize);
    memcpy(dst, src, size);
    _txPoolHits++;

    return _txBouncePoolPhys + (IOPhysicalAddress)((size_t)slot *
                                                    kTxBounceSlotSize);
}

bool RTW88PCIDevice::unmapTxBounce(IOPhysicalAddress phys)
{
    if (!_txBouncePoolDesc || !_txBounceLock || phys < _txBouncePoolPhys)
        return false;

    const IOPhysicalAddress span =
        (IOPhysicalAddress)((size_t)kTxBouncePoolSlots * kTxBounceSlotSize);
    IOPhysicalAddress off = phys - _txBouncePoolPhys;
    if (off >= span || (off % kTxBounceSlotSize) != 0)
        return false;

    uint32_t slot = (uint32_t)(off / kTxBounceSlotSize);
    IOSimpleLockLock(_txBounceLock);
    if (_txBounceUsed[slot]) {
        _txBounceUsed[slot] = false;
        if (_txPoolInUse > 0)
            _txPoolInUse--;
    }
    IOSimpleLockUnlock(_txBounceLock);
    return true;
}

void *RTW88PCIDevice::allocCoherent(size_t size, IOPhysicalAddress *phys)
{
    /*
     * rtw88 TX ring descriptors store DMA addresses in 32-bit fields.
     * Restrict physical allocation to the first 4GB so truncation to
     * cpu_to_le32() in the ring descriptor is lossless.
     * 0x00000000FFFFFFF0 = below 4GB, 16-byte aligned.
     */
    IOBufferMemoryDescriptor *desc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIOMemoryPhysicallyContiguous | kIODirectionInOut | kIOMemoryKernelUserShared,
        size,
        0x00000000FFFFFFF0ULL);

    if (!desc) { IOLog("rtw88: dma alloc failed, size=%zu\n", size); return nullptr; }
    desc->prepare();

    IOPhysicalAddress pa = desc->getPhysicalAddress();
    void *va = desc->getBytesNoCopy();
    memset(va, 0, size);

    if (phys) *phys = pa;

    /* Allocate the tracking node OUTSIDE the spinlock — IOMallocZero is
     * sleepable, and IOSimpleLock disables preemption.  Calling a sleepable
     * allocator from a preemption-disabled context panics XNU with
     * "blocking while holding a spinlock". */
    DMAEntry *entry = (DMAEntry *)IOMallocZero(sizeof(DMAEntry));
    if (!entry) {
        desc->complete();
        desc->release();
        return nullptr;
    }
    entry->desc = desc;
    entry->virt = va;
    entry->phys = pa;
    entry->size = size;

    IOSimpleLockLock(_dmaLock);
    entry->next = _dmaList;
    _dmaList = entry;
    IOSimpleLockUnlock(_dmaLock);

    return va;
}

void RTW88PCIDevice::freeCoherent(size_t size, void *virt, IOPhysicalAddress phys)
{
    IOSimpleLockLock(_dmaLock);
    DMAEntry **prev = &_dmaList;
    for (DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->virt == virt) {
            *prev = e->next;
            IOSimpleLockUnlock(_dmaLock);
            if (preemption_enabled()) {
                e->desc->complete();
                e->desc->release();
                IOFree(e, sizeof(*e));
            } else {
                IOSimpleLockLock(_pendingFreeLock);
                e->next = _dmaPendingFree;
                _dmaPendingFree = e;
                IOSimpleLockUnlock(_pendingFreeLock);
            }
            return;
        }
        prev = &e->next;
    }
    IOSimpleLockUnlock(_dmaLock);
    IOLog("rtw88: freeCoherent: virt %p not found\n", virt);
}

void RTW88PCIDevice::freeCoherentByPhys(IOPhysicalAddress phys)
{
    IOSimpleLockLock(_dmaLock);
    DMAEntry **prev = &_dmaList;
    for (DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == phys) {
            *prev = e->next;
            IOSimpleLockUnlock(_dmaLock);
            if (preemption_enabled()) {
                e->desc->complete();
                e->desc->release();
                IOFree(e, sizeof(*e));
            } else {
                IOSimpleLockLock(_pendingFreeLock);
                e->next = _dmaPendingFree;
                _dmaPendingFree = e;
                IOSimpleLockUnlock(_pendingFreeLock);
            }
            return;
        }
        prev = &e->next;
    }
    IOSimpleLockUnlock(_dmaLock);
}

void RTW88PCIDevice::drainPendingFree()
{
    if (!_pendingFreeLock) return;
    IOSimpleLockLock(_pendingFreeLock);
    DMAEntry *list      = _dmaPendingFree;
    _dmaPendingFree     = nullptr;
    IOSimpleLockUnlock(_pendingFreeLock);

    for (DMAEntry *e = list; e; ) {
        DMAEntry *next = e->next;
        e->desc->complete();
        e->desc->release();
        IOFree(e, sizeof(*e));
        e = next;
    }
}

void RTW88PCIDevice::setBounceOrigVA(IOPhysicalAddress phys, void *orig_va)
{
    IOSimpleLockLock(_dmaLock);
    for (DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == phys) {
            e->orig_va = orig_va;
            break;
        }
    }
    IOSimpleLockUnlock(_dmaLock);
}

void RTW88PCIDevice::syncBounceForCpu(IOPhysicalAddress dma, size_t size)
{
    /*
     * Called by dma_sync_single_for_cpu(DMA_FROM_DEVICE) after the chip
     * has finished writing received packet data into the bounce buffer.
     * Copy bounce → original skb->data so the driver can parse the packet.
     */
    IOSimpleLockLock(_dmaLock);
    for (DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == dma && e->orig_va && e->virt) {
            size_t copy_len = (size <= e->size) ? size : e->size;
            IOSimpleLockUnlock(_dmaLock);
            memcpy(e->orig_va, e->virt, copy_len);
            return;
        }
    }
    IOSimpleLockUnlock(_dmaLock);
}

/* ------------------------------------------------------------------ */
/*  PCI config space                                                    */
/* ------------------------------------------------------------------ */

UInt8 RTW88PCIDevice::pciReadByte(int offset)
{
    return _pciDev->configRead8((UInt8)offset);
}
UInt16 RTW88PCIDevice::pciReadWord(int offset)
{
    return _pciDev->configRead16((UInt8)offset);
}
UInt32 RTW88PCIDevice::pciReadDword(int offset)
{
    return _pciDev->configRead32((UInt8)offset);
}
void RTW88PCIDevice::pciWriteByte(int offset, UInt8 val)
{
    _pciDev->configWrite8((UInt8)offset, val);
}
void RTW88PCIDevice::pciWriteWord(int offset, UInt16 val)
{
    _pciDev->configWrite16((UInt8)offset, val);
}
void RTW88PCIDevice::pciWriteDword(int offset, UInt32 val)
{
    _pciDev->configWrite32((UInt8)offset, val);
}
int RTW88PCIDevice::pciFindCapability(int cap)
{
    /* Walk PCIe capability list */
    UInt8 cap_ptr = _pciDev->configRead8(0x34) & ~3;
    while (cap_ptr) {
        UInt8 cap_id = _pciDev->configRead8(cap_ptr);
        if (cap_id == cap) return cap_ptr;
        cap_ptr = _pciDev->configRead8(cap_ptr + 1) & ~3;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  IOUserClient creation                                               */
/* ------------------------------------------------------------------ */

IOReturn RTW88PCIDevice::newUserClient(task_t owningTask, void *securityID,
                                        UInt32 type, OSDictionary *properties,
                                        IOUserClient **handler)
{
    RTW88UserClient *client = new RTW88UserClient;
    if (!client) {
        IOLog("rtw88: RTW88UserClient allocation failed\n");
        return kIOReturnNoMemory;
    }

    /* initWithTask — not init() — binds the Mach task port.
     * Without this the kernel port is never "ready for callouts" and
     * IOServiceOpen returns kIOReturnBadArgument before our code runs. */
    if (!client->initWithTask(owningTask, securityID, type, properties)) {
        IOLog("rtw88: RTW88UserClient::initWithTask failed\n");
        client->release();
        return kIOReturnBadArgument;
    }

    if (!client->attach(this)) {
        IOLog("rtw88: RTW88UserClient::attach failed\n");
        client->release();
        return kIOReturnBadArgument;
    }

    if (!client->start(this)) {
        IOLog("rtw88: RTW88UserClient::start failed\n");
        client->detach(this);
        client->release();
        return kIOReturnBadArgument;
    }

    *handler = client;
    return kIOReturnSuccess;
}
