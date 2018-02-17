/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2004, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

	Module Name:
	rtmp_data.c

	Abstract:
	Data path subroutines

	Revision History:
	Who 		When			What
	--------	----------		----------------------------------------------
*/
#include "rt_config.h"





VOID STARxEAPOLFrameIndicate(
	IN struct rtmp_adapter *pAd,
	IN MAC_TABLE_ENTRY *pEntry,
	IN RX_BLK *pRxBlk,
	IN u8 FromWhichBSSID)
{
	u8 *pTmpBuf;


#ifdef WPA_SUPPLICANT_SUPPORT
	if (pAd->StaCfg.wpa_supplicant_info.WpaSupplicantUP) {
		/* All EAPoL frames have to pass to upper layer (ex. WPA_SUPPLICANT daemon) */
		/* TBD : process fragmented EAPol frames */
		{
			/* In 802.1x mode, if the received frame is EAP-SUCCESS packet, turn on the PortSecured variable */
			if ((pAd->StaCfg.wdev.IEEE8021X == true) &&
			    (pAd->StaCfg.wdev.WepStatus == Ndis802_11WEPEnabled) &&
			    (EAP_CODE_SUCCESS ==
			     WpaCheckEapCode(pAd, pRxBlk->pData,
					     pRxBlk->DataSize,
					     LENGTH_802_1_H))) {
				u8 *Key;
				u8 CipherAlg;
				int idx = 0;

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("Receive EAP-SUCCESS Packet\n"));
				STA_PORT_SECURED(pAd);

				if (pAd->StaCfg.wpa_supplicant_info.IEEE8021x_required_keys == false) {
					idx = pAd->StaCfg.wpa_supplicant_info.DesireSharedKeyId;
					CipherAlg = pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].CipherAlg;
					Key = pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].Key;

					if (pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].KeyLen > 0) {
						/* Set key material and cipherAlg to Asic */
						RTMP_ASIC_SHARED_KEY_TABLE(pAd,
									   BSS0,
									   idx,
									   &pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx]);

						/* STA doesn't need to set WCID attribute for group key */

						/* Assign pairwise key info */
						RTMP_SET_WCID_SEC_INFO(pAd,
								       BSS0,
								       idx,
								       CipherAlg,
								       BSSID_WCID,
								       SHAREDKEYTABLE);

						RTMP_IndicateMediaState(pAd,
									NdisMediaStateConnected);
						pAd->ExtraInfo = GENERAL_LINK_UP;

						/* For Preventing ShardKey Table is cleared by remove key procedure. */
						pAd->SharedKey[BSS0][idx].CipherAlg = CipherAlg;
						pAd->SharedKey[BSS0][idx].KeyLen =
						    pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].KeyLen;
						memmove(pAd->SharedKey[BSS0][idx].Key,
							       pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].Key,
							       pAd->StaCfg.wpa_supplicant_info.DesireSharedKey[idx].KeyLen);
					}
				}
			}

			Indicate_Legacy_Packet(pAd, pRxBlk, FromWhichBSSID);
			return;
		}
	} else
#endif /* WPA_SUPPLICANT_SUPPORT */
	{
		/*
		   Special DATA frame that has to pass to MLME
		   1. Cisco Aironet frames for CCX2. We need pass it to MLME for special process
		   2. EAPOL handshaking frames when driver supplicant enabled, pass to MLME for special process
		 */
		{
			pTmpBuf = pRxBlk->pData - LENGTH_802_11;
			memmove(pTmpBuf, pRxBlk->pHeader, LENGTH_802_11);

			REPORT_MGMT_FRAME_TO_MLME(pAd, pRxBlk->wcid,
						  pTmpBuf,
						  pRxBlk->DataSize +
						  LENGTH_802_11, pRxBlk->rssi[0],
						  pRxBlk->rssi[1], pRxBlk->rssi[2],
						  0,
						  OPMODE_STA);
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("!!! report EAPOL DATA to MLME (len=%d) !!!\n",
				      pRxBlk->DataSize));
		}
	}

	dev_kfree_skb_any(pRxBlk->pRxPacket);
	return;
}


VOID STARxDataFrameAnnounce(
	IN struct rtmp_adapter *pAd,
	IN MAC_TABLE_ENTRY *pEntry,
	IN RX_BLK *pRxBlk,
	IN u8 FromWhichBSSID)
{

	/* non-EAP frame */
	if (!RTMPCheckWPAframe
	    (pAd, pEntry, pRxBlk->pData, pRxBlk->DataSize, FromWhichBSSID)) {
		/* before LINK UP, all DATA frames are rejected */
		if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
			dev_kfree_skb_any(pRxBlk->pRxPacket);
			return;
		}




		{
			struct rtmp_wifi_dev *wdev = &pAd->StaCfg.wdev;
			/* drop all non-EAP DATA frame before */
			/* this client's Port-Access-Control is secured */
			if (pRxBlk->pHeader->FC.Wep) {
				/* unsupported cipher suite */
				if (wdev->WepStatus == Ndis802_11EncryptionDisabled) {
					/* release packet */
					dev_kfree_skb_any(pRxBlk->pRxPacket);
					return;
				}
			} else {
				/* encryption in-use but receive a non-EAPOL clear text frame, drop it */
				if ((wdev->WepStatus != Ndis802_11EncryptionDisabled)
				    && (wdev->PortSecured == WPA_802_1X_PORT_NOT_SECURED)) {
					/* release packet */
					dev_kfree_skb_any(pRxBlk->pRxPacket);
					return;
				}
			}
		}
		RX_BLK_CLEAR_FLAG(pRxBlk, fRX_EAP);


		if (!RX_BLK_TEST_FLAG(pRxBlk, fRX_ARALINK)) {
			/* Normal legacy, AMPDU or AMSDU */
			CmmRxnonRalinkFrameIndicate(pAd, pRxBlk,
						    FromWhichBSSID);

		} else {
			/* ARALINK */
			CmmRxRalinkFrameIndicate(pAd, pEntry, pRxBlk,
						 FromWhichBSSID);
		}
	} else {
		RX_BLK_SET_FLAG(pRxBlk, fRX_EAP);
		if (RX_BLK_TEST_FLAG(pRxBlk, fRX_AMPDU)
		    && (pAd->CommonCfg.bDisableReordering == 0)) {
			Indicate_AMPDU_Packet(pAd, pRxBlk, FromWhichBSSID);
		} else
		{
			/* Determin the destination of the EAP frame */
			/*  to WPA state machine or upper layer */
			STARxEAPOLFrameIndicate(pAd, pEntry, pRxBlk, FromWhichBSSID);
		}
	}
}




/* For TKIP frame, calculate the MIC value	*/
bool STACheckTkipMICValue(
	IN struct rtmp_adapter *pAd,
	IN MAC_TABLE_ENTRY *pEntry,
	IN RX_BLK * pRxBlk)
{
	PHEADER_802_11 pHeader = pRxBlk->pHeader;
	u8 *pData = pRxBlk->pData;
	unsigned short DataSize = pRxBlk->DataSize;
	u8 UserPriority = pRxBlk->UserPriority;
	PCIPHER_KEY pWpaKey;
	u8 *pDA, *pSA;

	pWpaKey = &pAd->SharedKey[BSS0][pRxBlk->key_idx];

	pDA = pHeader->Addr1;
	if (RX_BLK_TEST_FLAG(pRxBlk, fRX_INFRA)) {
		pSA = pHeader->Addr3;
	} else {
		pSA = pHeader->Addr2;
	}

	if (RTMPTkipCompareMICValue(pAd,
				    pData,
				    pDA,
				    pSA,
				    pWpaKey->RxMic,
				    UserPriority, DataSize) == false) {
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("Rx MIC Value error 2\n"));

#ifdef WPA_SUPPLICANT_SUPPORT
		if (pAd->StaCfg.wpa_supplicant_info.WpaSupplicantUP) {
			WpaSendMicFailureToWpaSupplicant(pAd->net_dev, pHeader->Addr2,
							 (pWpaKey->Type == PAIRWISEKEY) ? true : false,
							 (INT) pRxBlk->key_idx, NULL);
		} else
#endif /* WPA_SUPPLICANT_SUPPORT */
		{
			RTMPReportMicError(pAd, pWpaKey);
		}

		/* release packet */
		dev_kfree_skb_any(pRxBlk->pRxPacket);
		return false;
	}

	return true;
}


