# NativeV2 exp11 — Coalesced Datapath

Base: exp10 stability/recovery + exp8 VHT/80 MHz TX speed fix.

## Goals

- Keep automatic reconnect and stale-association cleanup from exp10.
- Preserve VHT 1SS / 80 MHz host-start TX rate selection from exp8.
- Reduce RX scheduling overhead without busy-waiting.
- Give A-MPDU several MPDUs per PCI doorbell without the aggressive exp9 latency window.
- Always create `dist/`; on a failed build, write `dist/BUILD_FAILED.txt`.

## exp11 changes

### RX / NAPI

- Restores a NAPI scheduled/running bit in the macOS compatibility layer.
- Repeated schedule requests while one NAPI poll is pending are collapsed.
- First poll is delayed asynchronously by 75 us; no `udelay()` or workloop spin.
- macOS queued input is flushed once after each NAPI poll.
- A 2 ms RX timer remains only as a fallback for delivery outside NAPI.

### TX

- A-MPDU-eligible data can defer the PCI TX kick.
- Batch target: 4 frames.
- Hard latency bound: 250 us.
- Frames <=256 bytes flush immediately.
- Management/EAPOL/pre-aggregation traffic still kicks immediately in rtw88.
- Ring stall thresholds remain the conservative exp10 values (96/160).

## Build

```bash
cd ~/Downloads/Feixiao-NativeV2-exp11
./build-nativev2.command
```

Successful output:

```text
dist/rtw88.kext
dist/rtw88ctl
dist/README.txt
```

On failure:

```text
dist/BUILD_FAILED.txt
```

Version:

```text
1.1.11
NativeV2-CoalescedDatapath-exp11
```

## Test

```bash
cd ~/Downloads/Feixiao-NativeV2-exp11/dist
./rtw88ctl status
./rtw88ctl perf 60
```

Run a speed test while `perf 60` is sampling. Important exp11 lines:

```text
exp11 NAPI coalescing:
exp11 TX batching:
exp11 stability:
exp11 link RA:
```
