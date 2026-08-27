# Security policy

AirportRtw88 is experimental kernel-level software. A defect can panic macOS,
corrupt network state or prevent a normal boot.

- Keep a bootable EFI backup before installation.
- Never replace the only working recovery path.
- Do not load more than one driver for PCI ID `10ec:c821`.
- Do not publish EFI folders, packet captures or logs before checking them for
  serial numbers, UUIDs, MAC addresses, SSIDs and other private data.

For a suspected vulnerability, open a GitHub security advisory instead of a
public issue when the repository supports private reporting.