/*
 All Rx routines use RX_BLK structure to hande rx events
 It is very important to build pRxBlk attributes
  1. pHeader pointer to 802.11 Header
  2. pData pointer to payload including LLC (just skip Header)
  3. set payload size including LLC to DataSize
  4. set some flags with RX_BLK_SET_FLAG()
*/
VOID STAHandleRxDataFrame(struct rtmp_adapter *pAd, RX_BLK *pRxBlk)
{
	struct mt7612u_rxinfo *pRxInfo = pRxBlk->pRxInfo;
	struct mt7612u_rxwi *pRxWI = pRxBlk->pRxWI;
	HEADER_802_11 *pHeader = pRxBlk->pHeader;
	struct sk_buff *pRxPacket = pRxBlk->pRxPacket;
	bool bFragment = false;
	MAC_TABLE_ENTRY *pEntry = NULL;
	u8 FromWhichBSSID = BSS0;
	u8 UserPriority = 0;

//+++Add by shiang for debug
//---Add by shiangf for debug

	if ((pHeader->FC.FrDs == 1) && (pHeader->FC.ToDs == 1)) {
		{
			dev_kfree_skb_any(pRxPacket);
			return;
		}
	}
	else
	{


		/* Drop not my BSS frames */
		if (pRxBlk->wcid < MAX_LEN_OF_MAC_TABLE)
			pEntry = &pAd->MacTab.Content[pRxBlk->wcid];

		if (pRxInfo->MyBss == 0) {
			{
				dev_kfree_skb_any(pRxPacket);
				return;
			}
		}


		pAd->RalinkCounters.RxCountSinceLastNULL++;


		/* Drop NULL, CF-ACK(no data), CF-POLL(no data), and CF-ACK+CF-POLL(no data) data frame */
		if ((pHeader->FC.SubType & 0x04)) /* bit 2 : no DATA */
		{
			dev_kfree_skb_any(pRxPacket);
			return;
		}

		if (pAd->StaCfg.BssType == BSS_INFRA) {
			/* Infrastructure mode, check address 2 for BSSID */
			if (1
			    ) {
				if (memcmp(&pHeader->Addr2, &pAd->MlmeAux.Bssid, 6))
				{
					/* Receive frame not my BSSID */
					dev_kfree_skb_any(pRxPacket);
					return;
				}
			}
		}
		else
		{	/* Ad-Hoc mode or Not associated */

			/* Ad-Hoc mode, check address 3 for BSSID */
			if (memcmp(&pHeader->Addr3, &pAd->CommonCfg.Bssid, 6)) {
				/* Receive frame not my BSSID */
				dev_kfree_skb_any(pRxPacket);
				return;
			}
		}

		/*/ find pEntry */
		if (pRxBlk->wcid < MAX_LEN_OF_MAC_TABLE) {
			pEntry = &pAd->MacTab.Content[pRxBlk->wcid];

		} else {
			/* IOT issue with Marvell test bed AP
			    Marvell AP ResetToOOB and do wps.
			    Because of AP send EAP Request too fast and without retransmit.
			    STA not yet add BSSID to WCID search table.
			    So, the EAP Request is dropped.
			    The patch lookup pEntry from MacTable.
			*/
			pEntry = MacTableLookup(pAd, &pHeader->Addr2[0]);
			if ( pEntry == NULL )
			{
				dev_kfree_skb_any(pRxPacket);
				return;
			}
		}

		/* infra or ad-hoc */
		if (pAd->StaCfg.BssType == BSS_INFRA) {
			RX_BLK_SET_FLAG(pRxBlk, fRX_INFRA);
				ASSERT(pRxBlk->wcid == BSSID_WCID);
			if (pRxBlk->wcid != BSSID_WCID)
			{
				printk("[%d] 1: %02x:%02x:%02x:%02x:%02x:%02x, 2: %02x:%02x:%02x:%02x:%02x:%02x, 3:%02x:%02x:%02x:%02x:%02x:%02x",
				pRxBlk->wcid, PRINT_MAC(pHeader->Addr1), PRINT_MAC(pHeader->Addr2), PRINT_MAC(pHeader->Addr3));
			}
		}
	}

	pRxBlk->pData = (u8 *) pHeader;

	/*
	   update RxBlk->pData, DataSize
	   802.11 Header, QOS, HTC, Hw Padding
	 */
	/* 1. skip 802.11 HEADER */
	{
		pRxBlk->pData += LENGTH_802_11;
		pRxBlk->DataSize -= LENGTH_802_11;
	}

	/* 2. QOS */
	if (pHeader->FC.SubType & 0x08) {
		RX_BLK_SET_FLAG(pRxBlk, fRX_QOS);
		UserPriority = *(pRxBlk->pData) & 0x0f;
		/* bit 7 in QoS Control field signals the HT A-MSDU format */
		if ((*pRxBlk->pData) & 0x80) {
			RX_BLK_SET_FLAG(pRxBlk, fRX_AMSDU);
		}

		/* skip QOS contorl field */
		pRxBlk->pData += 2;
		pRxBlk->DataSize -= 2;
	}
	pRxBlk->UserPriority = UserPriority;

	/* check if need to resend PS Poll when received packet with MoreData = 1 */
		{
			if ((RtmpPktPmBitCheck(pAd) == true) && (pHeader->FC.MoreData == 1))
			{
				if ((((UserPriority == 0) || (UserPriority == 3)) && pAd->CommonCfg.bAPSDAC_BE == 0) ||
		    			(((UserPriority == 1) || (UserPriority == 2)) && pAd->CommonCfg.bAPSDAC_BK == 0) ||
					(((UserPriority == 4) || (UserPriority == 5)) && pAd->CommonCfg.bAPSDAC_VI == 0) ||
					(((UserPriority == 6) || (UserPriority == 7)) && pAd->CommonCfg.bAPSDAC_VO == 0))
				{
					/* non-UAPSD delivery-enabled AC */
					RTMP_PS_POLL_ENQUEUE(pAd);
				}
			}
		}

	/* 3. Order bit: A-Ralink or HTC+ */
	if (pHeader->FC.Order) {
#ifdef AGGREGATION_SUPPORT
		if ((pRxBlk->rx_rate.field.MODE <= MODE_OFDM)
		    && (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED)))
		{
			RX_BLK_SET_FLAG(pRxBlk, fRX_ARALINK);
		} else
#endif /* AGGREGATION_SUPPORT */
		{
			RX_BLK_SET_FLAG(pRxBlk, fRX_HTC);
			/* skip HTC contorl field */
			pRxBlk->pData += 4;
			pRxBlk->DataSize -= 4;
		}
	}

	/* 4. skip HW padding */
	if (pRxInfo->L2PAD) {
		/* just move pData pointer, because DataSize excluding HW padding */
		RX_BLK_SET_FLAG(pRxBlk, fRX_PAD);
		pRxBlk->pData += 2;
	}
	if (pRxInfo->BA) {
		RX_BLK_SET_FLAG(pRxBlk, fRX_AMPDU);
	}

	/* Case I  Process Broadcast & Multicast data frame */
	if (pRxInfo->Bcast || pRxInfo->Mcast) {
#ifdef STATS_COUNT_SUPPORT
		INC_COUNTER64(pAd->WlanCounters.MulticastReceivedFrameCount);
#endif /* STATS_COUNT_SUPPORT */

		/* Drop Mcast/Bcast frame with fragment bit on */
		if (pHeader->FC.MoreFrag) {
			dev_kfree_skb_any(pRxPacket);
			return;
		}

		/* Filter out Bcast frame which AP relayed for us */
		if (pHeader->FC.FrDs
		    && MAC_ADDR_EQUAL(pHeader->Addr3, pAd->CurrentAddress)) {
			dev_kfree_skb_any(pRxPacket);
			return;
		}

		if (ADHOC_ON(pAd)) {
			MAC_TABLE_ENTRY *pAdhocEntry = NULL;
			pAdhocEntry = MacTableLookup(pAd, pHeader->Addr2);
			if (pAdhocEntry)
				Update_Rssi_Sample(pAd, &pAdhocEntry->RssiSample, pRxWI);
		}


		Indicate_Legacy_Packet(pAd, pRxBlk, FromWhichBSSID);
		return;
	}
	else if (pRxInfo->U2M)
	{
		pAd->LastRxRate = (ULONG)(pRxBlk->rx_rate.word);

		if (INFRA_ON(pAd)) {
			MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[BSSID_WCID];
			if (pEntry)
				Update_Rssi_Sample(pAd, &pEntry->RssiSample, pRxWI);
		}
		else if (ADHOC_ON(pAd)) {
			MAC_TABLE_ENTRY *pAdhocEntry = NULL;
			pAdhocEntry = MacTableLookup(pAd, pHeader->Addr2);
			if (pAdhocEntry)
				Update_Rssi_Sample(pAd, &pAdhocEntry->RssiSample, pRxWI);
		}

		Update_Rssi_Sample(pAd, &pAd->StaCfg.RssiSample, pRxWI);

#ifdef DBG_DIAGNOSE
		if (pAd->DiagStruct.inited) {
			struct dbg_diag_info *diag_info;
			diag_info = &pAd->DiagStruct.diag_info[pAd->DiagStruct.ArrayCurIdx];
			diag_info->RxDataCnt++;
#ifdef DBG_RX_MCS
			if (pRxBlk->rx_rate.field.MODE == MODE_HTMIX ||
				pRxBlk->rx_rate.field.MODE == MODE_HTGREENFIELD) {
				if (pRxBlk->rx_rate.field.MCS < MAX_MCS_SET)
					diag_info->RxMcsCnt_HT[pRxBlk->rx_rate.field.MCS]++;
			}
			if (pRxBlk->rx_rate.field.MODE == MODE_VHT) {
				INT mcs_idx = ((pRxBlk->rx_rate.field.MCS >> 4) * 10) +
								(pRxBlk->rx_rate.field.MCS & 0xf);
				if (mcs_idx < MAX_VHT_MCS_SET)
					diag_info->RxMcsCnt_VHT[mcs_idx]++;
			}
#endif /* DBG_RX_MCS */
		}
#endif /* DBG_DIAGNOSE */

		pAd->StaCfg.LastSNR0 = (u8) (pRxBlk->snr[0]);
		pAd->StaCfg.LastSNR1 = (u8) (pRxBlk->snr[1]);

		pAd->RalinkCounters.OneSecRxOkDataCnt++;

		if (pEntry != NULL)
		{
			pEntry->LastRxRate = pAd->LastRxRate;
			//if (pRxWI->ShortGI)
			if (pRxBlk->rx_rate.field.ShortGI)
				pEntry->OneSecRxSGICount++;
			else
				pEntry->OneSecRxLGICount++;

			pEntry->freqOffset = (CHAR)(pRxBlk->freq_offset);
			pEntry->freqOffsetValid = true;

		}

#ifdef PRE_ANT_SWITCH
#endif /* PRE_ANT_SWITCH */

		/* there's packet sent to me, keep awake for 1200ms */
		if (pAd->CountDowntoPsm < 12)
			pAd->CountDowntoPsm = 12;

		if (!((pHeader->Frag == 0) && (pHeader->FC.MoreFrag == 0))) {
			/* re-assemble the fragmented packets */
			/* return complete frame (pRxPacket) or NULL */
			bFragment = true;
			pRxPacket = RTMPDeFragmentDataFrame(pAd, pRxBlk);
		}

		if (pRxPacket) {
			pEntry = &pAd->MacTab.Content[pRxBlk->wcid];

			/* process complete frame */
			if (bFragment && (pRxInfo->Decrypted)
			    && (pEntry->WepStatus == Ndis802_11TKIPEnable)) {
				/* Minus MIC length */
				pRxBlk->DataSize -= 8;

				/* For TKIP frame, calculate the MIC value */
				if (STACheckTkipMICValue(pAd, pEntry, pRxBlk) == false) {
					return;
				}
			}

			STARxDataFrameAnnounce(pAd, pEntry, pRxBlk, FromWhichBSSID);
			return;
		} else {
			/*
			   just return because RTMPDeFragmentDataFrame() will release rx packet,
			   if packet is fragmented
			 */
			return;
		}
	}

	dev_kfree_skb_any(pRxPacket);

	return;
}




/*
	========================================================================

	Routine Description:
	Arguments:
		pAd 	Pointer to our adapter

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
VOID RTMPHandleTwakeupInterrupt(
	IN struct rtmp_adapter *pAd)
{
	AsicForceWakeup(pAd, false);
}


/*
========================================================================
Routine Description:
	This routine is used to do packet parsing and classification for Tx packet
	to STA device, and it will en-queue packets to our TxSwQ depends on AC
	class.

Arguments:
	pAd    		Pointer to our adapter
	pPacket 	Pointer to send packet

Return Value:
	NDIS_STATUS_SUCCESS		If succes to queue the packet into TxSwQ.
	NDIS_STATUS_FAILURE			If failed to do en-queue.

Note:
	You only can put OS-indepened & STA related code in here.
========================================================================
*/
INT STASendPacket(struct rtmp_adapter *pAd, struct sk_buff *pPacket)
{
	PACKET_INFO PacketInfo;
	u8 *pSrcBufVA;
	UINT SrcBufLen, AllowFragSize;
	u8 NumberOfFrag;
	u8 QueIdx;
	u8 UserPriority;
	u8 Wcid;
	MAC_TABLE_ENTRY *pMacEntry = NULL;
	struct rtmp_wifi_dev *wdev;


	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);
	if ((pSrcBufVA == NULL) || (SrcBufLen <= 14))
	{
		dev_kfree_skb_any(pPacket);
		DBGPRINT(RT_DEBUG_ERROR, ("%s():pkt error(%p, %d)\n",
					__FUNCTION__, pSrcBufVA, SrcBufLen));
		return NDIS_STATUS_FAILURE;
	}

	Wcid = RTMP_GET_PACKET_WCID(pPacket);
	/* In HT rate adhoc mode, A-MPDU is often used. So need to lookup BA Table and MAC Entry. */
	/* Note multicast packets in adhoc also use BSSID_WCID index. */
	{
		if (pAd->StaCfg.BssType == BSS_INFRA) {
			{
				pMacEntry = &pAd->MacTab.Content[BSSID_WCID];
				RTMP_SET_PACKET_WCID(pPacket, BSSID_WCID);
			}
		} else if (ADHOC_ON(pAd)) {
			if (*pSrcBufVA & 0x01) {
				RTMP_SET_PACKET_WCID(pPacket, MCAST_WCID);
				pMacEntry = &pAd->MacTab.Content[MCAST_WCID];
			} else {
					pMacEntry = MacTableLookup(pAd, pSrcBufVA);

				if (pMacEntry)
					RTMP_SET_PACKET_WCID(pPacket, pMacEntry->wcid);
			}
		}
	}

	if (!pMacEntry) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s():No such Addr(%2x:%2x:%2x:%2x:%2x:%2x) in MacTab\n",
			 __FUNCTION__, PRINT_MAC(pSrcBufVA)));
		dev_kfree_skb_any(pPacket);
		return NDIS_STATUS_FAILURE;
	}

	if (ADHOC_ON(pAd)
	    ) {
		RTMP_SET_PACKET_WCID(pPacket, (u8) pMacEntry->wcid);
	}

	wdev = &pAd->StaCfg.wdev;

	/* Check the Ethernet Frame type of this packet, and set the RTMP_SET_PACKET_SPECIFIC flags. */
	/*              Here we set the PACKET_SPECIFIC flags(LLC, VLAN, DHCP/ARP, EAPOL). */
	UserPriority = 0;
	QueIdx = QID_AC_BE;
	RTMPCheckEtherType(pAd, pPacket, pMacEntry, wdev, &UserPriority, &QueIdx);



	/* WPA 802.1x secured port control - drop all non-802.1x frame before port secured */
	if (((wdev->AuthMode == Ndis802_11AuthModeWPA) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPAPSK) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPA2) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPA2PSK)
