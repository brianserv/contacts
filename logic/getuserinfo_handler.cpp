/*
 * getuserinfo_handler.cpp
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#include "getuserinfo_handler.h"
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

	CGetUserInfoReq *pGetUserInfoReq = dynamic_cast<CGetUserInfoReq *>(pMsgBody);
	if(pGetUserInfoReq == NULL)
	{
		return 0;
	}

	UserBaseInfo *pConfigUserBaseInfo = (UserBaseInfo *)g_Frame.GetConfig(USER_BASEINFO);

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pSession = pRedisSessionBank->CreateSession(this, static_cast<RedisReply>(&CGetUserInfoHandler::OnSessionGetUserBaseInfo),
			static_cast<TimerProc>(&CGetUserInfoHandler::OnRedisSessionTimeout));
	UserSession *pSessionData = new(pSession->GetSessionData()) UserSession();
	pSessionData->m_stCtlHead = *pControlHead;
	pSessionData->m_stMsgHeadCS = *pMsgHeadCS;
	pSessionData->m_stGetUserInfoReq = *pGetUserInfoReq;

	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRedisChannel = pRedisBank->GetRedisChannel(pConfigUserBaseInfo->string);
	pRedisChannel->HMGet(pSession, itoa(pMsgHeadCS->m_nSrcUin), "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", pConfigUserBaseInfo->version,
			pConfigUserBaseInfo->oneselfwords, pConfigUserBaseInfo->school, pConfigUserBaseInfo->hometown, pConfigUserBaseInfo->birthday,
			pConfigUserBaseInfo->age, pConfigUserBaseInfo->liveplace, pConfigUserBaseInfo->height, pConfigUserBaseInfo->weight,
			pConfigUserBaseInfo->job, pConfigUserBaseInfo->care_people_count, pConfigUserBaseInfo->fans_count, pConfigUserBaseInfo->friends_count,
			pConfigUserBaseInfo->publishtopic_count, pConfigUserBaseInfo->jointopic_count);

	return 0;
}

int32_t CGetUserInfoHandler::OnSessionGetUserBaseInfo(int32_t nResult, void *pReply, void *pSession)
{
	redisReply *pRedisReply = (redisReply *)pReply;
	RedisSession *pRedisSession = (RedisSession *)pSession;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	CStringConfig *pStringConfig = (CStringConfig *)g_Frame.GetConfig(CONFIG_STRING);

	CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_GETUSERINFO_RESP));
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

	bool bIsReturn = false;
	do
	{
		if(pRedisReply->type == REDIS_REPLY_ERROR)
		{
			stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
			bIsReturn = true;
			break;
		}

		if(pRedisReply->type == REDIS_REPLY_ARRAY)
		{
			redisReply *pReplyElement = pRedisReply->element[0];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nVersion = atoi(pReplyElement->str);
			}
			else
			{
				stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_Unknown;
				bIsReturn = true;
				break;
			}

			if(stGetUserInfoResp.m_nVersion == pUserSession->m_stGetUserInfoReq.m_nVersion)
			{
				stGetUserInfoResp.m_nResult = CGetUserInfoResp::enmResult_DontNeedUpdate;
				break;
			}

			pReplyElement = pRedisReply->element[1];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strOneselfWords = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[2];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strSchool = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[3];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strHometown = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[4];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strBirthday = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[5];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nAge = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[6];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strLivePlace = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[7];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strHeight = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[8];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strWeight = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[9];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_strJob = string(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[10];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nCarePeopleCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[11];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nFansCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[12];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nFriendsCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[13];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nPublishTopicCount = atoi(pReplyElement->str);
			}

			pReplyElement = pRedisReply->element[14];
			if(pReplyElement->type != REDIS_REPLY_NIL)
			{
				stGetUserInfoResp.m_nJoinTopicCount = atoi(pReplyElement->str);
			}
		}
		else
		{
			bIsReturn = true;
			break;
		}
	}while(0);

	MsgHeadCS stMsgHeadCS;
	stMsgHeadCS.m_nMsgID = MSGID_GETUSERINFO_RESP;
	stMsgHeadCS.m_nSeq = pUserSession->m_stMsgHeadCS.m_nSeq;
	stMsgHeadCS.m_nSrcUin = pUserSession->m_stMsgHeadCS.m_nSrcUin;
	stMsgHeadCS.m_nDstUin = pUserSession->m_stMsgHeadCS.m_nDstUin;

	if(bIsReturn)
	{
		stGetUserInfoResp.m_strTips = pStringConfig->GetString(stMsgHeadCS.m_nMsgID, stGetUserInfoResp.m_nResult);
	}

	uint16_t nTotalSize = CServerHelper::MakeMsg(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, arrRespBuf, sizeof(arrRespBuf));
	pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}

int32_t CGetUserInfoHandler::OnRedisSessionTimeout(void *pTimerData)
{
	CRedisSessionBank *pRedisSessionBank = (CRedisSessionBank *)g_Frame.GetBank(BANK_REDIS_SESSION);
	RedisSession *pRedisSession = (RedisSession *)pTimerData;
	UserSession *pUserSession = (UserSession *)pRedisSession->GetSessionData();

	CMsgDispatchConfig *pMsgDispatchConfig = (CMsgDispatchConfig *)g_Frame.GetConfig(CONFIG_MSGDISPATCH);
	CRedisBank *pRedisBank = (CRedisBank *)g_Frame.GetBank(BANK_REDIS);
	CRedisChannel *pRespChannel = pRedisBank->GetRedisChannel(pMsgDispatchConfig->GetChannelKey(MSGID_GETUSERINFO_RESP));
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
	pRespChannel->RPush(NULL, (char *)arrRespBuf, nTotalSize);

	g_Frame.Dump(&pUserSession->m_stCtlHead, &stMsgHeadCS, &stGetUserInfoResp, "send ");

	pRedisSessionBank->DestroySession(pRedisSession);
	return 0;
}


