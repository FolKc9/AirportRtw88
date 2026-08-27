#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
p=root/'drivers/net/wireless/realtek/rtw88/tx.c'
s=p.read_text()
if 'rtw88_exp7_tx_diag_record(&pkt_info);' not in s:
    old='\trtw_tx_pkt_info_update(rtwdev, &pkt_info, control->sta, skb);\n\tret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);'
    new='\trtw_tx_pkt_info_update(rtwdev, &pkt_info, control->sta, skb);\n\t/* exp7: snapshot the exact packet-info fields that become TX descriptor\n\t * rate/rate-id/BW/SGI/use-rate bits.  Passive in auto mode. */\n\trtw88_exp7_tx_diag_record(&pkt_info);\n\tret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);'
    if old not in s:
        raise SystemExit('ERROR: exp7: rtw_tx() anchor not found')
    s=s.replace(old,new,1)
if 'exp7 diagnostic override' not in s:
    old='\tif (fix_rate < DESC_RATE_MAX) {\n\t\tpkt_info->rate = fix_rate;\n\t\tpkt_info->dis_rate_fallback = true;\n\t\tpkt_info->use_rate = true;\n\t}\n}'
    new='\tif (fix_rate < DESC_RATE_MAX) {\n\t\tpkt_info->rate = fix_rate;\n\t\tpkt_info->dis_rate_fallback = true;\n\t\tpkt_info->use_rate = true;\n\t\t/* exp7 diagnostic override: forced VHT probes use the already\n\t\t * negotiated station SGI capability; auto RA is unchanged. */\n\t\tif (sta && fix_rate >= DESC_RATEVHT1SS_MCS0)\n\t\t\tpkt_info->short_gi = si->sgi_enable;\n\t}\n}'
    if old not in s:
        raise SystemExit('ERROR: exp7: fix_rate anchor not found')
    s=s.replace(old,new,1)
p.write_text(s)
print('exp7 TX probe edits ready.')
