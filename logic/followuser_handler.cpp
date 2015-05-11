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

	if(pControlHead->m_nUin != pMsgHeadCS->m_nSrcUin)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pClientRespChannel = pRedisBank->GetRedisChannel(pControlHead->m_nGateID, CLIENT_RESP);

		return CServerHelper::KickUser(pControlHead, pMsgHeadCS, pClientRespChannel, KickReason_NotLogined);
	}

	CFollowUserReq *pFollowUserReq = dynamic_cast<CFollowUserReq *>(pMsgBody);
	if(pFollowUserReq == NULL)
	{
		return 0;
	}

	if(pFollowUserReq->m_nFollowType == CFollowUserReq::enmFollowTypw_Cancel)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(CLIENT_RESP);

		uint8_t arrRespBuf[MAX_MSG_SIZE];

		MsgHeadCS stMsgHeadCS;
		stMsgHeadCS.m_nMsgID = MSGID_FOLLOWUSER_RESP;
		stMsgHeadCS.m_nSeq = pMsgHeadCS->m_nSeq;
		stMsgHeadCS.m_nSrcUin = pMsgHeadCS->m_nSrcUin;
		stMsgHeadCS.m_nDstUin = pMsgHeadCS->m_nDstUin;

		CFollowUserResp stFollowUserResp;
		stFollowUserResp.m_nResult = CFollowUserResp::enmResult_OK;

		uint16_t nTotalSize = CServerHelper::MakeMsg(pCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(pCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(USER_FANS);
		pUserFansChannel->ZRem(NULL, itoa(pMsgHeadCS->m_nDstUin), "%u", pMsgHeadCS->m_nSrcUin);

		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(USER_FOLLOWERS);
		pUserFollowersChannel->ZRem(NULL, itoa(pMsgHeadCS->m_nSrcUin), "%u", pMsgHeadCS->m_nDstUin);

		CRedisChannel *pUserFriendsChannel = pRedisBank->GetRedisChannel(USER_FRIENDS);
		pUserFriendsChannel->ZAdd(NULL, itoa(pMsgHeadCS->m_nSrcUin), "%ld %u", pControlHead->m_nTimeStamp, pMsgHeadCS->m_nDstUin);
		pUserFriendsChannel->ZAdd(NULL, itoa(pMsgHeadCS->m_nDstUin), "%ld %u", pControlHead->m_nTimeStamp, pMsgHeadCS->m_nSrcUin);
	}
	else
	{
		UserBlackList *pConfigBlackList = (UserBlackList *)g_Frame.GetConfig(USER_BLACKLIST);

		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CFollowUserHandler::OnSessionExistInBlackList),
				static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
		pSessionData->m_stCtlHead = *pControlHead;
		pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
		pSessionData->m_stFollowUserReq = *pFollowUserReq;
		pSessionData->m_nMsgSize = pMsgHeadCS->m_nTotalSize;
		memcpy(pSessionData->m_arrMsg, pBuf + pControlHead->m_nHeadSize, nBufSize - pControlHead->m_nHeadSize);

		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pBlackListChannel = pRedisBank->GetRedisChannel(pConfigBlackList->string);
		pBlackListChannel->ZScore(pSession, itoa(pMsgHeadCS->m_nDstUin), pMsgHeadCS->m_nSrcUin);
	}

	return 0;
}

int32_t CFollowUserHandler::OnSessionExistInBlackList(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(CLIENT_RESP);
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
			stFollowUserResp.m_nResult = CFollowUserResp::enmResult_InBlackList;
			bCanFollow = false;
			break;
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
		//check if exist in target followers
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CFollowUserHandler::OnSessionExistInDstFollowList));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);
		pUserSession->m_bSendTimeoutResp = false;

		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(USER_FOLLOWERS);
		pUserFollowersChannel->ZScore(pRedisSession, itoa(pUserSession->m_stMsgHeadCS.m_nDstUin), pUserSession->m_stMsgHeadCS.m_nSrcUin);

		//unreadmsg
		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pQuoteSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CFollowUserHandler::OnSessionGetUserUnreadMsgCount),
				static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pQuoteSession->GetSessionData()) UserSession();
		*pSessionData = *pUserSession;

		char *szUin = itoa(pUserSession->m_stMsgHeadCS.m_nDstUin);

		CRedisChannel *pUnreadMsgChannel = pRedisBank->GetRedisChannel(USER_UNREADMSGLIST);
		pUnreadMsgChannel->Multi();
		pUnreadMsgChannel->ZAdd(NULL, szUin, "%ld %b", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_arrMsg, (size_t)pUserSession->m_nMsgSize);
		pUnreadMsgChannel->ZCount(NULL, szUin);
		pUnreadMsgChannel->Exec(pQuoteSession);

		//add to fans
		pUserFollowersChannel = pRedisBank->GetRedisChannel(USER_FOLLOWERS);
		pUserFollowersChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nSrcUin), "%ld %u", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nDstUin);

		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(USER_FANS);
		pUserFansChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nDstUin), "%ld %u", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nSrcUin);
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}

