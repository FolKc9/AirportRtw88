/* Tahoe IO80211ControllerV2/Skywalk controller bridge. */
#include "RTW88PCIDevice.hpp"
#include "RTW88AirportInterface.hpp"
#include "RTW88AirportSkywalkInterface.hpp"
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"

#ifdef RTW88_AIRPORT
#include <IOKit/IOLib.h>

static void rtw88SetVersion(apple80211_version_data *data, const char *text)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    strlcpy(data->string, text, sizeof(data->string));
    data->string_len = (uint16_t)strlen(data->string);
}

IOReturn RTW88PCIDevice::enable(IO80211SkywalkInterface *)
{
    return enable((IONetworkInterface *)_iface);
}

IOReturn RTW88PCIDevice::disable(IO80211SkywalkInterface *)
{
    return disable((IONetworkInterface *)_iface);
}

bool RTW88PCIDevice::isCommandProhibited(int) { return false; }

SInt32 RTW88PCIDevice::handleCardSpecific(IO80211SkywalkInterface *,
                                           unsigned long, void *, bool)
{
    return kIOReturnUnsupported;
}

SInt32 RTW88PCIDevice::enableFeature(IO80211FeatureCode code, void *)
{
    return code == kIO80211Feature80211n ?
        kIOReturnSuccess : kIOReturnUnsupported;
}

IOReturn RTW88PCIDevice::getDRIVER_VERSION(IO80211SkywalkInterface *,
                                            apple80211_version_data *data)
{
    rtw88SetVersion(data, "AirportRtw88 NativeV2 1.2.0-exp2");
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::getHARDWARE_VERSION(IO80211SkywalkInterface *,
                                              apple80211_version_data *data)
{
    rtw88SetVersion(data, "Realtek RTL8821CE (rtw8821c)");
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::getCARD_CAPABILITIES(IO80211SkywalkInterface *,
                                               apple80211_capability_data *data)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->capabilities[0] = 1U << APPLE80211_CAP_AES_CCM;
    data->capabilities[1] =
        (1U << (APPLE80211_CAP_WPA2 - 8)) |
        (1U << (APPLE80211_CAP_SHSLOT - 8)) |
        (1U << (APPLE80211_CAP_SHPREAMBLE - 8));
    data->capabilities[2] =
        (1U << (APPLE80211_CAP_WME - 16)) |
        (1U << (APPLE80211_CAP_SHORT_GI_20MHZ - 16));
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::getPOWER(IO80211SkywalkInterface *,
                                   apple80211_power_data *data)
{
    RTW88StateResult state = {};
    _ieee80211->cmdGetState(&state);
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->num_radios = 1;
    data->power_state[0] = state.powered ?
        APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setPOWER(IO80211SkywalkInterface *,
                                   apple80211_power_data *data)
{
    if (!data || !data->num_radios)
        return kIOReturnBadArgument;
    return data->power_state[0] == APPLE80211_POWER_OFF ?
        _ieee80211->cmdPowerOff() : _ieee80211->cmdPowerOn();
}

IOReturn RTW88PCIDevice::getCOUNTRY_CODE(IO80211SkywalkInterface *,
                                          apple80211_country_code_data *data)
{
    bzero(data, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->cc[0] = 'B';
    data->cc[1] = 'R';
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setCOUNTRY_CODE(IO80211SkywalkInterface *,
                                          apple80211_country_code_data *)
{
    return kIOReturnSuccess;
}

IOReturn RTW88PCIDevice::setGET_DEBUG_INFO(IO80211SkywalkInterface *,
                                            apple80211_debug_command *)
{
    return kIOReturnSuccess;
}

void RTW88PCIDevice::notifyAirportScanDone(bool)
{
    if (_airportScanDoneTimer) {
        _airportScanDoneTimer->cancelTimeout();
        _airportScanDoneTimer->setTimeoutMS(100);
    }
}

void RTW88PCIDevice::notifyAirportLinkUp()
{
    if (_iface)
        _iface->setAirportLinkState(kIO80211NetworkLinkUp);
    if (_skywalk) {
        _skywalk->setLinkState(kIO80211NetworkLinkUp, 0, false, 0);
        _skywalk->postMessage(APPLE80211_M_ASSOC_DONE, nullptr, 0, false);
        _skywalk->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE, nullptr, 0,
                              false);
    }
}

void RTW88PCIDevice::notifyAirportLinkDown(uint16_t reason)
{
    if (_iface)
        _iface->setAirportLinkState(kIO80211NetworkLinkDown);
    if (_skywalk) {
        _skywalk->setLinkState(kIO80211NetworkLinkDown, reason, false, reason);
        _skywalk->postMessage(APPLE80211_M_DEAUTH_RECEIVED, nullptr, 0, false);
    }
}
#endif
