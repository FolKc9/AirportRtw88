/*
 * Tahoe's IONetworkingFamily no longer exports the single-underscore names
 * generated for unused reserved slots by the Ventura-era Kernel SDK headers.
 * These slots are ABI padding and have no implementation by design. Keeping
 * the shims in the legacy-only target avoids leaking the workaround into the
 * normal Ethernet driver or the current Skywalk experiment.
 */
#include <IOKit/network/IONetworkController.h>
#include <IOKit/network/IONetworkInterface.h>

#if defined(RTW88_AIRPORT_LEGACY) && !defined(__PRIVATE_SPI__)
void IONetworkController::_RESERVEDIONetworkController2() {}
void IONetworkController::_RESERVEDIONetworkController3() {}
void IONetworkController::_RESERVEDIONetworkController4() {}
void IONetworkController::_RESERVEDIONetworkController5() {}
void IONetworkController::_RESERVEDIONetworkController6() {}
void IONetworkController::_RESERVEDIONetworkController7() {}

void IONetworkInterface::_RESERVEDIONetworkInterface5() {}
void IONetworkInterface::_RESERVEDIONetworkInterface6() {}
void IONetworkInterface::_RESERVEDIONetworkInterface7() {}
void IONetworkInterface::_RESERVEDIONetworkInterface8() {}
void IONetworkInterface::_RESERVEDIONetworkInterface9() {}
void IONetworkInterface::_RESERVEDIONetworkInterface10() {}
#endif
