/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * RTW88UserClient.hpp — IOUserClient for rtw88ctl IPC
 *
 * Selector numbers must match ctl/main.c RTW88_CMD_* defines.
 */
#pragma once

#include <IOKit/IOUserClient.h>

class RTW88PCIDevice;

/* ------------------------------------------------------------------ */
/*  Selector constants — keep in sync with ctl/main.c                  */
/* ------------------------------------------------------------------ */
enum RTW88UserClientSelector {
    kRTW88Scan        = 0,
    kRTW88Connect     = 1,
    kRTW88Disconnect  = 2,
    kRTW88GetState    = 3,
    kRTW88GetBSSList  = 4,
    kRTW88GetRSSI     = 5,
    kRTW88SetDebug    = 6,
    kRTW88GetLog      = 7,
    kRTW88PowerOn     = 8,
    kRTW88PowerOff    = 9,
    kRTW88GetPerf     = 10,
    kRTW88SetTxRate   = 11,
    kRTW88NumSelectors
};

/* Structures passed through IOConnectCallStructMethod */
struct RTW88ConnectArgs {
    char ssid[33];
    char password[64];
};

struct RTW88StateResult {
    uint32_t state;
    uint8_t  bssid[6];
    char     ssid[33];
    int32_t  rssi;
    uint32_t channel;
    uint8_t  mac_addr[6];
    uint16_t fw_version;
    uint8_t  fw_sub_version;
    char     chip_name[32];
    uint32_t rx_byte_count;
    uint32_t tx_byte_count;
    uint8_t  scan_offload_supported;
    uint8_t  powered;
};

struct RTW88PerfResult {
    uint32_t state;
    int32_t  rssi;
    uint32_t channel;
    uint32_t channel_width_mhz;
    uint32_t tx_byte_count;
    uint32_t rx_byte_count;
    uint32_t be_tx_free;
    uint8_t  tx_ba_active;
    uint8_t  rx_ba_active_count;
    uint8_t  powered;
    uint8_t  reserved0;
    uint64_t dma_tx_maps;
    uint64_t dma_rx_maps;
    uint64_t dma_tx_bytes;
    uint64_t dma_rx_bytes;
    uint64_t dma_rx_copy_bytes;
    uint64_t tx_stall_events;
    uint64_t tx_pool_hits;
    uint64_t tx_pool_fallbacks;
    uint64_t rx_queued_packets;
    uint64_t rx_flushes;
    uint32_t tx_pool_in_use;
    uint32_t tx_pool_peak;

    /* exp4: actual firmware/RA state and loss/reorder sampling */
    uint8_t  fw_tx_desc_rate;
    uint8_t  fw_rx_desc_rate;
    uint8_t  sta_vht;
    uint8_t  sta_sgi;
    uint8_t  sta_bw_mode;
    uint8_t  sta_rate_id;
    uint8_t  ra_mcs;
    uint8_t  ra_nss;
    uint8_t  ra_bw;
    uint8_t  ra_flags;
    uint16_t reserved1;
    uint32_t ra_bit_rate;
    uint64_t ra_mask;
    uint64_t tx_report_requested;
    uint64_t tx_report_acked;
    uint64_t tx_report_failed;
    uint64_t rx_reorder_inputs;
    uint64_t rx_reorder_stale;
    uint64_t rx_reorder_duplicates;
    uint64_t rx_reorder_ahead;
    uint64_t rx_reorder_timeout_flushes;
    uint64_t rx_reorder_holes_skipped;
    uint64_t irq_count;
    uint64_t napi_polls;
    uint64_t napi_work;
    uint64_t napi_budget_hits;

    /* exp5: station bridge + firmware RSSI feed diagnostics */
    uint8_t  sta_avg_rssi;
    uint8_t  sta_rssi_level;
    uint8_t  ra_calibrated_rssi;
    uint8_t  ra_fw_rssi;
    uint8_t  dm_min_rssi;
    uint8_t  sta_bridge_registered;
    uint32_t watchdog_count;
    uint64_t sta_iter_calls;
    uint64_t sta_find_hits;

