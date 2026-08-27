# NativeV2 exp8 — VHT TX Speed Fix

This build is based on the exp5 RX/RA-feed state and turns the VHT rate already
computed by rtw88 into the actual TX descriptor start rate on 5 GHz.

## What it fixes

The RTL8821CE was associated as VHT / 80 MHz / SGI, but firmware rate adaptation
remained stuck at VHT1SS-MCS0 (about 6.5 Mbps). rtw88 already computes the
highest peer-supported VHT rate and the station bandwidth before filling the TX
descriptor; with firmware RA enabled those fields were not being used as the
start data rate.

exp8 sets `USE_RATE` for unicast VHT data on 5 GHz so the hardware starts from
the peer-supported VHT rate, copies the negotiated 40/80 MHz bandwidth and SGI,
and **keeps hardware rate fallback enabled**. It does not blindly lock MCS9.

2.4 GHz and non-VHT traffic remain on the previous path.

## Build

```bash
cd ~/Downloads/Feixiao-NativeV2-exp8
rm -rf ../rtw88-stable
./build-nativev2.command
```

Output:

- `dist/rtw88.kext`
- `dist/rtw88ctl`

## Test

After installing the kext and rebooting:

```bash
cd ~/Downloads/Feixiao-NativeV2-exp8/dist
./rtw88ctl status
./rtw88ctl perf 30
```

During `perf 30`, run a Speedtest. The important line is `exp8 TX pkt-info`.
For the current 5 GHz / 80 MHz network it should show `use-rate=yes`,
`fallback-disabled=no`, `BW=80MHz` and a VHT MCS above MCS0.
