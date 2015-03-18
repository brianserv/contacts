/*
 * setuserinfo_handler.cpp
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#include "setuserinfo_handler.h"
#include "../../common/common_datetime.h"
#include "../../common/common_api.h"
#include "../../frame/frame.h"
#include "../../frame/server_helper.h"
#include "../../frame/redissession_bank.h"
#include "../../logger/logger.h"
#include "../../include/cachekey_define.h"
#include "../../include/control_head.h"
#include "../../include/typedef.h"
#include "../config/msgdispatch_config.h"
#include "../config/string_config.h"
#include "../server_typedef.h"
#include "../bank/redis_bank.h"

using namespace LOGGER;
using namespace FRAME;

int32_t CSetUserInfoHandler::SetUserInfo(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize)
{
	ControlHead *pControlHead = dynamic_cast<ControlHead *>(pCtlHead);
	if(pControlHead == NULL)
	{
		return 0;
	}

	MsgHeadCS *pMsgHeadCS = dynamic_cast<MsgHeadCS *>(pMsgHead);
	if(pMsgHeadCS == NULL)
	{
		return 0;
	}

	CSetUserInfoReq *pSetUserInfoReq = dynamic_cast<CSetUserInfoReq *>(pMsgBody);
	if(pSetUserInfoReq == NULL)
	{
		return 0;
	}

	UserBaseInfo *pConfigUserBaseInfo = (UserBaseInfo *)g_Frame.GetConfig(USER_BASE_INFO);

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pUserBaseInfoChannel = pRedisBank->GetRedisChannel(pConfigUserBaseInfo->string);

	const char *arrArgv[CSetUserInfoReq::enmMaxUserInfoCount * 2];
	size_t arrArgvLen[CSetUserInfoReq::enmMaxUserInfoCount * 2];
	for(int32_t i = 0; i < pSetUserInfoReq->m_nCount; ++i)
	{
		arrArgv[2 * i] = pSetUserInfoReq->m_arrKey[i].c_str();
		arrArgv[2 * i + 1] = pSetUserInfoReq->m_arrValue[i].c_str();
		arrArgvLen[2 * i] = pSetUserInfoReq->m_arrKey[i].size();
		arrArgvLen[2 * i + 1] = pSetUserInfoReq->m_arrValue[i].size();
	}

	if(pSetUserInfoReq->m_nCount > 0)
	{
		pUserBaseInfoChannel->HMSet(NULL, itoa(pMsgHeadCS->m_nSrcUin), pSetUserInfoReq->m_nCount, arrArgv, arrArgvLen);

		UserBaseInfo *pConfigUserBaseInfo = (UserBaseInfo *)g_Frame.GetConfig(REGIST_PHONE_INFO);
		pUserBaseInfoChannel->HIncrBy(NULL, itoa(pMsgHeadCS->m_nSrcUin), "%s %d", pConfigUserBaseInfo->version, 1);
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_SETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pMsgHeadCS->m_nSeq;
	stMsgHeadCS.m_nSrcUin = pMsgHeadCS->m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pMsgHeadCS->m_nDstUin;

	CSetUserInfoResp stSetUserInfoResp;
	stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_OK;

	CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_SETUSERINFO_RESP));
	uint16_t nTotalSize = CServerHelper::MakeMsg(pCtlHead, &stMsgHeadCS, &stSetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->Publish(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(pCtlHead, &stMsgHeadCS, &stSetUserInfoResp, "send ");

	return 0;
}