    /* exp8 TX descriptor/path diagnostics */
    uint8_t  txpi_rate;
    uint8_t  txpi_rate_id;
    uint8_t  txpi_bw;
    uint8_t  txpi_short_gi;
    uint8_t  txpi_use_rate;
    uint8_t  txpi_dis_rate_fallback;
    uint8_t  txpi_ampdu_en;
    uint8_t  fix_rate;
    uint64_t txpi_samples;
    /* exp10 stability + fast IRQ diagnostics */
    uint64_t link_loss_events;
    uint64_t auto_reconnect_attempts;
    uint64_t auto_reconnect_successes;
    uint64_t radio_recovery_count;
    uint64_t beacon_watchdog_recoveries;
    uint64_t irq_inline_bottom_halves;
    uint32_t reconnect_attempt_streak;
    uint16_t last_ap_disconnect_reason;
    uint8_t  beacon_miss_windows;
    uint8_t  beacon_watchdog_active;

    /* exp12 coalesced datapath diagnostics */
    uint64_t napi_schedule_calls;
    uint64_t napi_coalesced_calls;
    uint64_t napi_delayed_calls;
    uint64_t napi_flush_callbacks;
    uint64_t tx_batch_packets;
    uint64_t tx_batch_kicks;
    uint64_t tx_batch_timer_kicks;
    uint64_t tx_batch_immediate_kicks;
    uint32_t tx_batch_pending;
    uint32_t tx_batch_max_pending;

};

/* ------------------------------------------------------------------ */
class RTW88UserClient : public IOUserClient {
    OSDeclareDefaultStructors(RTW88UserClient)

public:
    static RTW88UserClient *create(RTW88PCIDevice *dev, task_t owningTask);

    bool     init(OSDictionary *props) override;
    bool     initWithTask(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties) override;
    bool     initWithTask(task_t owningTask, void *securityID, UInt32 type) override;
    bool     start(IOService *provider) override;
    void     stop(IOService *provider) override;
    void     free() override;

    /* IOUserClient */
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
                            IOExternalMethodDispatch *dispatch,
                            OSObject *target, void *reference) override;

private:
    /* Dispatch table */
    static IOReturn sScan(RTW88UserClient *target, void *ref,
                          IOExternalMethodArguments *args);
    static IOReturn sConnect(RTW88UserClient *target, void *ref,
                             IOExternalMethodArguments *args);
    static IOReturn sDisconnect(RTW88UserClient *target, void *ref,
                                IOExternalMethodArguments *args);
    static IOReturn sGetState(RTW88UserClient *target, void *ref,
                              IOExternalMethodArguments *args);
    static IOReturn sGetBSSList(RTW88UserClient *target, void *ref,
                                IOExternalMethodArguments *args);
    static IOReturn sGetRSSI(RTW88UserClient *target, void *ref,
                              IOExternalMethodArguments *args);
    static IOReturn sSetDebug(RTW88UserClient *target, void *ref,
                               IOExternalMethodArguments *args);
    static IOReturn sGetLog(RTW88UserClient *target, void *ref,
                             IOExternalMethodArguments *args);
    static IOReturn sPowerOn(RTW88UserClient *target, void *ref,
                             IOExternalMethodArguments *args);
    static IOReturn sPowerOff(RTW88UserClient *target, void *ref,
                              IOExternalMethodArguments *args);
    static IOReturn sGetPerf(RTW88UserClient *target, void *ref,
                             IOExternalMethodArguments *args);
    static IOReturn sSetTxRate(RTW88UserClient *target, void *ref,
                               IOExternalMethodArguments *args);

    static const IOExternalMethodDispatch sMethods[kRTW88NumSelectors];

    RTW88PCIDevice *_provider    = nullptr;
    task_t          _owningTask  = nullptr;
};
