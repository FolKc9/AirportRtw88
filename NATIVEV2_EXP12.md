# NativeV2 exp12 — Adaptive Datapath

Base: exp11-fixed.

Goals:
- preserve exp10/exp11 connection recovery and stability;
- preserve exp8 VHT 80 MHz / high-MCS TX fix;
- reduce TX doorbells without delaying ARP/EAPOL/DHCP/ICMP/TCP handshakes;
- coalesce pure TCP ACKs briefly instead of kicking once per ACK;
- batch bulk A-MPDU data up to 8 frames;
- adapt NAPI delay from 60–220 us based on the previous poll density;
- do not enlarge the software TX queue or weaken backpressure thresholds.

Build:
```bash
cd ~/Downloads/Feixiao-NativeV2-exp12
./build-nativev2.command
```

Successful output:
- `dist/rtw88.kext`
- `dist/rtw88ctl`
- `dist/README.txt`

After reboot:
```bash
cd ~/Downloads/Feixiao-NativeV2-exp12/dist
./rtw88ctl status
./rtw88ctl perf 60
```
Run Speedtest during the perf sample.
