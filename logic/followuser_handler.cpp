/*
 * followuser_handler.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: jimm
 */

#include "followuser_handler.h"
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
		CRedisChannel *pClientRespChannel = pRedisBank->GetRedisChannel(pControlHead->m_nGateRedisAddress, pControlHead->m_nGateRedisPort);

		return CServerHelper::KickUser(pControlHead, pMsgHeadCS, pClientRespChannel, KickReason_NotLogined);
	}

	CFollowUserReq *pFollowUserReq = dynamic_cast<CFollowUserReq *>(pMsgBody);
	if(pFollowUserReq == NULL)
	{
		return 0;
	}

	if(pFollowUserReq->m_nFollowType == CFollowUserReq::enmFollowType_Cancel)
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);

		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(UserFans::servername, pMsgHeadCS->m_nDstUin);
		pUserFansChannel->ZRem(NULL, CServerHelper::MakeRedisKey(UserFans::keyname, pMsgHeadCS->m_nDstUin), "%u", pMsgHeadCS->m_nSrcUin);

		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(UserFollowers::servername, pMsgHeadCS->m_nSrcUin);
		pUserFollowersChannel->ZRem(NULL, CServerHelper::MakeRedisKey(UserFollowers::keyname, pMsgHeadCS->m_nSrcUin), "%u", pMsgHeadCS->m_nDstUin);

		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CFollowUserHandler::OnSessionExistInFriendList),
				static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
		pSessionData->m_stCtlHead = *pControlHead;
		pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
		pSessionData->m_stFollowUserReq = *pFollowUserReq;
		pSessionData->m_nMsgSize = pMsgHeadCS->m_nTotalSize;
		memcpy(pSessionData->m_arrMsg, pBuf + pControlHead->m_nHeadSize, nBufSize - pControlHead->m_nHeadSize);

		CRedisChannel *pUserFriendsChannel = pRedisBank->GetRedisChannel(UserFriends::servername, pMsgHeadCS->m_nSrcUin);
		pUserFriendsChannel->ZScore(pSession, CServerHelper::MakeRedisKey(UserFriends::keyname, pMsgHeadCS->m_nSrcUin), pMsgHeadCS->m_nDstUin);
	}
	else
	{
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
		CRedisChannel *pBlackListChannel = pRedisBank->GetRedisChannel(UserBlackList::servername, pMsgHeadCS->m_nDstUin);
		pBlackListChannel->ZScore(pSession, CServerHelper::MakeRedisKey(UserBlackList::keyname, pMsgHeadCS->m_nDstUin), pMsgHeadCS->m_nSrcUin);
	}

	return 0;
}

