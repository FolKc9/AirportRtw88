# Development history

AirportRtw88 was developed through hardware-driven experiments on a Realtek
RTL8821CE (`10ec:c821`) in an x86_64 OpenCore system running macOS Tahoe.

## Milestones

1. Bring up the PCIe device and imported `rtw88` hardware path.
2. Expose scanning through the legacy AirPort/IO80211 interface so networks
   appear in the native macOS Wi-Fi menu and CoreWLAN.
3. Implement association and WPA2-Personal/CCMP key handling.
4. Route Ethernet, ARP, DHCP, IPv4 and IPv6 traffic through the native `en0`
   interface while preserving the IO80211 EAPOL path.
5. Correct scan-cache lifecycle, disconnect state and remembered-network
   auto-join behavior.
6. Add rate adaptation, TX batching and BlockAck/A-MPDU experiments.
7. Fix RX workloop ownership in exp40, restoring valid DHCP/IP traffic after
   association.

The retained `NATIVEV2_EXP*.md`, `patches/` and `scripts/apply_exp*.py` files
record the main intermediate investigations. Not every binary experiment was
kept because later source revisions accumulate or supersede those changes.

## Current recommendation

- Use **exp40** as the tested baseline.
- Use WPA2-Personal/CCMP for initial testing. WPA3 and Apple peer-to-peer
  services are not implemented.

## What is intentionally absent

- Complete EFI backups and machine-specific OpenCore configuration.
- Apple `IOSkywalkFamily` and `IO80211FamilyLegacy` binaries.
- Packet captures and diagnostics that may reveal network names, MAC addresses
  or other local identifiers.
- Firmware binaries whose redistribution terms should be reviewed separately;
  use `firmware/README` and the generation tooling to provide firmware locally.
