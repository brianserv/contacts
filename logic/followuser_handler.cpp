/*
 * followuser_handler.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: jimm
 */

#include "followuser_handler.h"
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

int32_t CFollowUserHandler::FollowUser(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize)
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

	CFollowUserReq *pFollowUserReq = dynamic_cast<CFollowUserReq *>(pMsgBody);
	if(pFollowUserReq == NULL)
	{
		return 0;
	}

	if(pFollowUserReq->m_nFollowType == CFollowUserReq::enmFollowTypw_Cancel)
	{
		CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_FOLLOWUSER_RESP));

		uint8_t arrRespBuf[MAX_MSG_SIZE];

		MsgHeadCS stMsgHeadCS;
		stMsgHeadCS.m_nMsgID = MSGID_FOLLOWUSER_RESP;
		stMsgHeadCS.m_nSeq = pMsgHeadCS->m_nSeq;
		stMsgHeadCS.m_nSrcUin = pMsgHeadCS->m_nSrcUin;
		stMsgHeadCS.m_nDstUin = pMsgHeadCS->m_nDstUin;

		CFollowUserResp stFollowUserResp;
		stFollowUserResp.m_nResult = CFollowUserResp::enmResult_OK;

		uint16_t nTotalSize = CServerHelper::MakeMsg(pCtlHead, pMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(pCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

		UserFans *pConfigUserFans = (UserFans *)g_Frame.GetConfig(USER_FANS);
		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(pConfigUserFans->string);
		pUserFansChannel->ZRem(NULL, itoa(pMsgHeadCS->m_nDstUin), "%u", pMsgHeadCS->m_nSrcUin);

		UserFollowers *pConfigUserFollowers = (UserFollowers *)g_Frame.GetConfig(USER_FOLLOWERS);
		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(pConfigUserFollowers->string);
		pUserFollowersChannel->ZRem(NULL, itoa(pMsgHeadCS->m_nSrcUin), "%u", pMsgHeadCS->m_nDstUin);
	}
	else
	{
		UserBlackList *pConfigBlackList = (UserBlackList *)g_Frame.GetConfig(USER_BLACKLIST);

		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CFollowUserHandler::OnSessionGetBlackList),
				static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
		pSessionData->m_stCtlHead = *pControlHead;
		pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
		pSessionData->m_stFollowUserReq = *pFollowUserReq;

		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pBlackListChannel = pRedisBank->GetRedisChannel(pConfigBlackList->string);
		pBlackListChannel->HExists(pSession, itoa(pMsgHeadCS->m_nDstUin), "%u", pMsgHeadCS->m_nSrcUin);
	}


	return 0;
}

int32_t CFollowUserHandler::OnSessionGetBlackList(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_FOLLOWUSER_RESP));
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_FOLLOWUSER_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CFollowUserResp stFollowUserResp;
	stFollowUserResp.m_nResult = CFollowUserResp::enmResult_OK;

	bool bCanFollow = true;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stFollowUserResp.m_nResult = CFollowUserResp::enmResult_Unknown;
			bCanFollow = false;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			if(pRedisReply->type == REDIS_REPLY_INTEGER)
			{
				if(pRedisReply->integer != 0)
				{
					break;
				}
			}
		}
		else
		{
			stFollowUserResp.m_nResult = CFollowUserResp::enmResult_Unknown;
			bCanFollow = false;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_FOLLOWUSER_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(!bCanFollow)
	{
		stFollowUserResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stFollowUserResp.m_nResult);
	}
	else
	{
		UserFans *pConfigUserFans = (UserFans *)g_Frame.GetConfig(USER_FANS);
		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(pConfigUserFans->string);
		pUserFansChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nDstUin), "%u", pUserSession->m_stMsgHeadCS.m_nSrcUin);

		UserFollowers *pConfigUserFollowers = (UserFollowers *)g_Frame.GetConfig(USER_FOLLOWERS);
		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(pConfigUserFollowers->string);
		pUserFollowersChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nSrcUin), "%u", pUserSession->m_stMsgHeadCS.m_nDstUin);
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}

int32_t CFollowUserHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_FOLLOWUSER_RESP));
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_FOLLOWUSER_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_FOLLOWUSER_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	CFollowUserResp stFollowUserResp;
	stFollowUserResp.m_nResult = CFollowUserResp::enmResult_Unknown;
	stFollowUserResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stFollowUserResp.m_nResult);

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}



