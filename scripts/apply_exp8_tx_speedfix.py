#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1])
p = root / 'drivers/net/wireless/realtek/rtw88/tx.c'
s = p.read_text()

# Snapshot final packet-info fields immediately before HCI submission.
if 'rtw88_exp7_tx_diag_record(&pkt_info);' not in s:
    old = '\trtw_tx_pkt_info_update(rtwdev, &pkt_info, control->sta, skb);\n\tret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);'
    new = ('\trtw_tx_pkt_info_update(rtwdev, &pkt_info, control->sta, skb);\n'
           '\t/* exp8: snapshot the exact packet-info fields handed to HCI. */\n'
           '\trtw88_exp7_tx_diag_record(&pkt_info);\n'
           '\tret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);')
    if old not in s:
        raise SystemExit('ERROR: exp8: rtw_tx() anchor not found')
    s = s.replace(old, new, 1)

# Replace the stock fixed-rate tail with a 5 GHz VHT host-start speed fix.
# The normal code already computes the highest supported VHT rate, station BW,
# rate-id, LDPC and A-MPDU settings.  In Feixiao, firmware RA is stuck at MCS0
# despite an 80 MHz/VHT association.  For unicast VHT data we therefore set
# USE_RATE so the descriptor starts at the already-computed VHT rate, while
# keeping hardware data-rate fallback enabled.  This is deliberately not a
# blind MCS9 lock: the rate is capped by the peer's advertised TX MCS map and
# fallback remains allowed.
marker = 'exp8 host-start VHT speed fix'
if marker not in s:
    old = ('\tfix_rate = dm_info->fix_rate;\n'
           '\tif (fix_rate < DESC_RATE_MAX) {\n'
           '\t\tpkt_info->rate = fix_rate;\n'
           '\t\tpkt_info->dis_rate_fallback = true;\n'
           '\t\tpkt_info->use_rate = true;\n'
           '\t}\n}')
    # exp7 trees have the diagnostic short-GI addition in this same block.
    old_exp7 = ('\tfix_rate = dm_info->fix_rate;\n'
                '\tif (fix_rate < DESC_RATE_MAX) {\n'
                '\t\tpkt_info->rate = fix_rate;\n'
                '\t\tpkt_info->dis_rate_fallback = true;\n'
                '\t\tpkt_info->use_rate = true;\n'
                '\t\t/* exp7 diagnostic override: forced VHT probes use the already\n'
                '\t\t * negotiated station SGI capability; auto RA is unchanged. */\n'
                '\t\tif (sta && fix_rate >= DESC_RATEVHT1SS_MCS0)\n'
                '\t\t\tpkt_info->short_gi = si->sgi_enable;\n'
                '\t}\n}')
    new = ('\tfix_rate = dm_info->fix_rate;\n'
           '\tif (fix_rate < DESC_RATE_MAX) {\n'
           '\t\tpkt_info->rate = fix_rate;\n'
           '\t\t/* Explicit diagnostic overrides still allow hardware fallback;\n'
           '\t\t * this avoids turning a transient fade into a total TX stall. */\n'
           '\t\tpkt_info->dis_rate_fallback = false;\n'
           '\t\tpkt_info->use_rate = true;\n'
           '\t\tif (sta && fix_rate >= DESC_RATEVHT1SS_MCS0) {\n'
           '\t\t\tpkt_info->bw = si->bw_mode;\n'
           '\t\t\tpkt_info->short_gi = si->sgi_enable;\n'
           '\t\t}\n'
           '\t} else if (sta && sta->deflink.vht_cap.vht_supported &&\n'
           '\t\t   rtwdev->hal.current_band_type == RTW_BAND_5G) {\n'
           '\t\t/* exp8 host-start VHT speed fix:\n'
           '\t\t * rtw_tx_data_pkt_info_update() has already selected the highest\n'
           '\t\t * rate allowed by the peer VHT MCS map and copied si->bw_mode.\n'
           '\t\t * Feixiao firmware RA remains stuck at MCS0/20 MHz, so make those\n'
           '\t\t * descriptor values effective while retaining HW rate fallback. */\n'
           '\t\tpkt_info->use_rate = true;\n'
           '\t\tpkt_info->dis_rate_fallback = false;\n'
           '\t\tpkt_info->bw = si->bw_mode;\n'
           '\t\tpkt_info->short_gi = si->sgi_enable;\n'
           '\t}\n}')
    if old_exp7 in s:
        s = s.replace(old_exp7, new, 1)
    elif old in s:
        s = s.replace(old, new, 1)
    else:
        raise SystemExit('ERROR: exp8: TX rate-control anchor not found')

p.write_text(s)
print('exp8 VHT host-start speed fix edits ready.')
