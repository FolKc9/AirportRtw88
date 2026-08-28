# EXP44 — Uplink All Fixes

Single integrated test build based on EXP40.

Includes:
- release the temporary 6 Mbps TX bootstrap after the link becomes connected;
- keep firmware rate adaptation enabled after channel restore;
- correct IO80211 pull-model backpressure before dequeue;
- stop servicing the inactive legacy output queue on TX reclaim;
- restore actual host-deferred A-MPDU doorbell batching;
- retry uplink ADDBA while connected until BlockAck is established;
- publish a VHT-capable link medium instead of the legacy 54 Mbps placeholder.

EXP40/main remains untouched as the rollback baseline.
