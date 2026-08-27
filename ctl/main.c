// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// rtw88ctl — userspace control binary for rtw88 macOS kext

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

/* ------------------------------------------------------------------ */
/*  Selector numbers — must match RTW88UserClient.hpp                  */
/* ------------------------------------------------------------------ */
enum {
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
};

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

    uint8_t  txpi_rate;
    uint8_t  txpi_rate_id;
    uint8_t  txpi_bw;
    uint8_t  txpi_short_gi;
    uint8_t  txpi_use_rate;
    uint8_t  txpi_dis_rate_fallback;
    uint8_t  txpi_ampdu_en;
    uint8_t  fix_rate;
    uint64_t txpi_samples;
    /* exp12 stability + fast IRQ diagnostics */
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


static void desc_rate_format(uint8_t r, char *buf, size_t len)
{
    static const char *legacy[] = {
        "1M", "2M", "5.5M", "11M", "6M", "9M", "12M", "18M",
        "24M", "36M", "48M", "54M"
    };
    if (!buf || len == 0) return;
    if (r <= 0x0b) { snprintf(buf, len, "%s", legacy[r]); return; }
    if (r >= 0x0c && r <= 0x2b) {
        snprintf(buf, len, "HT-MCS%u", (unsigned)(r - 0x0c));
        return;
    }
    if (r >= 0x2c && r <= 0x53) {
        unsigned x = (unsigned)(r - 0x2c);
        unsigned nss = x / 10 + 1;
        unsigned mcs = x % 10;
        snprintf(buf, len, "VHT%uSS-MCS%u", nss, mcs);
        return;
    }
    snprintf(buf, len, "desc-0x%02x", r);
}

static unsigned bw_mode_mhz(uint8_t bw)
{
    switch (bw) { case 2: return 80; case 1: return 40; default: return 20; }
}

static double estimate_vht_phy_mbps(uint8_t mcs, uint8_t nss, uint8_t bw, int sgi)
{
    static const double vht20_lgi[10] = {6.5,13.0,19.5,26.0,39.0,52.0,58.5,65.0,78.0,86.7};
    static const double vht20_sgi[10] = {7.2,14.4,21.7,28.9,43.3,57.8,65.0,72.2,86.7,96.3};
    static const double vht40_lgi[10] = {13.5,27.0,40.5,54.0,81.0,108.0,121.5,135.0,162.0,180.0};
    static const double vht40_sgi[10] = {15.0,30.0,45.0,60.0,90.0,120.0,135.0,150.0,180.0,200.0};
    static const double vht80_lgi[10] = {29.3,58.5,87.8,117.0,175.5,234.0,263.3,292.5,351.0,390.0};
    static const double vht80_sgi[10] = {32.5,65.0,97.5,130.0,195.0,260.0,292.5,325.0,390.0,433.3};
    if (mcs > 9 || nss == 0) return 0.0;
    const double *t = NULL;
    if (bw == 2) t = sgi ? vht80_sgi : vht80_lgi;
    else if (bw == 1) t = sgi ? vht40_sgi : vht40_lgi;
    else t = sgi ? vht20_sgi : vht20_lgi;
    return t[mcs] * nss;
}

static const char *state_name(uint32_t s)
{
    switch (s) {
    case 0: return "idle";
    case 1: return "scanning";
    case 2: return "authenticating";
    case 3: return "associating";
    case 4: return "handshaking";
    case 5: return "connected";
    case 6: return "disconnecting";
    default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/*  IOKit connection helpers                                            */
/* ------------------------------------------------------------------ */
static io_connect_t open_kext(void)
{
    /* IOServiceMatching finds the instantiated C++ IOService object */
    CFMutableDictionaryRef matching = IOServiceMatching("RTW88PCIDevice");
    if (!matching) {
        fprintf(stderr, "rtw88ctl: failed to create matching dict\n");
        return MACH_PORT_NULL;
    }

    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                       matching);
    if (!service) {
        fprintf(stderr, "rtw88ctl: no rtw88 device found "
                "(is rtw88.kext loaded?)\n");
        return MACH_PORT_NULL;
    }

    io_connect_t conn = MACH_PORT_NULL;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &conn);
    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: IOServiceOpen failed: %s\n",
                mach_error_string(kr));
        return MACH_PORT_NULL;
    }
    return conn;
}

/* ------------------------------------------------------------------ */
/*  Commands                                                            */
/* ------------------------------------------------------------------ */