#ifdef WPA_SUPPLICANT_SUPPORT
	     || (pAd->StaCfg.wdev.IEEE8021X == true)
#endif /* WPA_SUPPLICANT_SUPPORT */
	    )
	    && ((wdev->PortSecured == WPA_802_1X_PORT_NOT_SECURED)
		|| (pAd->StaCfg.MicErrCnt >= 2))
	    && (RTMP_GET_PACKET_EAPOL(pPacket) == false)
	    ) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s():Drop packet before port secured!\n", __FUNCTION__));
		dev_kfree_skb_any(pPacket);

		return NDIS_STATUS_FAILURE;
	}

	/*
		STEP 1. Decide number of fragments required to deliver this MSDU.
			The estimation here is not very accurate because difficult to
			take encryption overhead into consideration here. The result
			"NumberOfFrag" is then just used to pre-check if enough free
			TXD are available to hold this MSDU.
	*/
	if (*pSrcBufVA & 0x01)	/* fragmentation not allowed on multicast & broadcast */
		NumberOfFrag = 1;
	else if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED))
		NumberOfFrag = 1;	/* Aggregation overwhelms fragmentation */
	else if (CLIENT_STATUS_TEST_FLAG(pMacEntry, fCLIENT_STATUS_AMSDU_INUSED))
		NumberOfFrag = 1;	/* Aggregation overwhelms fragmentation */
	else if ((wdev->HTPhyMode.field.MODE == MODE_HTMIX)
		 || (wdev->HTPhyMode.field.MODE == MODE_HTGREENFIELD))
		NumberOfFrag = 1;	/* MIMO RATE overwhelms fragmentation */
	else
	{
		/*
			The calculated "NumberOfFrag" is a rough estimation because of various
			encryption/encapsulation overhead not taken into consideration. This number is just
			used to make sure enough free TXD are available before fragmentation takes place.
			In case the actual required number of fragments of an NDIS packet
			excceeds "NumberOfFrag"caculated here and not enough free TXD available, the
			last fragment (i.e. last MPDU) will be dropped in RTMPHardTransmit() due to out of
			resource, and the NDIS packet will be indicated NDIS_STATUS_FAILURE. This should
			rarely happen and the penalty is just like a TX RETRY fail. Affordable.
		*/
		uint32_t Size;

		AllowFragSize = (pAd->CommonCfg.FragmentThreshold) - LENGTH_802_11 - LENGTH_CRC;
		Size = PacketInfo.TotalPacketLength - LENGTH_802_3 + LENGTH_802_1_H;
		NumberOfFrag = (Size / AllowFragSize) + 1;
		/* To get accurate number of fragmentation, Minus 1 if the size just match to allowable fragment size */
		if ((Size % AllowFragSize) == 0) {
			NumberOfFrag--;
		}
	}
	/* Save fragment number to Ndis packet reserved field */
	RTMP_SET_PACKET_FRAGMENTS(pPacket, NumberOfFrag);

{
	bool RTSRequired;

	/*
		STEP 2. Check the requirement of RTS; decide packet TX rate
		If multiple fragment required, RTS is required only for the first fragment
		if the fragment size large than RTS threshold
	 */
	if (NumberOfFrag > 1)
		RTSRequired = (pAd->CommonCfg.FragmentThreshold > pAd->CommonCfg.RtsThreshold) ? 1 : 0;
	else
		RTSRequired = (PacketInfo.TotalPacketLength > pAd->CommonCfg.RtsThreshold) ? 1 : 0;
	RTMP_SET_PACKET_RTS(pPacket, RTSRequired);
}

	RTMP_SET_PACKET_UP(pPacket, UserPriority);


	{
		/* Make sure SendTxWait queue resource won't be used by other threads */
		spin_lock_bh(&pAd->irq_lock);
		if (pAd->TxSwQueue[QueIdx].Number >= pAd->TxSwQMaxLen) {
			spin_unlock_bh(&pAd->irq_lock);
			dev_kfree_skb_any(pPacket);

			return NDIS_STATUS_FAILURE;
		} else {
				InsertTailQueueAc(pAd, pMacEntry, &pAd->TxSwQueue[QueIdx], PACKET_TO_QUEUE_ENTRY(pPacket));
		}
		spin_unlock_bh(&pAd->irq_lock);
	}

	RTMP_BASetup(pAd, pMacEntry, UserPriority);

	pAd->RalinkCounters.OneSecOsTxCount[QueIdx]++;	/* TODO: for debug only. to be removed */
	return NDIS_STATUS_SUCCESS;
}


/*
	========================================================================

	Routine Description:
		This subroutine will scan through releative ring descriptor to find
		out avaliable free ring descriptor and compare with request size.

	Arguments:
		pAd Pointer to our adapter
		QueIdx		Selected TX Ring

	Return Value:
		NDIS_STATUS_FAILURE 	Not enough free descriptor
		NDIS_STATUS_SUCCESS 	Enough free descriptor

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/


/*
	Actually, this function used to check if the TxHardware Queue still has frame need to send.
	If no frame need to send, go to sleep, else, still wake up.
*/
int RTMPFreeTXDRequest(
	IN struct rtmp_adapter *pAd,
	IN u8 QueIdx,
	IN u8 NumberRequired,
	IN u8 *FreeNumberIs)
{
	int Status = NDIS_STATUS_FAILURE;
	HT_TX_CONTEXT *pHTTXContext;

	switch (QueIdx) {
	case QID_AC_BK:
	case QID_AC_BE:
	case QID_AC_VI:
	case QID_AC_VO:
	case QID_HCCA:
		{
			pHTTXContext = &pAd->TxContext[QueIdx];
			spin_lock_bh(&pAd->TxContextQueueLock[QueIdx]);
			if ((pHTTXContext->CurWritePosition !=
			     pHTTXContext->ENextBulkOutPosition)
			    || (pHTTXContext->IRPPending == true)) {
				Status = NDIS_STATUS_FAILURE;
			} else {
				Status = NDIS_STATUS_SUCCESS;
			}
			spin_unlock_bh(&pAd->TxContextQueueLock[QueIdx]);
		}
		break;

	case QID_MGMT:
		if (pAd->MgmtRing.TxSwFreeIdx != MGMT_RING_SIZE)
			Status = NDIS_STATUS_FAILURE;
		else
			Status = NDIS_STATUS_SUCCESS;
		break;

	default:
		DBGPRINT(RT_DEBUG_ERROR,
			 ("RTMPFreeTXDRequest::Invalid QueIdx(=%d)\n", QueIdx));
		break;
	}

	return (Status);

}


VOID RTMPSendNullFrame(
	IN struct rtmp_adapter *pAd,
	IN u8 TxRate,
	IN bool bQosNull,
	IN unsigned short PwrMgmt)
{
	u8 NullFrame[48];
	ULONG Length;
	PHEADER_802_11 pHeader_802_11;
	struct rtmp_wifi_dev *wdev = &pAd->StaCfg.wdev;

	/* WPA 802.1x secured port control */
	if (((wdev->AuthMode == Ndis802_11AuthModeWPA) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPAPSK) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPA2) ||
	     (wdev->AuthMode == Ndis802_11AuthModeWPA2PSK)
#ifdef WPA_SUPPLICANT_SUPPORT
	     || (pAd->StaCfg.wdev.IEEE8021X == true)
#endif
	    ) && (wdev->PortSecured == WPA_802_1X_PORT_NOT_SECURED)) {
		return;
	}

	memset(NullFrame, 0, 48);
	Length = sizeof (HEADER_802_11);

	pHeader_802_11 = (PHEADER_802_11) NullFrame;

	pHeader_802_11->FC.Type = FC_TYPE_DATA;
	pHeader_802_11->FC.SubType = SUBTYPE_DATA_NULL;
	pHeader_802_11->FC.ToDs = 1;
	COPY_MAC_ADDR(pHeader_802_11->Addr1, pAd->CommonCfg.Bssid);
	COPY_MAC_ADDR(pHeader_802_11->Addr2, pAd->CurrentAddress);
	COPY_MAC_ADDR(pHeader_802_11->Addr3, pAd->CommonCfg.Bssid);

	if (pAd->CommonCfg.bAPSDForcePowerSave) {
		pHeader_802_11->FC.PwrMgmt = PWR_SAVE;
	} else {
		bool FlgCanPmBitSet = true;


		if (FlgCanPmBitSet == true)
		pHeader_802_11->FC.PwrMgmt = PwrMgmt;
		else
			pHeader_802_11->FC.PwrMgmt = PWR_ACTIVE;
	}

	pHeader_802_11->Duration = pAd->CommonCfg.Dsifs + RTMPCalcDuration(pAd, TxRate, 14);

	/* sequence is increased in MlmeHardTx */
	pHeader_802_11->Sequence = pAd->Sequence;
	pAd->Sequence = (pAd->Sequence + 1) & MAXSEQ;	/* next sequence  */

	/* Prepare QosNull function frame */
	if (bQosNull) {
		pHeader_802_11->FC.SubType = SUBTYPE_QOS_NULL;

		/* copy QOS control bytes */
		NullFrame[Length] = 0;
		NullFrame[Length + 1] = 0;
		Length += 2;	/* if pad with 2 bytes for alignment, APSD will fail */
	}

	HAL_KickOutNullFrameTx(pAd, 0, NullFrame, Length);

}


