#include "RTW88AirportSkywalkInterface.hpp"
#include "RTW88PCIDevice.hpp"
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"

#ifdef RTW88_AIRPORT
#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(RTW88AirportSkywalkInterface,
                              IO80211InfraProtocol)

static uint32_t rtwAirportState(uint32_t state)
{
    switch (state) {
    case RTW88_STATE_SCANNING:       return APPLE80211_S_SCAN;
    case RTW88_STATE_AUTHENTICATING: return APPLE80211_S_AUTH;
    case RTW88_STATE_ASSOCIATING:
    case RTW88_STATE_HANDSHAKING:    return APPLE80211_S_ASSOC;
    case RTW88_STATE_CONNECTED:      return APPLE80211_S_RUN;
    default:                         return APPLE80211_S_INIT;
    }
}

static uint32_t rtwAirportChannelFlags(uint8_t channel)
{
    return APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ |
        (channel <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
}

bool RTW88AirportSkywalkInterface::init(IOService *provider)
{
    if (!IO80211InfraInterface::init())
        return false;
    instance = OSDynamicCast(RTW88PCIDevice, provider);
    return instance != nullptr;
}

IOReturn RTW88AirportSkywalkInterface::getSSID(apple80211_ssid_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->ssid_len = (uint32_t)strnlen(state.ssid, sizeof(state.ssid));
    memcpy(data->ssid_bytes, state.ssid, data->ssid_len);
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getAUTH_TYPE(apple80211_authtype_data *data)
{
    data->version = APPLE80211_VERSION;
    data->authtype_lower = current_authtype_lower;
    data->authtype_upper = current_authtype_upper;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::setAUTH_TYPE(apple80211_authtype_data *data)
{
    current_authtype_lower = data->authtype_lower;
    current_authtype_upper = data->authtype_upper;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getCHANNEL(apple80211_channel_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->channel.version = APPLE80211_VERSION;
    data->channel.channel = state.channel;
    data->channel.flags = rtwAirportChannelFlags((uint8_t)state.channel);
    return state.channel ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn RTW88AirportSkywalkInterface::getBSSID(apple80211_bssid_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    memcpy(data->bssid.octet, state.bssid, sizeof(state.bssid));
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getSTATE(apple80211_state_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    data->version = APPLE80211_VERSION;
    data->state = rtwAirportState(state.state);
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getPHY_MODE(apple80211_phymode_data *data)
{
    data->version = APPLE80211_VERSION;
    data->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B |
        APPLE80211_MODE_11G | APPLE80211_MODE_11N | APPLE80211_MODE_11AC;
    data->active_phy_mode = APPLE80211_MODE_11AC;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getOP_MODE(apple80211_opmode_data *data)
{
    data->version = APPLE80211_VERSION;
    data->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getRSSI(apple80211_rssi_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->num_radios = 1;
    data->rssi_unit = APPLE80211_UNIT_DBM;
    data->rssi[0] = data->aggregate_rssi = state.rssi;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getNOISE(apple80211_noise_data *data)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->num_radios = 1;
    data->noise_unit = APPLE80211_UNIT_DBM;
    data->noise[0] = data->aggregate_noise = -95;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getPOWERSAVE(apple80211_powersave_data *data)
{
    data->version = APPLE80211_VERSION;
    data->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getTXPOWER(apple80211_txpower_data *data)
{
    data->version = APPLE80211_VERSION;
    data->txpower_unit = APPLE80211_UNIT_PERCENT;
    data->txpower = 100;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getRATE(apple80211_rate_data *data)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->num_radios = 1;
    data->rate[0] = 433;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getSUPPORTED_CHANNELS(
    apple80211_sup_channel_data *data)
{
    static const uint8_t channels[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
        36, 40, 44, 48, 149, 153, 157, 161, 165
    };
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->num_channels = sizeof(channels);
    for (uint32_t i = 0; i < data->num_channels; i++) {
        data->supported_channels[i].version = APPLE80211_VERSION;
        data->supported_channels[i].channel = channels[i];
        data->supported_channels[i].flags = rtwAirportChannelFlags(channels[i]);
    }
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getLOCALE(apple80211_locale_data *data)
{
    data->version = APPLE80211_VERSION;
    data->locale = APPLE80211_LOCALE_FCC;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::setSCAN_REQ(apple80211_scan_data *)
{
    scanLength = 0;
    scanOffset = 4;
    return instance->get80211()->cmdScan();
}

IOReturn RTW88AirportSkywalkInterface::getSCAN_RESULT(
    apple80211_scan_result *result)
{
    if (scanLength < 4) {
        uint32_t length = sizeof(scanCache);
        if (instance->get80211()->cmdGetBSSList(scanCache, &length) !=
            kIOReturnSuccess)
            return kIOReturnNotReady;
        scanLength = length;
        scanOffset = 4;
    }
    if (scanOffset >= scanLength)
        return 5;

    uint32_t p = scanOffset;
    uint8_t ssidLen = scanCache[p++];
    uint32_t need = 1U + ssidLen + 6U + 2U + 1U + 4U;
    if (ssidLen > APPLE80211_MAX_SSID_LEN || scanOffset + need > scanLength) {
        scanOffset = scanLength;
        return kIOReturnBadMedia;
    }
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    result->asr_ssid_len = ssidLen;
    memcpy(result->asr_ssid, scanCache + p, ssidLen);
    p += ssidLen;
    memcpy(result->asr_bssid, scanCache + p, 6);
    p += 6;
    result->asr_rssi = (int16_t)(((uint16_t)scanCache[p] << 8) |
                                 scanCache[p + 1]);
    p += 2;
    uint8_t channel = scanCache[p++];
    uint32_t cipher = 0;
    memcpy(&cipher, scanCache + p, sizeof(cipher));
    p += sizeof(cipher);
    result->asr_channel.version = APPLE80211_VERSION;
    result->asr_channel.channel = channel;
    result->asr_channel.flags = rtwAirportChannelFlags(channel);
    result->asr_noise = -95;
    result->asr_snr = result->asr_rssi + 95;
    result->asr_beacon_int = 100;
    result->asr_cap = APPLE80211_CAPINFO_ESS |
        (cipher ? APPLE80211_CAPINFO_PRIVACY : 0);
    result->asr_nrates = 3;
    result->asr_rates[0] = 12;
    result->asr_rates[1] = 24;
    result->asr_rates[2] = 108;
    if (cipher) {
        static const uint8_t rsn[] = {
            0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
            0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
        };
        result->asr_ie_len = sizeof(rsn);
        memcpy(result->asr_ie_data, rsn, sizeof(rsn));
    }
    scanOffset = p;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::setASSOCIATE(apple80211_assoc_data *data)
{
    if (!data || !data->ad_ssid_len ||
        data->ad_ssid_len > APPLE80211_MAX_SSID_LEN)
        return kIOReturnBadArgument;
    char ssid[APPLE80211_MAX_SSID_LEN + 1] = {};
    memcpy(ssid, data->ad_ssid, data->ad_ssid_len);
    current_authtype_lower = data->ad_auth_lower;
    current_authtype_upper = data->ad_auth_upper;
    if (data->ad_key.key_len == 32)
        return instance->get80211()->cmdConnectWithPMK(ssid, data->ad_key.key);
    if (data->ad_key.key_len == 0)
        return instance->get80211()->cmdConnect(ssid, "");
    return kIOReturnUnsupported;
}

IOReturn RTW88AirportSkywalkInterface::setDISASSOCIATE(apple80211_disassoc_data *)
{
    return instance->get80211()->cmdDisconnect();
}

IOReturn RTW88AirportSkywalkInterface::setDEAUTH(apple80211_deauth_data *)
{
    return instance->get80211()->cmdDisconnect();
}

IOReturn RTW88AirportSkywalkInterface::getASSOCIATION_STATUS(
    apple80211_assoc_status_data *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    data->version = APPLE80211_VERSION;
    data->status = state.state == RTW88_STATE_CONNECTED ?
        APPLE80211_STATUS_SUCCESS : APPLE80211_STATUS_UNAVAILABLE;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getCURRENT_NETWORK(
    apple80211_scan_result *data)
{
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    if (state.state != RTW88_STATE_CONNECTED)
        return kIOReturnNotReady;
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->asr_channel.version = APPLE80211_VERSION;
    data->asr_channel.channel = state.channel;
    data->asr_channel.flags = rtwAirportChannelFlags((uint8_t)state.channel);
    data->asr_rssi = state.rssi;
    data->asr_noise = -95;
    data->asr_ssid_len = (uint8_t)strnlen(state.ssid, sizeof(state.ssid));
    memcpy(data->asr_ssid, state.ssid, data->asr_ssid_len);
    memcpy(data->asr_bssid, state.bssid, sizeof(state.bssid));
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getLINK_CHANGED_EVENT_DATA(
    apple80211_link_changed_event_data *data)
{
    bzero(data, sizeof(*data));
    RTW88StateResult state = {};
    instance->get80211()->cmdGetState(&state);
    data->isLinkDown = state.state != RTW88_STATE_CONNECTED;
    data->rssi = state.rssi;
    data->nf = 95;
    return kIOReturnSuccess;
}

IOReturn RTW88AirportSkywalkInterface::getNSS(apple80211_nss_data *data)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = 1;
    return kIOReturnSuccess;
}

#define RTW88_UNSUPPORTED(method, type) \
    IOReturn RTW88AirportSkywalkInterface::method(type *) { return kIOReturnUnsupported; }
RTW88_UNSUPPORTED(getDEAUTH, apple80211_deauth_data)
RTW88_UNSUPPORTED(getRATE_SET, apple80211_rate_set_data)
RTW88_UNSUPPORTED(getRSN_IE, apple80211_rsn_ie_data)
RTW88_UNSUPPORTED(getAP_IE_LIST, apple80211_ap_ie_data)
RTW88_UNSUPPORTED(getMCS, apple80211_mcs_data)
RTW88_UNSUPPORTED(getMCS_INDEX_SET, apple80211_mcs_index_set_data)
RTW88_UNSUPPORTED(getVHT_MCS_INDEX_SET, apple80211_vht_mcs_index_set_data)
RTW88_UNSUPPORTED(getMCS_VHT, apple80211_mcs_vht_data)
RTW88_UNSUPPORTED(getCOLOCATED_NETWORK_SCOPE_ID, apple80211_colocated_network_scope_id)
RTW88_UNSUPPORTED(setCIPHER_KEY, apple80211_key)
RTW88_UNSUPPORTED(setRSN_IE, apple80211_rsn_ie_data)
#undef RTW88_UNSUPPORTED

IOReturn RTW88AirportSkywalkInterface::setSCANCACHE_CLEAR(void *)
{
    scanLength = 0;
    scanOffset = 4;
    return kIOReturnSuccess;
}
#endif
