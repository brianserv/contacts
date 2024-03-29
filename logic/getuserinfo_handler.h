/*
 * getuserinfo_handler.h
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#ifndef GETUSERINFO_HANDLER_H_
#define GETUSERINFO_HANDLER_H_

#include "common/common_object.h"
#include "frame/frame_impl.h"
#include "frame/redis_session.h"
#include "include/control_head.h"
#include "include/contacts_msg.h"
#include "include/msg_head.h"
#include <string>

using namespace std;
using namespace FRAME;

class CGetUserInfoHandler : public CBaseObject
{
	struct UserSession
	{
		UserSession()
		{
			m_nIsFollow = 0;
		}
		ControlHead			m_stCtlHead;
		MsgHeadCS			m_stMsgHeadCS;
		CGetUserInfoReq		m_stGetUserInfoReq;
		CGetUserInfoResp 	m_stGetUserInfoResp;
		uint8_t				m_nIsFollow;
	};

public:

	virtual int32_t Init()
	{
		return 0;
	}
	virtual int32_t Uninit()
	{
		return 0;
	}
	virtual int32_t GetSize()
	{
		return 0;
	}

	int32_t GetUserInfo(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize);

	int32_t OnSessionExistInBlackList(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionIsFollow(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserBaseInfo(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserRelationInfo(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserUnreadMsgCount(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserSessionInfo(int32_t nResult, void *pReply, void *pSession);

	int32_t OnRedisSessionTimeout(void *pTimerData);
};


#endif /* GETUSERINFO_HANDLER_H_ */
