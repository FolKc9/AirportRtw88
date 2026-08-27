# v1 release checklist

Do not promote a candidate to v1.0.0 until all required checks pass on the
verified RTL8821CE machine.

## Build and package

- [x] Native legacy target builds successfully for x86_64
- [x] `Info.plist` validates
- [x] Release personality matches only `10ec:c821`
- [x] EFI binary SHA-256 matches the built binary
- [x] Previous working kext is backed up
- [ ] Clean public repository builds from its documented prerequisites
- [ ] Release archive and SHA-256 are published together

## Boot and system integration

- [ ] Cold boot succeeds three consecutive times
- [ ] Driver version is visible in `system_profiler SPAirPortDataType`
- [ ] Native Wi-Fi menu opens and shows current scan results
- [ ] No repeated `Resource busy` scan loop appears in `airportd`
- [ ] Auto-Join connects to the saved WPA2 network without user interaction
- [ ] Auto-Join obtains a valid DHCP address and default route

## Connectivity

- [ ] WPA2/CCMP association succeeds
- [ ] IPv4, IPv6, DNS and HTTPS traffic work
- [ ] Disconnect and reconnect work from the macOS Wi-Fi menu
- [ ] Forgetting and re-adding a network works
- [ ] 2.4 GHz connection passes a basic traffic test
- [ ] 5 GHz connection negotiates the expected 40/80 MHz width
- [ ] TX and RX BlockAck telemetry becomes active under traffic
- [ ] `networkQuality -I en0 -v` is recorded for comparison with the baseline

## Stability

- [ ] Ten-minute bidirectional traffic test completes without a stall
- [ ] Repeated native scans do not drop the active connection
- [ ] Sleep/wake behavior is documented, whether passing or still limited
- [ ] No kernel panic or use-after-free signature appears
- [ ] Rollback to the previous kext has been tested or independently verified

## Documentation

- [x] Installation guide includes exact OpenCore order and kernel ranges
- [x] Limitations clearly exclude WPA3, AWDL and unverified hardware
- [x] Troubleshooting avoids the unsupported `rtw88ctl` path
- [x] Credits and upstream projects are documented
- [ ] Final v1 version replaces experimental version strings