static int cmd_list(io_connect_t conn); /* forward */

static int cmd_scan(io_connect_t conn, int wait_secs)
{
    kern_return_t kr = IOConnectCallScalarMethod(conn, kRTW88Scan,
                                                  NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: scan failed: %s\n", mach_error_string(kr));
        return 1;
    }

    printf("Scanning");
    fflush(stdout);

    /* Poll state until scan is done (kext returns to idle) or timeout.
     * The kernel scan is async; we wait here so the user sees the
     * results immediately after the command returns. */
    int done = 0;
    for (int i = 0; i < wait_secs; i++) {
        sleep(1);
        putchar('.');
        fflush(stdout);

        struct RTW88StateResult result = {};
        size_t sz = sizeof(result);
        if (IOConnectCallStructMethod(conn, kRTW88GetState,
                                      NULL, 0, &result, &sz) == KERN_SUCCESS) {
            /* state==1 is SCANNING; anything else (idle) means done */
            if (result.state != 1) { done = 1; break; }
        }
    }
    putchar('\n');
    if (!done)
        printf("Scan still running after %d s — showing partial results.\n",
               wait_secs);

    return cmd_list(conn);
}

static int cmd_list(io_connect_t conn)
{
    /* Must stay ≤ 4095 bytes — IOKit MIG inband limit is 4096.
     * Larger buffers switch to the OOL descriptor path where
     * args->structureOutput is NULL, causing a silent empty result. */
    uint8_t buf[4095] = {};
    size_t  len = sizeof(buf);

    kern_return_t kr = IOConnectCallStructMethod(conn, kRTW88GetBSSList,
                                                  NULL, 0, buf, &len);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: get BSS list failed: %s\n",
                mach_error_string(kr));
        return 1;
    }

    /* Debug: show raw returned length and first 32 bytes */
    fprintf(stderr, "[dbg] IOKit returned len=%zu, first bytes:", len);
    for (size_t i = 0; i < 32 && i < len; i++) fprintf(stderr, " %02x", buf[i]);
    fprintf(stderr, "\n");

    printf("%-33s %-18s %5s %7s %s\n",
           "SSID", "BSSID", "RSSI", "Channel", "Security");
    printf("%s\n",
           "-----------------------------------------------------------------------");

    const uint8_t *p   = buf;
    
    uint32_t explicit_len = 0;
    if (len >= 4) {
        memcpy(&explicit_len, p, 4);
        p += 4;
        /* Ensure we don't read past what was actually copied back */
        if (explicit_len > len) explicit_len = (uint32_t)len;
    }

    const uint8_t *end = buf + explicit_len;
    while (p + 1 <= end) {
        uint8_t ssid_len = *p++;
        if (p + ssid_len + 6 + 2 + 1 + 4 > end) break;

        char ssid[33] = {};
        memcpy(ssid, p, ssid_len);
        p += ssid_len;

        uint8_t bssid[6];
        memcpy(bssid, p, 6); p += 6;

        int16_t rssi = (int16_t)(((uint16_t)p[0] << 8) | p[1]); p += 2;
        uint8_t ch   = *p++;
        uint32_t cipher; memcpy(&cipher, p, 4); p += 4;

        const char *sec = (cipher == 0x000FAC04) ? "WPA2" :
                          (cipher == 0x000FAC02) ? "TKIP" :
                          (cipher == 0) ? "Open" : "Unknown";

        printf("%-33s %02x:%02x:%02x:%02x:%02x:%02x %4d dBm %4d  %s\n",
               ssid,
               bssid[0], bssid[1], bssid[2],
               bssid[3], bssid[4], bssid[5],
               rssi, ch, sec);
    }
    return 0;
}