/*
--------------------------------------------------------
FIND ENCRYPT KEY AND DECIDE CIPHER ALGORITHM
	Find the WPA key, either Group or Pairwise Key
	LEAP + TKIP also use WPA key.
--------------------------------------------------------
Decide WEP bit and cipher suite to be used. Same cipher suite should be used for whole fragment burst
	In Cisco CCX 2.0 Leap Authentication
	WepStatus is Ndis802_11WEPEnabled but the key will use PairwiseKey
	Instead of the SharedKey, SharedKey Length may be Zero.
*/
VOID STAFindCipherAlgorithm(struct rtmp_adapter *pAd, TX_BLK *pTxBlk)
{
	NDIS_802_11_ENCRYPTION_STATUS Cipher;	/* To indicate cipher used for this packet */
	u8 CipherAlg = CIPHER_NONE;	/* cipher alogrithm */
	u8 KeyIdx = 0xff;
	u8 *pSrcBufVA;
	PCIPHER_KEY pKey = NULL;
	PMAC_TABLE_ENTRY pMacEntry;
	struct rtmp_wifi_dev *wdev = &pAd->StaCfg.wdev;

	pSrcBufVA = pTxBlk->pPacket->data;
	pMacEntry = pTxBlk->pMacEntry;

	{
		/* Select Cipher */
		if ((*pSrcBufVA & 0x01) && (ADHOC_ON(pAd)))
			Cipher = pAd->StaCfg.GroupCipher;	/* Cipher for Multicast or Broadcast */
		else
			Cipher = pAd->StaCfg.PairCipher;	/* Cipher for Unicast */

		if (RTMP_GET_PACKET_EAPOL(pTxBlk->pPacket)) {
			ASSERT(pAd->SharedKey[BSS0][0].CipherAlg <= CIPHER_CKIP128);

			/* 4-way handshaking frame must be clear */
			if (!(TX_BLK_TEST_FLAG(pTxBlk, fTX_bClearEAPFrame)) &&
			    (pAd->SharedKey[BSS0][0].CipherAlg) &&
			    (pAd->SharedKey[BSS0][0].KeyLen)) {
				CipherAlg = pAd->SharedKey[BSS0][0].CipherAlg;
				KeyIdx = 0;
			}
		} else if (Cipher == Ndis802_11WEPEnabled) {
			KeyIdx = wdev->DefaultKeyId;
		} else if ((Cipher == Ndis802_11TKIPEnable) ||
			   (Cipher == Ndis802_11AESEnable)) {
			if ((*pSrcBufVA & 0x01) && (ADHOC_ON(pAd)))	/* multicast */
				KeyIdx = wdev->DefaultKeyId;
			else if (pAd->SharedKey[BSS0][0].KeyLen)
				KeyIdx = 0;
			else
				KeyIdx = wdev->DefaultKeyId;
		}

		if (KeyIdx == 0xff)
			CipherAlg = CIPHER_NONE;
		else if ((Cipher == Ndis802_11EncryptionDisabled)
			 || (pAd->SharedKey[BSS0][KeyIdx].KeyLen == 0))
			CipherAlg = CIPHER_NONE;
#ifdef WPA_SUPPLICANT_SUPPORT
		else if (pAd->StaCfg.wpa_supplicant_info.WpaSupplicantUP &&
			 (Cipher == Ndis802_11WEPEnabled) &&
			 (wdev->IEEE8021X == true) &&
			 (wdev->PortSecured == WPA_802_1X_PORT_NOT_SECURED))
			CipherAlg = CIPHER_NONE;
#endif /* WPA_SUPPLICANT_SUPPORT */
		else {
			CipherAlg = pAd->SharedKey[BSS0][KeyIdx].CipherAlg;
			pKey = &pAd->SharedKey[BSS0][KeyIdx];
		}
	}

	pTxBlk->CipherAlg = CipherAlg;
	pTxBlk->pKey = pKey;
	pTxBlk->KeyIdx = KeyIdx;
}




VOID STABuildCommon802_11Header(struct rtmp_adapter *pAd, TX_BLK *pTxBlk)
{
	HEADER_802_11 *wifi_hdr;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	/* MAKE A COMMON 802.11 HEADER */

	/* normal wlan header size : 24 octets */
	pTxBlk->MpduHeaderLen = sizeof (HEADER_802_11);
	wifi_hdr = (HEADER_802_11 *)&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
	memset(wifi_hdr, 0, sizeof (HEADER_802_11));

	wifi_hdr->FC.FrDs = 0;
	wifi_hdr->FC.Type = FC_TYPE_DATA;
	wifi_hdr->FC.SubType = ((TX_BLK_TEST_FLAG(pTxBlk, fTX_bWMM)) ? SUBTYPE_QDATA : SUBTYPE_DATA);


	if (pTxBlk->pMacEntry) {
		if (TX_BLK_TEST_FLAG(pTxBlk, fTX_bForceNonQoS)) {
			wifi_hdr->Sequence = pTxBlk->pMacEntry->NonQosDataSeq;
			pTxBlk->pMacEntry->NonQosDataSeq = (pTxBlk->pMacEntry->NonQosDataSeq + 1) & MAXSEQ;
		} else {
			wifi_hdr->Sequence = pTxBlk->pMacEntry->TxSeq[pTxBlk->UserPriority];
			pTxBlk->pMacEntry->TxSeq[pTxBlk->UserPriority] = (pTxBlk->pMacEntry->TxSeq[pTxBlk->UserPriority] + 1) & MAXSEQ;
		}
	} else {
		wifi_hdr->Sequence = pAd->Sequence;
		pAd->Sequence = (pAd->Sequence + 1) & MAXSEQ;	/* next sequence  */
	}

	wifi_hdr->Frag = 0;

	wifi_hdr->FC.MoreData = TX_BLK_TEST_FLAG(pTxBlk, fTX_bMoreData);

	{
		if (pAd->StaCfg.BssType == BSS_INFRA) {
			{
				COPY_MAC_ADDR(wifi_hdr->Addr1, pAd->CommonCfg.Bssid);
				COPY_MAC_ADDR(wifi_hdr->Addr2, pAd->CurrentAddress);
				COPY_MAC_ADDR(wifi_hdr->Addr3, pTxBlk->pSrcBufHeader);
				wifi_hdr->FC.ToDs = 1;
			}
		}
		else if (ADHOC_ON(pAd))
		{
			COPY_MAC_ADDR(wifi_hdr->Addr1, pTxBlk->pSrcBufHeader);
				COPY_MAC_ADDR(wifi_hdr->Addr2, pAd->CurrentAddress);
			COPY_MAC_ADDR(wifi_hdr->Addr3, pAd->CommonCfg.Bssid);
			wifi_hdr->FC.ToDs = 0;
		}
	}

	if (pTxBlk->CipherAlg != CIPHER_NONE)
		wifi_hdr->FC.Wep = 1;

	/*
	   -----------------------------------------------------------------
	   STEP 2. MAKE A COMMON 802.11 HEADER SHARED BY ENTIRE FRAGMENT BURST. Fill sequence later.
	   -----------------------------------------------------------------
	 */
	if (pAd->CommonCfg.bAPSDForcePowerSave)
		wifi_hdr->FC.PwrMgmt = PWR_SAVE;
	else
		wifi_hdr->FC.PwrMgmt = (RtmpPktPmBitCheck(pAd) == true);

}


VOID STABuildCache802_11Header(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk,
	IN u8 *pHeader)
{
	MAC_TABLE_ENTRY *pMacEntry;
	PHEADER_802_11 pHeader80211;

	pHeader80211 = (PHEADER_802_11) pHeader;
	pMacEntry = pTxBlk->pMacEntry;

	/* Update the cached 802.11 HEADER */

	/* normal wlan header size : 24 octets */
	pTxBlk->MpduHeaderLen = sizeof (HEADER_802_11);

	/* More Bit */
	pHeader80211->FC.MoreData = TX_BLK_TEST_FLAG(pTxBlk, fTX_bMoreData);

	/* Sequence */
	pHeader80211->Sequence = pMacEntry->TxSeq[pTxBlk->UserPriority];
	pMacEntry->TxSeq[pTxBlk->UserPriority] =
	    (pMacEntry->TxSeq[pTxBlk->UserPriority] + 1) & MAXSEQ;

	{
		/* Check if the frame can be sent through DLS direct link interface
		   If packet can be sent through DLS, then force aggregation disable. (Hard to determine peer STA's capability) */

		/* The addr3 of normal packet send from DS is Dest Mac address. */
		if (ADHOC_ON(pAd))
			COPY_MAC_ADDR(pHeader80211->Addr3,
				      pAd->CommonCfg.Bssid);
		else {
			COPY_MAC_ADDR(pHeader80211->Addr3,
				      pTxBlk->pSrcBufHeader);
		}
	}

	/*
	   -----------------------------------------------------------------
	   STEP 2. MAKE A COMMON 802.11 HEADER SHARED BY ENTIRE FRAGMENT BURST. Fill sequence later.
	   -----------------------------------------------------------------
	 */
	if (pAd->CommonCfg.bAPSDForcePowerSave)
		pHeader80211->FC.PwrMgmt = PWR_SAVE;
	else
		pHeader80211->FC.PwrMgmt = (RtmpPktPmBitCheck(pAd) == true);
}

static inline u8 *STA_Build_ARalink_Frame_Header(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk)
{
	u8 *pHeaderBufPtr;
	HEADER_802_11 *pHeader_802_11;
	struct sk_buff *pNextPacket;
	uint32_t nextBufLen;
	PQUEUE_ENTRY pQEntry;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	STAFindCipherAlgorithm(pAd, pTxBlk);
	STABuildCommon802_11Header(pAd, pTxBlk);

	pHeaderBufPtr = &pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
	pHeader_802_11 = (HEADER_802_11 *) pHeaderBufPtr;

	/* steal "order" bit to mark "aggregation" */
	pHeader_802_11->FC.Order = 1;

	/* skip common header */
	pHeaderBufPtr += pTxBlk->MpduHeaderLen;

	if (TX_BLK_TEST_FLAG(pTxBlk, fTX_bWMM)) {
		/* build QOS Control bytes */
		*pHeaderBufPtr = (pTxBlk->UserPriority & 0x0F);


		*(pHeaderBufPtr + 1) = 0;
		pHeaderBufPtr += 2;
		pTxBlk->MpduHeaderLen += 2;
	}

	/* padding at front of LLC header. LLC header should at 4-bytes aligment. */
	pTxBlk->HdrPadLen = (ULONG) pHeaderBufPtr;
	pHeaderBufPtr = (u8 *) ROUND_UP(pHeaderBufPtr, 4);
	pTxBlk->HdrPadLen = (ULONG) (pHeaderBufPtr - pTxBlk->HdrPadLen);

	/* For RA Aggregation, */
	/* put the 2nd MSDU length(extra 2-byte field) after QOS_CONTROL in little endian format */
	pQEntry = pTxBlk->TxPacketList.Head;
	pNextPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
	nextBufLen = pNextPacket->len;
	if (RTMP_GET_PACKET_VLAN(pNextPacket))
		nextBufLen -= LENGTH_802_1Q;

	*pHeaderBufPtr = (u8) nextBufLen & 0xff;
	*(pHeaderBufPtr + 1) = (u8) (nextBufLen >> 8);

	pHeaderBufPtr += 2;
	pTxBlk->MpduHeaderLen += 2;

	return pHeaderBufPtr;

}