int32_t CFollowUserHandler::OnSessionExistInFriendList(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_FOLLOWUSER_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CFollowUserResp stFollowUserResp;
	stFollowUserResp.m_nResult = CFollowUserResp::enmResult_OK;

	bool bIsFriend = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stFollowUserResp.m_nResult = CFollowUserResp::enmResult_Unknown;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			bIsFriend = true;
			break;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_FOLLOWUSER_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(!bIsFriend)
	{
		stFollowUserResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stFollowUserResp.m_nResult);
	}
	else
	{
		CRedisChannel *pUserFriendsChannel = pRedisBank->GetRedisChannel(UserFriends::servername, pUserSession->m_stMsgHeadCS.m_nSrcUin);
		pUserFriendsChannel->ZRem(NULL, CServerHelper::MakeRedisKey(UserFriends::keyname, pUserSession->m_stMsgHeadCS.m_nSrcUin), "%u",
				pUserSession->m_stMsgHeadCS.m_nDstUin);

		pUserFriendsChannel = pRedisBank->GetRedisChannel(UserFriends::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserFriendsChannel->ZRem(NULL, CServerHelper::MakeRedisKey(UserFriends::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin), "%u",
				pUserSession->m_stMsgHeadCS.m_nSrcUin);
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stMsgHeadCS.m_nSrcUin), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
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
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
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

		CRedisChannel *pUserFollowersChannel = pRedisBank->GetRedisChannel(UserFollowers::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserFollowersChannel->ZScore(pRedisSession, CServerHelper::MakeRedisKey(UserFollowers::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin),
				pUserSession->m_stMsgHeadCS.m_nSrcUin);

		//unreadmsg
		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pUnreadMsgSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CFollowUserHandler::OnSessionGetUserUnreadMsgCount),
				static_cast<TimerProc>(&CFollowUserHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pUnreadMsgSession->GetSessionData()) UserSession();
		*pSessionData = *pUserSession;

		const char *szUin = CServerHelper::MakeRedisKey(UserUnreadMsgList::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin);

		CRedisChannel *pUnreadMsgChannel = pRedisBank->GetRedisChannel(UserUnreadMsgList::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUnreadMsgChannel->Multi();
		pUnreadMsgChannel->ZAdd(NULL, szUin, "%ld %b", pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_arrMsg, (size_t)pUserSession->m_nMsgSize);
		pUnreadMsgChannel->ZCount(NULL, szUin);
		pUnreadMsgChannel->Exec(pUnreadMsgSession);

		//add to fans
		pUserFollowersChannel = pRedisBank->GetRedisChannel(UserFollowers::servername, pUserSession->m_stMsgHeadCS.m_nSrcUin);
		pUserFollowersChannel->ZAdd(NULL, CServerHelper::MakeRedisKey(UserFollowers::keyname, pUserSession->m_stMsgHeadCS.m_nSrcUin), "%ld %u",
				pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nDstUin);

		CRedisChannel *pUserFansChannel = pRedisBank->GetRedisChannel(UserFans::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserFansChannel->ZAdd(NULL, CServerHelper::MakeRedisKey(UserFans::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin), "%ld %u",
				pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nSrcUin);
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");
	return 0;
}

int32_t CFollowUserHandler::OnSessionExistInDstFollowList(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
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
		CRedisChannel *pUserFriendsChannel = pRedisBank->GetRedisChannel(UserFriends::servername, pUserSession->m_stMsgHeadCS.m_nSrcUin);
		pUserFriendsChannel->ZAdd(NULL, CServerHelper::MakeRedisKey(UserFriends::keyname, pUserSession->m_stMsgHeadCS.m_nSrcUin), "%ld %u",
				pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nDstUin);

		pUserFriendsChannel = pRedisBank->GetRedisChannel(UserFriends::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserFriendsChannel->ZAdd(NULL, CServerHelper::MakeRedisKey(UserFriends::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin), "%ld %u",
				pUserSession->m_stCtlHead.m_nTimeStamp, pUserSession->m_stMsgHeadCS.m_nSrcUin);
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

		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pUserSessionChannel = pRedisBank->GetRedisChannel(UserSessionInfo::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserSessionChannel->HMGet(pRedisSession, CServerHelper::MakeRedisKey(UserSessionInfo::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin),
				"%s %s %s %s %s %s", UserSessionInfo::clientaddress, UserSessionInfo::clientport, UserSessionInfo::sessionid,
				UserSessionInfo::gateid, UserSessionInfo::gateredisaddress, UserSessionInfo::gateredisport);
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

			pReplyElement = pRedisReply->element[4];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nGateRedisAddress = atoi(pReplyElement->str);
			}
			else
			{
				bGetSessionSuccess = false;
				break;
			}

			pReplyElement = pRedisReply->element[5];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stCtlHead.m_nGateRedisPort = atoi(pReplyElement->str);
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
		CRedisChannel *pPushClientChannel = pRedisBank->GetRedisChannel(stCtlHead.m_nGateRedisAddress, stCtlHead.m_nGateRedisPort);

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
		CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
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
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stFollowUserResp, "send ");
	}

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}



