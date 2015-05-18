/*
 * setuserinfo_handler.cpp
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#include "setuserinfo_handler.h"
#include "common/common_datetime.h"
#include "common/common_api.h"
#include "frame/frame.h"
#include "frame/server_helper.h"
#include "frame/redissession_bank.h"
#include "frame/cachekey_define.h"
#include "logger/logger.h"
#include "include/control_head.h"
#include "include/typedef.h"
#include "config/string_config.h"
#include "server_typedef.h"
#include "bank/redis_bank.h"

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

	if(pControlHead->m_nUin != pMsgHeadCS->m_nSrcUin)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pClientRespChannel = pRedisBank->GetRedisChannel(pControlHead->m_nGateRedisAddress, pControlHead->m_nGateRedisPort);

		return CServerHelper::KickUser(pControlHead, pMsgHeadCS, pClientRespChannel, KickReason_NotLogined);
	}

	CSetUserInfoReq *pSetUserInfoReq = dynamic_cast<CSetUserInfoReq *>(pMsgBody);
	if(pSetUserInfoReq == NULL)
	{
		return 0;
	}

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pUserBaseInfoChannel = pRedisBank->GetRedisChannel(UserBaseInfo::servername, pMsgHeadCS->m_nSrcUin);

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CSetUserInfoHandler::OnSessionSetUserBaseInfo),
			static_cast<TimerProc>(&CSetUserInfoHandler::OnRedisSessionTimeout));
	UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
	pSessionData->m_stCtlHead = *pControlHead;
	pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
	pSessionData->m_stSetUserInfoReq = *pSetUserInfoReq;

	pUserBaseInfoChannel->HIncrBy(pSession, CServerHelper::MakeRedisKey(UserBaseInfo::keyname, pMsgHeadCS->m_nSrcUin),
			UserBaseInfo::version, 1);

	return 0;
}

int32_t CSetUserInfoHandler::OnSessionSetUserBaseInfo(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_SETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CSetUserInfoResp stSetUserInfoResp;
	stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_OK;

	bool bIsReturn = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_Unknown;
			bIsReturn = true;
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_INTEGER)
		{
			stSetUserInfoResp.m_nVersion = pRedisReply->integer;
		}
		else
		{
			stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_OK;
			bIsReturn = true;
			break;
		}
	}while(0);

	for(int32_t i = 0; i < pUserSession->m_stSetUserInfoReq.m_nCount; ++i)
	{
		if(!UserBaseInfo::CanWrite(pUserSession->m_stSetUserInfoReq.m_arrKey[i]))
		{
			bIsReturn = true;
			stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_CanNotWrite;
			break;
		}
	}

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_SETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(bIsReturn)
	{
		stSetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stSetUserInfoResp.m_nResult);
	}
	else
	{
		const char *arrArgv[CSetUserInfoReq::enmMaxUserInfoCount * 2];
		size_t arrArgvLen[CSetUserInfoReq::enmMaxUserInfoCount * 2];
		for(int32_t i = 0; i < pUserSession->m_stSetUserInfoReq.m_nCount; ++i)
		{
			arrArgv[2 * i] = pUserSession->m_stSetUserInfoReq.m_arrKey[i].c_str();
			arrArgv[2 * i + 1] = pUserSession->m_stSetUserInfoReq.m_arrValue[i].c_str();
			arrArgvLen[2 * i] = pUserSession->m_stSetUserInfoReq.m_arrKey[i].size();
			arrArgvLen[2 * i + 1] = pUserSession->m_stSetUserInfoReq.m_arrValue[i].size();
		}

		if(pUserSession->m_stSetUserInfoReq.m_nCount > 0)
		{
			CRedisChannel *pUserBaseInfoChannel = pRedisBank->GetRedisChannel(UserBaseInfo::servername, pUserSession->m_stMsgHeadCS.m_nSrcUin);
			pUserBaseInfoChannel->HMSet(NULL, CServerHelper::MakeRedisKey(UserBaseInfo::keyname, pUserSession->m_stMsgHeadCS.m_nSrcUin),
					pUserSession->m_stSetUserInfoReq.m_nCount * 2, arrArgv, arrArgvLen);
		}
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stSetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stSetUserInfoResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}

int32_t CSetUserInfoHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_SETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_SETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	CSetUserInfoResp stSetUserInfoResp;
	stSetUserInfoResp.m_nResult = CSetUserInfoResp::enmResult_Unknown;
	stSetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stSetUserInfoResp.m_nResult);

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stSetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stSetUserInfoResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}


