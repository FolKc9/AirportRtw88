# Troubleshooting

These checks are read-only. Collect them before changing OpenCore or deleting a
saved network.

## Confirm that the native driver loaded

```sh
system_profiler SPAirPortDataType
```

The driver line should contain `AirportRtw88 Legacy`. The verified PCI device is
RTL8821CE (`10ec:c821`).

```sh
ioreg -r -c RTW88PCIDevice -l | egrep 'RTW88|IO80211|IOLink'
```

Useful v1 candidate properties include:

- `RTW88IO80211InitialLinkDownAccepted`: initial disconnected state reached IO80211
- `RTW88AssocRequests`: number of native association requests
- `RTW88InitialEmptyScanRetries`: first empty boot scan was retried
- `RTW88ScanCacheEntries`: networks in the completed scan snapshot
- `RTW88ConnectedChannelWidth`: negotiated channel width
- `RTW88TxBlockAckActive` and `RTW88RxBlockAckActive`: aggregation agreements

## Connected but no Internet

```sh
ifconfig en0
route -n get default
```

A `169.254.x.x` address is self-assigned and means DHCP did not complete. A
normal private address such as `192.168.x.x` or `10.x.x.x`, plus a default
route, confirms the IPv4 path is configured.

The native legacy receive path must send normal Ethernet frames through
`IOEthernetInterface`; EAPOL remains on the IO80211 path. The telemetry
properties `RTW88BSDInputEthernetPath` and `RTW88BSDInputEAPOLPath` help confirm
that split.

## Auto-Join does not run after reboot

1. Confirm Wi-Fi is on and Auto-Join is enabled for the saved network.
2. Confirm `RTW88IO80211InitialLinkDownAccepted` is `1`.
3. Confirm `RTW88ScanCacheEntries` is greater than zero.
4. If the profile was created with an experimental build, forget the network,
   join it again and reboot.
5. Use WPA2-Personal/CCMP. WPA3 and transition mode are not supported yet.

Inspect the system decision without exposing passwords:

```sh
log show --last boot --info --style compact \
  --predicate 'process == "airportd"' | \
  egrep -i 'auto-join|scan request|associate|resource busy|known network'
```

An initial `0 results` followed by a populated retry is expected only once. A
continuous stream of `Resource busy` scan failures is a driver regression.

## 5 GHz is slow

Confirm that macOS selected the expected band and channel:

```sh
system_profiler SPAirPortDataType
ioreg -r -c RTW88PCIDevice -l | \
  egrep 'RTW88ConnectedChannelWidth|RTW88.*BlockAck|IO80211Band|IO80211Channel'
```

Then measure the same network more than once:

```sh
networkQuality -I en0 -v
```

The generic `IOLinkSpeed` value can remain at 54 Mbps because it is a legacy
IOKit medium field; use measured throughput and the RTW88 width/BlockAck
telemetry instead.

For router-side testing, use a clean 5 GHz WPA2 SSID, 80 MHz channel width, and
compare at the same distance. DFS channels can add scan/join delays.

## Safety note

Do not use `rtw88ctl` with the native v1 driver. The old user-client diagnostic
path is not required for normal operation and is excluded from the supported
workflow. Use IOKit properties and unified logs instead.

When reporting a bug, include macOS build, OpenCore version, PCI ID, kext
version, the relevant IOKit properties, and the filtered `airportd`/kernel log.
Remove SSIDs, BSSIDs, IP addresses and other personal identifiers before
posting logs publicly.

