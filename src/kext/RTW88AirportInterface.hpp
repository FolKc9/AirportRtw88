#pragma once

#ifdef RTW88_AIRPORT
#include "Airport/Apple80211.h"
#ifdef IO80211FAMILY_V2
#include <IOKit/network/IOEthernetInterface.h>

class RTW88AirportInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(RTW88AirportInterface)

public:
    bool initWithSkywalkInterfaceAndProvider(IONetworkController *controller,
                                             IO80211SkywalkInterface *interface);
    IOReturn attachToDataLinkLayer(IOOptionBits options, void *parameter) override;
    void detachFromDataLinkLayer(IOOptionBits options, void *parameter) override;
    IOService *getProvider() const override;
    bool setAirportLinkState(IO80211LinkState state);

private:
    IO80211SkywalkInterface *_skywalk = nullptr;
    bool _dataLinkAttached = false;
};
#else
class RTW88AirportInterface : public IO80211Interface {
    OSDeclareDefaultStructors(RTW88AirportInterface)

public:
    bool init(IO80211Controller *controller);
    UInt32 inputPacket(mbuf_t packet, UInt32 length = 0,
                       IOOptionBits options = 0, void *parameter = nullptr) override;
    UInt32 outputPacket(mbuf_t packet, void *parameter) override;

private:
    bool _bsdEthernetInputSeen = false;
    bool _bsdEapolInputSeen = false;
};
#endif
#endif
