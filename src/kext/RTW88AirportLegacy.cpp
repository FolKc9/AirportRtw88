/* Ventura IO80211 ABI bridge, intended for IO80211FamilyLegacy on Tahoe. */
#include "RTW88PCIDevice.hpp"
#include "RTW88AirportInterface.hpp"
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"

#if defined(RTW88_AIRPORT) && !defined(IO80211FAMILY_V2)
#include <IOKit/IOLib.h>

static const uint32_t kAirportScanCacheCapacity = 64;
static RTW88BSS gScanCache[kAirportScanCacheCapacity];
static uint32_t gScanCount;
static uint32_t gScanIndex;
static uint32_t gScanRejected;
static apple80211_scan_result gScanResult;
static uint32_t gScanRequests;
static uint32_t gScanCompletions;
static uint32_t gScanResultReads;
static uint32_t gScanCoalesced;
static uint32_t gScanRSSINormalized;
static uint32_t gScanCacheWraps;
static uint32_t gScanResultCalls;
static uint32_t gScanEmptyReads;
static uint32_t gScanCacheRetained;
static uint32_t gScanCacheClearDeferred;
static uint32_t gAssocRequests;
static uint32_t gEmptyScanCompletionRetries;
static bool gScanResultsPending;
static uint32_t gAuthLower;
static uint32_t gAuthUpper;
static const uint8_t kRsnElementId = 48;

static uint32_t rtwLegacyState(uint32_t state)
{
    /* Keep IO80211 in SCAN until it consumes the terminating result code.
     * Returning INIT before APPLE80211_M_SCAN_DONE makes wifiagent discard
     * the completion without ever asking for APPLE80211_IOC_SCAN_RESULT. */
    if (gScanResultsPending && state == RTW88_STATE_IDLE)
        return APPLE80211_S_SCAN;

    switch (state) {
    case RTW88_STATE_SCANNING:       return APPLE80211_S_SCAN;
    case RTW88_STATE_AUTHENTICATING: return APPLE80211_S_AUTH;
    case RTW88_STATE_ASSOCIATING:
    case RTW88_STATE_HANDSHAKING:    return APPLE80211_S_ASSOC;
    case RTW88_STATE_CONNECTED:      return APPLE80211_S_RUN;
    default:                         return APPLE80211_S_INIT;
    }
}