static int cmd_connect(io_connect_t conn, const char *ssid, const char *pass)
{
    struct RTW88ConnectArgs args = {};
    strlcpy(args.ssid, ssid, sizeof(args.ssid));
    if (pass) strlcpy(args.password, pass, sizeof(args.password));

    kern_return_t kr = IOConnectCallStructMethod(conn, kRTW88Connect,
                                                  &args, sizeof(args),
                                                  NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: connect failed: %s\n",
                mach_error_string(kr));
        return 1;
    }
    printf("Connecting to '%s'...\n", ssid);

    /* Poll state until connected or 30s timeout */
    for (int i = 0; i < 30; i++) {
        sleep(1);
        struct RTW88StateResult result = {};
        size_t sz = sizeof(result);
        kr = IOConnectCallStructMethod(conn, kRTW88GetState,
                                       NULL, 0, &result, &sz);
        if (kr == KERN_SUCCESS) {
            printf("\r[%2ds] state: %-16s", i + 1, state_name(result.state));
            fflush(stdout);
            if (result.state == 5) { /* connected */
                printf("\nConnected! RSSI: %d dBm\n", result.rssi);
                return 0;
            }
            if (result.state == 0 && i > 3) {
                printf("\nConnection failed\n");
                return 1;
            }
        }
    }
    printf("\nConnection timed out\n");
    return 1;
}

static int cmd_disconnect(io_connect_t conn)
{
    kern_return_t kr = IOConnectCallScalarMethod(conn, kRTW88Disconnect,
                                                  NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: disconnect failed: %s\n",
                mach_error_string(kr));
        return 1;
    }
    printf("Disconnected\n");
    return 0;
}

static int cmd_status(io_connect_t conn)
{
    struct RTW88StateResult result = {};
    size_t sz = sizeof(result);

    kern_return_t kr = IOConnectCallStructMethod(conn, kRTW88GetState,
                                                  NULL, 0, &result, &sz);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: get state failed: %s\n",
                mach_error_string(kr));
        return 1;
    }
    printf("State:      %s\n", state_name(result.state));
    printf("Card:       %s\n", result.chip_name);
    printf("MAC:        %02x:%02x:%02x:%02x:%02x:%02x\n",
           result.mac_addr[0], result.mac_addr[1], result.mac_addr[2],
           result.mac_addr[3], result.mac_addr[4], result.mac_addr[5]);
    printf("Firmware:   v%u.%u\n", result.fw_version, result.fw_sub_version);
    printf("Scan offld: %s\n", result.scan_offload_supported ? "yes" : "no");
    printf("Power:      %s\n", result.powered ? "on" : "off");
    
    if (result.state == 5) {
        printf("SSID:       %s\n", result.ssid[0] ? result.ssid : "(unknown)");
        printf("BSSID:      %02x:%02x:%02x:%02x:%02x:%02x\n",
               result.bssid[0], result.bssid[1], result.bssid[2],
               result.bssid[3], result.bssid[4], result.bssid[5]);
        printf("RSSI:       %d dBm\n", result.rssi);
        printf("Channel:    %u\n", result.channel);
        printf("TX Bytes:   %u\n", result.tx_byte_count);
        printf("RX Bytes:   %u\n", result.rx_byte_count);
    }
    return 0;
}

static int cmd_log(io_connect_t conn)
{
    char buf[4096] = {};
    size_t sz = sizeof(buf);
    kern_return_t kr = IOConnectCallStructMethod(conn, kRTW88GetLog,
                                                  NULL, 0, buf, &sz);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: get log failed: %s\n",
                mach_error_string(kr));
        return 1;
    }
    fwrite(buf, 1, sz, stdout);
    return 0;
}

static int cmd_debug(io_connect_t conn, int level)
{
    uint64_t in = (uint64_t)level;
    kern_return_t kr = IOConnectCallScalarMethod(conn, kRTW88SetDebug,
                                                  &in, 1, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: set debug failed: %s\n",
                mach_error_string(kr));
        return 1;
    }
    printf("Debug level set to %d\n", level);
    return 0;
}

static int cmd_power(io_connect_t conn, int on)
{
    kern_return_t kr = IOConnectCallStructMethod(conn,
                                                  on ? kRTW88PowerOn : kRTW88PowerOff,
                                                  NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: power %s failed: %s\n",
                on ? "on" : "off", mach_error_string(kr));
        return 1;
    }
    printf("Power %s\n", on ? "on" : "off");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Usage                                                               */
/* ------------------------------------------------------------------ */

static int get_perf(io_connect_t conn, struct RTW88PerfResult *r)
{
    size_t sz = sizeof(*r);
    memset(r, 0, sizeof(*r));
    kern_return_t kr = IOConnectCallStructMethod(conn, kRTW88GetPerf,
                                                  NULL, 0, r, &sz);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: perf query failed: %s\n", mach_error_string(kr));
        return 1;
    }
    return 0;
}

