# AirportRtw88

Native macOS Wi-Fi integration for the Realtek RTL8821CE PCIe adapter, based
on the Linux `rtw88` driver.

AirportRtw88 presents the adapter to macOS as an AirPort/IO80211 interface.
Networks appear in the system Wi-Fi menu and applications can use CoreWLAN;
no separate connection application is required.

> **Project status:** experimental. `exp40` is the current hardware-tested
> baseline. The native path has been verified on
> RTL8821CE, including scanning, WPA2 association, DHCP, IPv4/IPv6 traffic and
> the macOS Wi-Fi menu.

To the project authors' knowledge, this is the first publicly documented,
hardware-verified native IO80211 integration for RTL8821CE. This is a scoped
statement, not a claim that every Realtek or macOS Wi-Fi project has been
exhaustively catalogued.

## Verified configuration

| Component | Verified value |
| --- | --- |
| Adapter | Realtek RTL8821CE PCIe, `10ec:c821` |
| Architecture | Intel x86_64 |
| macOS | Tahoe 26.6.2 (build 25G83) |
| Boot loader | OpenCore |
| Native stack | `IOSkywalkFamily` + `IO80211FamilyLegacy` |
| Security | WPA2-Personal / CCMP |
| Bands | 2.4 GHz and 5 GHz |

Only the device above is currently enabled in the release personality. Other
chips present in the imported rtw88 source tree are **not** supported claims.

## What works

- Native Wi-Fi menu and CoreWLAN discovery
- Live 2.4 GHz and 5 GHz scans
- Open networks and WPA2-Personal with a 32-byte PMK supplied by macOS
- DHCP, IPv4, IPv6, ARP and DNS traffic through the native `en0` interface
- Remembered-network auto-join path (v1 candidate)
- 802.11n/ac negotiation, QoS data frames and BlockAck/A-MPDU path (v1 candidate)
- Clean disconnect and reconnect notifications

## Known limitations

- WPA3 and WPA2/WPA3 transition mode are not supported yet. Configure a
  WPA2-Personal/CCMP SSID for testing.
- AWDL, AirDrop, Sidecar, Personal Hotspot and Apple peer-to-peer features are
  not implemented.
- Sleep/wake and roaming need broader hardware testing.
- Apple Silicon, USB/SDIO adapters and Realtek PCI IDs other than `10ec:c821`
  are not supported by this release.
- This is a kernel extension for OpenCore systems. It is not a normal macOS app.

## Install

Read [docs/INSTALL.md](docs/INSTALL.md) before changing your EFI. The order of
the four kexts and the `IOSkywalkFamily` block entry are required on the tested
Tahoe configuration.

Keep a bootable EFI backup. Do not remove a working Ethernet or USB recovery
path until this driver has passed a cold boot on your machine.

The hardware-tested archive is kept in [`releases/`](releases/):
`AirportRtw88Legacy-exp40-rx-workloop-hotfix.zip`.

## Build

Requirements:

- Xcode Command Line Tools
- MacKernelSDK in `MacKernelSDK/`
- The rtw88 sources already included/configured by this tree

Build the native legacy target:

```sh
make FLAVOR=airport-legacy kext
```

The result is written to:

```text
build-airport-legacy/out/AirportRtw88Legacy.kext
```

The release target is intentionally limited to x86_64 and RTL8821CE.

## Diagnostics

Use the read-only commands in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).
Do **not** use `rtw88ctl` with the native release: its old user-client path is
not part of v1 and has caused unsafe lifetime races in development builds.

## Release process

The acceptance tests are listed in
[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md). See
[CHANGELOG.md](CHANGELOG.md) for the changes included in the current candidate.

The development path and the reasoning behind the experiments are summarized
in [docs/DEVELOPMENT_HISTORY.md](docs/DEVELOPMENT_HISTORY.md).

## Safety and privacy

This repository intentionally excludes complete EFI folders, machine-specific
`config.plist` files, diagnostic captures and Apple networking kexts. Those
files can contain hardware identifiers or software that should not be
redistributed. The repository contains only the driver project, reproducible
build tooling, public documentation and selected AirportRtw88 test builds.

## Credits

AirportRtw88 builds on work from:

- [Feixiao](https://github.com/thegwchr/Feixiao)
- the Linux kernel `rtw88` maintainers and contributors
- [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
- [AirportItlwm](https://github.com/OpenIntelWireless/itlwm), used as an
  IO80211 lifecycle reference
- [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher),
  whose legacy wireless work documents the modern macOS stack constraints

Apple kernel extensions are not redistributed by this project. Source files
retain their SPDX license identifiers; imported components remain under their
respective upstream licenses.