static uint32_t rtwLegacyChannelFlags(uint8_t channel)
{
    bool dfs = channel >= 52 && channel <= 144;
    return (dfs ? APPLE80211_C_FLAG_DFS : APPLE80211_C_FLAG_ACTIVE) |
        APPLE80211_C_FLAG_20MHZ |
        (channel <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
}

static IOReturn rtwLegacyRefreshScanCache(RTW88PCIDevice *device)
{
    if (!device || !device->get80211())
        return kIOReturnNotReady;

    uint32_t count = kAirportScanCacheCapacity;
    IOReturn ret = device->get80211()->cmdGetAirportBSSList(gScanCache,
                                                            &count);
    if (ret != kIOReturnSuccess)
        return ret;

    gScanCount = count;
    gScanIndex = 0;
    gScanRejected = 0;
    return kIOReturnSuccess;
}

static uint16_t rtwLegacyCopySecurityIE(const RTW88BSS &bss, uint8_t *out,
                                        uint16_t capacity)
{
    uint16_t offset = 0;
    while (offset + 2 <= bss.ies_len) {
        uint8_t id = bss.ies[offset];
        uint16_t length = (uint16_t)(2 + bss.ies[offset + 1]);
        if (offset + length > bss.ies_len)
            break;
        if (id == kRsnElementId) {
            if (length > capacity)
                length = capacity;
            memcpy(out, bss.ies + offset, length);
            return length;
        }
        offset += length;
    }
    return 0;
}

static bool rtwLegacyValidBSS(const RTW88BSS &bss)
{
    if (!bss.ssid_len || bss.ssid_len > APPLE80211_MAX_SSID_LEN)
        return false;
    if (!bss.channel || bss.channel > 196)
        return false;
    static const uint8_t zeroBSSID[6] = {};
    return memcmp(bss.bssid, zeroBSSID, sizeof(zeroBSSID)) != 0;
}

static int16_t rtwLegacyRSSI(const RTW88BSS &bss)
{
    /* Realtek frames without PHY-status metadata legitimately arrive with a
     * zero signal field.  The BSS itself is still valid; rejecting it made an
     * entire completed scan disappear from macOS.  Use a conservative signal
     * estimate for those frames and clamp malformed values to the dBm range
     * accepted by CoreWLAN. */
    if (bss.rssi >= 0 || bss.rssi < -127) {
        gScanRSSINormalized++;
        return -80;
    }
    if (bss.rssi < -95)
        return -95;
    if (bss.rssi > -20)
        return -20;
    return bss.rssi;
}

static IOReturn rtwLegacyScanResult(RTW88PCIDevice *device,
                                     apple80211_scan_result **out)
{
    gScanResultCalls++;
    device->setProperty("RTW88ScanResultCalls", gScanResultCalls, 32);
    if (!out)
        return kIOReturnBadArgument;
    if (!gScanCount) {
        if (rtwLegacyRefreshScanCache(device) != kIOReturnSuccess)
            return kIOReturnNotReady;
    }

    if (!gScanCount) {
        gScanEmptyReads++;
        device->setProperty("RTW88ScanEmptyReads", gScanEmptyReads, 32);
    }

    while (gScanIndex < gScanCount &&
           !rtwLegacyValidBSS(gScanCache[gScanIndex])) {
        gScanIndex++;
        gScanRejected++;
    }
    if (gScanIndex >= gScanCount) {
        /* APPLE80211_IOC_SCAN_RESULT is an iterator.  Returning 5 terminates
         * the current enumeration, but the next CoreWLAN scan must start at
         * the first cached BSS again.  Leaving the index at gScanCount made
         * every later native scan terminate immediately with EINVAL. */
        gScanIndex = 0;
        gScanCacheWraps++;
        gScanResultsPending = false;
        device->setProperty("RTW88ScanResultsPending", kOSBooleanFalse);
        device->setProperty("RTW88ScanRejected", gScanRejected, 32);
        device->setProperty("RTW88ScanCacheWraps", gScanCacheWraps, 32);
        return 5;
    }

    const RTW88BSS &bss = gScanCache[gScanIndex++];

    bzero(&gScanResult, sizeof(gScanResult));
    gScanResult.version = APPLE80211_VERSION;
    gScanResult.asr_ssid_len = bss.ssid_len;
    memcpy(gScanResult.asr_ssid, bss.ssid, bss.ssid_len);
    memcpy(gScanResult.asr_bssid, bss.bssid, sizeof(bss.bssid));
    gScanResult.asr_rssi = rtwLegacyRSSI(bss);
    gScanResult.asr_channel.version = APPLE80211_VERSION;
    gScanResult.asr_channel.channel = bss.channel;
    gScanResult.asr_channel.flags = rtwLegacyChannelFlags(bss.channel);
    gScanResult.asr_noise = -95;
    gScanResult.asr_snr = gScanResult.asr_rssi - gScanResult.asr_noise;
    gScanResult.asr_beacon_int = (int16_t)(bss.beacon_interval ?
        bss.beacon_interval : 100);
    gScanResult.asr_age = 1;
    gScanResult.asr_cap = (int16_t)bss.capabilities;
    gScanResult.asr_nrates = bss.nrates;
    for (uint8_t i = 0; i < bss.nrates && i < APPLE80211_MAX_RATES; i++)
        gScanResult.asr_rates[i] = bss.rates[i];
    gScanResult.asr_ie_len = (int16_t)rtwLegacyCopySecurityIE(
        bss, gScanResult.asr_ie_data, sizeof(gScanResult.asr_ie_data));
    gScanResultReads++;
    device->setProperty("RTW88ScanResultReads", gScanResultReads, 32);
    device->setProperty("RTW88ScanRSSINormalized", gScanRSSINormalized, 32);
    *out = &gScanResult;
    return kIOReturnSuccess;
}

SInt32 RTW88PCIDevice::apple80211Request(unsigned int requestType,
                                          int requestNumber,
                                          IO80211Interface *, void *data)
{
    if (!data && requestNumber != APPLE80211_IOC_DISASSOCIATE)
        return kIOReturnBadArgument;
    bool get = requestType == SIOCGA80211;
    bool set = requestType == SIOCSA80211;
    if (!get && !set)
        return kIOReturnBadArgument;

    RTW88StateResult state = {};
    if (_ieee80211)
        _ieee80211->cmdGetState(&state);

    switch (requestNumber) {
    case APPLE80211_IOC_SSID:
        if (get) {
            apple80211_ssid_data *v = (apple80211_ssid_data *)data;
            bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION;
            v->ssid_len = (uint32_t)strnlen(state.ssid, sizeof(state.ssid));
            memcpy(v->ssid_bytes, state.ssid, v->ssid_len);
        }
        return kIOReturnSuccess;
    case APPLE80211_IOC_AUTH_TYPE: {
        apple80211_authtype_data *v = (apple80211_authtype_data *)data;
        if (set) { gAuthLower = v->authtype_lower; gAuthUpper = v->authtype_upper; }
        else { v->version = APPLE80211_VERSION; v->authtype_lower = gAuthLower; v->authtype_upper = gAuthUpper; }
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_CHANNEL:
        if (get) {
            apple80211_channel_data *v = (apple80211_channel_data *)data;
            bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION;
            v->channel.version = APPLE80211_VERSION;
            v->channel.channel = state.channel;
            v->channel.flags = rtwLegacyChannelFlags((uint8_t)state.channel);
        }
        return kIOReturnSuccess;
    case APPLE80211_IOC_BSSID:
        if (get) {
            apple80211_bssid_data *v = (apple80211_bssid_data *)data;
            bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION;
            memcpy(v->bssid.octet, state.bssid, sizeof(state.bssid));
        }
        return kIOReturnSuccess;
    case APPLE80211_IOC_SCAN_REQ:
    case APPLE80211_IOC_SCAN_REQ_MULTIPLE:
        if (!set) return kIOReturnUnsupported;
        gScanRequests++;
        setProperty("RTW88ScanRequests", gScanRequests, 32);
        /* A live CoreWLAN request can overlap a scan already in progress.  It
         * still needs a fresh enumeration of the last completed cache. */
        gScanIndex = 0;
        /* wifiagent legitimately overlaps requests.  Do not turn that into
         * kIOReturnBusy: coalesce against the active scan, or re-post the
         * completion while a finished list is waiting to be consumed. */
        if (state.state == RTW88_STATE_SCANNING || gScanResultsPending) {
            gScanCoalesced++;
            setProperty("RTW88ScanCoalesced", gScanCoalesced, 32);
            if (gScanResultsPending && _airportScanDoneTimer)
                _airportScanDoneTimer->setTimeoutMS(10);
            return kIOReturnSuccess;
        }
        /* Tahoe's airportd no longer opens the Legacy IO80211 asynchronous
         * event client.  It therefore asks for SCAN_RESULT immediately after
         * SCAN_REQ instead of waiting for SCAN_DONE.  Keep the last completed
         * snapshot readable while the radio refreshes its internal BSS list;
         * notifyAirportScanDone() atomically replaces this snapshot when the
         * new scan completes.  Clearing it here made every native live scan
         * fail with EINVAL even though the radio found the networks later. */
        gScanCacheRetained++;
        setProperty("RTW88ScanCacheRetained", gScanCacheRetained, 32);
        gScanIndex = 0;
        return _ieee80211->cmdScan();
    case APPLE80211_IOC_SCAN_RESULT:
        return get ? rtwLegacyScanResult(this,
            (apple80211_scan_result **)data) : kIOReturnUnsupported;
    case APPLE80211_IOC_STATE:
        if (get) { apple80211_state_data *v = (apple80211_state_data *)data; v->version = APPLE80211_VERSION; v->state = rtwLegacyState(state.state); }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_PHY_MODE:
        if (get) { apple80211_phymode_data *v = (apple80211_phymode_data *)data; v->version = APPLE80211_VERSION; v->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B | APPLE80211_MODE_11G | APPLE80211_MODE_11N | APPLE80211_MODE_11AC; v->active_phy_mode = APPLE80211_MODE_11AC; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_OP_MODE:
        if (get) { apple80211_opmode_data *v = (apple80211_opmode_data *)data; v->version = APPLE80211_VERSION; v->op_mode = APPLE80211_M_STA; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_RSSI:
        if (get) { apple80211_rssi_data *v = (apple80211_rssi_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->num_radios = 1; v->rssi_unit = APPLE80211_UNIT_DBM; v->rssi[0] = v->aggregate_rssi = state.rssi; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_NOISE:
        if (get) { apple80211_noise_data *v = (apple80211_noise_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->num_radios = 1; v->noise_unit = APPLE80211_UNIT_DBM; v->noise[0] = v->aggregate_noise = -95; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_POWER: {
        apple80211_power_data *v = (apple80211_power_data *)data;
        if (get) { bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->num_radios = 1; v->power_state[0] = state.powered ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF; return kIOReturnSuccess; }
        if (!v->num_radios) return kIOReturnBadArgument;
        return v->power_state[0] == APPLE80211_POWER_OFF ? _ieee80211->cmdPowerOff() : _ieee80211->cmdPowerOn();
    }
    case APPLE80211_IOC_ASSOCIATE: {
        if (!set) return kIOReturnUnsupported;
        apple80211_assoc_data *v = (apple80211_assoc_data *)data;
        if (!v->ad_ssid_len || v->ad_ssid_len > APPLE80211_MAX_SSID_LEN) return kIOReturnBadArgument;
        char ssid[APPLE80211_MAX_SSID_LEN + 1] = {};
        memcpy(ssid, v->ad_ssid, v->ad_ssid_len);
        setProperty("RTW88AssocRequests", ++gAssocRequests, 32);
        setProperty("RTW88LastAssocSSID", ssid);
        setProperty("RTW88LastAssocKeyLength", v->ad_key.key_len, 32);
        gAuthLower = v->ad_auth_lower; gAuthUpper = v->ad_auth_upper;
        if (v->ad_key.key_len == 32) return _ieee80211->cmdConnectWithPMK(ssid, v->ad_key.key);
        if (v->ad_key.key_len == 0) return _ieee80211->cmdConnect(ssid, "");
        return kIOReturnUnsupported;
    }
    case APPLE80211_IOC_ASSOCIATE_RESULT:
        if (get) { apple80211_assoc_result_data *v = (apple80211_assoc_result_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->result = state.state == RTW88_STATE_CONNECTED ? APPLE80211_RESULT_SUCCESS : APPLE80211_RESULT_UNSPECIFIED_FAILURE; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_DISASSOCIATE:
    case APPLE80211_IOC_DEAUTH:
        return set ? _ieee80211->cmdDisconnect() : kIOReturnUnsupported;
    case APPLE80211_IOC_CHANNELS_INFO:
        if (get) {
            static const uint8_t ch[] = {
                1,2,3,4,5,6,7,8,9,10,11,12,13,
                36,40,44,48,52,56,60,64,
                100,104,108,112,116,120,124,128,132,136,140,144,
                149,153,157,161,165
            };
            apple80211_channels_info *v =
                (apple80211_channels_info *)data;
            bzero(v, sizeof(*v));
            v->version = APPLE80211_VERSION;
            v->num_chan_specs = sizeof(ch);
            for (uint32_t i = 0; i < sizeof(ch); i++) {
                v->chan_num[i] = ch[i];
                v->support_40Mhz[i] = ch[i] != 165;
                v->support_80Mhz[i] = ch[i] >= 36 && ch[i] != 165;
            }
        }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_SUPPORTED_CHANNELS:
    case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
        if (get) {
            static const uint8_t ch[] = {
                1,2,3,4,5,6,7,8,9,10,11,12,13,
                36,40,44,48,52,56,60,64,
                100,104,108,112,116,120,124,128,132,136,140,144,
                149,153,157,161,165
            };
            apple80211_sup_channel_data *v = (apple80211_sup_channel_data *)data;
            bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->num_channels = sizeof(ch);
            for (uint32_t i = 0; i < v->num_channels; i++) { v->supported_channels[i].version = APPLE80211_VERSION; v->supported_channels[i].channel = ch[i]; v->supported_channels[i].flags = rtwLegacyChannelFlags(ch[i]); }
        }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_LOCALE:
        if (get) { apple80211_locale_data *v = (apple80211_locale_data *)data; v->version = APPLE80211_VERSION; v->locale = APPLE80211_LOCALE_FCC; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_CARD_CAPABILITIES:
        if (get) { apple80211_capability_data *v = (apple80211_capability_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->capabilities[0] = 1U << APPLE80211_CAP_AES_CCM; v->capabilities[1] = (1U << (APPLE80211_CAP_WPA2 - 8)) | (1U << (APPLE80211_CAP_SHSLOT - 8)) | (1U << (APPLE80211_CAP_SHPREAMBLE - 8)); v->capabilities[2] = (1U << (APPLE80211_CAP_WME - 16)) | (1U << (APPLE80211_CAP_SHORT_GI_20MHZ - 16)); }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_DRIVER_VERSION:
    case APPLE80211_IOC_HARDWARE_VERSION:
        if (get) { apple80211_version_data *v = (apple80211_version_data *)data; const char *s = requestNumber == APPLE80211_IOC_DRIVER_VERSION ? "AirportRtw88 Legacy 1.4.10-exp40" : "Realtek RTL8821CE (rtw8821c)"; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; strlcpy(v->string, s, sizeof(v->string)); v->string_len = (uint16_t)strlen(v->string); }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_ASSOCIATION_STATUS:
        if (get) { apple80211_assoc_status_data *v = (apple80211_assoc_status_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->status = state.state == RTW88_STATE_CONNECTED ? APPLE80211_STATUS_SUCCESS : APPLE80211_STATUS_UNAVAILABLE; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
        if (get) {
            apple80211_link_changed_event_data *v =
                (apple80211_link_changed_event_data *)data;
            bzero(v, sizeof(*v));
            v->isLinkDown = state.state != RTW88_STATE_CONNECTED;
            v->rssi = state.rssi;
            v->nf = 95;
            if (v->isLinkDown)
                v->reason = APPLE80211_LINK_DOWN_REASON_DEAUTH;
        }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_COUNTRY_CODE:
        if (get) { apple80211_country_code_data *v = (apple80211_country_code_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->cc[0] = 'B'; v->cc[1] = 'R'; }
        return kIOReturnSuccess;
    case APPLE80211_IOC_POWERSAVE:
        if (get) { apple80211_powersave_data *v = (apple80211_powersave_data *)data; v->version = APPLE80211_VERSION; v->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    case APPLE80211_IOC_SCANCACHE_CLEAR:
        if (set) {
            /* Preserve the last hardware snapshot for Tahoe's no-event-client
             * fallback.  Resetting the iterator still gives each requester a
             * fresh enumeration without exposing an empty cache mid-scan. */
            gScanIndex = 0;
            gScanCacheClearDeferred++;
            setProperty("RTW88ScanCacheClearDeferred",
                        gScanCacheClearDeferred, 32);
            return kIOReturnSuccess;
        }
        return kIOReturnUnsupported;
    case APPLE80211_IOC_NSS:
        if (get) { apple80211_nss_data *v = (apple80211_nss_data *)data; bzero(v, sizeof(*v)); v->version = APPLE80211_VERSION; v->nss = 1; }
        return get ? kIOReturnSuccess : kIOReturnUnsupported;
    default:
        return kIOReturnUnsupported;
    }
}

IOReturn RTW88PCIDevice::getHardwareAddressForInterface(IO80211Interface *, IOEthernetAddress *addr) { return getHardwareAddress(addr); }
SInt32 RTW88PCIDevice::monitorModeSetEnabled(IO80211Interface *, bool, UInt32) { return kIOReturnUnsupported; }
/* rtw88's MLME consumes EAPOL internally after macOS supplies the 32-byte
 * PMK in ASSOCIATE, so advertising Apple's supplicant would create two
 * competing four-way handshakes. */
bool RTW88PCIDevice::useAppleRSNSupplicant(IO80211Interface *) { return false; }
int RTW88PCIDevice::outputRaw80211Packet(IO80211Interface *, mbuf_t) { return kIOReturnUnsupported; }
SInt32 RTW88PCIDevice::enableVirtualInterface(IO80211VirtualInterface *) { return kIOReturnUnsupported; }
SInt32 RTW88PCIDevice::disableVirtualInterface(IO80211VirtualInterface *) { return kIOReturnUnsupported; }
IO80211VirtualInterface *RTW88PCIDevice::createVirtualInterface(ether_addr *, uint) { return nullptr; }
SInt32 RTW88PCIDevice::apple80211VirtualRequest(uint, int, IO80211VirtualInterface *, void *) { return kIOReturnUnsupported; }
SInt32 RTW88PCIDevice::stopDMA() { return kIOReturnSuccess; }
UInt32 RTW88PCIDevice::hardwareOutputQueueDepth(IO80211Interface *) { return 256; }
SInt32 RTW88PCIDevice::performCountryCodeOperation(IO80211Interface *, IO80211CountryCodeOp) { return kIOReturnSuccess; }
SInt32 RTW88PCIDevice::enableFeature(IO80211FeatureCode code, void *) { return code == kIO80211Feature80211n ? kIOReturnSuccess : kIOReturnUnsupported; }
void RTW88PCIDevice::requestPacketTx(void *, UInt) {}
int RTW88PCIDevice::bpfOutputPacket(OSObject *, UInt, mbuf_t) { return kIOReturnUnsupported; }

void RTW88PCIDevice::notifyAirportScanDone(bool aborted)
{
    /* Replace any early/empty SCAN_RESULT snapshot with the completed BSS
     * list before wifiagent receives SCAN_DONE. */
    IOReturn cacheRet = rtwLegacyRefreshScanCache(this);
    gScanResultsPending = cacheRet == kIOReturnSuccess;
    gScanCompletions++;
    setProperty("RTW88ScanCompletions", gScanCompletions, 32);

    /* The RTL8821CE can finish its very first scan immediately after power-on
     * before RX has populated a BSS entry.  Completing that request with an
     * empty list makes CoreWLAN abort boot auto-join and wait two minutes.
     * Retry once inside the same logical request, then publish the result even
     * if it is still empty so a missing network never creates an infinite scan. */
    if (!aborted && cacheRet == kIOReturnSuccess && gScanCount == 0 &&
        gEmptyScanCompletionRetries == 0 && _ieee80211) {
        gEmptyScanCompletionRetries++;
        gScanResultsPending = false;
        setProperty("RTW88InitialEmptyScanRetries",
                    gEmptyScanCompletionRetries, 32);
        if (_ieee80211->cmdScan() == kIOReturnSuccess)
            return;
    }
    if (gScanCount)
        gEmptyScanCompletionRetries = 0;

    setProperty("RTW88ScanCacheBytes", gScanCount * sizeof(RTW88BSS), 32);
    setProperty("RTW88ScanCacheEntries", gScanCount, 32);
    setProperty("RTW88ScanCacheReady",
                cacheRet == kIOReturnSuccess ? kOSBooleanTrue : kOSBooleanFalse);
    setProperty("RTW88ScanResultsPending",
                gScanResultsPending ? kOSBooleanTrue : kOSBooleanFalse);
    if (_airportScanDoneTimer) {
        _airportScanDoneTimer->cancelTimeout();
        _airportScanDoneTimer->setTimeoutMS(100);
    }
}

IOReturn RTW88PCIDevice::setAirportLinkUpGated(OSObject *owner, void *, void *,
                                                void *, void *)
{
    RTW88PCIDevice *self = OSDynamicCast(RTW88PCIDevice, owner);
    if (!self || !self->_iface)
        return kIOReturnNotReady;
    return self->_iface->setLinkState(kIO80211NetworkLinkUp, 0) ?
        kIOReturnSuccess : kIOReturnError;
}

IOReturn RTW88PCIDevice::setAirportLinkDownGated(OSObject *owner, void *, void *,
                                                  void *, void *)
{
    RTW88PCIDevice *self = OSDynamicCast(RTW88PCIDevice, owner);
    if (!self || !self->_iface)
        return kIOReturnNotReady;
    return self->_iface->setLinkState(kIO80211NetworkLinkDown, 0) ?
        kIOReturnSuccess : kIOReturnError;
}

void RTW88PCIDevice::notifyAirportLinkUp()
{
    if (_iface) {
        /* Keep the IO80211 interface's identity in sync with the completed
         * association.  Without these properties CoreWLAN can show a link
         * while treating en0 as unassociated, so configd never starts DHCP. */
        RTW88StateResult assoc = {};
        if (_ieee80211 && _ieee80211->cmdGetState(&assoc) == kIOReturnSuccess) {
            uint32_t ssidLen = (uint32_t)strnlen(assoc.ssid, sizeof(assoc.ssid));
            if (ssidLen) {
                OSString *ssid = OSString::withCString(assoc.ssid);
                if (ssid) {
                    _iface->setProperty("IO80211SSID", ssid);
                    ssid->release();
                }
            }
            OSData *bssid = OSData::withBytes(assoc.bssid, 6);
            if (bssid) {
                _iface->setProperty("IO80211BSSID", bssid);
                bssid->release();
            }
            setProperty("RTW88AirportSSIDPublished", ssidLen ? 1 : 0, 32);
            setProperty("RTW88AirportBSSIDPublished", 1, 32);
        }
        _iface->postMessage(APPLE80211_M_ASSOC_DONE);
        _iface->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE);
        IONetworkMedium *wifiMedium = IONetworkMedium::getMediumWithType(
            getMediumDictionary(),
            kIOMediumIEEE80211);
        if (wifiMedium) {
            setCurrentMedium(wifiMedium);
            setLinkStatus(kIONetworkLinkActive | kIONetworkLinkValid,
                          wifiMedium);
        } else {
            setLinkStatus(kIONetworkLinkActive | kIONetworkLinkValid);
        }
        _iface->startOutputThread();
        IOReturn gateResult = _cmdGate ?
            _cmdGate->runAction(setAirportLinkUpGated) : kIOReturnNotReady;
        bool ioLinkUp = (gateResult == kIOReturnSuccess);
        _iface->postMessage(APPLE80211_M_SSID_CHANGED);
        _iface->postMessage(APPLE80211_M_BSSID_CHANGED);
        _iface->postMessage(APPLE80211_M_LINK_CHANGED);
        setProperty("RTW88BSDLinkActiveFinal", 1, 64);
        setProperty("RTW88IO80211LinkUpAccepted", ioLinkUp ? 1 : 0, 64);
        setProperty("RTW88WiFiMediumPresent", wifiMedium ? 1 : 0, 64);
        setProperty("RTW88LinkStateGated", _cmdGate ? 1 : 0, 32);
    }
}

void RTW88PCIDevice::notifyAirportLinkIdle()
{
    if (_iface) {
        _iface->stopOutputThread();
        setLinkStatus(kIONetworkLinkValid);
        _iface->removeProperty("IO80211SSID");
        _iface->removeProperty("IO80211BSSID");
        IOReturn gateResult = _cmdGate ?
            _cmdGate->runAction(setAirportLinkDownGated) : kIOReturnNotReady;
        _iface->postMessage(APPLE80211_M_SSID_CHANGED);
        _iface->postMessage(APPLE80211_M_BSSID_CHANGED);
        _iface->postMessage(APPLE80211_M_LINK_CHANGED);
        setProperty("RTW88IO80211InitialLinkDownAccepted",
                    gateResult == kIOReturnSuccess ? 1 : 0, 32);
    }
}

void RTW88PCIDevice::notifyAirportLinkDown(uint16_t)
{
    if (_iface) {
        _iface->stopOutputThread();
        _iface->flushOutputQueue();
        _iface->removeProperty("IO80211SSID");
        _iface->removeProperty("IO80211BSSID");
        IOReturn gateResult = _cmdGate ?
            _cmdGate->runAction(setAirportLinkDownGated) : kIOReturnNotReady;
        _iface->postMessage(APPLE80211_M_SSID_CHANGED);
        _iface->postMessage(APPLE80211_M_BSSID_CHANGED);
        _iface->postMessage(APPLE80211_M_LINK_CHANGED);
        _iface->postMessage(APPLE80211_M_DEAUTH_RECEIVED);
        setProperty("RTW88IO80211LinkDownAccepted",
                    gateResult == kIOReturnSuccess ? 1 : 0, 32);
    }
}
#endif
