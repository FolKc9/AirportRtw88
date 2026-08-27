/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Master compatibility header for rtw88 macOS port.
 * Every driver .c file that includes linux/ or net/ headers ends up here
 * via the -I src/compat include path override in the Makefile.
 */
#ifndef _RTW88_COMPAT_H
#define _RTW88_COMPAT_H

/* GCC/clang attribute stubs not available on macOS kernel */
#ifndef __nonstring
#define __nonstring
#endif
/* __cold and __pure may be defined by sys/cdefs.h — guard and redefine */
#ifdef __cold
#undef __cold
#endif
#define __cold __attribute__((cold))
#ifdef __pure
#undef __pure
#endif
#define __pure __attribute__((pure))
#ifndef fallthrough
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 7)
#define fallthrough __attribute__((__fallthrough__))
#else
#define fallthrough do {} while (0)
#endif
#endif

/* Pull in all compat headers in dependency order */
#include "iokit_shim.h"
#include "linux/types.h"
#include "linux/device.h"
#include "linux/bitops.h"
#include "linux/bitfield.h"
#include "linux/atomic.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "linux/jiffies.h"
#include "linux/delay.h"
#include "linux/iopoll.h"
#include "linux/average.h"
#include "linux/spinlock.h"
#include "linux/mutex.h"
#include "linux/completion.h"
#include "linux/timer.h"
#include "linux/workqueue.h"
#include "linux/if_ether.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "linux/interrupt.h"
#include "linux/netdevice.h"
#include "linux/firmware.h"
#include "linux/dma-mapping.h"
#include "linux/pci.h"
#include "linux/usb.h"
#include "linux/module.h"
#include "linux/leds.h"
#include "linux/devcoredump.h"
#include "linux/seq_file.h"
#include "net/mac80211.h"

/* ------------------------------------------------------------------ */
/*  Missing RTW88 driver bits that mac80211.h references               */
/* ------------------------------------------------------------------ */

struct rtw_dev;

/* exp4: compact snapshot of rtw88 firmware rate-adaptation state.  Defined
 * here so the C++ kext can query opaque driver internals without including
 * the Linux driver's main.h directly. */
struct rtw88_link_diag {
    u8 fw_tx_desc_rate;
    u8 fw_rx_desc_rate;
    u8 sta_vht;
    u8 sta_sgi;
    u8 sta_bw_mode;
    u8 sta_rate_id;
    u8 ra_mcs;
    u8 ra_nss;
    u8 ra_bw;
    u8 ra_flags;
    u16 reserved;
    u32 ra_bit_rate;
    u64 ra_mask;
    /* exp5: prove the mac80211 station bridge is feeding RSSI to firmware. */
    u8 sta_avg_rssi;
    u8 sta_rssi_level;
    u8 ra_calibrated_rssi;
    u8 ra_fw_rssi;
    u8 dm_min_rssi;
    u8 sta_bridge_registered;
    u32 watchdog_count;
    u64 sta_iter_calls;
    u64 sta_find_hits;

    /* exp8: packet-info / descriptor inputs actually handed to HCI. */
    u8 txpi_rate;
    u8 txpi_rate_id;
    u8 txpi_bw;
    u8 txpi_short_gi;
    u8 txpi_use_rate;
    u8 txpi_dis_rate_fallback;
    u8 txpi_ampdu_en;
    u8 fix_rate;
    u64 txpi_samples;
};

struct rtw_tx_pkt_info;
void rtw88_exp7_tx_diag_record(const struct rtw_tx_pkt_info *pkt);
void rtw88_set_fix_rate(struct ieee80211_hw *hw, u8 rate);
/* macOS IOWorkLoop-owned RSSI/RA tick; avoids the generic Linux workqueue. */
void rtw88_macos_watchdog_tick(struct rtw_dev *rtwdev);
void rtw88_get_link_diag(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
                         struct rtw88_link_diag *out);
void rtw88_get_sched_diag(u64 *irq_count, u64 *napi_polls, u64 *napi_work,
                          u64 *napi_budget_hits);
void rtw88_get_napi_coalesce_diag(u64 *schedule_calls, u64 *coalesced_calls,
                                  u64 *delayed_calls, u64 *flush_callbacks);
u64 rtw88_get_inline_irq_bh_count(void);

/* dev_err / dev_warn / dev_info / dev_dbg */
void rtw88_dev_printk(int level, struct device *dev, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define dev_err(dev, fmt, ...)  rtw88_dev_printk(KERN_ERR,   dev, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) rtw88_dev_printk(KERN_WARN,  dev, fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) rtw88_dev_printk(KERN_INFO,  dev, fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)  rtw88_dev_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__)

/* devcoredump used in main.c */
#define dev_coredump(dev, data, datalen, gfp) dev_coredumpv(dev, data, datalen, gfp)

/* rculist stubs — used in some rtw88 files */
#define rcu_read_lock()   do {} while (0)
#define rcu_read_unlock() do {} while (0)
#define rcu_dereference(p) (p)
#define rcu_dereference_protected(p, c) (p)
#define RCU_INIT_POINTER(p, v) ((p) = (v))
#define rcu_assign_pointer(p, v) ((p) = (v))
#define synchronize_rcu() do {} while (0)

/* srcu stubs */
struct srcu_struct { int dummy; };
#define __SRCU_DEP_MAP_INIT(name)
#define DEFINE_STATIC_SRCU(name) struct srcu_struct name
static inline int srcu_read_lock(struct srcu_struct *s) { return 0; }
static inline void srcu_read_unlock(struct srcu_struct *s, int idx) {}
static inline void synchronize_srcu(struct srcu_struct *s) {}