static inline u8 *STA_Build_AMSDU_Frame_Header(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk)
{
	u8 *pHeaderBufPtr;
	HEADER_802_11 *pHeader_802_11;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	STAFindCipherAlgorithm(pAd, pTxBlk);
	STABuildCommon802_11Header(pAd, pTxBlk);

	pHeaderBufPtr = &pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
	pHeader_802_11 = (HEADER_802_11 *) pHeaderBufPtr;

	/* skip common header */
	pHeaderBufPtr += pTxBlk->MpduHeaderLen;

	/* build QOS Control bytes */
	*pHeaderBufPtr =
	    (pTxBlk->UserPriority & 0x0F) | (pAd->CommonCfg.
					     AckPolicy[pTxBlk->QueIdx] << 5);


	/* A-MSDU packet */
	*pHeaderBufPtr |= 0x80;

	*(pHeaderBufPtr + 1) = 0;
	pHeaderBufPtr += 2;
	pTxBlk->MpduHeaderLen += 2;

	/*
	   padding at front of LLC header
	   LLC header should locate at 4-octets aligment

	   @@@ MpduHeaderLen excluding padding @@@
	 */
	pTxBlk->HdrPadLen = (ULONG) pHeaderBufPtr;
	pHeaderBufPtr = (u8 *) ROUND_UP(pHeaderBufPtr, 4);
	pTxBlk->HdrPadLen = (ULONG) (pHeaderBufPtr - pTxBlk->HdrPadLen);

	return pHeaderBufPtr;

}


VOID STA_AMPDU_Frame_Tx(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk)
{
	HEADER_802_11 *pHeader_802_11;
	u8 *pHeaderBufPtr;
	unsigned short FreeNumber = 0;
	MAC_TABLE_ENTRY *pMacEntry;
	bool bVLANPkt;
	PQUEUE_ENTRY pQEntry;
	bool			bHTCPlus;
	UINT8 TXWISize = pAd->chipCap.TXWISize;


	ASSERT(pTxBlk);

	while (pTxBlk->TxPacketList.Head) {
		pQEntry = RemoveHeadQueue(&pTxBlk->TxPacketList);
		pTxBlk->pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
		if (RTMP_FillTxBlkInfo(pAd, pTxBlk) != true) {
			dev_kfree_skb_any(pTxBlk->pPacket);
			continue;
		}

		bVLANPkt = (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket) ? true : false);

		pMacEntry = pTxBlk->pMacEntry;
		if ((pMacEntry->isCached)
			&& (pMacEntry->TxSndgType == SNDG_TYPE_DISABLE)
		)
		{
			/* NOTE: Please make sure the size of pMacEntry->CachedBuf[] is smaller than pTxBlk->HeaderBuf[]!!!! */
			pTxBlk->HeaderBuf = (u8 *) (pMacEntry->HeaderBuf);

			pHeaderBufPtr = (u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize]);
			STABuildCache802_11Header(pAd, pTxBlk, pHeaderBufPtr);

		} else {
			STAFindCipherAlgorithm(pAd, pTxBlk);
			STABuildCommon802_11Header(pAd, pTxBlk);

			pHeaderBufPtr = &pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
		}

		if (pMacEntry->isCached
		    && (pMacEntry->Protocol ==
			RTMP_GET_PACKET_PROTOCOL(pTxBlk->pPacket))
			&& (pMacEntry->TxSndgType == SNDG_TYPE_DISABLE)
			)
		{
			pHeader_802_11 = (HEADER_802_11 *) pHeaderBufPtr;

			/* skip common header */
			pHeaderBufPtr += pTxBlk->MpduHeaderLen;

			/* build QOS Control bytes */
			*pHeaderBufPtr = (pTxBlk->UserPriority & 0x0F);
			pTxBlk->MpduHeaderLen = pMacEntry->MpduHeaderLen;
			pHeaderBufPtr = ((u8 *) pHeader_802_11) + pTxBlk->MpduHeaderLen;

			pTxBlk->HdrPadLen = pMacEntry->HdrPadLen;

			/* skip 802.3 header */
			pTxBlk->pSrcBufData =
			    pTxBlk->pSrcBufHeader + LENGTH_802_3;
			pTxBlk->SrcBufLen -= LENGTH_802_3;

			/* skip vlan tag */
			if (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket)) {
				pTxBlk->pSrcBufData += LENGTH_802_1Q;
				pTxBlk->SrcBufLen -= LENGTH_802_1Q;
			}
		}
		else
		{
			pHeader_802_11 = (HEADER_802_11 *) pHeaderBufPtr;

			/* skip common header */
			pHeaderBufPtr += pTxBlk->MpduHeaderLen;

			/*
			   build QOS Control bytes
			 */
			*pHeaderBufPtr = (pTxBlk->UserPriority & 0x0F);
			*(pHeaderBufPtr + 1) = 0;
			pHeaderBufPtr += 2;
			pTxBlk->MpduHeaderLen += 2;

			/*
			   build HTC+
			   HTC control field following QoS field
			 */
			bHTCPlus = false;

			if ((pAd->CommonCfg.bRdg == true)
			    && CLIENT_STATUS_TEST_FLAG(pTxBlk->pMacEntry, fCLIENT_STATUS_RDG_CAPABLE)
				&& (pMacEntry->TxSndgType != SNDG_TYPE_NDP)
			)
			{
				if (pMacEntry->isCached == false)
				{
					/* mark HTC bit */
					pHeader_802_11->FC.Order = 1;

					memset(pHeaderBufPtr, 0, sizeof(HT_CONTROL));
					((PHT_CONTROL)pHeaderBufPtr)->RDG = 1;
				}

				bHTCPlus = true;
			}

			pTxBlk->TxSndgPkt = SNDG_TYPE_DISABLE;

			spin_lock_bh(&pMacEntry->TxSndgLock);
			if (pMacEntry->TxSndgType >= SNDG_TYPE_SOUNDING)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("--Sounding in AMPDU: TxSndgType=%d, MCS=%d\n",
								pMacEntry->TxSndgType,
								pMacEntry->TxSndgType==SNDG_TYPE_NDP? pMacEntry->sndgMcs: pTxBlk->pTransmit->field.MCS));

				// Set HTC bit
				if (bHTCPlus == false)
				{
					bHTCPlus = true;
					memset(pHeaderBufPtr, 0, sizeof(HT_CONTROL));
				}

				if (pMacEntry->TxSndgType == SNDG_TYPE_SOUNDING)
				{
					// Select compress if supported. Otherwise select noncompress
					if (pAd->CommonCfg.ETxBfNoncompress==0 &&
						(pMacEntry->HTCapability.TxBFCap.ExpComBF>0) )
						((PHT_CONTROL)pHeaderBufPtr)->CSISTEERING = 3;
					else
						((PHT_CONTROL)pHeaderBufPtr)->CSISTEERING = 2;

				}
				else if (pMacEntry->TxSndgType == SNDG_TYPE_NDP)
				{
					// Select compress if supported. Otherwise select noncompress
					if (pAd->CommonCfg.ETxBfNoncompress==0 &&
						(pMacEntry->HTCapability.TxBFCap.ExpComBF>0) &&
						(pMacEntry->HTCapability.TxBFCap.ComSteerBFAntSup >= (pMacEntry->sndgMcs/8))
						)
						((PHT_CONTROL)pHeaderBufPtr)->CSISTEERING = 3;
					else
						((PHT_CONTROL)pHeaderBufPtr)->CSISTEERING = 2;

					// Set NDP Announcement
					((PHT_CONTROL)pHeaderBufPtr)->NDPAnnounce = 1;

					pTxBlk->TxNDPSndgBW = pMacEntry->sndgBW;
					pTxBlk->TxNDPSndgMcs = pMacEntry->sndgMcs;
				}

				pTxBlk->TxSndgPkt = pMacEntry->TxSndgType;
				pMacEntry->TxSndgType = SNDG_TYPE_DISABLE;
			}

			spin_unlock_bh(&pMacEntry->TxSndgLock);


			if (bHTCPlus)
			{
				pHeader_802_11->FC.Order = 1;
				pHeaderBufPtr += 4;
				pTxBlk->MpduHeaderLen += 4;
			}

			/* pTxBlk->MpduHeaderLen = pHeaderBufPtr - pTxBlk->HeaderBuf - TXWI_SIZE - MT_DMA_HDR_LEN; */
			ASSERT(pTxBlk->MpduHeaderLen >= 24);

			/* skip 802.3 header */
			pTxBlk->pSrcBufData = pTxBlk->pSrcBufHeader + LENGTH_802_3;
			pTxBlk->SrcBufLen -= LENGTH_802_3;

			/* skip vlan tag */
			if (bVLANPkt) {
				pTxBlk->pSrcBufData += LENGTH_802_1Q;
				pTxBlk->SrcBufLen -= LENGTH_802_1Q;
			}

			/*
			   padding at front of LLC header
			   LLC header should locate at 4-octets aligment

			   @@@ MpduHeaderLen excluding padding @@@
			 */
			pTxBlk->HdrPadLen = (ULONG) pHeaderBufPtr;
			pHeaderBufPtr = (u8 *) ROUND_UP(pHeaderBufPtr, 4);
			pTxBlk->HdrPadLen = (ULONG) (pHeaderBufPtr - pTxBlk->HdrPadLen);

			pMacEntry->HdrPadLen = pTxBlk->HdrPadLen;

			{

				/*
				   Insert LLC-SNAP encapsulation - 8 octets
				 */
				EXTRA_LLCSNAP_ENCAP_FROM_PKT_OFFSET(pTxBlk->pSrcBufData - 2,
								    pTxBlk->pExtraLlcSnapEncap);
				if (pTxBlk->pExtraLlcSnapEncap) {
					memmove(pHeaderBufPtr,
						       pTxBlk->pExtraLlcSnapEncap, 6);
					pHeaderBufPtr += 6;
					/* get 2 octets (TypeofLen) */
					memmove(pHeaderBufPtr,
						       pTxBlk->pSrcBufData - 2,
						       2);
					pHeaderBufPtr += 2;
					pTxBlk->MpduHeaderLen += LENGTH_802_1_H;
				}

			}

			pMacEntry->Protocol =
			    RTMP_GET_PACKET_PROTOCOL(pTxBlk->pPacket);
			pMacEntry->MpduHeaderLen = pTxBlk->MpduHeaderLen;
		}

		if ((pMacEntry->isCached)
			&& (pTxBlk->TxSndgPkt == SNDG_TYPE_DISABLE)
		)
		{
			RTMPWriteTxWI_Cache(pAd, (struct mt7612u_txwi *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);
		} else {
			RTMPWriteTxWI_Data(pAd, (struct mt7612u_txwi *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);

			memset((u8 *) (&pMacEntry->CachedBuf[0]), 0, sizeof (pMacEntry->CachedBuf));
			memmove((u8 *) (&pMacEntry->CachedBuf[0]), (u8 *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), (pHeaderBufPtr -(u8 *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN])));

			/* use space to get performance enhancement */
			memset((u8 *) (&pMacEntry->HeaderBuf[0]), 0, sizeof (pMacEntry->HeaderBuf));
			memmove((u8 *) (&pMacEntry->HeaderBuf[0]),
				       (u8 *) (&pTxBlk->HeaderBuf[0]),
				       (pHeaderBufPtr - (u8 *) (&pTxBlk->HeaderBuf[0])));

			pMacEntry->isCached = true;
		}

		if (pTxBlk->TxSndgPkt != SNDG_TYPE_DISABLE)
			pMacEntry->isCached = false;

#ifdef STATS_COUNT_SUPPORT
		/* calculate Transmitted AMPDU count and ByteCount  */
		{
			pAd->RalinkCounters.TransmittedMPDUsInAMPDUCount.u.LowPart++;
			pAd->RalinkCounters.TransmittedOctetsInAMPDUCount.QuadPart += pTxBlk->SrcBufLen;
		}
#endif /* STATS_COUNT_SUPPORT */

		RtmpUSB_WriteSingleTxResource(pAd, pTxBlk, true, &FreeNumber);

#ifdef DBG_CTRL_SUPPORT
#ifdef INCLUDE_DEBUG_QUEUE
		if (pAd->CommonCfg.DebugFlags & DBF_DBQ_TXFRAME)
			dbQueueEnqueueTxFrame((u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), (u8 *)pHeader_802_11);
#endif /* INCLUDE_DEBUG_QUEUE */
#endif /* DBG_CTRL_SUPPORT */

		/* Kick out Tx */
			HAL_KickOutTx(pAd, pTxBlk, pTxBlk->QueIdx);

		pAd->RalinkCounters.KickTxCount++;
		pAd->RalinkCounters.OneSecTxDoneCount++;
	}
}