static int cmd_perf(io_connect_t conn, int seconds)
{
    struct RTW88PerfResult a = {}, b = {};
    if (get_perf(conn, &a)) return 1;
    if (seconds < 1) seconds = 1;

    printf("NativeV2 performance diagnostics (%d s sample)\n", seconds);
    printf("State: %s | RSSI: %d dBm | Channel: %u | Width: %u MHz\n",
           state_name(a.state), a.rssi, a.channel, a.channel_width_mhz);
    printf("A-MPDU: TX=%s RX-TIDs=%u | BE ring free=%u | powered=%s\n",
           a.tx_ba_active ? "on" : "off", a.rx_ba_active_count,
           a.be_tx_free, a.powered ? "yes" : "no");

    sleep((unsigned)seconds);
    if (get_perf(conn, &b)) return 1;

    uint32_t dtx = b.tx_byte_count - a.tx_byte_count;
    uint32_t drx = b.rx_byte_count - a.rx_byte_count;
    double tx_mbps = ((double)dtx * 8.0) / ((double)seconds * 1000000.0);
    double rx_mbps = ((double)drx * 8.0) / ((double)seconds * 1000000.0);
    uint64_t dma_tx = b.dma_tx_bytes - a.dma_tx_bytes;
    uint64_t dma_rx = b.dma_rx_bytes - a.dma_rx_bytes;
    uint64_t dma_cp = b.dma_rx_copy_bytes - a.dma_rx_copy_bytes;

    printf("Driver payload rate: TX %.2f Mbps | RX %.2f Mbps\n", tx_mbps, rx_mbps);
    printf("DMA mapped during sample: TX %.2f MiB | RX %.2f MiB | RX memcpy %.2f MiB\n",
           dma_tx / 1048576.0, dma_rx / 1048576.0, dma_cp / 1048576.0);
    printf("DMA maps: TX +%llu | RX +%llu | TX-stall events +%llu\n",
           (unsigned long long)(b.dma_tx_maps - a.dma_tx_maps),
           (unsigned long long)(b.dma_rx_maps - a.dma_rx_maps),
           (unsigned long long)(b.tx_stall_events - a.tx_stall_events));

    uint64_t pool_hits = b.tx_pool_hits - a.tx_pool_hits;
    uint64_t pool_fallbacks = b.tx_pool_fallbacks - a.tx_pool_fallbacks;
    uint64_t rx_pkts = b.rx_queued_packets - a.rx_queued_packets;
    uint64_t rx_flushes = b.rx_flushes - a.rx_flushes;
    double batch = rx_flushes ? (double)rx_pkts / (double)rx_flushes : 0.0;
    printf("exp12 TX pool: hits +%llu | fallback +%llu | in-use=%u peak=%u\n",
           (unsigned long long)pool_hits,
           (unsigned long long)pool_fallbacks,
           b.tx_pool_in_use, b.tx_pool_peak);
    printf("exp12 RX batching: packets +%llu | flushes +%llu | %.1f pkt/flush\n",
           (unsigned long long)rx_pkts,
           (unsigned long long)rx_flushes, batch);
    double rx_copy_mib = (double)(b.dma_rx_copy_bytes - a.dma_rx_copy_bytes) / (1024.0 * 1024.0);
    double rx_copy_kib_per_pkt = rx_pkts ?
        (double)(b.dma_rx_copy_bytes - a.dma_rx_copy_bytes) / 1024.0 / (double)rx_pkts : 0.0;
    printf("exp12 RX copy cost: %.2f MiB total | %.2f KiB/delivered-pkt\n",
           rx_copy_mib, rx_copy_kib_per_pkt);

    /* exp4: distinguish a correctly configured 80 MHz channel from the
     * firmware actually selecting a fast VHT data rate. */
    char tx_rate_name[32], rx_rate_name[32];
    desc_rate_format(b.fw_tx_desc_rate, tx_rate_name, sizeof(tx_rate_name));
    desc_rate_format(b.fw_rx_desc_rate, rx_rate_name, sizeof(rx_rate_name));
    printf("exp12 link RA: FW-TX=%s (0x%02x) | FW-RX=%s (0x%02x) | "
           "STA VHT=%s BW=%uMHz SGI=%s rate-id=%u\n",
           tx_rate_name, b.fw_tx_desc_rate, rx_rate_name, b.fw_rx_desc_rate,
           b.sta_vht ? "yes" : "no", bw_mode_mhz(b.sta_bw_mode),
           b.sta_sgi ? "yes" : "no", b.sta_rate_id);
    double phy = (b.ra_flags & 0x02) ?
        estimate_vht_phy_mbps(b.ra_mcs, b.ra_nss, b.ra_bw, (b.ra_flags & 0x04) != 0) : 0.0;
    printf("exp12 RA report: flags=0x%02x MCS=%u NSS=%u BW=%uMHz SGI=%s",
           b.ra_flags, b.ra_mcs, b.ra_nss, bw_mode_mhz(b.ra_bw),
           (b.ra_flags & 0x04) ? "yes" : "no");
    if (phy > 0.0) printf(" | estimated PHY %.1f Mbps", phy);
    printf(" | mask=0x%016llx\n", (unsigned long long)b.ra_mask);

    char txpi_name[32];
    desc_rate_format(b.txpi_rate, txpi_name, sizeof(txpi_name));
    printf("exp12 TX pkt-info: rate=%s (0x%02x) rate-id=%u BW=%uMHz SGI=%s "
           "use-rate=%s fallback-disabled=%s AMPDU=%s | fix=0x%02x | samples +%llu\n",
           txpi_name, b.txpi_rate, b.txpi_rate_id, bw_mode_mhz(b.txpi_bw),
           b.txpi_short_gi ? "yes" : "no", b.txpi_use_rate ? "yes" : "no",
           b.txpi_dis_rate_fallback ? "yes" : "no", b.txpi_ampdu_en ? "yes" : "no",
           b.fix_rate, (unsigned long long)(b.txpi_samples - a.txpi_samples));

    uint64_t rpt_req = b.tx_report_requested - a.tx_report_requested;
    uint64_t rpt_ack = b.tx_report_acked - a.tx_report_acked;
    uint64_t rpt_fail = b.tx_report_failed - a.tx_report_failed;
    double ack_pct = (rpt_ack + rpt_fail) ?
        100.0 * (double)rpt_ack / (double)(rpt_ack + rpt_fail) : 0.0;
    printf("exp12 sampled TX ACK: requested +%llu | acked +%llu | failed +%llu | %.1f%% ACK\n",
           (unsigned long long)rpt_req, (unsigned long long)rpt_ack,
           (unsigned long long)rpt_fail, ack_pct);

    printf("exp12 RX reorder: inputs +%llu | stale +%llu | dup +%llu | ahead +%llu | "
           "timeout +%llu | holes-skipped +%llu\n",
           (unsigned long long)(b.rx_reorder_inputs - a.rx_reorder_inputs),
           (unsigned long long)(b.rx_reorder_stale - a.rx_reorder_stale),
           (unsigned long long)(b.rx_reorder_duplicates - a.rx_reorder_duplicates),
           (unsigned long long)(b.rx_reorder_ahead - a.rx_reorder_ahead),
           (unsigned long long)(b.rx_reorder_timeout_flushes - a.rx_reorder_timeout_flushes),
           (unsigned long long)(b.rx_reorder_holes_skipped - a.rx_reorder_holes_skipped));
    printf("exp12 IRQ/NAPI: IRQ +%llu | polls +%llu | RX-work +%llu | budget-hit +%llu\n",
           (unsigned long long)(b.irq_count - a.irq_count),
           (unsigned long long)(b.napi_polls - a.napi_polls),
           (unsigned long long)(b.napi_work - a.napi_work),
           (unsigned long long)(b.napi_budget_hits - a.napi_budget_hits));
    uint64_t napi_sched = b.napi_schedule_calls - a.napi_schedule_calls;
    uint64_t napi_coal = b.napi_coalesced_calls - a.napi_coalesced_calls;
    uint64_t napi_delay = b.napi_delayed_calls - a.napi_delayed_calls;
    uint64_t napi_flush = b.napi_flush_callbacks - a.napi_flush_callbacks;
    double work_per_poll = (b.napi_polls - a.napi_polls) ?
        (double)(b.napi_work - a.napi_work) / (double)(b.napi_polls - a.napi_polls) : 0.0;
    printf("exp12 NAPI coalescing: schedule +%llu | collapsed +%llu | delayed +%llu | "
           "flush-cb +%llu | %.2f work/poll\n",
           (unsigned long long)napi_sched, (unsigned long long)napi_coal,
           (unsigned long long)napi_delay, (unsigned long long)napi_flush, work_per_poll);

    uint64_t txbp = b.tx_batch_packets - a.tx_batch_packets;
    uint64_t txbk = b.tx_batch_kicks - a.tx_batch_kicks;
    uint64_t txbt = b.tx_batch_timer_kicks - a.tx_batch_timer_kicks;
    uint64_t txbi = b.tx_batch_immediate_kicks - a.tx_batch_immediate_kicks;
    double pkt_per_kick = txbk ? (double)txbp / (double)txbk : 0.0;
    printf("exp12 TX batching: packets +%llu | kicks +%llu | %.2f pkt/kick | "
           "timer +%llu immediate +%llu | pending=%u max=%u\n",
           (unsigned long long)txbp, (unsigned long long)txbk, pkt_per_kick,
           (unsigned long long)txbt, (unsigned long long)txbi,
           b.tx_batch_pending, b.tx_batch_max_pending);
    printf("exp12 fast IRQ: inline bottom-halves +%llu / IRQ +%llu\n",
           (unsigned long long)(b.irq_inline_bottom_halves - a.irq_inline_bottom_halves),
           (unsigned long long)(b.irq_count - a.irq_count));
    printf("exp12 stability: link-loss +%llu | reconnect-attempt +%llu | recovered +%llu | "
           "radio-reset +%llu | beacon-watchdog +%llu | streak=%u | AP-reason=%u | "
           "beacon-watch=%s miss=%u\n",
           (unsigned long long)(b.link_loss_events - a.link_loss_events),
           (unsigned long long)(b.auto_reconnect_attempts - a.auto_reconnect_attempts),
           (unsigned long long)(b.auto_reconnect_successes - a.auto_reconnect_successes),
           (unsigned long long)(b.radio_recovery_count - a.radio_recovery_count),
           (unsigned long long)(b.beacon_watchdog_recoveries - a.beacon_watchdog_recoveries),
           b.reconnect_attempt_streak, b.last_ap_disconnect_reason,
           b.beacon_watchdog_active ? "active" : "passive", b.beacon_miss_windows);
    printf("exp23 RA truth: bridge=%s | EWMA=%u | calibrated=%u | level=%u | fw-rssi=%u | dm-min=%u | "
           "watchdog=%u (+%u) | sta-iter +%llu | sta-find +%llu\n",
           b.sta_bridge_registered ? "yes" : "no",
           b.sta_avg_rssi, b.ra_calibrated_rssi, b.sta_rssi_level,
           b.ra_fw_rssi, b.dm_min_rssi,
           b.watchdog_count, (unsigned)(b.watchdog_count - a.watchdog_count),
           (unsigned long long)(b.sta_iter_calls - a.sta_iter_calls),
           (unsigned long long)(b.sta_find_hits - a.sta_find_hits));

    printf("End: width=%u MHz, TX A-MPDU=%s, RX-TIDs=%u, BE free=%u\n",
           b.channel_width_mhz, b.tx_ba_active ? "on" : "off",
           b.rx_ba_active_count, b.be_tx_free);

    if (b.channel_width_mhz < 80 && b.channel > 14)
        printf("NOTE: 5 GHz link is below 80 MHz; this can cap throughput.\n");
    if (!b.tx_ba_active || b.rx_ba_active_count == 0)
        printf("NOTE: BlockAck/A-MPDU is incomplete; this is a likely throughput limiter.\n");
    if ((b.tx_stall_events - a.tx_stall_events) > 0)
        printf("NOTE: TX ring backpressure occurred during the sample.\n");
    if (pool_fallbacks > 0)
        printf("NOTE: TX DMA pool fell back %llu times; pool pressure/oversize remains.\n",
               (unsigned long long)pool_fallbacks);
    if (rx_pkts > 100 && batch < 2.0)
        printf("NOTE: RX delivery batching is still weak (<2 packets/flush).\n");
    if ((b.napi_work - a.napi_work) > 100 && work_per_poll < 2.0)
        printf("NOTE: NAPI is still averaging <2 RX descriptors per poll.\n");
    if (txbp > 100 && pkt_per_kick < 1.5)
        printf("NOTE: TX doorbell batching is being defeated by immediate/sparse traffic.\n");

    return 0;
}

