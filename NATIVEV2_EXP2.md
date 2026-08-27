# NativeV2 exp2 — RTL8821CE throughput pass

This branch keeps the current Feixiao/Starskiff control ABI intact. Selector 10 (`rtw88ctl perf`) is extended with exp2 counters.

## Why exp2 exists

The exp1 trace from the target RTL8821CE proved the radio side is already healthy: 80 MHz, TX A-MPDU enabled, seven active RX BlockAck TIDs, and RSSI around -29 to -38 dBm. Despite that, payload stayed around 7–11 Mbps and the BE TX ring repeatedly entered backpressure. One 30 s sample performed about 13,900 TX DMA maps while moving only ~3.6 MiB through those maps. exp1 allocated/prepared/released a fresh IOBufferMemoryDescriptor for every one of those TX packets. RX also flushed the macOS input queue once per packet.

## exp2 changes

- Preallocated 1 MiB TX DMA bounce pool: 256 x 4 KiB slots below 4 GiB.
- TX completion returns a pool slot instead of releasing an IOKit DMA descriptor.
- Oversize/exhausted-pool mappings safely fall back to exp1 behavior.
- RX packets are queued and `flushInputQueue()` runs in ~1 ms batches instead of once per packet.
- BE-ring flow control now stalls at 24 free descriptors and resumes at 80 (exp1 used 96/160).
- `rtw88ctl perf` reports TX-pool hits/fallbacks and RX packets-per-flush.

## Test

Build on macOS:

```bash
./build-nativev2.command
```

Install `dist/rtw88.kext`, reboot, connect, then run:

```bash
./rtw88ctl status
./rtw88ctl perf 30
```

Start a Speedtest while the 30-second sample is running. The useful exp2 lines are:

- `exp2 TX pool: hits ... fallback ...` — fallback should ideally stay near zero.
- `exp2 RX batching: ... pkt/flush` — under download load this should rise above 1.
- `TX-stall events` — should fall substantially relative to exp1.

The goal of this pass is to remove host-side packet/DMA overhead before changing the native CoreWiFi/IO80211 integration layer.
