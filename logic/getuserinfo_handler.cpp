/*
 * getuserinfo_handler.cpp
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#include "getuserinfo_handler.h"
#include "common/common_datetime.h"
#include "common/common_api.h"
#include "frame/frame.h"
#include "frame/server_helper.h"
#include "frame/redissession_bank.h"
#include "frame/cachekey_define.h"
#include "logger/logger.h"
#include "include/control_head.h"
#include "include/sys_msg.h"
#include "include/typedef.h"
#include "config/string_config.h"
#include "server_typedef.h"
#include "bank/redis_bank.h"

using namespace LOGGER;
using namespace FRAME;

int32_t CGetUserInfoHandler::GetUserInfo(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize)
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

	if((pControlHead->m_nUin == 0) || (pControlHead->m_nUin != pMsgHeadCS->m_nSrcUin))
	{
		CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
		CRedisChannel *pClientRespChannel = pRedisBank->GetRedisChannel(pControlHead->m_nGateRedisAddress, pControlHead->m_nGateRedisPort);

		return CServerHelper::KickUser(pControlHead, pMsgHeadCS, pClientRespChannel, KickReason_NotLogined);
	}

	CGetUserInfoReq *pGetUserInfoReq = dynamic_cast<CGetUserInfoReq *>(pMsgBody);
	if(pGetUserInfoReq == NULL)
	{
		return 0;
	}

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	if(pMsgHeadCS->m_nSrcUin == pMsgHeadCS->m_nDstUin)
	{
		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserBaseInfo),
				static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
		pSessionData->m_stCtlHead = *pControlHead;
		pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
		pSessionData->m_stGetUserInfoReq = *pGetUserInfoReq;

		CRedisChannel *pUserBaseInfoChannel = pRedisBank->GetRedisChannel(UserBaseInfo::servername, pMsgHeadCS->m_nDstUin);
		pUserBaseInfoChannel->HMGet(pSession, CServerHelper::MakeRedisKey(UserBaseInfo::keyname, pMsgHeadCS->m_nDstUin),
				"%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
				UserBaseInfo::version, UserBaseInfo::uin, UserBaseInfo::accountid, UserBaseInfo::nickname,
				UserBaseInfo::headimage, UserBaseInfo::oneselfwords, UserBaseInfo::gender,
				UserBaseInfo::school, UserBaseInfo::hometown, UserBaseInfo::birthday,
				UserBaseInfo::age, UserBaseInfo::liveplace, UserBaseInfo::height, UserBaseInfo::weight,
				UserBaseInfo::job, UserBaseInfo::createtopics_count, UserBaseInfo::jointopics_count,
				UserBaseInfo::photowall, UserBaseInfo::createtime, UserBaseInfo::followbusline_count,
				UserBaseInfo::radar);
	}
	else
	{
		CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
		RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionIsFollow),
				static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout));
		UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
		pSessionData->m_stCtlHead = *pControlHead;
		pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
		pSessionData->m_stGetUserInfoReq = *pGetUserInfoReq;

		CRedisChannel *pRedisChannel = pRedisBank->GetRedisChannel(UserFollowers::servername, pMsgHeadCS->m_nSrcUin);
		pRedisChannel->ZScore(pSession, CServerHelper::MakeRedisKey(UserFollowers::keyname, pMsgHeadCS->m_nSrcUin), pMsgHeadCS->m_nDstUin);
	}

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionIsFollow(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t nRespResult = CGetUserInfoResp::enmResult_OK;
	bool bIsReturn = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			nRespResult = CGetUserInfoResp::enmResult_Unknown;
			bIsReturn = true;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			pUserSession->m_nIsFollow = 1;
		}
		else
		{
			pUserSession->m_nIsFollow = 0;
		}
	}while(false);

	uint8_t arrRespBuf[MAX_MSG_SIZE];
	if(bIsReturn)
	{
		MsgHeadCS stMsgHeadCS;
		stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
		stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
		stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
		stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

		CGetUserInfoResp stGetUserInfoResp;
		stGetUserInfoResp.m_nResult = nRespResult;
		stGetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUserInfoResp.m_nResult);

		uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

		pRedisSessionBank->DestroySession(pRedisSession);
	}
	else
	{
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionExistInBlackList));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		CRedisChannel *pUserBlackListChannel = pRedisBank->GetRedisChannel(UserBlackList::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserBlackListChannel->ZScore(pRedisSession, CServerHelper::MakeRedisKey(UserBlackList::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin),
				pUserSession->m_stMsgHeadCS.m_nSrcUin);
	}

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionExistInBlackList(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	CGetUserInfoResp stGetUserInfoResp;
	stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_OK;

	bool bIsInBlack = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
			bIsInBlack = true;
			break;
		}

		if(pRedisReply->type != REDIS_REPLY_NIL)
		{
			stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_LessPrivilege;
			bIsInBlack = true;
			break;
		}
	}while(0);

	if(bIsInBlack)
	{
		MsgHeadCS stMsgHeadCS;
		stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
		stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
		stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
		stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

		stGetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUserInfoResp.m_nResult);
		stGetUserInfoResp.m_nIsFollow = pUserSession->m_nIsFollow;

		uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

		pRedisSessionBank->DestroySession(pRedisSession);
	}
	else
	{
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserBaseInfo));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		CRedisChannel *pUserBaseInfoChannel = pRedisBank->GetRedisChannel(UserBaseInfo::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUserBaseInfoChannel->HMGet(pRedisSession, CServerHelper::MakeRedisKey(UserBaseInfo::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin),
				"%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
				UserBaseInfo::version, UserBaseInfo::uin, UserBaseInfo::accountid, UserBaseInfo::nickname,
				UserBaseInfo::headimage, UserBaseInfo::oneselfwords, UserBaseInfo::gender,
				UserBaseInfo::school, UserBaseInfo::hometown, UserBaseInfo::birthday,
				UserBaseInfo::age, UserBaseInfo::liveplace, UserBaseInfo::height, UserBaseInfo::weight,
				UserBaseInfo::job, UserBaseInfo::createtopics_count, UserBaseInfo::jointopics_count,
				UserBaseInfo::photowall, UserBaseInfo::createtime, UserBaseInfo::followbusline_count,
				UserBaseInfo::radar);
	}

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionGetUserBaseInfo(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	CGetUserInfoResp stGetUserInfoResp;
	stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_OK;
	stGetUserInfoResp.m_nIsFollow = pUserSession->m_nIsFollow;

	bool bIsFailed = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
			bIsFailed = true;
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_ARRAY)
		{
			int32_t nIndex = 0;
			redisReply *pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nVersion = atoi(pReplyElement->str);
			}
			else
			{
				stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
				bIsFailed = true;
				break;
			}

			if(stGetUserInfoResp.m_nVersion == pUserSession->m_stGetUserInfoReq.m_nVersion)
			{
				stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_DontNeedUpdate;
				break;
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nUin = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strAccountID = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strNickName = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strHeadImage = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strOneselfWords = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nGender = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strSchool = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strHometown = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strBirthday = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nAge = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strLivePlace = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strHeight = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strWeight = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strJob = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nCreateTopicsCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nJoinTopicsCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strPhotoWall = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nCreateTime = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nFollowBusLineCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[nIndex++];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nRadar = atoi(pReplyElement->str);
			}
		}
		else
		{
			stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
			bIsFailed = true;
			break;
		}
	}while(0);

	if(bIsFailed)
	{
		MsgHeadCS stMsgHeadCS;
		stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
		stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
		stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
		stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

		stGetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUserInfoResp.m_nResult);

		uint8_t arrRespBuf[MAX_MSG_SIZE];
		uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
		pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

		g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

		pRedisSessionBank->DestroySession(pRedisSession);
	}
	else
	{
		pUserSession->m_stGetUserInfoResp = stGetUserInfoResp;

		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserRelationInfo));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		CRedisChannel *pRelationCount = pRedisBank->GetRedisChannel(UserFollowers::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRelationCount->Multi();
		pRelationCount->ZCard(NULL, CServerHelper::MakeRedisKey(UserFollowers::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin));
		pRelationCount->ZCard(NULL, CServerHelper::MakeRedisKey(UserFans::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin));
		pRelationCount->ZCard(NULL, CServerHelper::MakeRedisKey(UserLookMe::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin));
		pRelationCount->Exec(pRedisSession);
	}

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionGetUserRelationInfo(int32_t nResult, void *pReply, void *pSession)
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
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_ARRAY)
		{
			redisReply *pFollowersCount = pRedisReply->element[0];
			if(pFollowersCount->type == REDIS_REPLY_INTEGER)
			{
				pUserSession->m_stGetUserInfoResp.m_nFollowersCount = pFollowersCount->integer;
			}

			redisReply *pFansCount = pRedisReply->element[1];
			if(pFansCount->type == REDIS_REPLY_INTEGER)
			{
				pUserSession->m_stGetUserInfoResp.m_nFansCount = pFansCount->integer;
			}

			redisReply *pLookMeCount = pRedisReply->element[2];
			if(pLookMeCount->type == REDIS_REPLY_INTEGER)
			{
				pUserSession->m_stGetUserInfoResp.m_nLookMeCount = pLookMeCount->integer;
			}
		}

	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	uint8_t arrRespBuf[MAX_MSG_SIZE];
	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &pUserSession->m_stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &pUserSession->m_stGetUserInfoResp, "send ");

	if(pUserSession->m_stMsgHeadCS.m_nSrcUin != pUserSession->m_stMsgHeadCS.m_nDstUin)
	{
		CRedisChannel *pLookMeChannel = pRedisBank->GetRedisChannel(UserLookMe::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pLookMeChannel->ZAdd(NULL, CServerHelper::MakeRedisKey(UserLookMe::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin), "%ld %u",
				pUserSession->m_stCtlHead.m_nTimeStamp / 1000, pUserSession->m_stMsgHeadCS.m_nSrcUin);

		//add to unreadmsglist
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserUnreadMsgCount));
		pRedisSession->SetTimerProc(static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout), 60 * MS_PER_SECOND);

		stMsgHeadCS.m_nMsgID = MSGID_UNREADMSGTIP_NOTI;
		stMsgHeadCS.m_nSrcUin = 0;
		stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

		CUnreadMsgTipNoti stUnreadMsgTipNoti;
		stUnreadMsgTipNoti.m_nTipType = CUnreadMsgTipNoti::enmUnreadMsgTipType_LookMe;

		uint32_t nOffset = 0;
		stMsgHeadCS.Encode(arrRespBuf, sizeof(arrRespBuf), nOffset);
		stUnreadMsgTipNoti.Encode(arrRespBuf, sizeof(arrRespBuf), nOffset);

		const char *szUin = CServerHelper::MakeRedisKey(UserUnreadMsgList::keyname, pUserSession->m_stMsgHeadCS.m_nDstUin);
		CRedisChannel *pUnreadMsgChannel = pRedisBank->GetRedisChannel(UserUnreadMsgList::servername, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pUnreadMsgChannel->Multi();
		pUnreadMsgChannel->ZAdd(NULL, szUin, "%ld %b", pUserSession->m_stCtlHead.m_nTimeStamp, arrRespBuf, (size_t)nOffset);
		pUnreadMsgChannel->ZCard(NULL, szUin);
		pUnreadMsgChannel->Exec(pRedisSession);
	}
	else
	{
		pRedisSessionBank->DestroySession(pRedisSession);
	}

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionGetUserUnreadMsgCount(int32_t nResult, void *pReply, void *pSession)
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
		pRedisSession->SetHandleRedisReply(static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserSessionInfo));

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

int32_t CGetUserInfoHandler::OnSessionGetUserSessionInfo(int32_t nResult, void *pReply, void *pSession)
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

int32_t CGetUserInfoHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pUserSession->m_stCtlHead.m_nGateRedisAddress, pUserSession->m_stCtlHead.m_nGateRedisPort);
	if(pRespChannel == NULL)
	{
		WRITE_WARN_LOG(SERVER_NAME, "it's not found redis channel by msgid!{msgid=%d, srcuin=%u, dstuin=%u}\n", MSGID_GETUSERINFO_RESP,
				pUserSession->m_stMsgHeadCS.m_nSrcUin, pUserSession->m_stMsgHeadCS.m_nDstUin);
		pRedisSessionBank->DestroySession(pRedisSession);
		return 0;
	}

	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	uint8_t arrRespBuf[MAX_MSG_SIZE];

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	CGetUserInfoResp stGetUserInfoResp;
	stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
	stGetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUserInfoResp.m_nResult);

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, CServerHelper::MakeRedisKey(ClientResp::keyname, pUserSession->m_stCtlHead.m_nGateID), (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}


