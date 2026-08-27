# NativeV2 exp10 — Stability + Fast IRQ

Target: RTL8821CE on macOS Tahoe 26.x.

This build starts from the exp8 datapath (the first version that moved TX from
MCS0 to high VHT rates) and deliberately does **not** include exp9's TX-doorbell
batching or 50 us NAPI delay.

## What exp10 changes

### Connection recovery

- Fully clears `bss_conf`, `vif->cfg`, keys, BlockAck and the registered STA when
  the AP sends deauth/disassoc.
- Fixes an association-response use-after-free: status/AID are now read before
  the skb is released.
- Automatically retries authentication/association after AP link loss and
  state-machine timeouts.
- After two normal reassociation retries, one automatic soft radio restart is
  allowed. This mirrors the manual Wi-Fi off/on action that previously restored
  the card, but prevents an endless power-cycle loop.
- Flushes the macOS TX software queue on link loss so packets from the old
  association cannot be replayed into the new one.
- Connected beacon watchdog: after target beacons have been observed, three
  consecutive 4-second silent windows trigger recovery. If the firmware never
  exposes beacons, the watchdog stays passive instead of false-triggering.

### Throughput

- Preserves exp3 RX partial DMA sync.
- Preserves exp5 RSSI/RA feed.
- Preserves exp8 VHT/80 MHz host-start TX speed fix with hardware fallback.
- Removes exp9 TX doorbell batching and NAPI delay because that experiment made
  disconnects materially more frequent.
- Removes per-interrupt `IOLog` calls from the PCI IRQ hot path and changes the
  compatibility logger default from DEBUG to WARN. The CLI can still enable
  `debug 2`/`debug 3` temporarily when detailed logs are needed.
- Removes one redundant XNU `thread_call` hop from every PCI threaded interrupt:
  the rtw88 threaded bottom-half now runs directly on the already-deferred
  `IOInterruptEventSource` workloop. NAPI remains separately deferred. The goal
  is to reclaim TX descriptors sooner without increasing software queue depth.

## Build

No manual `rm -rf ../rtw88-stable` is required anymore:

```bash
cd ~/Downloads/Feixiao-NativeV2-exp10
./build-nativev2.command
```

The script always recreates a clean `rtw88-stable` `feixiao` branch, applies the
known-good exp3/exp5/exp8 changes, verifies their post-conditions, builds, and
verifies both output files.

`dist/` exists even before compilation. On success it contains:

```text
dist/rtw88.kext
dist/rtw88ctl
dist/README.txt
```

On failure it leaves `dist/BUILD_FAILED.txt` and prints the failing stage.

## Test

After installing the new `dist/rtw88.kext` in the EFI and rebooting:

```bash
cd ~/Downloads/Feixiao-NativeV2-exp10/dist
./rtw88ctl status
./rtw88ctl perf 60
```

Run a Speedtest during the 60-second sample. The new lines are:

```text
exp10 fast IRQ: ...
exp10 stability: ...
```

For stability, leave the machine connected long enough to catch the previous
random-drop behavior. A successful self-heal increments `reconnect-attempt` and
`recovered`; a recovery that had to emulate Wi-Fi off/on increments
`radio-reset`.

## Safety

This is experimental kernel code. Keep a bootable backup EFI with the last
working kext before testing.