VOID STA_AMSDU_Frame_Tx(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk)
{
	u8 *pHeaderBufPtr;
	unsigned short FreeNumber = 0;
	unsigned short subFramePayloadLen = 0;	/* AMSDU Subframe length without AMSDU-Header / Padding */
	unsigned short totalMPDUSize = 0;
	u8 *subFrameHeader;
	u8 padding = 0;
	unsigned short FirstTx = 0, LastTxIdx = 0;
	bool bVLANPkt;
	int frameNum = 0;
	PQUEUE_ENTRY pQEntry;


	ASSERT(pTxBlk);

	ASSERT((pTxBlk->TxPacketList.Number > 1));

	while (pTxBlk->TxPacketList.Head) {
		pQEntry = RemoveHeadQueue(&pTxBlk->TxPacketList);
		pTxBlk->pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
		if (RTMP_FillTxBlkInfo(pAd, pTxBlk) != true) {
			dev_kfree_skb_any(pTxBlk->pPacket);
			continue;
		}

		bVLANPkt =
		    (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket) ? true : false);

		/* skip 802.3 header */
		pTxBlk->pSrcBufData = pTxBlk->pSrcBufHeader + LENGTH_802_3;
		pTxBlk->SrcBufLen -= LENGTH_802_3;

		/* skip vlan tag */
		if (bVLANPkt) {
			pTxBlk->pSrcBufData += LENGTH_802_1Q;
			pTxBlk->SrcBufLen -= LENGTH_802_1Q;
		}

		if (frameNum == 0) {
			pHeaderBufPtr = STA_Build_AMSDU_Frame_Header(pAd, pTxBlk);

			/* NOTE: TxWI->TxWIMPDUByteCnt will be updated after final frame was handled. */
			RTMPWriteTxWI_Data(pAd, (struct mt7612u_txwi *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);
		} else {
			pHeaderBufPtr = &pTxBlk->HeaderBuf[0];
			padding = ROUND_UP(AMSDU_SUBHEAD_LEN + subFramePayloadLen, 4) -
								(AMSDU_SUBHEAD_LEN + subFramePayloadLen);
			memset(pHeaderBufPtr, 0, padding + AMSDU_SUBHEAD_LEN);
			pHeaderBufPtr += padding;
			pTxBlk->MpduHeaderLen = padding;
		}

		/*
		   A-MSDU subframe
		   DA(6)+SA(6)+Length(2) + LLC/SNAP Encap
		 */
		subFrameHeader = pHeaderBufPtr;
		subFramePayloadLen = pTxBlk->SrcBufLen;

		memmove(subFrameHeader, pTxBlk->pSrcBufHeader, 12);


		pHeaderBufPtr += AMSDU_SUBHEAD_LEN;
		pTxBlk->MpduHeaderLen += AMSDU_SUBHEAD_LEN;

		/* Insert LLC-SNAP encapsulation - 8 octets */
		EXTRA_LLCSNAP_ENCAP_FROM_PKT_OFFSET(pTxBlk->pSrcBufData - 2,
						    pTxBlk->pExtraLlcSnapEncap);

		subFramePayloadLen = pTxBlk->SrcBufLen;

		if (pTxBlk->pExtraLlcSnapEncap) {
			memmove(pHeaderBufPtr,
				       pTxBlk->pExtraLlcSnapEncap, 6);
			pHeaderBufPtr += 6;
			/* get 2 octets (TypeofLen) */
			memmove(pHeaderBufPtr, pTxBlk->pSrcBufData - 2,
				       2);
			pHeaderBufPtr += 2;
			pTxBlk->MpduHeaderLen += LENGTH_802_1_H;
			subFramePayloadLen += LENGTH_802_1_H;
		}

		/* update subFrame Length field */
		subFrameHeader[12] = (subFramePayloadLen & 0xFF00) >> 8;
		subFrameHeader[13] = subFramePayloadLen & 0xFF;

		totalMPDUSize += pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;

		if (frameNum == 0)
			FirstTx =
			    RtmpUSB_WriteMultiTxResource(pAd, pTxBlk, frameNum,
						     &FreeNumber);
		else
			LastTxIdx =
			    RtmpUSB_WriteMultiTxResource(pAd, pTxBlk, frameNum,
						     &FreeNumber);

#ifdef DBG_CTRL_SUPPORT
#ifdef INCLUDE_DEBUG_QUEUE
		if (pAd->CommonCfg.DebugFlags & DBF_DBQ_TXFRAME)
			dbQueueEnqueueTxFrame((u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), NULL);
#endif /* INCLUDE_DEBUG_QUEUE */
#endif /* DBG_CTRL_SUPPORT */

		frameNum++;

		pAd->RalinkCounters.KickTxCount++;
		pAd->RalinkCounters.OneSecTxDoneCount++;

		/* calculate Transmitted AMSDU Count and ByteCount */
		{
			pAd->RalinkCounters.TransmittedAMSDUCount.u.LowPart++;
			pAd->RalinkCounters.TransmittedOctetsInAMSDU.QuadPart +=
			    totalMPDUSize;
		}

	}

	RtmpUSB_FinalWriteTxResource(pAd, pTxBlk, totalMPDUSize, FirstTx);
	HAL_LastTxIdx(pAd, pTxBlk->QueIdx, LastTxIdx);

	/* Kick out Tx */
		HAL_KickOutTx(pAd, pTxBlk, pTxBlk->QueIdx);
}

VOID STA_Legacy_Frame_Tx(struct rtmp_adapter *pAd, TX_BLK *pTxBlk)
{
	HEADER_802_11 *wifi_hdr;
	u8 *pHeaderBufPtr;
	unsigned short FreeNumber = 0;
	bool bVLANPkt;
	PQUEUE_ENTRY pQEntry;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	ASSERT(pTxBlk);

	pQEntry = RemoveHeadQueue(&pTxBlk->TxPacketList);
	pTxBlk->pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
	if (RTMP_FillTxBlkInfo(pAd, pTxBlk) != true) {
		dev_kfree_skb_any(pTxBlk->pPacket);
		return;
	}
#ifdef STATS_COUNT_SUPPORT
	if (pTxBlk->TxFrameType == TX_MCAST_FRAME) {
		INC_COUNTER64(pAd->WlanCounters.MulticastTransmittedFrameCount);
	}
#endif /* STATS_COUNT_SUPPORT */

	if (pTxBlk->TxRate < pAd->CommonCfg.MinTxRate)
		pTxBlk->TxRate = pAd->CommonCfg.MinTxRate;

	STAFindCipherAlgorithm(pAd, pTxBlk);
	STABuildCommon802_11Header(pAd, pTxBlk);

	/* skip 802.3 header */
	pTxBlk->pSrcBufData = pTxBlk->pSrcBufHeader + LENGTH_802_3;
	pTxBlk->SrcBufLen -= LENGTH_802_3;

	/* skip vlan tag */
	bVLANPkt = (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket) ? true : false);
	if (bVLANPkt) {
		pTxBlk->pSrcBufData += LENGTH_802_1Q;
		pTxBlk->SrcBufLen -= LENGTH_802_1Q;
	}

	pHeaderBufPtr = &pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
	wifi_hdr = (HEADER_802_11 *) pHeaderBufPtr;

	/* skip common header */
	pHeaderBufPtr += pTxBlk->MpduHeaderLen;

	if (TX_BLK_TEST_FLAG(pTxBlk, fTX_bWMM)) {
		/* build QOS Control bytes */
		*(pHeaderBufPtr) =
		    ((pTxBlk->UserPriority & 0x0F) | (pAd->CommonCfg.AckPolicy[pTxBlk->QueIdx] << 5));
		*(pHeaderBufPtr + 1) = 0;
		pHeaderBufPtr += 2;
		pTxBlk->MpduHeaderLen += 2;
	}

	/* The remaining content of MPDU header should locate at 4-octets aligment */
	pTxBlk->HdrPadLen = (ULONG) pHeaderBufPtr;
	pHeaderBufPtr = (u8 *) ROUND_UP(pHeaderBufPtr, 4);
	pTxBlk->HdrPadLen = (ULONG) (pHeaderBufPtr - pTxBlk->HdrPadLen);

	{

		/*
		   Insert LLC-SNAP encapsulation - 8 octets

		   if original Ethernet frame contains no LLC/SNAP,
		   then an extra LLC/SNAP encap is required
		 */
		EXTRA_LLCSNAP_ENCAP_FROM_PKT_START(pTxBlk->pSrcBufHeader,
						   pTxBlk->pExtraLlcSnapEncap);
		if (pTxBlk->pExtraLlcSnapEncap) {
			u8 vlan_size;

			memmove(pHeaderBufPtr, pTxBlk->pExtraLlcSnapEncap, 6);
			pHeaderBufPtr += 6;
			/* skip vlan tag */
			vlan_size = (bVLANPkt) ? LENGTH_802_1Q : 0;
			/* get 2 octets (TypeofLen) */
			memmove(pHeaderBufPtr,
				       pTxBlk->pSrcBufHeader + 12 + vlan_size,
				       2);
			pHeaderBufPtr += 2;
			pTxBlk->MpduHeaderLen += LENGTH_802_1_H;
		}

	}


	/*
	   prepare for TXWI
	   use Wcid as Key Index
	 */

	RTMPWriteTxWI_Data(pAd, (struct mt7612u_txwi *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);
	RtmpUSB_WriteSingleTxResource(pAd, pTxBlk, true, &FreeNumber);

#ifdef DBG_CTRL_SUPPORT
#ifdef INCLUDE_DEBUG_QUEUE
	if (pAd->CommonCfg.DebugFlags & DBF_DBQ_TXFRAME)
		dbQueueEnqueueTxFrame((u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), (u8 *)wifi_hdr);
#endif /* INCLUDE_DEBUG_QUEUE */
#endif /* DBG_CTRL_SUPPORT */

	pAd->RalinkCounters.KickTxCount++;
	pAd->RalinkCounters.OneSecTxDoneCount++;

	/*
	   Kick out Tx
	 */
		HAL_KickOutTx(pAd, pTxBlk, pTxBlk->QueIdx);
}



