# NativeV2 exp4 — Link/RA diagnostics + latency-safe TX

Changes from exp3:

- Keeps the exp3 partial RX DMA sync optimization (copy only actual RX frame bytes).
- Keeps the pre-mapped 256-slot TX DMA pool and RX batching.
- Restores TX resume hysteresis to 160 free descriptors while keeping the smaller 128-packet software queue; this targets the user's higher loaded ping without returning to exp2's deep ring queueing.
- Samples a real firmware TX ACK report on 1 out of every 64 data frames.
- Exposes firmware TX/RX descriptor rates, STA VHT/BW/SGI state, RA MCS/NSS/bandwidth and RA mask through `rtw88ctl perf`.
- Adds RX BlockAck reorder diagnostics: stale/duplicate/ahead frames, timeout flushes and holes skipped.
- Adds IRQ/NAPI diagnostics: interrupt count, poll count, RX work and budget exhaustion count.

Why this pass exists:

exp3 reduced RX bounce-copy cost from ~23 KiB per delivered packet to ~1.4 KiB, but throughput remained low. That means the large RX memcpy was real overhead but not the main throughput limiter. exp4 distinguishes an on-air rate/ACK/reorder problem from a macOS scheduler/NAPI problem before making another risky datapath change.