/* refcount */
typedef atomic_t refcount_t;
#define refcount_set(r, v)  atomic_set(r, v)
#define refcount_read(r)    atomic_read(r)
#define refcount_inc(r)     atomic_inc(r)
#define refcount_dec_and_test(r) atomic_dec_and_test(r)

/* nl80211 reason codes */
#define WLAN_REASON_UNSPECIFIED             1
#define WLAN_REASON_DEAUTH_LEAVING          3
#define WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY 4
#define WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT  15
#define WLAN_REASON_GROUP_KEY_UPDATE_TIMEOUT 16
#define WLAN_REASON_IE_IN_4WAY_DIFFERS      17

/* nl80211 status codes */
#define WLAN_STATUS_SUCCESS              0
#define WLAN_STATUS_UNSPECIFIED_FAILURE  1
#define WLAN_STATUS_CAPS_UNSUPPORTED     10

/* WMM/QoS */
#define IEEE80211_MAX_QUEUES  4

/* IEEE80211 element IDs */
#define WLAN_EID_SSID                0
#define WLAN_EID_SUPP_RATES          1
#define WLAN_EID_DS_PARAMS           3
#define WLAN_EID_TIM                 5
#define WLAN_EID_IBSS_PARAMS         6
#define WLAN_EID_COUNTRY             7
#define WLAN_EID_RSN                48
#define WLAN_EID_HT_CAPABILITY       45
#define WLAN_EID_HT_OPERATION        61
#define WLAN_EID_VHT_CAPABILITY     191
#define WLAN_EID_VHT_OPERATION      192
#define WLAN_EID_EXT_CAPABILITY      127
#define WLAN_EID_EXT_SUPP_RATES      50
#define WLAN_EID_VENDOR_SPECIFIC     221

/* Action-frame categories / BlockAck actions (802.11 BlockAck, category 3).
 * Used by the MLME to negotiate A-MPDU aggregation over the air. */
#define WLAN_CATEGORY_BACK           3
#define WLAN_ACTION_ADDBA_REQ        0
#define WLAN_ACTION_ADDBA_RESP       1
#define WLAN_ACTION_DELBA            2

/* ------------------------------------------------------------------ */
/*  Compat global state init/exit                                       */
/* ------------------------------------------------------------------ */

int  rtw88_compat_init(void);
void rtw88_compat_exit(void);

/* Set the firmware resources directory (call before rtw_pci_probe) */
void rtw88_set_fw_dir(const char *dir);
/* Auto-detect firmware directory from boot-args or well-known paths */
void rtw88_find_fw_dir(void);

void rtw88_get_fw_version(struct rtw_dev *rtwdev, uint16_t *version, uint8_t *sub_version);
void rtw88_get_chip_name(struct rtw_dev *rtwdev, char *name_buf, size_t buf_sz);
void rtw88_get_stats(struct rtw_dev *rtwdev, uint32_t *tx_bytes, uint32_t *rx_bytes);
uint32_t rtw88_read_log(char *out_buf, uint32_t max_len);

void rtw88_reenable_interrupt(void);

/* Dump BE TX ring + interrupt state to IOLog. Called periodically by the
 * kext's debug timer to diagnose TX freeze; see rtw88_compat.c. */
void rtw88_debug_dump_tx_state(void);

/* Force-disable BT coexistence by clearing efuse.btcoex. Must be called
 * between rtw_pci_probe and the chip's start() op. See rtw88_compat.c. */
void rtw88_force_wifi_only(void);

/* TX flow control. rtw88_be_tx_avail() returns the BE ring's free-slot count
 * so the output path can stall (backpressure) instead of dropping when full.
 * rtw88_set_tx_resume_cb() registers a hook fired after each IRQ bottom-half
 * (post tx_isr, no locks held) so the kext can resume a stalled queue. */
unsigned int rtw88_be_tx_avail(void);
void rtw88_set_tx_resume_cb(void (*cb)(void));
/* exp12: called after each NAPI poll so macOS can flush the packet batch once
 * per poll instead of once per frame/timer tick. */
void rtw88_set_napi_flush_cb(void (*cb)(void));

struct ieee80211_hw;
struct ieee80211_vif;

/* Returns true while the driver's RTW_FLAG_SCANNING bit is set.
 * Used by the kext to wait for post-scan MMIO cleanup before connecting. */
bool rtw88_is_scanning(void);
bool rtw88_hw_scan_supported(struct ieee80211_hw *hw);
void rtw88_sw_scan_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void rtw88_sw_scan_switch_channel(struct ieee80211_hw *hw);
void rtw88_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif);

/*
 * Configure channel + BSSID in the chip for the connect flow.
 * Bypasses rtw_ops_config / rtw_ops_bss_info_changed (and their
 * rtw_leave_lps_deep MMIO-read polling) to avoid post-scan PCIe hangs.
 * Caller must set hw->conf.chandef before calling.
 */
void rtw88_connect_hw_setup(struct ieee80211_hw *hw,
                             struct ieee80211_vif *vif,
                             const uint8_t *bssid);
void rtw88_restore_connected_hw(struct ieee80211_hw *hw,
                                 struct ieee80211_vif *vif,
                                 const uint8_t *bssid);

/* Register the single active VIF so ieee80211_iterate_active_interfaces*
 * can call back into rtw88 internals (e.g. rtw_build_rsvd_page_iter for
 * the firmware reserved-page download after association). */
void rtw88_register_vif(struct ieee80211_vif *vif);
void rtw88_unregister_vif(void);

/* exp5: this port has one associated peer.  Register it so the Linux rtw88
 * station iterators / lookups used by RX RSSI accounting and rate adaptation
 * see the same ieee80211_sta that the kext gave to sta_add(). */
void rtw88_register_sta(struct ieee80211_sta *sta);
void rtw88_unregister_sta(struct ieee80211_sta *sta);

#endif /* _RTW88_COMPAT_H */
