# Changelog

## 1.4.10 / exp40 — tested baseline

- Move RX delivery onto the correct workloop ownership path.
- Fix the invalid-IP/DHCP regression observed after association.
- Restore stable download and native macOS Wi-Fi integration on the verified
  RTL8821CE machine.

## 1.4.9 / exp39

- Add the preceding RX hotfix iteration used to isolate the DHCP/data-path
  failure before exp40.

## v1.0.0-rc1 — internal 1.4.8 / exp38

### Added

- Native AirPort/IO80211 interface for RTL8821CE (`10ec:c821`)
- Native CoreWLAN scan and association requests
- WPA2-Personal PMK association path
- 2.4 GHz and 5 GHz channel discovery
- Negotiated HT/VHT channel-width telemetry
- TX and RX BlockAck/A-MPDU support and telemetry
- Boot auto-join recovery for an initially empty hardware scan

### Fixed

- Route normal Ethernet/IP/ARP/DHCP frames through `IOEthernetInterface` while
  preserving the IO80211 EAPOL path
- Publish the network medium before attaching the interface
- Publish valid/active IO80211 and BSD link states in lifecycle order
- Clear stale SSID/BSSID identity on disconnect
- Set initial link-down state on the IO80211 command gate so CoreWLAN does not
  mistake a newly created interface for an existing association
- Allow background scans while connected instead of returning `Resource busy`
- Keep EAPOL and bootstrap traffic outside aggregation until BlockAck is active

### Known limitations

- Final cold-boot auto-join and 5 GHz throughput validation is pending
- WPA3, AWDL and Apple peer-to-peer features are not implemented
- Only RTL8821CE on x86_64 is a supported claim

## 1.4.6 / exp36

- First hardware-verified native DHCP and Internet connectivity on the legacy
  IO80211 path.