int32_t CFollowUserHandler::OnSessionExistInDstFollowList(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(CLIENT_RESP);
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_FOLLOWUSER_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	bool bCanBeFriend = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			bCanBeFriend = true;
			break;
		}
	}while(0);

	if(bCanBeFriend)
	{
		CRedisChannel *pUserFriendsChannel = pRedisBank->GetRedisChannel(USER_FRIENDS);
		pUserFriendsChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nSrcUin), "%ld %u", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserFriendsChannel->ZAdd(NULL, itoa(pUserSession->m_stMsgHeadCS.m_nDstUin), "%ld %u", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nSrcUin);
	}

	pRedisSessionBank->DestroySession(pRedisSession);

	return 0;
}

int32_t CFollowUserHandler::OnSessionGetUserUnreadMsgCount(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	bool bIsSyncNoti = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_ARRAY)
		{
			redisReply *pReplyElement = pRedisReply->element[1];
			if(pReplyElement->type == REDIS_REPLY_INTEGER)
			{
				if(pReplyElement->integer <= 1)
				{
					bIsSyncNoti = true;
					break;
				}
			}
		}
	}while(0);

	if(bIsSyncNoti)
	{
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CFollowUserHandler::OnSessionGetUserSessionInfo));

		UserSessionInfo *pUserSessionInfo = (UserSessionInfo *)g_Frame.GetConfig(USER_SESSIONINFO);
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pUserSessionChannel = pRedisBank->GetRedisChannel(USER_SESSIONINFO);
		pUserSessionChannel->HMGet(pRedisSession, itoa(pUserSession->m_stMsgHeadCS.m_nDstUin), "%s %s %s %s", pUserSessionInfo->clientaddress,
				pUserSessionInfo->clientport, pUserSessionInfo->sessionid, pUserSessionInfo->gateid);
	}
	else
	{
		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		pRedisSessionBank->DestroySession(pRedisSession);
	}

	return 0;
}

int32_t CFollowUserHandler::OnSessionGetUserSessionInfo(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	ControlHead stCtlHead;
	stCtlHead.m_nUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	bool bGetSessionSuccess = true;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			bGetSessionSuccess = false;
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_ARRAY)
		{
			redisReply *pReplyElement = pRedisReply->element[0];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nClientAddress = atoi(pReplyElement->str);
			}
			else
			{
				bGetSessionSuccess = false;
				break;
			}

			pReplyElement = pRedisReply->element[1];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nClientPort = atoi(pReplyElement->str);
			}
			else
			{
				bGetSessionSuccess = false;
				break;
			}

			pReplyElement = pRedisReply->element[2];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nSessionID = atoi(pReplyElement->str);
			}
			else
			{
				bGetSessionSuccess = false;
				break;
			}

			pReplyElement = pRedisReply->element[3];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nGateID = atoi(pReplyElement->str);
			}
			else
			{
				bGetSessionSuccess = false;
				break;
			}
		}
		else
		{
			bGetSessionSuccess = false;
			break;
		}
	}while(0);

	if(bGetSessionSuccess)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pPushClientChannel = pRedisBank->GetRedisChannel(stCtlHead.m_nGateID, CLIENT_RESP);

		CServerHelper::SendSyncNoti(pPushClientChannel, &stCtlHead, pUserSession->m_stMsgHeadCS.m_nDstUin);
	}

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	pRedisSessionBank->DestroySession(pRedisSession);

	return 0;
}

int32_t CFollowUserHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	if(pUserSession->m_bSendTimeoutResp)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(CLIENT_RESP);
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
	}

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}



