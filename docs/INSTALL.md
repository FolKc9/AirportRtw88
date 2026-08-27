# Installation guide

This guide documents the configuration that was verified with an RTL8821CE
PCIe adapter (`10ec:c821`) on macOS Tahoe 26.6.2. Treat other macOS versions and
hardware as untested.

## Before you begin

You need:

- A working OpenCore EFI and a way to boot its backup
- `Lilu.kext`
- `IOSkywalkFamily.kext`
- `IO80211FamilyLegacy.kext`
- `AirportRtw88Legacy.kext` from this project's release archive
- A WPA2-Personal/CCMP network for the first test

The two Apple networking kexts must come from a compatible local installation
or a workflow such as OpenCore Legacy Patcher. They are Apple software and are
not included in AirportRtw88 releases.

Make a complete copy of the working EFI before proceeding. Keep the backup on
another volume or USB drive that you know how to boot.

## Copy the kexts

Copy these bundles to `EFI/OC/Kexts/`:

```text
Lilu.kext
IOSkywalkFamily.kext
IO80211FamilyLegacy.kext
AirportRtw88Legacy.kext
```

Do not mix `AirportRtw88Legacy.kext` with another driver bound to PCI ID
`10ec:c821`.

## Configure OpenCore

Add the bundles to `Kernel -> Add` in this exact dependency order:

| Order | BundlePath | ExecutablePath | MinKernel | MaxKernel | Arch |
| --- | --- | --- | --- | --- | --- |
| 1 | `Lilu.kext` | `Contents/MacOS/Lilu` | your existing value | your existing value | x86_64 |
| 2 | `IOSkywalkFamily.kext` | `Contents/MacOS/IOSkywalkFamily` | `25.0.0` | `25.99.99` | x86_64 |
| 3 | `IO80211FamilyLegacy.kext` | `Contents/MacOS/IO80211FamilyLegacy` | `25.0.0` | `25.99.99` | x86_64 |
| 4 | `AirportRtw88Legacy.kext` | `Contents/MacOS/AirportRtw88Legacy` | `25.0.0` | `25.99.99` | x86_64 |

For every entry, use `Contents/Info.plist` as `PlistPath` and set `Enabled` to
`true`.

Add this entry to `Kernel -> Block`:

| Field | Value |
| --- | --- |
| Identifier | `com.apple.iokit.IOSkywalkFamily` |
| Strategy | `Exclude` |
| MinKernel | `25.0.0` |
| MaxKernel | `25.99.99` |
| Arch | `x86_64` |
| Enabled | `true` |

The version ranges above deliberately target Darwin 25 only. Adjusting them for
another macOS release is a porting change, not a normal installation step.

Run an OpenCore snapshot/validation after editing `config.plist`. Resolve every
error before rebooting.

## First boot

1. Boot through the edited OpenCore entry.
2. Open System Settings -> Wi-Fi.
3. Join a WPA2-Personal network from the normal macOS menu.
4. Confirm that the interface receives an address from your router and that a
   website loads.
5. Open the network's Details page and leave **Auto-Join** enabled.
6. Reboot without manually selecting the network. The v1 candidate should scan,
   associate and obtain a valid IP automatically.

If the network was saved while an older experimental build was failing,
CoreWLAN may have marked its profile as disabled. Forget that network once,
join it again manually, confirm Auto-Join is enabled, and repeat the reboot
test.

## Updating

Replace only `EFI/OC/Kexts/AirportRtw88Legacy.kext`. Preserve the previous kext
as a zip or in the backup EFI, validate `config.plist`, then reboot. Do not try
to unload and replace this driver on a running system.

## Rollback

Boot the backup OpenCore entry or mount the EFI from another operating system,
restore the previous `AirportRtw88Legacy.kext`, and restore the previous
`config.plist` if its entries changed. A kext replacement is not active until
the next boot.

