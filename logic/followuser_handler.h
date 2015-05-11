/*
 * followuser_handler.h
 *
 *  Created on: Apr 25, 2015
 *      Author: jimm
 */

#ifndef LOGIC_FOLLOWUSER_HANDLER_H_
#define LOGIC_FOLLOWUSER_HANDLER_H_

#include "../../common/common_object.h"
#include "../../frame/frame_impl.h"
#include "../../frame/redis_session.h"
#include "../../include/control_head.h"
#include "../../include/contacts_msg.h"
#include "../../include/msg_head.h"
#include <string>

using namespace std;
using namespace FRAME;

class CFollowUserHandler : public CBaseObject
{
	struct UserSession
	{
		UserSession()
		{
			m_nMsgSize = 0;
			m_bSendTimeoutResp = true;
		}
		ControlHead			m_stCtlHead;
		MsgHeadCS			m_stMsgHeadCS;
		uint16_t				m_nMsgSize;
		uint8_t				m_arrMsg[1024];
		CFollowUserReq		m_stFollowUserReq;
		bool				m_bSendTimeoutResp;
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

	int32_t FollowUser(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize);

	int32_t OnSessionExistInBlackList(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionExistInDstFollowList(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserUnreadMsgCount(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserSessionInfo(int32_t nResult, void *pReply, void *pSession);

	int32_t OnRedisSessionTimeout(void *pTimerData);
};

#endif /* LOGIC_FOLLOWUSER_HANDLER_H_ */
