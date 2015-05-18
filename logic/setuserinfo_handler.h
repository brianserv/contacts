/*
 * setuserinfo_handler.h
 *
 *  Created on: Mar 18, 2015
 *      Author: jimm
 */

#ifndef SETUSERINFO_HANDLER_H_
#define SETUSERINFO_HANDLER_H_

#include "common/common_object.h"
#include "frame/frame_impl.h"
#include "frame/redis_session.h"
#include "include/control_head.h"
#include "include/contacts_msg.h"
#include "include/msg_head.h"
#include <string>

using namespace std;
using namespace FRAME;

class CSetUserInfoHandler : public CBaseObject
{
	struct UserSession
	{
		UserSession()
		{
		}
		ControlHead			m_stCtlHead;
		MsgHeadCS			m_stMsgHeadCS;
		CSetUserInfoReq		m_stSetUserInfoReq;
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

	int32_t SetUserInfo(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize);

	int32_t OnSessionSetUserBaseInfo(int32_t nResult, void *pReply, void *pSession);

	int32_t OnRedisSessionTimeout(void *pTimerData);
};


#endif /* SETUSERINFO_HANDLER_H_ */