static int cmd_txrate(io_connect_t conn, const char *mode)
{
    uint64_t rate = 0xff;
    if (!mode || strcmp(mode, "auto") == 0) rate = 0xff;
    else if (strcmp(mode, "mcs7") == 0) rate = 0x33;
    else if (strcmp(mode, "mcs8") == 0) rate = 0x34;
    else if (strcmp(mode, "mcs9") == 0) rate = 0x35;
    else {
        fprintf(stderr, "rtw88ctl: txrate must be auto|mcs7|mcs8|mcs9\n");
        return 1;
    }
    kern_return_t kr = IOConnectCallScalarMethod(conn, kRTW88SetTxRate,
                                                  &rate, 1, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "rtw88ctl: txrate failed: %s\n", mach_error_string(kr));
        return 1;
    }
    printf("TX diagnostic rate mode: %s%s\n", mode,
           rate == 0xff ? " (firmware RA)" : " (forced VHT 1SS / descriptor test)");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s <command> [options]\n"
        "\n"
        "Commands:\n"
        "  scan [-w <secs>]         Scan and print results when done (default 10s)\n"
        "  list                     Show last scan results without re-scanning\n"
        "  connect <ssid> [pass]    Connect to a network\n"
        "  disconnect               Disconnect\n"
        "  power on|off             Toggle IEEE80211 radio power\n"
        "  status                   Show current connection status\n"
        "  perf [seconds]           Sample link/DMA/descriptor diagnostics\n"
        "  txrate auto|mcs7|mcs8|mcs9  Diagnostic TX-rate override (default auto)\n"
        "  log                      Dump driver log buffer\n"
        "  debug <level>            Set debug level (0=err 1=warn 2=info 3=dbg)\n"
        "\n"
        "Examples:\n"
        "  %s scan -w 10            Scan for 10 seconds\n"
        "  %s list                  Show found networks\n"
        "  %s connect \"MyWiFi\" \"password123\"\n"
        "  %s status\n"
        "\n"
        "Notes:\n"
        "  - rtw88.kext must be loaded (OpenCore injection or kextload)\n"
        "  - Works in BaseSystem (Recovery) environment\n"
        "  - Run with sudo if IOServiceOpen fails\n",
        argv0, argv0, argv0, argv0, argv0);
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    io_connect_t conn = open_kext();
    if (conn == MACH_PORT_NULL) return 1;

    int ret = 0;
    const char *cmd = argv[1];

    if (strcmp(cmd, "scan") == 0) {
        int wait = 10;
        int opt;
        while ((opt = getopt(argc - 1, argv + 1, "w:")) != -1) {
            if (opt == 'w') wait = atoi(optarg);
        }
        ret = cmd_scan(conn, wait);

    } else if (strcmp(cmd, "list") == 0) {
        ret = cmd_list(conn);

    } else if (strcmp(cmd, "connect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "rtw88ctl: connect requires SSID\n");
            ret = 1;
        } else {
            ret = cmd_connect(conn, argv[2], argc >= 4 ? argv[3] : NULL);
        }

    } else if (strcmp(cmd, "disconnect") == 0) {
        ret = cmd_disconnect(conn);

    } else if (strcmp(cmd, "power") == 0) {
        if (argc < 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
            fprintf(stderr, "rtw88ctl: power requires on or off\n");
            ret = 1;
        } else {
            ret = cmd_power(conn, strcmp(argv[2], "on") == 0);
        }

    } else if (strcmp(cmd, "status") == 0) {
        ret = cmd_status(conn);
    } else if (strcmp(cmd, "perf") == 0) {
        int seconds = 5;
        if (argc >= 3) {
            seconds = atoi(argv[2]);
            if (seconds < 1) seconds = 1;
            if (seconds > 120) seconds = 120;
        }
        ret = cmd_perf(conn, seconds);

    } else if (strcmp(cmd, "txrate") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s txrate auto|mcs7|mcs8|mcs9\n", argv[0]);
            ret = 1;
        } else {
            ret = cmd_txrate(conn, argv[2]);
        }

    } else if (strcmp(cmd, "log") == 0) {
        ret = cmd_log(conn);

    } else if (strcmp(cmd, "debug") == 0) {
        if (argc < 3) { fprintf(stderr, "debug requires level\n"); ret = 1; }
        else ret = cmd_debug(conn, atoi(argv[2]));

    } else {
        fprintf(stderr, "rtw88ctl: unknown command '%s'\n", cmd);
        usage(argv[0]);
        ret = 1;
    }

    IOServiceClose(conn);
    return ret;
}
