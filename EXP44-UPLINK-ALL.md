# EXP44 — Uplink All Fixes

Single integrated uplink test build based on the working EXP40 AirPort lifecycle.

The EXP40 behavior for native macOS integration is intentionally preserved:
- APPLE80211 power handling;
- enable/disable lifecycle;
- AirPort attach and link-state notifications;
- existing medium dictionary;
- existing TX queue resume path.

Uplink fixes applied on top:
- release the temporary 6 Mbps TX bootstrap after the link becomes fully connected;
- keep firmware rate adaptation enabled after connected-channel restore;
- check BE-ring resources before dequeuing an IO80211 output packet;
- restore actual host-deferred A-MPDU doorbell batching for aggregation-eligible data;
- keep management, EAPOL and pre-BlockAck traffic immediate;
- retry uplink ADDBA while connected until TX BlockAck is established.

The branch `exp44-uplink-all-fixes` is the canonical EXP44 implementation. `main`/EXP40 remains untouched as the rollback baseline.
