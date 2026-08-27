#include "RTW88AirportInterface.hpp"
#include <IOKit/network/IOOutputQueue.h>

#if defined(RTW88_AIRPORT) && !defined(IO80211FAMILY_V2)
#define super IO80211Interface
OSDefineMetaClassAndStructors(RTW88AirportInterface, IO80211Interface)

bool RTW88AirportInterface::init(IO80211Controller *controller)
{
    return super::init(controller);
}

UInt32 RTW88AirportInterface::inputPacket(mbuf_t packet, UInt32 length,
                                           IOOptionBits options,
                                           void *parameter)
{
    /* IO80211Interface::inputPacket() owns the AirPort/EAPOL receive path.
     * Passing an already-decapsulated Ethernet/IP frame through it consumes
     * the frame before it reaches DLIL, so en0 never records the DHCP OFFER.
     * AirportItlwm uses the same split: EAPOL stays with IO80211, while normal
     * Ethernet traffic enters through IOEthernetInterface.  Our WPA2 state
     * machine normally consumes EAPOL before this point, but preserve the
     * correct route for rekey frames that may arrive after association. */
    uint8_t ethernetHeader[14] = {};
    bool eapol = packet && mbuf_pkthdr_len(packet) >= sizeof(ethernetHeader) &&
                 mbuf_copydata(packet, 0, sizeof(ethernetHeader),
                               ethernetHeader) == 0 &&
                 ethernetHeader[12] == 0x88 && ethernetHeader[13] == 0x8e;

    IO80211Controller *controller = getController();
    if (controller && eapol && !_bsdEapolInputSeen) {
        _bsdEapolInputSeen = true;
        controller->setProperty("RTW88BSDInputEAPOLPath", 1, 64);
    } else if (controller && !eapol && !_bsdEthernetInputSeen) {
        _bsdEthernetInputSeen = true;
        controller->setProperty("RTW88BSDInputEthernetPath", 1, 64);
    }

    if (eapol)
        return IO80211Interface::inputPacket(packet, length, options,
                                             parameter);
    return IOEthernetInterface::inputPacket(packet, length, options,
                                            parameter);
}

UInt32 RTW88AirportInterface::outputPacket(mbuf_t packet, void *parameter)
{
    /* IO80211Interface owns private flow queues. In the legacy bridge those
     * queues accepted Ethernet packets from en0 without reaching the
     * controller, leaving DHCP with no TX. Feed the controller's gated queue
     * explicitly so PCIe ring submissions stay serialized. */
    IO80211Controller *controller = getController();
    if (!controller) {
        if (packet) mbuf_freem(packet);
        return kIOReturnOutputDropped;
    }

    controller->setProperty("RTW88InterfaceOutputSeen", 1, 64);
    IOOutputQueue *queue = controller->getOutputQueue();
    if (queue)
        return queue->enqueue(packet, parameter);
    return controller->outputPacket(packet, parameter);
}
#endif