VOID STA_ARalink_Frame_Tx(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK * pTxBlk)
{
	u8 *pHeaderBufPtr;
	unsigned short freeCnt = 0;
	unsigned short totalMPDUSize = 0;
	unsigned short FirstTx, LastTxIdx;
	int frameNum = 0;
	bool bVLANPkt;
	PQUEUE_ENTRY pQEntry;

	ASSERT(pTxBlk);

	ASSERT((pTxBlk->TxPacketList.Number == 2));

	FirstTx = LastTxIdx = 0;	/* Is it ok init they as 0? */
	while (pTxBlk->TxPacketList.Head) {
		pQEntry = RemoveHeadQueue(&pTxBlk->TxPacketList);
		pTxBlk->pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);

		if (RTMP_FillTxBlkInfo(pAd, pTxBlk) != true) {
			dev_kfree_skb_any(pTxBlk->pPacket);
			continue;
		}

		bVLANPkt =
		    (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket) ? true : false);

		/* skip 802.3 header */
		pTxBlk->pSrcBufData = pTxBlk->pSrcBufHeader + LENGTH_802_3;
		pTxBlk->SrcBufLen -= LENGTH_802_3;

		/* skip vlan tag */
		if (bVLANPkt) {
			pTxBlk->pSrcBufData += LENGTH_802_1Q;
			pTxBlk->SrcBufLen -= LENGTH_802_1Q;
		}

		if (frameNum == 0) {	/* For first frame, we need to create the 802.11 header + padding(optional) + RA-AGG-LEN + SNAP Header */

			pHeaderBufPtr =
			    STA_Build_ARalink_Frame_Header(pAd, pTxBlk);

			/*
			   It's ok write the TxWI here, because the TxWI->TxWIMPDUByteCnt
			   will be updated after final frame was handled.
			 */
			RTMPWriteTxWI_Data(pAd, (struct mt7612u_txwi *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);


			/*
			   Insert LLC-SNAP encapsulation - 8 octets
			 */
			EXTRA_LLCSNAP_ENCAP_FROM_PKT_OFFSET(pTxBlk->pSrcBufData - 2,
							    pTxBlk->pExtraLlcSnapEncap);

			if (pTxBlk->pExtraLlcSnapEncap) {
				memmove(pHeaderBufPtr,
					       pTxBlk->pExtraLlcSnapEncap, 6);
				pHeaderBufPtr += 6;
				/* get 2 octets (TypeofLen) */
				memmove(pHeaderBufPtr, pTxBlk->pSrcBufData - 2, 2);
				pHeaderBufPtr += 2;
				pTxBlk->MpduHeaderLen += LENGTH_802_1_H;
			}
		} else {	/* For second aggregated frame, we need create the 802.3 header to headerBuf, because PCI will copy it to SDPtr0. */

			pHeaderBufPtr = &pTxBlk->HeaderBuf[0];
			pTxBlk->MpduHeaderLen = 0;

			/*
			   A-Ralink sub-sequent frame header is the same as 802.3 header.
			   DA(6)+SA(6)+FrameType(2)
			 */
			memmove(pHeaderBufPtr, pTxBlk->pSrcBufHeader,
				       12);
			pHeaderBufPtr += 12;
			/* get 2 octets (TypeofLen) */
			memmove(pHeaderBufPtr, pTxBlk->pSrcBufData - 2,
				       2);
			pHeaderBufPtr += 2;
			pTxBlk->MpduHeaderLen = ARALINK_SUBHEAD_LEN;
		}

		totalMPDUSize += pTxBlk->MpduHeaderLen + pTxBlk->SrcBufLen;

		/* FreeNumber = GET_TXRING_FREENO(pAd, QueIdx); */
		if (frameNum == 0)
			FirstTx =
			    RtmpUSB_WriteMultiTxResource(pAd, pTxBlk, frameNum,
						     &freeCnt);
		else
			LastTxIdx =
			    RtmpUSB_WriteMultiTxResource(pAd, pTxBlk, frameNum,
						     &freeCnt);

#ifdef DBG_CTRL_SUPPORT
#ifdef INCLUDE_DEBUG_QUEUE
		if (pAd->CommonCfg.DebugFlags & DBF_DBQ_TXFRAME)
			dbQueueEnqueueTxFrame((u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), NULL);
#endif /* INCLUDE_DEBUG_QUEUE */
#endif /* DBG_CTRL_SUPPORT */

		frameNum++;

		pAd->RalinkCounters.OneSecTxAggregationCount++;
		pAd->RalinkCounters.KickTxCount++;
		pAd->RalinkCounters.OneSecTxDoneCount++;
	}

	RtmpUSB_FinalWriteTxResource(pAd, pTxBlk, totalMPDUSize, FirstTx);
	HAL_LastTxIdx(pAd, pTxBlk->QueIdx, LastTxIdx);

	/*
	   Kick out Tx
	 */
		HAL_KickOutTx(pAd, pTxBlk, pTxBlk->QueIdx);

}


VOID STA_Fragment_Frame_Tx(
	IN struct rtmp_adapter *pAd,
	IN TX_BLK *pTxBlk)
{
	HEADER_802_11 *pHeader_802_11;
	u8 *pHeaderBufPtr;
	unsigned short freeCnt = 0;
	u8 fragNum = 0;
	PACKET_INFO PacketInfo;
	unsigned short EncryptionOverhead = 0;
	uint32_t FreeMpduSize, SrcRemainingBytes;
	unsigned short AckDuration;
	UINT NextMpduSize;
	bool bVLANPkt;
	PQUEUE_ENTRY pQEntry;
	HTTRANSMIT_SETTING *pTransmit;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	ASSERT(pTxBlk);

	pQEntry = RemoveHeadQueue(&pTxBlk->TxPacketList);
	pTxBlk->pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
	if (RTMP_FillTxBlkInfo(pAd, pTxBlk) != true) {
		dev_kfree_skb_any(pTxBlk->pPacket);
		return;
	}

	ASSERT(TX_BLK_TEST_FLAG(pTxBlk, fTX_bAllowFrag));
	bVLANPkt = (RTMP_GET_PACKET_VLAN(pTxBlk->pPacket) ? true : false);

	STAFindCipherAlgorithm(pAd, pTxBlk);
	STABuildCommon802_11Header(pAd, pTxBlk);

	if (pTxBlk->CipherAlg == CIPHER_TKIP) {
		pTxBlk->pPacket =
		    duplicate_pkt_with_TKIP_MIC(pAd, pTxBlk->pPacket);
		if (pTxBlk->pPacket == NULL)
			return;
		RTMP_QueryPacketInfo(pTxBlk->pPacket, &PacketInfo,
				     &pTxBlk->pSrcBufHeader,
				     &pTxBlk->SrcBufLen);
	}

	/* skip 802.3 header */
	pTxBlk->pSrcBufData = pTxBlk->pSrcBufHeader + LENGTH_802_3;
	pTxBlk->SrcBufLen -= LENGTH_802_3;

	/* skip vlan tag */
	if (bVLANPkt) {
		pTxBlk->pSrcBufData += LENGTH_802_1Q;
		pTxBlk->SrcBufLen -= LENGTH_802_1Q;
	}

	pHeaderBufPtr = &pTxBlk->HeaderBuf[MT_DMA_HDR_LEN + TXWISize];
	pHeader_802_11 = (HEADER_802_11 *) pHeaderBufPtr;

	/* skip common header */
	pHeaderBufPtr += pTxBlk->MpduHeaderLen;

	if (TX_BLK_TEST_FLAG(pTxBlk, fTX_bWMM)) {
		/*
		   build QOS Control bytes
		 */
		*pHeaderBufPtr = (pTxBlk->UserPriority & 0x0F);


		*(pHeaderBufPtr + 1) = 0;
		pHeaderBufPtr += 2;
		pTxBlk->MpduHeaderLen += 2;
	}

	/*
	   padding at front of LLC header
	   LLC header should locate at 4-octets aligment
	 */
	pTxBlk->HdrPadLen = (ULONG) pHeaderBufPtr;
	pHeaderBufPtr = (u8 *) ROUND_UP(pHeaderBufPtr, 4);
	pTxBlk->HdrPadLen = (ULONG) (pHeaderBufPtr - pTxBlk->HdrPadLen);

	{


		/*
		   Insert LLC-SNAP encapsulation - 8 octets

		   if original Ethernet frame contains no LLC/SNAP,
		   then an extra LLC/SNAP encap is required
		 */
		EXTRA_LLCSNAP_ENCAP_FROM_PKT_START(pTxBlk->pSrcBufHeader,
						   pTxBlk->pExtraLlcSnapEncap);
		if (pTxBlk->pExtraLlcSnapEncap) {
			u8 vlan_size;

			memmove(pHeaderBufPtr,
				       pTxBlk->pExtraLlcSnapEncap, 6);
			pHeaderBufPtr += 6;
			/* skip vlan tag */
			vlan_size = (bVLANPkt) ? LENGTH_802_1Q : 0;
			/* get 2 octets (TypeofLen) */
			memmove(pHeaderBufPtr,
				       pTxBlk->pSrcBufHeader + 12 + vlan_size,
				       2);
			pHeaderBufPtr += 2;
			pTxBlk->MpduHeaderLen += LENGTH_802_1_H;
		}
	}

	/*
	   If TKIP is used and fragmentation is required. Driver has to
	   append TKIP MIC at tail of the scatter buffer
	   MAC ASIC will only perform IV/EIV/ICV insertion but no TKIP MIC
	 */
	if (pTxBlk->CipherAlg == CIPHER_TKIP) {
		RTMPCalculateMICValue(pAd, pTxBlk->pPacket,
				      pTxBlk->pExtraLlcSnapEncap, pTxBlk->pKey,
				      0);

		/*
		   NOTE: DON'T refer the skb->len directly after following copy. Becasue the length is not adjust
		   to correct lenght, refer to pTxBlk->SrcBufLen for the packet length in following progress.
		 */
		memmove(pTxBlk->pSrcBufData + pTxBlk->SrcBufLen,
			       &pAd->PrivateInfo.Tx.MIC[0], 8);
		pTxBlk->SrcBufLen += 8;
		pTxBlk->TotalFrameLen += 8;
	}

	/*
	   calcuate the overhead bytes that encryption algorithm may add. This
	   affects the calculate of "duration" field
	 */
	if ((pTxBlk->CipherAlg == CIPHER_WEP64)
	    || (pTxBlk->CipherAlg == CIPHER_WEP128))
		EncryptionOverhead = 8;	/* WEP: IV[4] + ICV[4]; */
	else if (pTxBlk->CipherAlg == CIPHER_TKIP)
		EncryptionOverhead = 12;	/* TKIP: IV[4] + EIV[4] + ICV[4], MIC will be added to TotalPacketLength */
	else if (pTxBlk->CipherAlg == CIPHER_AES)
		EncryptionOverhead = 16;	/* AES: IV[4] + EIV[4] + MIC[8] */
	else
		EncryptionOverhead = 0;

	pTransmit = pTxBlk->pTransmit;
	/* Decide the TX rate */
	if (pTransmit->field.MODE == MODE_CCK)
		pTxBlk->TxRate = pTransmit->field.MCS;
	else if (pTransmit->field.MODE == MODE_OFDM)
		pTxBlk->TxRate = pTransmit->field.MCS + RATE_FIRST_OFDM_RATE;
	else
		pTxBlk->TxRate = RATE_6_5;

	/* decide how much time an ACK/CTS frame will consume in the air */
	if (pTxBlk->TxRate <= RATE_LAST_OFDM_RATE)
		AckDuration =
		    RTMPCalcDuration(pAd,
				     pAd->CommonCfg.ExpectedACKRate[pTxBlk->TxRate],
				     14);
	else
		AckDuration = RTMPCalcDuration(pAd, RATE_6_5, 14);

	/* Init the total payload length of this frame. */
	SrcRemainingBytes = pTxBlk->SrcBufLen;

	pTxBlk->TotalFragNum = 0xff;

	do {
		FreeMpduSize = pAd->CommonCfg.FragmentThreshold - LENGTH_CRC - pTxBlk->MpduHeaderLen;
		if (SrcRemainingBytes <= FreeMpduSize) {	/* this is the last or only fragment */

			pTxBlk->SrcBufLen = SrcRemainingBytes;

			pHeader_802_11->FC.MoreFrag = 0;
			pHeader_802_11->Duration =
			    pAd->CommonCfg.Dsifs + AckDuration;

			/* Indicate the lower layer that this's the last fragment. */
			pTxBlk->TotalFragNum = fragNum;
		} else {	/* more fragment is required */

			pTxBlk->SrcBufLen = FreeMpduSize;

			NextMpduSize =
			    min(((UINT) SrcRemainingBytes - pTxBlk->SrcBufLen),
				((UINT) pAd->CommonCfg.FragmentThreshold));
			pHeader_802_11->FC.MoreFrag = 1;
			pHeader_802_11->Duration =
			    (3 * pAd->CommonCfg.Dsifs) + (2 * AckDuration) +
			    RTMPCalcDuration(pAd, pTxBlk->TxRate,
					     NextMpduSize + EncryptionOverhead);
		}

		SrcRemainingBytes -= pTxBlk->SrcBufLen;

		if (fragNum == 0)
			pTxBlk->FrameGap = IFS_HTTXOP;
		else
			pTxBlk->FrameGap = IFS_SIFS;

		RTMPWriteTxWI_Data(pAd, (struct mt7612u_txwi *) (&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), pTxBlk);
		RtmpUSB_WriteFragTxResource(pAd, pTxBlk, fragNum, &freeCnt);

#ifdef DBG_CTRL_SUPPORT
#ifdef INCLUDE_DEBUG_QUEUE
		if (pAd->CommonCfg.DebugFlags & DBF_DBQ_TXFRAME)
			dbQueueEnqueueTxFrame((u8 *)(&pTxBlk->HeaderBuf[MT_DMA_HDR_LEN]), (u8 *)pHeader_802_11);
#endif /* INCLUDE_DEBUG_QUEUE */
#endif /* DBG_CTRL_SUPPORT */

		pAd->RalinkCounters.KickTxCount++;
		pAd->RalinkCounters.OneSecTxDoneCount++;

		/* Update the frame number, remaining size of the NDIS packet payload. */
		{
			/* space for 802.11 header. */
			if (fragNum == 0 && pTxBlk->pExtraLlcSnapEncap)
				pTxBlk->MpduHeaderLen -= LENGTH_802_1_H;
		}

		fragNum++;
		/* SrcRemainingBytes -= pTxBlk->SrcBufLen; */
		pTxBlk->pSrcBufData += pTxBlk->SrcBufLen;

		pHeader_802_11->Frag++;	/* increase Frag # */

	} while (SrcRemainingBytes > 0);

	/*
	   Kick out Tx
	 */
		HAL_KickOutTx(pAd, pTxBlk, pTxBlk->QueIdx);
}


