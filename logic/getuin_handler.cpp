/*
 * getuin_handler.h
 *
 *  Created on: May 6, 2015
 *      Author: jimm
 */

#ifndef LOGIC_GETUIN_HANDLER_CPP1_
#define LOGIC_GETUIN_HANDLER_CPP1_

#include "getuin_handler.h"
#include "common/common_datetime.h"
#include "common/common_api.h"
#include "frame/frame.h"
#include "frame/server_helper.h"
#include "frame/redissession_bank.h"
#include "frame/cachekey_define.h"
#include "logger/logger.h"
#include "include/control_head.h"
#include "include/typedef.h"
#include "include/sync_msg.h"
#include "config/string_config.h"
#include "server_typedef.h"
#include "bank/redis_bank.h"

using namespace LOGGER;
using namespace FRAME;

int32_t CGetUinHandler::GetUin(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize)
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

	CGetUinReq *pGetUinReq = dynamic_cast<CGetUinReq *>(pMsgBody);
	if(pGetUinReq == NULL)
	{
		return 0;
	}

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CGetUinHandler::OnSessionGetAccountName),
			static_cast<TimerProc>(&CGetUinHandler::OnRedisSessionTimeout));
	UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
	pSessionData->m_stCtlHead = *pControlHead;
	pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
	pSessionData->m_stGetUinReq = *pGetUinReq;

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pAccountIDChannel = pRedisBank->GetRedisChannel(AccountID::servername, pGetUinReq->m_strAccountID.c_str());
	pAccountIDChannel->Get(pSession, CServerHelper::MakeRedisKey(AccountID::keyname, pGetUinReq->m_strAccountID.c_str()));

	return 0;
}

int32_t CGetUinHandler::OnSessionGetAccountName(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUIN_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CGetUinResp stGetUinResp;
	stGetUinResp.m_nResult = CGetUinResp::enmResult_OK;

	bool bSuccess = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_Unknown;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			if(pRedisReply->type == REDIS_REPLY_STRING)
			{
				pUserSession->m_strAccountName = pRedisReply->str;
				bSuccess = true;
				break;
			}
		}
		else
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_USERNOTEXIST;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUIN_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(!bSuccess)
	{
		stGetUinResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUinResp.m_nResult);
		uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, "send ");

		pRedisSessionBank->DestroySession(pRedisSession);
	}
	else
	{
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUinHandler::OnSessionGetUserUin));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUinHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		CRedisChannel *pAccountInfoChannl = pRedisBank->GetRedisChannel(AccountInfo::servername, pUserSession->m_strAccountName.c_str());
		pAccountInfoChannl->HMGet(pRedisSession, CServerHelper::MakeRedisKey(AccountInfo::keyname, pUserSession->m_strAccountName.c_str()),
				"%s", AccountInfo::uin);
	}

	return 0;
}

int32_t CGetUinHandler::OnSessionGetUserUin(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUIN_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CGetUinResp stGetUinResp;
	stGetUinResp.m_nResult = CGetUinResp::enmResult_OK;

	bool bSuccess = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_Unknown;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			if(pRedisReply->type == REDIS_REPLY_ARRAY)
			{
				redisReply *pReplyElement = pRedisReply->element[0];
				pUserSession->m_nUin = atoi(pReplyElement->str);
				bSuccess = true;
				break;
			}
		}
		else
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_USERNOTEXIST;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUIN_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(!bSuccess)
	{
		stGetUinResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUinResp.m_nResult);
		uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, "send ");

		pRedisSessionBank->DestroySession(pRedisSession);
	}
	else
	{
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUinHandler::OnSessionGetVersion));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUinHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		CRedisChannel *pUserBaseInfoChannl = pRedisBank->GetRedisChannel(UserBaseInfo::servername, pUserSession->m_nUin);
		pUserBaseInfoChannl->HMGet(pRedisSession, CServerHelper::MakeRedisKey(UserBaseInfo::keyname, pUserSession->m_nUin), "%s",
				UserBaseInfo::version);
	}

	return 0;
}

int32_t CGetUinHandler::OnSessionGetVersion(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUIN_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CGetUinResp stGetUinResp;
	stGetUinResp.m_nResult = CGetUinResp::enmResult_OK;

	uint32_t nVersion = 0;
	bool bSuccess = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_Unknown;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			if(pRedisReply->type == REDIS_REPLY_ARRAY)
			{
				redisReply *pReplyElement = pRedisReply->element[0];
				nVersion = atoi(pReplyElement->str);
				bSuccess = true;
				break;
			}
		}
		else
		{
			stGetUinResp.m_nResult = CGetUinResp::enmResult_USERNOTEXIST;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUIN_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(!bSuccess)
	{
		stGetUinResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUinResp.m_nResult);
	}
	else
	{
		stGetUinResp.m_nUin = pUserSession->m_nUin;
		stGetUinResp.m_nVersion = nVersion;
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);

	return 0;
}

int32_t CGetUinHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUIN_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUIN_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	CGetUinResp stGetUinResp;
	stGetUinResp.m_nResult = CGetUinResp::enmResult_Unknown;
	stGetUinResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUinResp.m_nResult);

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUinResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}


#endif /* LOGIC_GETUIN_HANDLER_CPP1_ */
