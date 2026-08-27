# NativeV2 exp22 — exp12 Secure Performance Baseline

This experiment intentionally returns to the exp12 datapath, the best measured
download/latency baseline, and adds only fixes outside the steady-state packet
path.

Preserved from exp12: adaptive RX/NAPI, RX batching, TX batching timers and
thresholds, VHT 80 MHz host TX speed fix, A-MPDU/BlockAck, ring flow control,
and exp12 stability/reconnect behavior.

Added: Linux-correct EWMA semantics; exact-sized/NUL-terminated UserClient
connect input; constant-time EAPOL MIC comparison; replay-aware M1/M3 handling;
and protection against PTK/GTK reinstallation on retransmitted M3.

Not imported: forced ASPM-off, BQL, immediate NAPI, fast-reschedule, host
A-MSDU, or aggressive recovery changes from later experiments.

Primary test network: VERO_GUSTAVO-PLUS, 5 GHz, channel 120, VHT 80 MHz.
First milestone is to reproduce the exp12 ~67 Mbps download with low latency
and stable connection before changing throughput behavior again.