#define RELEASE_FRAMES_OF_TXBLK(_pAd, _pTxBlk, _pQEntry, _Status) 										\
		while(_pTxBlk->TxPacketList.Head)														\
		{																						\
			_pQEntry = RemoveHeadQueue(&_pTxBlk->TxPacketList);									\
			dev_kfree_skb_any(QUEUE_ENTRY_TO_PACKET(_pQEntry));	\
		}


VOID STA_NDPA_Frame_Tx(struct rtmp_adapter *pAd, TX_BLK *pTxBlk)
{
	u8 *buf;
	VHT_NDPA_FRAME *vht_ndpa;
	struct rtmp_wifi_dev *wdev;
	UINT frm_len, sta_cnt;
	SNDING_STA_INFO *sta_info;
	MAC_TABLE_ENTRY *pMacEntry;

	pTxBlk->Wcid = RTMP_GET_PACKET_WCID(pTxBlk->pPacket);
	pTxBlk->pMacEntry = &pAd->MacTab.Content[pTxBlk->Wcid];
	pMacEntry = pTxBlk->pMacEntry;

	if (pMacEntry)
	{
		wdev = pMacEntry->wdev;

		buf = kmalloc(MGMT_DMA_BUFFER_SIZE, GFP_ATOMIC);
		if (buf == NULL)
			return;

		memset(buf, 0, MGMT_DMA_BUFFER_SIZE);

		vht_ndpa = (VHT_NDPA_FRAME *)buf;
		frm_len = sizeof(VHT_NDPA_FRAME);
		vht_ndpa->fc.Type = FC_TYPE_CNTL;
		vht_ndpa->fc.SubType = SUBTYPE_VHT_NDPA;
		COPY_MAC_ADDR(vht_ndpa->ra, pMacEntry->Addr);
		COPY_MAC_ADDR(vht_ndpa->ta, wdev->if_addr);

		/* Currnetly we only support 1 STA for a VHT DNPA */
		sta_info = vht_ndpa->sta_info;
		sta_info->aid12 = 0;
		sta_info->fb_type = SNDING_FB_SU;
		sta_info->nc_idx = 0;
		vht_ndpa->token.token_num = pMacEntry->snd_dialog_token;
		frm_len += sizeof(SNDING_STA_INFO);

		if (frm_len >= (MGMT_DMA_BUFFER_SIZE - sizeof(SNDING_STA_INFO))) {
			DBGPRINT(RT_DEBUG_ERROR, ("%s(): len(%d) too large!cnt=%d\n",
						__FUNCTION__, frm_len, sta_cnt));
		}

		if (pMacEntry->snd_dialog_token & 0xc0)
			pMacEntry->snd_dialog_token = 0;
		else
			pMacEntry->snd_dialog_token++;

		vht_ndpa->duration = 100;

		//DBGPRINT(RT_DEBUG_OFF, ("Send VHT NDPA Frame to STA(%02x:%02x:%02x:%02x:%02x:%02x)\n",
		//						PRINT_MAC(pMacEntry->Addr)));
		//hex_dump("VHT NDPA Frame", buf, frm_len);

		// NDPA's BW needs to sync with Tx BW
		pAd->CommonCfg.MlmeTransmit.field.BW = pMacEntry->HTPhyMode.field.BW;

		pTxBlk->Flags = false; // No Acq Request

		MiniportMMRequest(pAd, 0, buf, frm_len);
		kfree(buf);
	}

	pMacEntry->TxSndgType = SNDG_TYPE_DISABLE;
}


/*
	========================================================================

	Routine Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware encryption before really
	sent out to air.

	Arguments:
		pAd 	Pointer to our adapter
		struct sk_buff *Pointer to outgoing Ndis frame
		NumberOfFrag	Number of fragment required

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
int STAHardTransmit(struct rtmp_adapter *pAd, TX_BLK *pTxBlk, u8 QueIdx)
{
	struct sk_buff *pPacket;
	PQUEUE_ENTRY pQEntry;


	/*
	   ---------------------------------------------
	   STEP 0. DO SANITY CHECK AND SOME EARLY PREPARATION.
	   ---------------------------------------------
	 */
	ASSERT(pTxBlk->TxPacketList.Number);
	if (pTxBlk->TxPacketList.Head == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("pTxBlk->TotalFrameNum == %d!\n",
			  pTxBlk->TxPacketList.Number));
		return NDIS_STATUS_FAILURE;
	}

	pPacket = QUEUE_ENTRY_TO_PACKET(pTxBlk->TxPacketList.Head);

	/* there's packet to be sent, keep awake for 1200ms */
	if (pAd->CountDowntoPsm < 12)
		pAd->CountDowntoPsm = 12;

	/* ------------------------------------------------------------------
	   STEP 1. WAKE UP PHY
	   outgoing frame always wakeup PHY to prevent frame lost and
	   turn off PSM bit to improve performance
	   ------------------------------------------------------------------
	   not to change PSM bit, just send this frame out?
	 */
	if ((pAd->StaCfg.Psm == PWR_SAVE)
	    && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)) {
		DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicForceWakeup At HardTx\n"));
		RTEnqueueInternalCmd(pAd, CMDTHREAD_FORCE_WAKE_UP, NULL, 0);
	}

	/* It should not change PSM bit, when APSD turn on. */
	if ((!
	     (pAd->StaCfg.UapsdInfo.bAPSDCapable
	      && pAd->CommonCfg.APEdcaParm.bAPSDCapable)
	     && (pAd->CommonCfg.bAPSDForcePowerSave == false))
	    || (RTMP_GET_PACKET_EAPOL(pTxBlk->pPacket))
	    || (RTMP_GET_PACKET_WAI(pTxBlk->pPacket))) {
		if ((RtmpPktPmBitCheck(pAd) == true) &&
		    (pAd->StaCfg.WindowsPowerMode ==
		     Ndis802_11PowerModeFast_PSP))
			RTMP_SET_PSM_BIT(pAd, PWR_ACTIVE);
	}


	if ((pTxBlk->TxFrameType & TX_NDPA_FRAME) > 0)
	{
		u8 mlmeMCS, mlmeBW, mlmeMode;

		mlmeMCS  = pAd->CommonCfg.MlmeTransmit.field.MCS;
		mlmeBW   = pAd->CommonCfg.MlmeTransmit.field.BW;
		mlmeMode = pAd->CommonCfg.MlmeTransmit.field.MODE;

		pAd->NDPA_Request = true;

		STA_NDPA_Frame_Tx(pAd, pTxBlk);

		pAd->NDPA_Request = false;
		pTxBlk->TxFrameType &= ~TX_NDPA_FRAME;

		// Finish NDPA and then recover to mlme's own setting
		pAd->CommonCfg.MlmeTransmit.field.MCS  = mlmeMCS;
		pAd->CommonCfg.MlmeTransmit.field.BW   = mlmeBW;
		pAd->CommonCfg.MlmeTransmit.field.MODE = mlmeMode;
	}

	switch (pTxBlk->TxFrameType) {
	case TX_AMPDU_FRAME:
		STA_AMPDU_Frame_Tx(pAd, pTxBlk);

		break;
	case TX_AMSDU_FRAME:
		STA_AMSDU_Frame_Tx(pAd, pTxBlk);
		break;
	case TX_LEGACY_FRAME:
		{
			STA_Legacy_Frame_Tx(pAd, pTxBlk);
		break;
		}
	case TX_MCAST_FRAME:
		STA_Legacy_Frame_Tx(pAd, pTxBlk);
		break;
	case TX_RALINK_FRAME:
		STA_ARalink_Frame_Tx(pAd, pTxBlk);
		break;
	case TX_FRAG_FRAME:
		STA_Fragment_Frame_Tx(pAd, pTxBlk);
		break;
	default:
		{
			/* It should not happened! */
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Send a pacekt was not classified!! It should not happen!\n"));
			while (pTxBlk->TxPacketList.Number) {
				pQEntry =
				    RemoveHeadQueue(&pTxBlk->TxPacketList);
				pPacket = QUEUE_ENTRY_TO_PACKET(pQEntry);
				if (pPacket)
					dev_kfree_skb_any(pPacket);
			}
		}
		break;
	}

	return (NDIS_STATUS_SUCCESS);

}


VOID Sta_Announce_or_Forward_802_3_Packet(
	IN struct rtmp_adapter *pAd,
	IN struct sk_buff *pPacket,
	IN u8 FromWhichBSSID)
{
	if (true
	) {
		announce_802_3_packet(pAd, pPacket, OPMODE_STA);
	} else {
		/* release packet */
		dev_kfree_skb_any(pPacket);
	}
}

