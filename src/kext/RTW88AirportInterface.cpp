#include "RTW88AirportInterface.hpp"

#ifdef RTW88_AIRPORT
#define super IOEthernetInterface
OSDefineMetaClassAndStructors(RTW88AirportInterface, IOEthernetInterface)

bool RTW88AirportInterface::initWithSkywalkInterfaceAndProvider(
    IONetworkController *controller, IO80211SkywalkInterface *interface)
{
    if (!super::init(controller))
        return false;
    _skywalk = interface;
    return true;
}

IOReturn RTW88AirportInterface::attachToDataLinkLayer(IOOptionBits options,
                                                       void *parameter)
{
    IOReturn ret = super::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess && _skywalk) {
        char name[IFNAMSIZ] = {};
        snprintf(name, sizeof(name), "%s%u", ifnet_name(getIfnet()),
                 ifnet_unit(getIfnet()));
        _skywalk->setProperty("IOInterfaceName", OSString::withCString(name));
        _skywalk->setProperty(kIOInterfaceUnit,
                              OSNumber::withNumber(ifnet_unit(getIfnet()), 8));
        _skywalk->setProperty(kIOInterfaceNamePrefix,
                              OSString::withCString(ifnet_name(getIfnet())));
        _skywalk->registerService();
        _skywalk->prepareBSDInterface(getIfnet(), 0);
    }
    _dataLinkAttached = ret == kIOReturnSuccess;
    return ret;
}

void RTW88AirportInterface::detachFromDataLinkLayer(IOOptionBits options,
                                                     void *parameter)
{
    _dataLinkAttached = false;
    super::detachFromDataLinkLayer(options, parameter);
}

IOService *RTW88AirportInterface::getProvider() const
{
    return (_dataLinkAttached && _skywalk) ? _skywalk : super::getProvider();
}

bool RTW88AirportInterface::setAirportLinkState(IO80211LinkState state)
{
    ifnet_t netif = getIfnet();
    if (!netif)
        return false;
    if (state == kIO80211NetworkLinkUp)
        ifnet_set_flags(netif, ifnet_flags(netif) | IFF_UP | IFF_RUNNING,
                        IFF_UP | IFF_RUNNING);
    else
        ifnet_set_flags(netif, ifnet_flags(netif) & ~(IFF_UP | IFF_RUNNING),
                        IFF_UP | IFF_RUNNING);
    return true;
}
#endif
